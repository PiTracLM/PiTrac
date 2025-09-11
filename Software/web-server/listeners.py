import asyncio
import base64
import logging
from typing import Any, Dict, List, Optional, Union

import msgpack
import stomp

from managers import ConnectionManager, ShotDataStore
from parsers import ShotDataParser

logger = logging.getLogger(__name__)


class ActiveMQListener(stomp.ConnectionListener):
    def __init__(
        self,
        shot_store: ShotDataStore,
        connection_manager: ConnectionManager,
        parser: ShotDataParser,
        loop: Optional[asyncio.AbstractEventLoop] = None,
    ):

        self.connected: bool = False
        self.loop = loop
        self.shot_store = shot_store
        self.connection_manager = connection_manager
        self.parser = parser
        self.message_count = 0
        self.error_count = 0

    def on_error(self, frame: Any) -> None:
        self.error_count += 1
        logger.error(f"ActiveMQ error #{self.error_count}: {frame.body}")

    def on_message(self, frame: Any) -> None:
        self.message_count += 1
        logger.info(f"Received ActiveMQ message #{self.message_count}")

        try:
            msgpack_data = self._extract_message_data(frame)
            data = msgpack.unpackb(msgpack_data, raw=False, strict_map_key=False)
            logger.debug(f"Unpacked message data: {data}")

            if self.loop:
                asyncio.run_coroutine_threadsafe(
                    self._process_and_broadcast(data), self.loop
                )
            else:
                logger.error("Event loop not set in listener")

        except msgpack.exceptions.ExtraData as e:
            logger.error(f"Extra data in msgpack (message #{self.message_count}): {e}")
        except msgpack.exceptions.UnpackException as e:
            logger.error(f"Failed to unpack message #{self.message_count}: {e}")
        except Exception as e:
            logger.error(
                f"Error processing message #{self.message_count}: {e}", exc_info=True
            )

    def _extract_message_data(self, frame: Any) -> bytes:
        if not hasattr(frame, "body"):
            raise ValueError("Frame has no body attribute")

        if isinstance(frame.body, bytes):
            body = frame.body
        else:
            try:
                body = bytes(frame.body, "latin-1")
            except (UnicodeDecodeError, TypeError) as e:
                logger.warning(
                    f"Failed to decode with latin-1: {e}, falling back to byte conversion"
                )
                body = bytes(
                    [ord(c) if ord(c) < 256 else ord(c) & 0xFF for c in frame.body]
                )

        if hasattr(frame, "headers") and frame.headers.get("encoding") == "base64":
            logger.debug("Message is base64 encoded, decoding...")
            base64_str = body.decode("utf-8") if isinstance(body, bytes) else body
            decoded = base64.b64decode(base64_str)
            logger.debug(f"Decoded {len(decoded)} bytes from base64")
            return decoded

        return body

    async def _process_and_broadcast(
        self, data: Union[List[Any], Dict[str, Any]]
    ) -> None:
        try:
            if isinstance(data, list):
                parsed_data = self.parser.parse_array_format(data)
            else:
                current = self.shot_store.get()
                parsed_data = self.parser.parse_dict_format(data, current)

            if not self.parser.validate_shot_data(parsed_data):
                logger.warning("Data validation failed, but continuing...")

            # Check if this is a status message (preserve existing shot data)
            is_status_message = parsed_data.result_type in [
                "Ball Ready",  # kBallPlacedAndReadyForHit
                "Initializing",
                "Waiting For Ball",
                "Waiting For Simulator", 
                "Pausing For Stabilization",
                "Multiple Balls",
                "Error",
                "Calibration",
            ]

            if is_status_message:
                # For status messages, update only the status and message, preserve shot data
                current = self.shot_store.get()
                status_update = current.to_dict()
                status_update.update({
                    "result_type": parsed_data.result_type,
                    "message": parsed_data.message,
                    "timestamp": parsed_data.timestamp,
                })
                from models import ShotData
                updated_data = ShotData.from_dict(status_update)
                self.shot_store.update(updated_data)
                await self.connection_manager.broadcast(updated_data.to_dict())

                logger.info(
                    f"Processed status #{self.message_count}: "
                    f"type={parsed_data.result_type}, "
                    f"message='{parsed_data.message}'"
                )
            else:
                # This is actual shot data - update everything
                self.shot_store.update(parsed_data)
                await self.connection_manager.broadcast(parsed_data.to_dict())

                logger.info(
                    f"Processed shot #{self.message_count}: "
                    f"speed={parsed_data.speed} mph, "
                    f"launch={parsed_data.launch_angle}°, "
                    f"side={parsed_data.side_angle}°"
                )

        except ValueError as e:
            logger.error(f"Invalid data format: {e}")
        except Exception as e:
            logger.error(f"Error processing message: {e}", exc_info=True)

    def on_connected(self, frame: Any) -> None:
        logger.info("Connected to ActiveMQ")
        self.connected = True
        self.message_count = 0
        self.error_count = 0

    def on_disconnected(self) -> None:
        logger.warning(
            f"Disconnected from ActiveMQ (processed {self.message_count} messages)"
        )
        self.connected = False

    def on_heartbeat(self) -> None:
        """Called when a heartbeat is received from the broker"""
        pass

    def on_heartbeat_timeout(self) -> None:
        """Called when heartbeat timeout occurs"""
        logger.warning("ActiveMQ heartbeat timeout detected")
        self.connected = False

    def get_stats(self) -> Dict[str, Any]:
        return {
            "connected": self.connected,
            "messages_processed": self.message_count,
            "errors": self.error_count,
        }

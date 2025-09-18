import asyncio
import logging
import time
from typing import Any, Dict, List, Optional, Union

import msgpack
import zmq
import zmq.asyncio

from managers import ConnectionManager, ShotDataStore
from parsers import ShotDataParser

logger = logging.getLogger(__name__)


class ZeroMQListener:

    def __init__(
        self,
        shot_store: ShotDataStore,
        connection_manager: ConnectionManager,
        parser: ShotDataParser,
        endpoint: str = "tcp://localhost:5556",
        loop: Optional[asyncio.AbstractEventLoop] = None,
    ):
        self.endpoint = endpoint
        self.shot_store = shot_store
        self.connection_manager = connection_manager
        self.parser = parser
        self.loop = loop or asyncio.get_event_loop()

        self.context: Optional[zmq.asyncio.Context] = None
        self.subscriber: Optional[zmq.asyncio.Socket] = None

        self.connected = False
        self.running = False
        self.message_count = 0
        self.error_count = 0

        self.topic_prefix = "Golf.Sim"
        self.receive_timeout_ms = 1000
        self.high_water_mark = 1000

        self.message_types = {
            0: "kUnknown",
            1: "kRequestForCamera2Image",
            2: "kCamera2Image",
            3: "kRequestForCamera2TestStillImage",
            4: "kResults",
            5: "kShutdown",
            6: "kCamera2ReturnPreImage",
            7: "kControlMessage"
        }

    async def start(self) -> bool:
        if self.running:
            logger.warning("ZeroMQ listener already running")
            return True

        try:
            logger.info(f"Starting ZeroMQ listener on {self.endpoint}")

            self.context = zmq.asyncio.Context()
            self.subscriber = self.context.socket(zmq.SUB)

            self.subscriber.set(zmq.RCVHWM, self.high_water_mark)
            self.subscriber.set(zmq.RCVTIMEO, self.receive_timeout_ms)

            self.subscriber.setsockopt_string(zmq.SUBSCRIBE, self.topic_prefix)

            self.subscriber.connect(self.endpoint)

            await asyncio.sleep(0.1)

            self.connected = True
            self.running = True
            self.message_count = 0
            self.error_count = 0

            logger.info(f"ZeroMQ listener connected to {self.endpoint}")

            asyncio.create_task(self._message_loop())

            return True

        except Exception as e:
            self.error_count += 1
            logger.error(f"Failed to start ZeroMQ listener: {e}")
            await self.stop()
            return False

    async def stop(self) -> None:
        if not self.running:
            return

        logger.info("Stopping ZeroMQ listener")
        self.running = False
        self.connected = False

        try:
            if self.subscriber:
                self.subscriber.close()
                self.subscriber = None

            if self.context:
                self.context.term()
                self.context = None

        except Exception as e:
            logger.error(f"Error stopping ZeroMQ listener: {e}")

        logger.info(f"ZeroMQ listener stopped (processed {self.message_count} messages)")

    async def _message_loop(self) -> None:
        while self.running and self.subscriber:
            try:
                try:
                    message_parts = await self.subscriber.recv_multipart(zmq.NOBLOCK)
                except zmq.Again:
                    await asyncio.sleep(0.01)
                    continue

                if len(message_parts) < 2:
                    logger.warning("Received incomplete ZeroMQ message")
                    continue

                topic = message_parts[0].decode('utf-8')
                data = message_parts[1]

                properties = {}
                if len(message_parts) >= 3:
                    try:
                        properties = msgpack.unpackb(message_parts[2], raw=False, strict_map_key=False)
                    except Exception as e:
                        logger.debug(f"Could not unpack properties: {e}")

                self.message_count += 1
                logger.debug(f"Received ZeroMQ message #{self.message_count} on topic: {topic}")

                await self._process_message(topic, data, properties)

            except asyncio.CancelledError:
                logger.info("ZeroMQ message loop cancelled")
                break
            except Exception as e:
                self.error_count += 1
                logger.error(f"Error in ZeroMQ message loop: {e}", exc_info=True)
                await asyncio.sleep(0.1)  # Prevent tight error loop

    async def _process_message(self, topic: str, data: bytes, properties: Dict[str, Any]) -> None:
        try:
            if len(data) > 100000:
                logger.info(f"Skipping large binary message on topic {topic} ({len(data)} bytes)")
                return

            message_type = self._get_message_type(topic, properties)

            if message_type in ["kCamera2Image", "kCamera2ReturnPreImage"]:
                logger.info(f"Skipping camera image message: {message_type}")
                return

            if message_type != "kResults":
                logger.debug(f"Skipping non-results message: {message_type}")
                return

            try:
                unpacked_data = msgpack.unpackb(data, raw=False, strict_map_key=False)
                logger.debug(f"Unpacked message data: {type(unpacked_data)}")

                shot_data = self._extract_shot_data(unpacked_data)

                if shot_data:
                    await self._process_and_broadcast(shot_data)
                else:
                    logger.debug("No shot data extracted from message")

            except msgpack.exceptions.ExtraData:
                logger.info(f"Large binary message #{self.message_count} - Extra data in msgpack, skipping")
            except msgpack.exceptions.UnpackException as e:
                logger.error(f"Failed to unpack message #{self.message_count}: {e}")
            except Exception as e:
                logger.error(f"Error processing message #{self.message_count}: {e}", exc_info=True)

        except Exception as e:
            logger.error(f"Error processing ZeroMQ message: {e}", exc_info=True)

    def _get_message_type(self, topic: str, properties: Dict[str, Any]) -> str:
        if "Message_Type" in properties:
            try:
                msg_type_int = int(properties["Message_Type"])
                return self.message_types.get(msg_type_int, "kUnknown")
            except (ValueError, TypeError):
                pass

        if "Results" in topic:
            return "kResults"
        elif "Control" in topic:
            return "kControlMessage"
        elif "Golf.Sim.Message" in topic:
            return "kUnknown"

        return "kUnknown"

    def _extract_shot_data(self, unpacked_data: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        try:
            if isinstance(unpacked_data, dict):
                if "header" in unpacked_data and "result_data" in unpacked_data:
                    result_data = unpacked_data["result_data"]
                    if isinstance(result_data, dict) and result_data:
                        return result_data

                elif any(key in unpacked_data for key in ["speed", "launch_angle", "side_angle"]):
                    return unpacked_data

                elif "data" in unpacked_data:
                    nested_data = unpacked_data["data"]
                    if isinstance(nested_data, dict):
                        return nested_data

            elif isinstance(unpacked_data, list) and len(unpacked_data) > 0:
                for item in unpacked_data:
                    if isinstance(item, dict) and any(key in item for key in ["speed", "launch_angle", "side_angle"]):
                        return item

        except Exception as e:
            logger.debug(f"Error extracting shot data: {e}")

        return None

    async def _process_and_broadcast(self, data: Union[List[Any], Dict[str, Any]]) -> None:
        try:
            if isinstance(data, list):
                parsed_data = self.parser.parse_array_format(data)
            else:
                current = self.shot_store.get()
                parsed_data = self.parser.parse_dict_format(data, current)

            if not self.parser.validate_shot_data(parsed_data):
                logger.warning("Data validation failed, but continuing...")

            is_status_message = parsed_data.result_type in ShotDataParser._get_status_message_strings()

            if is_status_message:
                current = self.shot_store.get()
                status_update = current.to_dict()
                status_update.update(
                    {
                        "result_type": parsed_data.result_type,
                        "message": parsed_data.message,
                        "timestamp": parsed_data.timestamp,
                    }
                )
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

    def get_stats(self) -> Dict[str, Any]:
        return {
            "connected": self.connected,
            "running": self.running,
            "messages_processed": self.message_count,
            "errors": self.error_count,
            "endpoint": self.endpoint,
            "topic_prefix": self.topic_prefix,
        }

    def set_endpoint(self, endpoint: str) -> None:
        if self.running:
            logger.warning("Cannot change endpoint while listener is running")
            return
        self.endpoint = endpoint

    def set_topic_prefix(self, prefix: str) -> None:
        if self.running:
            logger.warning("Cannot change topic prefix while listener is running")
            return
        self.topic_prefix = prefix

    async def __aenter__(self):
        await self.start()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self.stop()
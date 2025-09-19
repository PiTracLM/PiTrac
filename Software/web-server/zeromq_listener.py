import asyncio
import json
import logging
import time
from typing import Any, Dict, List, Optional, Union

import msgpack
import zmq
import zmq.asyncio

from managers import ConnectionManager, ShotDataStore
from parsers import ShotDataParser
from config_manager import ConfigurationManager

logger = logging.getLogger(__name__)


class ZeroMQListener:

    def __init__(
        self,
        shot_store: ShotDataStore,
        connection_manager: ConnectionManager,
        parser: ShotDataParser,
        config_manager: Optional[ConfigurationManager] = None,
        endpoint: str = "tcp://localhost:5556",
        mode: str = "single",  # "single" for single camera/Pi, "dual" for dual cameras
        loop: Optional[asyncio.AbstractEventLoop] = None,
    ):
        self.shot_store = shot_store
        self.connection_manager = connection_manager
        self.parser = parser
        self.config_manager = config_manager or ConfigurationManager()
        self.mode = mode
        self.loop = loop or asyncio.get_event_loop()

        self.endpoints = self._configure_endpoints(endpoint, mode)
        self.contexts: Dict[str, Optional[zmq.asyncio.Context]] = {}
        self.subscribers: Dict[str, Optional[zmq.asyncio.Socket]] = {}

        self.connected: Dict[str, bool] = {}
        self.running = False
        self.message_counts: Dict[str, int] = {}
        self.error_counts: Dict[str, int] = {}

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
            7: "kControlMessage",
        }

        self.system_id = self._get_system_id()

    def _configure_endpoints(self, default_endpoint: str, mode: str) -> Dict[str, str]:
        """Configure endpoints based on mode using ConfigurationManager."""
        if mode == "single":
            return {"camera1": default_endpoint}
        elif mode == "dual":
            return {
                "camera1": "tcp://localhost:5556",
                "camera2": "tcp://localhost:5557",
            }
        elif mode == "dual_pi":
            camera1_host = self.config_manager.get_config("gs_config.ipc_interface.kCamera1Host") or "localhost"
            camera2_host = self.config_manager.get_config("gs_config.ipc_interface.kCamera2Host") or "localhost"

            return {
                "camera1": f"tcp://{camera1_host}:5556",
                "camera2": f"tcp://{camera2_host}:5557",
            }
        else:
            return {"camera1": default_endpoint}

    async def start(self) -> bool:
        if self.running:
            logger.warning("ZeroMQ listener already running")
            return True

        self.running = True
        success_count = 0

        for camera_name, endpoint in self.endpoints.items():
            try:
                logger.info(f"Starting ZeroMQ listener for {camera_name} on {endpoint}")

                context = zmq.asyncio.Context()
                subscriber = context.socket(zmq.SUB)

                subscriber.set(zmq.RCVHWM, self.high_water_mark)
                subscriber.set(zmq.RCVTIMEO, self.receive_timeout_ms)
                subscriber.setsockopt_string(zmq.SUBSCRIBE, self.topic_prefix)

                subscriber.connect(endpoint)

                self.contexts[camera_name] = context
                self.subscribers[camera_name] = subscriber
                self.connected[camera_name] = True
                self.message_counts[camera_name] = 0
                self.error_counts[camera_name] = 0

                asyncio.create_task(self._message_loop(camera_name))

                await asyncio.sleep(0.1)
                logger.info(f"ZeroMQ listener connected to {camera_name}: {endpoint}")
                success_count += 1

            except Exception as e:
                logger.error(f"Failed to start {camera_name} listener: {e}")
                self.connected[camera_name] = False
                self.error_counts[camera_name] = self.error_counts.get(camera_name, 0) + 1

        if success_count == 0:
            logger.error("Failed to start any ZeroMQ listeners")
            self.running = False
            await self.stop()
            return False

        logger.info(f"ZeroMQ listener started: {success_count}/{len(self.endpoints)} cameras connected")
        return True

    async def stop(self) -> None:
        if not self.running:
            return

        logger.info("Stopping ZeroMQ listener")
        self.running = False

        for camera_name in list(self.subscribers.keys()):
            self.connected[camera_name] = False

        try:
            for camera_name, subscriber in self.subscribers.items():
                if subscriber:
                    subscriber.close()
            self.subscribers.clear()

            for camera_name, context in self.contexts.items():
                if context:
                    context.term()
            self.contexts.clear()

        except Exception as e:
            logger.error(f"Error stopping ZeroMQ listener: {e}")

        total_messages = sum(self.message_counts.values())
        logger.info(f"ZeroMQ listener stopped (processed {total_messages} messages)")

    async def _message_loop(self, camera_name: str) -> None:
        reconnect_attempts = 0
        max_reconnect_attempts = 5
        base_reconnect_delay = 1.0
        subscriber = self.subscribers.get(camera_name)

        while self.running and camera_name in self.subscribers:
            subscriber = self.subscribers.get(camera_name)
            if not subscriber:
                if reconnect_attempts >= max_reconnect_attempts:
                    logger.error(f"Failed to reconnect {camera_name} after {max_reconnect_attempts} attempts")
                    self.connected[camera_name] = False
                    break

                delay = base_reconnect_delay * (2**reconnect_attempts) + (0.1 * reconnect_attempts)
                logger.warning(
                    f"{camera_name} disconnected, attempting reconnect {reconnect_attempts + 1}/{max_reconnect_attempts} after {delay:.1f}s"
                )
                await asyncio.sleep(delay)

                if await self._reconnect(camera_name):
                    reconnect_attempts = 0
                    logger.info(f"Successfully reconnected {camera_name} to ZeroMQ")
                else:
                    reconnect_attempts += 1
                    continue

            try:
                try:
                    message_parts = await subscriber.recv_multipart(zmq.NOBLOCK)
                    if reconnect_attempts > 0:
                        reconnect_attempts = 0
                except zmq.Again:
                    await asyncio.sleep(0.01)
                    continue
                except zmq.error.ContextTerminated:
                    logger.warning(f"ZeroMQ context terminated for {camera_name}, attempting to recover")
                    self.subscribers[camera_name] = None
                    continue

                if len(message_parts) < 2:
                    logger.warning("Received incomplete ZeroMQ message")
                    continue

                topic = message_parts[0].decode("utf-8")

                properties = {}
                data = message_parts[1]

                if len(message_parts) >= 3:
                    try:
                        props_json = message_parts[1].decode("utf-8")
                        properties = json.loads(props_json)
                        data = message_parts[2]
                    except json.JSONDecodeError as e:
                        logger.debug(f"Could not parse properties JSON: {e}")
                    except UnicodeDecodeError as e:
                        logger.debug(f"Could not decode properties: {e}")

                self.message_counts[camera_name] = self.message_counts.get(camera_name, 0) + 1
                logger.debug(
                    f"Received ZeroMQ message #{self.message_counts[camera_name]} from {camera_name} on topic: {topic}"
                )

                # Add camera identification to properties
                properties["camera_source"] = camera_name
                await self._process_message(topic, data, properties)

            except asyncio.CancelledError:
                logger.info(f"ZeroMQ message loop for {camera_name} cancelled")
                break
            except Exception as e:
                self.error_counts[camera_name] = self.error_counts.get(camera_name, 0) + 1
                logger.error(f"Error in {camera_name} ZeroMQ message loop: {e}", exc_info=True)
                await asyncio.sleep(0.1)  # Prevent tight error loop

    async def _process_message(self, topic: str, data: bytes, properties: Dict[str, Any]) -> None:
        try:
            camera_source = properties.get("camera_source", "unknown")

            if self._is_self_message(properties):
                logger.debug(
                    f"Filtering out self-message from {camera_source} with System_ID: {properties.get('System_ID')}"
                )
                return

            if len(data) > 100000:
                logger.info(f"Skipping large binary message from {camera_source} on topic {topic} ({len(data)} bytes)")
                return

            message_type = self._get_message_type(topic, properties)

            if message_type in ["kCamera2Image", "kCamera2ReturnPreImage"]:
                logger.info(f"Skipping camera image message from {camera_source}: {message_type}")
                return

            if message_type != "kResults":
                logger.debug(f"Skipping non-results message from {camera_source}: {message_type}")
                return

            try:
                unpacked_data = msgpack.unpackb(data, raw=False, strict_map_key=False)
                logger.debug(f"Unpacked message data from {camera_source}: {type(unpacked_data)}")

                shot_data = self._extract_shot_data(unpacked_data)

                if shot_data:
                    shot_data["camera_source"] = camera_source
                    await self._process_and_broadcast(shot_data)
                else:
                    logger.debug(f"No shot data extracted from {camera_source} message")

            except msgpack.exceptions.ExtraData:
                logger.info(f"Large binary message from {camera_source} - Extra data in msgpack, skipping")
            except msgpack.exceptions.UnpackException as e:
                logger.error(f"Failed to unpack message from {camera_source}: {e}")
            except Exception as e:
                logger.error(f"Error processing message from {camera_source}: {e}", exc_info=True)

        except Exception as e:
            camera_source = properties.get("camera_source", "unknown")
            logger.error(f"Error processing ZeroMQ message from {camera_source}: {e}", exc_info=True)

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
            camera_source = data.get("camera_source", "unknown") if isinstance(data, dict) else "unknown"

            if isinstance(data, list):
                parsed_data = self.parser.parse_array_format(data)
            else:
                data_copy = data.copy() if isinstance(data, dict) else data
                if isinstance(data_copy, dict) and "camera_source" in data_copy:
                    del data_copy["camera_source"]
                current = self.shot_store.get()
                parsed_data = self.parser.parse_dict_format(data_copy, current)

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

                broadcast_data = updated_data.to_dict()
                broadcast_data["camera_source"] = camera_source
                await self.connection_manager.broadcast(broadcast_data)

                logger.info(
                    f"Processed status from {camera_source}: "
                    f"type={parsed_data.result_type}, "
                    f"message='{parsed_data.message}'"
                )
            else:
                self.shot_store.update(parsed_data)

                broadcast_data = parsed_data.to_dict()
                broadcast_data["camera_source"] = camera_source
                await self.connection_manager.broadcast(broadcast_data)

                logger.info(
                    f"Processed shot from {camera_source}: "
                    f"speed={parsed_data.speed} mph, "
                    f"launch={parsed_data.launch_angle}°, "
                    f"side={parsed_data.side_angle}°"
                )

        except ValueError as e:
            logger.error(f"Invalid data format: {e}")
        except Exception as e:
            logger.error(f"Error processing message: {e}", exc_info=True)

    def get_stats(self) -> Dict[str, Any]:
        total_messages = sum(self.message_counts.values())
        total_errors = sum(self.error_counts.values())
        any_connected = any(self.connected.values())

        camera_stats = {}
        for camera_name in self.endpoints.keys():
            camera_stats[camera_name] = {
                "connected": self.connected.get(camera_name, False),
                "endpoint": self.endpoints[camera_name],
                "messages_processed": self.message_counts.get(camera_name, 0),
                "errors": self.error_counts.get(camera_name, 0),
            }

        return {
            "mode": self.mode,
            "connected": any_connected,
            "running": self.running,
            "total_messages": total_messages,
            "total_errors": total_errors,
            "cameras": camera_stats,
            "topic_prefix": self.topic_prefix,
        }

    def set_endpoint(self, endpoint: str, camera_name: str = "camera1") -> None:
        if self.running:
            logger.warning("Cannot change endpoint while listener is running")
            return
        self.endpoints[camera_name] = endpoint

    def set_topic_prefix(self, prefix: str) -> None:
        if self.running:
            logger.warning("Cannot change topic prefix while listener is running")
            return
        self.topic_prefix = prefix

    async def _reconnect(self, camera_name: str) -> bool:
        """Attempt to reconnect to ZeroMQ endpoint for a specific camera."""
        try:
            if camera_name in self.subscribers and self.subscribers[camera_name]:
                try:
                    self.subscribers[camera_name].close()
                except Exception:
                    pass
                self.subscribers[camera_name] = None

            if camera_name in self.contexts and self.contexts[camera_name]:
                try:
                    self.contexts[camera_name].term()
                except Exception:
                    pass
                self.contexts[camera_name] = None

            endpoint = self.endpoints.get(camera_name)
            if not endpoint:
                logger.error(f"No endpoint configured for {camera_name}")
                return False

            context = zmq.asyncio.Context()
            subscriber = context.socket(zmq.SUB)

            subscriber.set(zmq.RCVHWM, self.high_water_mark)
            subscriber.set(zmq.RCVTIMEO, self.receive_timeout_ms)
            subscriber.setsockopt_string(zmq.SUBSCRIBE, self.topic_prefix)

            subscriber.connect(endpoint)

            self.contexts[camera_name] = context
            self.subscribers[camera_name] = subscriber

            await asyncio.sleep(0.1)

            self.connected[camera_name] = True
            return True

        except Exception as e:
            logger.error(f"Failed to reconnect {camera_name}: {e}")
            self.connected[camera_name] = False
            return False

    def _get_system_id(self) -> str:
        """Get the system ID for this instance."""
        return "WEB_SERVER"

    def _is_self_message(self, properties: Dict[str, Any]) -> bool:
        """Check if this message originated from ourselves."""
        if not properties:
            return False

        sender_id = properties.get("System_ID", "")
        if sender_id and sender_id == self.system_id:
            return True

        return False

    async def __aenter__(self):
        await self.start()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self.stop()

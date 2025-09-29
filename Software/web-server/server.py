import asyncio
import json
import logging
import os
import subprocess
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

import stomp
import yaml
from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect, File, UploadFile
from fastapi.responses import FileResponse, HTMLResponse, Response, JSONResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from constants import (
    CONFIG_FILE,
    DEFAULT_BROKER,
    DEFAULT_PASSWORD,
    DEFAULT_USERNAME,
    IMAGES_DIR,
    STOMP_PORT,
)
from listeners import ActiveMQListener
from managers import ConnectionManager, ShotDataStore
from parsers import ShotDataParser
from config_manager import ConfigurationManager
from pitrac_manager import PiTracProcessManager
from camera_detector import CameraDetector
from calibration_manager import CalibrationManager
from testing_tools_manager import TestingToolsManager

logger = logging.getLogger(__name__)


class PiTracServer:

    def __init__(self):
        self.app = FastAPI(title="PiTrac Dashboard")
        self.templates = Jinja2Templates(directory="templates")
        self.connection_manager = ConnectionManager()
        self.shot_store = ShotDataStore()
        self.parser = ShotDataParser()
        self.config_manager = ConfigurationManager()
        self.pitrac_manager = PiTracProcessManager(self.config_manager)
        self.calibration_manager = CalibrationManager(self.config_manager)
        self.testing_manager = TestingToolsManager(self.config_manager)
        self.mq_conn: Optional[stomp.Connection] = None
        self.listener: Optional[ActiveMQListener] = None
        self.reconnect_task: Optional[asyncio.Task] = None
        self.shutdown_flag = False
        self.background_tasks: set[asyncio.Task] = set()
        IMAGES_DIR.mkdir(parents=True, exist_ok=True)

        self.app.mount("/static", StaticFiles(directory="static"), name="static")
        self.app.mount("/images", StaticFiles(directory=str(IMAGES_DIR)), name="images")

        self._setup_routes()

        self.app.add_event_handler("startup", self.startup_event)
        self.app.add_event_handler("shutdown", self.shutdown_event)

    def _setup_routes(self) -> None:

        @self.app.get("/", response_class=HTMLResponse)
        async def dashboard(request: Request) -> Response:
            return self.templates.TemplateResponse(
                "dashboard.html",
                {"request": request, "shot": self.shot_store.get().to_dict()},
            )

        @self.app.websocket("/ws")
        async def websocket_endpoint(websocket: WebSocket) -> None:
            await self.connection_manager.connect(websocket)

            await websocket.send_json(self.shot_store.get().to_dict())

            try:
                while True:
                    await websocket.receive_text()
            except WebSocketDisconnect:
                self.connection_manager.disconnect(websocket)
                logger.info("WebSocket client disconnected normally")
            except Exception as e:
                logger.error(f"WebSocket error: {e}")
                self.connection_manager.disconnect(websocket)

        @self.app.get("/api/shot")
        async def get_current_shot() -> Dict[str, Any]:
            return self.shot_store.get().to_dict()

        @self.app.get("/api/history")
        async def get_shot_history(limit: int = 10) -> list:
            return [shot.to_dict() for shot in self.shot_store.get_history(limit)]

        @self.app.get("/api/images/{filename}", response_model=None)
        async def get_image(filename: str):
            image_path = IMAGES_DIR / filename
            if image_path.exists() and image_path.is_file():
                return FileResponse(image_path)
            return {"error": "Image not found"}

        @self.app.post("/api/reset")
        async def reset_shot() -> Dict[str, Optional[str]]:
            shot_data = self.shot_store.reset()
            await self.connection_manager.broadcast(shot_data.to_dict())
            logger.info("Shot data reset via API")
            return {"status": "reset", "timestamp": shot_data.timestamp}

        @self.app.get("/health")
        async def health_check() -> Dict[str, Union[str, bool, int, Dict]]:
            mq_connected = self.mq_conn.is_connected() if self.mq_conn else False

            pitrac_running = False
            try:
                result = subprocess.run(
                    ["pgrep", "-f", "golf_sim"],
                    capture_output=True,
                    text=True,
                    timeout=1,
                )
                pitrac_running = result.returncode == 0
            except Exception:
                pass

            activemq_running = False
            try:
                result = subprocess.run(["ss", "-tln"], capture_output=True, text=True, timeout=1)
                activemq_running = ":61616" in result.stdout or ":61613" in result.stdout
            except Exception:
                pass

            listener_stats = self.listener.get_stats() if self.listener else {}

            return {
                "status": "healthy" if mq_connected else "degraded",
                "activemq_connected": mq_connected,
                "activemq_running": activemq_running,
                "pitrac_running": pitrac_running,
                "websocket_clients": self.connection_manager.connection_count,
                "listener_stats": listener_stats,
            }

        @self.app.get("/api/stats")
        async def get_stats() -> Dict[str, Any]:
            return {
                "websocket_connections": self.connection_manager.connection_count,
                "listener": self.listener.get_stats() if self.listener else None,
                "shot_history_count": len(self.shot_store.get_history(100)),
            }

        # Configuration API endpoints
        @self.app.get("/config", response_class=HTMLResponse)
        async def config_page(request: Request) -> Response:
            """Serve configuration UI page"""
            return self.templates.TemplateResponse("config.html", {"request": request})

        @self.app.get("/api/config")
        async def get_config(key: Optional[str] = None) -> Dict[str, Any]:
            """Get merged configuration or specific key"""
            config = self.config_manager.get_config(key)
            if config is None and key:
                return {"error": f"Configuration key '{key}' not found"}
            return {"data": config}

        @self.app.get("/api/config/defaults")
        async def get_defaults(key: Optional[str] = None) -> Dict[str, Any]:
            """Get system default configuration"""
            if key:
                config = self.config_manager.get_default(key)
                if config is None:
                    return {"error": f"Configuration key '{key}' not found"}
                return {"data": config}
            else:
                config = self.config_manager.get_all_defaults_with_metadata()
                return {"data": config}

        @self.app.get("/api/config/user")
        async def get_user_settings() -> Dict[str, Any]:
            """Get user overrides only"""
            return {"data": self.config_manager.get_user_settings()}

        @self.app.get("/api/config/categories")
        async def get_categories() -> Dict[str, Dict[str, List[str]]]:
            """Get configuration organized by categories"""
            return self.config_manager.get_categories()

        @self.app.get("/api/config/metadata")
        async def get_config_metadata() -> Dict[str, Any]:
            """Get configuration metadata including descriptions"""
            metadata = self.config_manager.load_configurations_metadata()
            return metadata.get("settings", {})

        @self.app.get("/api/config/diff")
        async def get_config_diff() -> Dict[str, Any]:
            """Get differences between user settings and defaults"""
            return {"data": self.config_manager.get_diff()}

        @self.app.put("/api/config/{key:path}")
        async def update_config(key: str, request: Request) -> Dict[str, Any]:
            """Update a configuration value"""
            try:
                body = await request.json()
                value = body.get("value")

                # Validate the value
                is_valid, error_msg = self.config_manager.validate_config(key, value)
                if not is_valid:
                    return {"error": error_msg}

                # Set the value
                success, message, requires_restart = self.config_manager.set_config(key, value)

                if success:
                    # Broadcast update to WebSocket clients
                    await self.connection_manager.broadcast(
                        {
                            "type": "config_update",
                            "key": key,
                            "value": value,
                            "requires_restart": requires_restart,
                        }
                    )

                    return {
                        "success": True,
                        "message": message,
                        "requires_restart": requires_restart,
                    }
                else:
                    return {"error": message}

            except Exception as e:
                logger.error(f"Failed to update config: {e}")
                return {"error": str(e)}

        @self.app.post("/api/config/reset")
        async def reset_config() -> Dict[str, Any]:
            """Reset all user settings to defaults"""
            success, message = self.config_manager.reset_all()

            if success:
                await self.connection_manager.broadcast({"type": "config_reset"})

            return {"success": success, "message": message}

        @self.app.post("/api/config/reload")
        async def reload_config() -> Dict[str, str]:
            """Reload configuration from disk"""
            self.config_manager.reload()
            return {"status": "Configuration reloaded"}

        @self.app.get("/api/config/export")
        async def export_config() -> Dict[str, Any]:
            """Export configuration for backup/sharing"""
            return self.config_manager.export_config()

        @self.app.post("/api/config/import")
        async def import_config(request: Request) -> Dict[str, Any]:
            """Import configuration from exported data"""
            try:
                config_data = await request.json()
                success, message = self.config_manager.import_config(config_data)

                if success:
                    await self.connection_manager.broadcast({"type": "config_import"})

                return {"success": success, "message": message}
            except Exception as e:
                logger.error(f"Failed to import config: {e}")
                return {"error": str(e)}

        # PiTrac process management endpoints
        @self.app.post("/api/pitrac/start")
        async def start_pitrac() -> Dict[str, Any]:
            """Start the PiTrac launch monitor process"""
            result = await self.pitrac_manager.start()
            logger.info(f"PiTrac start request: {result}")
            return result

        @self.app.post("/api/pitrac/stop")
        async def stop_pitrac() -> Dict[str, Any]:
            """Stop the PiTrac launch monitor process"""
            result = await self.pitrac_manager.stop()
            logger.info(f"PiTrac stop request: {result}")
            return result

        @self.app.post("/api/pitrac/restart")
        async def restart_pitrac() -> Dict[str, Any]:
            """Restart the PiTrac launch monitor process"""
            result = await self.pitrac_manager.restart()
            logger.info(f"PiTrac restart request: {result}")
            return result

        @self.app.get("/api/pitrac/status")
        async def pitrac_status() -> Dict[str, Any]:
            """Get the status of the PiTrac launch monitor process"""
            return self.pitrac_manager.get_status()

        @self.app.get("/calibration", response_class=HTMLResponse)
        async def calibration_page(request: Request) -> Response:
            """Serve calibration UI page"""
            return self.templates.TemplateResponse("calibration.html", {"request": request})

        @self.app.get("/api/calibration/status")
        async def calibration_status() -> Dict[str, Any]:
            """Get calibration status for all cameras"""
            return self.calibration_manager.get_status()

        @self.app.get("/api/calibration/data")
        async def get_calibration_data() -> Dict[str, Any]:
            """Get current calibration data"""
            return self.calibration_manager.get_calibration_data()

        @self.app.post("/api/calibration/ball-location/{camera}")
        async def check_ball_location(camera: str) -> Dict[str, Any]:
            """Check ball location for calibration setup"""
            if camera not in ["camera1", "camera2"]:
                return {"status": "error", "message": "Invalid camera"}
            return await self.calibration_manager.check_ball_location(camera)

        @self.app.post("/api/calibration/auto/{camera}")
        async def run_auto_calibration(camera: str) -> Dict[str, Any]:
            """Run automatic calibration for specified camera"""
            if camera not in ["camera1", "camera2"]:
                return {"status": "error", "message": "Invalid camera"}
            return await self.calibration_manager.run_auto_calibration(camera)

        @self.app.post("/api/calibration/manual/{camera}")
        async def run_manual_calibration(camera: str) -> Dict[str, Any]:
            """Run manual calibration for specified camera"""
            if camera not in ["camera1", "camera2"]:
                return {"status": "error", "message": "Invalid camera"}
            return await self.calibration_manager.run_manual_calibration(camera)

        @self.app.post("/api/calibration/capture/{camera}")
        async def capture_still(camera: str) -> Dict[str, Any]:
            """Capture a still image for camera setup"""
            if camera not in ["camera1", "camera2"]:
                return {"status": "error", "message": "Invalid camera"}
            return await self.calibration_manager.capture_still_image(camera)

        @self.app.post("/api/calibration/stop")
        async def stop_calibration() -> Dict[str, Any]:
            """Stop any running calibration process"""
            return await self.calibration_manager.stop_calibration()

        @self.app.get("/testing", response_class=HTMLResponse)
        async def testing_page(request: Request) -> Response:
            """Serve testing tools UI page"""
            return self.templates.TemplateResponse("testing.html", {"request": request})

        @self.app.get("/api/testing/tools")
        async def get_testing_tools() -> Dict[str, Any]:
            """Get available testing tools organized by category"""
            return self.testing_manager.get_available_tools()

        @self.app.post("/api/testing/run/{tool_id}")
        async def run_testing_tool(tool_id: str) -> Dict[str, Any]:
            """Run a specific testing tool"""
            if self.pitrac_manager.is_running():
                return {
                    "status": "error",
                    "message": "Cannot run testing tools while PiTrac is running. Please stop PiTrac first.",
                }

            task = asyncio.create_task(self._run_tool_async(tool_id))
            self.background_tasks.add(task)
            task.add_done_callback(self.background_tasks.discard)

            return {"status": "started", "message": f"Tool {tool_id} started", "tool_id": tool_id}

        @self.app.post("/api/testing/stop/{tool_id}")
        async def stop_testing_tool(tool_id: str) -> Dict[str, Any]:
            """Stop a running testing tool"""
            return await self.testing_manager.stop_tool(tool_id)

        @self.app.get("/api/testing/status")
        async def get_testing_status() -> Dict[str, Any]:
            """Get status of running testing tools"""
            running = self.testing_manager.get_running_tools()

            results = {}
            if hasattr(self.testing_manager, "completed_results"):
                results = self.testing_manager.completed_results
                self.testing_manager.completed_results = {}

            return {"running": running, "results": results}

        @self.app.post("/api/testing/upload-image")
        async def upload_test_image(file: UploadFile = File(...)) -> Dict[str, Any]:
            """Upload a test image for pipeline testing"""
            try:
                # Validate file type
                if not file.content_type or not file.content_type.startswith("image/"):
                    return {"status": "error", "message": "File must be an image"}

                # Save to TestImages directory
                test_images_dir = Path.home() / "LM_Shares/TestImages"
                test_images_dir.mkdir(parents=True, exist_ok=True)

                # Use original filename or generate one
                filename = file.filename or f"test_{datetime.now().strftime('%Y%m%d_%H%M%S')}.png"
                file_path = test_images_dir / filename

                # Save uploaded file
                content = await file.read()
                with open(file_path, "wb") as f:
                    f.write(content)

                logger.info(f"Test image uploaded: {file_path}")

                return {
                    "status": "success",
                    "message": f"Image uploaded successfully",
                    "filename": filename,
                    "path": str(file_path)
                }

            except Exception as e:
                logger.error(f"Error uploading test image: {e}")
                return {"status": "error", "message": str(e)}

        @self.app.get("/api/cameras/detect")
        async def detect_cameras() -> Dict[str, Any]:
            """Auto-detect connected cameras"""
            logger.info("Camera auto-detection initiated")
            try:
                detector = CameraDetector()
                logger.info(f"Using detection tool: {detector.camera_cmd}, Pi model: {detector.pi_model}")

                result = detector.detect()

                if result.get("success"):
                    camera_count = len(result.get("cameras", []))
                    logger.info(f"Camera detection successful: {camera_count} camera(s) found")
                    if camera_count > 0:
                        for cam in result["cameras"]:
                            logger.info(
                                f"  - Camera {cam['index']}: {cam['model']} ({cam['sensor']}) on {cam['port']}, Type: {cam['pitrac_type']}"
                            )

                    config = result.get("configuration", {})
                    logger.info(
                        f"Recommended configuration - Slot1: Type {config.get('slot1', {}).get('type', 'N/A')}, Slot2: Type {config.get('slot2', {}).get('type', 'N/A')}"
                    )
                else:
                    logger.warning(
                        f"Camera detection completed with no cameras found: {result.get('message', 'Unknown error')}"
                    )
                    if result.get("warnings"):
                        for warning in result["warnings"]:
                            logger.warning(f"  Warning: {warning}")

                return result
            except Exception as e:
                logger.error(f"Camera detection failed with exception: {e}", exc_info=True)
                return {
                    "success": False,
                    "message": f"Detection failed: {str(e)}",
                    "cameras": [],
                    "configuration": {
                        "slot1": {"type": 4, "lens": 1},
                        "slot2": {"type": 4, "lens": 1},
                    },
                }

        @self.app.get("/api/cameras/types")
        async def get_camera_types() -> Dict[str, Any]:
            """Get available camera types and their descriptions"""
            detector = CameraDetector()
            return {
                "camera_types": detector.get_camera_types(),
                "lens_types": detector.get_lens_types(),
            }

        @self.app.get("/logs", response_class=HTMLResponse)
        async def logs_page(request: Request) -> Response:
            """Serve logs viewer page"""
            return self.templates.TemplateResponse("logs.html", {"request": request})

        @self.app.websocket("/ws/logs")
        async def websocket_logs(websocket: WebSocket) -> None:
            """WebSocket endpoint for streaming logs"""
            await websocket.accept()

            try:
                data = await websocket.receive_json()
                service = data.get("service", "pitrac")

                await self._stream_service_logs(websocket, service)

            except WebSocketDisconnect:
                logger.debug("Logs WebSocket client disconnected")
            except Exception as e:
                logger.error(f"Error in logs WebSocket: {e}")
                try:
                    await websocket.close()
                except Exception:
                    pass

        @self.app.get("/api/logs/services")
        async def get_log_services() -> Dict[str, List[Dict[str, Any]]]:
            """Get list of available services and their status"""
            services = []

            pitrac_status = self.pitrac_manager.get_status()
            services.append(
                {
                    "id": "pitrac",
                    "name": "PiTrac Camera 1",
                    "status": "running" if pitrac_status["is_running"] else "stopped",
                    "pid": pitrac_status.get("pid"),
                }
            )

            if pitrac_status.get("is_dual_camera"):
                services.append(
                    {
                        "id": "pitrac_camera2",
                        "name": "PiTrac Camera 2",
                        "status": ("running" if pitrac_status.get("camera2_running") else "stopped"),
                        "pid": pitrac_status.get("camera2_pid"),
                    }
                )

            activemq_running = False
            try:
                result = subprocess.run(
                    ["systemctl", "is-active", "activemq"],
                    capture_output=True,
                    text=True,
                    timeout=1,
                )
                activemq_running = result.stdout.strip() == "active"
            except Exception:
                pass

            services.append(
                {
                    "id": "activemq",
                    "name": "ActiveMQ Broker",
                    "status": "running" if activemq_running else "stopped",
                    "pid": None,
                }
            )

            services.append(
                {
                    "id": "pitrac-web",
                    "name": "PiTrac Web Server",
                    "status": "running",
                    "pid": os.getpid(),
                }
            )

            return {"services": services}

    async def _stream_service_logs(self, websocket: WebSocket, service: str) -> None:
        """Stream logs for a specific service via WebSocket"""
        try:
            if service == "pitrac":
                log_file = self.pitrac_manager.log_file
                await self._stream_file_logs(websocket, log_file)
            elif service == "pitrac_camera2":
                log_file = self.pitrac_manager.camera2_log_file
                await self._stream_file_logs(websocket, log_file)
            elif service == "activemq":
                await self._stream_systemd_logs(websocket, "activemq")
            elif service == "pitrac-web":
                await self._stream_systemd_logs(websocket, "pitrac-web")
            else:
                await websocket.send_json({"error": f"Unknown service: {service}"})

        except Exception as e:
            logger.error(f"Error streaming logs for {service}: {e}")
            try:
                await websocket.send_json({"error": f"Failed to stream logs: {str(e)}"})
            except Exception:
                pass

    async def _stream_systemd_logs(self, websocket: WebSocket, unit: str) -> None:
        """Stream systemd journal logs for a unit"""
        try:
            recent_proc = await asyncio.create_subprocess_exec(
                "journalctl",
                "-u",
                unit,
                "-n",
                "100",
                "--no-pager",
                "--output=json",
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )

            if recent_proc.stdout:
                async for line in recent_proc.stdout:
                    try:
                        log_entry = json.loads(line.decode("utf-8", errors="replace"))
                        await websocket.send_json(
                            {
                                "timestamp": log_entry.get("__REALTIME_TIMESTAMP", ""),
                                "message": log_entry.get("MESSAGE", ""),
                                "level": log_entry.get("PRIORITY", "6"),
                                "service": unit,
                                "historical": True,
                            }
                        )
                    except json.JSONDecodeError:
                        continue
                    except WebSocketDisconnect:
                        return

            await recent_proc.wait()

            follow_proc = await asyncio.create_subprocess_exec(
                "journalctl",
                "-u",
                unit,
                "-f",
                "--output=json",
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )

            if follow_proc.stdout:
                async for line in follow_proc.stdout:
                    try:
                        log_entry = json.loads(line.decode("utf-8", errors="replace"))
                        await websocket.send_json(
                            {
                                "timestamp": log_entry.get("__REALTIME_TIMESTAMP", ""),
                                "message": log_entry.get("MESSAGE", ""),
                                "level": log_entry.get("PRIORITY", "6"),
                                "service": unit,
                                "historical": False,
                            }
                        )
                    except json.JSONDecodeError:
                        continue
                    except WebSocketDisconnect:
                        follow_proc.terminate()
                        return

        except Exception as e:
            logger.error(f"Error streaming systemd logs: {e}")

    async def _stream_file_logs(self, websocket: WebSocket, log_file: Path) -> None:
        """Stream logs from a file"""
        try:
            if not log_file.exists():
                await websocket.send_json({"message": f"Log file not found: {log_file}", "level": "warning"})
                return

            with open(log_file, "r") as f:
                lines = f.readlines()
                recent = lines[-100:] if len(lines) > 100 else lines
                for line in recent:
                    await websocket.send_json({"message": line.rstrip(), "historical": True})

            follow_proc = await asyncio.create_subprocess_exec(
                "tail",
                "-f",
                str(log_file),
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )

            if follow_proc.stdout:
                async for line in follow_proc.stdout:
                    try:
                        await websocket.send_json(
                            {
                                "message": line.decode("utf-8", errors="replace").rstrip(),
                                "historical": False,
                            }
                        )
                    except WebSocketDisconnect:
                        follow_proc.terminate()
                        return

        except Exception as e:
            logger.error(f"Error streaming file logs: {e}")

    def _load_config(self) -> Dict[str, Any]:
        if not CONFIG_FILE.exists():
            logger.warning(f"Config file not found: {CONFIG_FILE}")
            return {}

        try:
            with open(CONFIG_FILE, "r") as f:
                config = yaml.safe_load(f) or {}
                logger.info(f"Loaded config from {CONFIG_FILE}")
                return config
        except Exception as e:
            logger.error(f"Error loading config: {e}")
            return {}

    def setup_activemq(self, loop: Optional[asyncio.AbstractEventLoop] = None) -> Optional[stomp.Connection]:
        try:
            config = self._load_config()

            network_config = config.get("network", {})
            broker_address = network_config.get("broker_address", DEFAULT_BROKER)
            username = network_config.get("username", DEFAULT_USERNAME)
            password = network_config.get("password", DEFAULT_PASSWORD)

            if broker_address.startswith("tcp://"):
                broker_address = broker_address[6:]

            broker_host = broker_address.split(":")[0] if ":" in broker_address else broker_address

            conn = stomp.Connection([(broker_host, STOMP_PORT)])

            self.listener = ActiveMQListener(self.shot_store, self.connection_manager, self.parser, loop)
            conn.set_listener("", self.listener)

            conn.connect(username, password, wait=True)
            conn.subscribe(destination="/topic/Golf.Sim", id=1, ack="auto")

            logger.info(f"Connected to ActiveMQ at {broker_host}:{STOMP_PORT}")
            return conn

        except stomp.exception.ConnectFailedException as e:
            logger.error(f"Failed to connect to ActiveMQ broker: {e}")
            return None
        except Exception as e:
            logger.error(f"Unexpected error connecting to ActiveMQ: {e}", exc_info=True)
            return None

    async def reconnect_activemq_loop(self) -> None:
        """Background task to maintain ActiveMQ connection"""
        loop = asyncio.get_event_loop()
        retry_delay = 5
        max_retry_delay = 60

        while not self.shutdown_flag:
            try:
                if self.mq_conn and self.mq_conn.is_connected():
                    retry_delay = 5
                    await asyncio.sleep(10)
                    continue

                logger.info("ActiveMQ connection lost, attempting to reconnect...")

                if self.mq_conn:
                    try:
                        self.mq_conn.disconnect()
                    except Exception:
                        pass
                    self.mq_conn = None

                self.mq_conn = self.setup_activemq(loop)

                if self.mq_conn:
                    logger.info("Successfully reconnected to ActiveMQ")
                    retry_delay = 5
                else:
                    logger.warning(f"Failed to reconnect to ActiveMQ, retrying in {retry_delay} seconds")
                    await asyncio.sleep(retry_delay)
                    retry_delay = min(retry_delay * 2, max_retry_delay)

            except Exception as e:
                logger.error(f"Error in reconnection loop: {e}", exc_info=True)
                await asyncio.sleep(retry_delay)
                retry_delay = min(retry_delay * 2, max_retry_delay)

    async def startup_event(self) -> None:
        logger.info("Starting PiTrac Web Server...")
        loop = asyncio.get_event_loop()

        self.mq_conn = self.setup_activemq(loop)

        if not self.mq_conn:
            logger.warning("Could not connect to ActiveMQ at startup - will retry in background")

        self.reconnect_task = asyncio.create_task(self.reconnect_activemq_loop())
        logger.info("Started ActiveMQ reconnection monitor")

    async def _run_tool_async(self, tool_id: str) -> None:
        """Helper method to run a testing tool asynchronously"""
        try:
            result = await self.testing_manager.run_tool(tool_id)

            if not hasattr(self.testing_manager, "completed_results"):
                self.testing_manager.completed_results = {}
            self.testing_manager.completed_results[tool_id] = result

            logger.info(f"Testing tool {tool_id} completed with status: {result.get('status')}")
        except Exception as e:
            logger.error(f"Error running testing tool {tool_id}: {e}")
            if not hasattr(self.testing_manager, "completed_results"):
                self.testing_manager.completed_results = {}
            self.testing_manager.completed_results[tool_id] = {"status": "error", "message": str(e)}

    async def shutdown_event(self) -> None:
        logger.info("Shutting down PiTrac Web Server...")

        self.shutdown_flag = True

        for task in self.background_tasks:
            if not task.done():
                task.cancel()

        if self.background_tasks:
            await asyncio.gather(*self.background_tasks, return_exceptions=True)

        if self.reconnect_task and not self.reconnect_task.done():
            self.reconnect_task.cancel()
            try:
                await self.reconnect_task
            except asyncio.CancelledError:
                pass

        if self.mq_conn:
            try:
                self.mq_conn.disconnect()
                logger.info("Disconnected from ActiveMQ")
            except Exception as e:
                logger.error(f"Error disconnecting from ActiveMQ: {e}")

        for ws in self.connection_manager.connections:
            try:
                await ws.close()
            except Exception:
                pass


server = PiTracServer()
app = server.app

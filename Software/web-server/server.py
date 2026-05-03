import asyncio
import json
import logging
import os
import shutil
import subprocess
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

from fastapi import FastAPI, File, Request, UploadFile, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, Response
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from calibration_manager import CalibrationManager
from camera_detector import CameraDetector
from config_manager import ConfigurationManager
from constants import (
    IMAGES_DIR,
    MPS_TO_MPH,
)
from managers import ConnectionManager, ShotDataStore
from models import ShotData
from parsers import ShotDataParser
from pitrac_manager import PiTracProcessManager
from strobe_calibration_manager import StrobeCalibrationManager
from testing_tools_manager import TestingToolsManager
from update_manager import UpdateManager

logger = logging.getLogger(__name__)

CHARUCO_SQUARES_X = 8
CHARUCO_SQUARES_Y = 11
CHARUCO_SQUARE_LENGTH = 0.023
CHARUCO_MARKER_LENGTH = 0.017

# Locked to defeat AE/AWB/tuning drift across calibration frames (sub-pixel
# corner bias). Tuning file matches production rcPi5GS.sh; shutter is shorter
# than production (11 vs 20 ms) to limit hand-tremor blur.
RPICAM_TUNING_FILE = "/usr/share/libcamera/ipa/rpi/pisp/imx296_noir.json"
RPICAM_CAL_SHUTTER_US = 11000
RPICAM_CAL_GAIN = 1.3


class RpicamVideoStream:
    """cv2.VideoCapture-compatible wrapper around rpicam-vid for Pi CSI cameras."""

    _MAX_BUF = 10 * 1024 * 1024  # 10 MB — far larger than any single JPEG frame

    def __init__(self, camera_index: int, width: int, height: int):
        import cv2
        import numpy as np
        self._cv2 = cv2
        self._np = np
        self._width = width
        self._height = height
        self._actual_width: Optional[int] = None
        self._actual_height: Optional[int] = None
        self._buf = bytearray()
        self._proc = subprocess.Popen(
            [
                "rpicam-vid", "--camera", str(camera_index),
                "--width", str(width), "--height", str(height),
                "--framerate", "15",
                "--codec", "mjpeg", "--quality", "100", "--output", "-",
                "--timeout", "0", "--nopreview",
                "--shutter", str(RPICAM_CAL_SHUTTER_US),
                "--gain", str(RPICAM_CAL_GAIN),
                "--awbgains", "1.0,1.0",
                "--denoise", "cdn_off",
                "--tuning-file", RPICAM_TUNING_FILE,
            ],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        )
        self._opened = self._proc.poll() is None

    def isOpened(self) -> bool:
        return self._opened and self._proc.poll() is None

    def read(self):
        """Read one JPEG frame, decode to BGR numpy array."""
        CHUNK = 65536
        while self.isOpened():
            data = self._proc.stdout.read(CHUNK)
            if not data:
                self._opened = False
                return False, None
            self._buf.extend(data)

            # Guard against unbounded growth from a corrupted stream
            if len(self._buf) > self._MAX_BUF:
                self._buf.clear()
                continue

            # Scan for a complete JPEG (SOI 0xFFD8 … EOI 0xFFD9)
            start = self._buf.find(b'\xff\xd8')
            if start < 0:
                self._buf.clear()
                continue
            end = self._buf.find(b'\xff\xd9', start + 2)
            if end < 0:
                continue
            jpeg = bytes(self._buf[start:end + 2])
            del self._buf[:end + 2]
            frame = self._cv2.imdecode(
                self._np.frombuffer(jpeg, self._np.uint8), self._cv2.IMREAD_COLOR)
            if frame is not None:
                if self._actual_width is None:
                    self._actual_height, self._actual_width = frame.shape[:2]
                return True, frame
        return False, None

    def set(self, prop_id, value):
        pass  # resolution is fixed at construction

    def get(self, prop_id):
        import cv2
        if prop_id == cv2.CAP_PROP_FRAME_WIDTH:
            return float(self._actual_width or self._width)
        if prop_id == cv2.CAP_PROP_FRAME_HEIGHT:
            return float(self._actual_height or self._height)
        return 0.0

    def release(self):
        if self._proc and self._proc.poll() is None:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self._proc.kill()
        if self._proc and self._proc.stderr:
            try:
                err = self._proc.stderr.read()
                if err:
                    logger.debug(f"rpicam-vid stderr: {err.decode(errors='replace').strip()}")
            except Exception:
                pass
        self._opened = False

    def __del__(self):
        self.release()


def open_camera(camera_index: int, width: int = 1280, height: int = 720):
    """Open a camera, falling back to rpicam-vid for Pi CSI cameras."""
    import cv2
    cap = cv2.VideoCapture(camera_index)
    if cap.isOpened():
        # Validate with a test read — some V4L2 nodes report open but can't capture
        ret, frame = cap.read()
        if ret and frame is not None:
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
            return cap
        logger.info(f"cv2.VideoCapture({camera_index}) opened but test read failed")
        cap.release()

    if shutil.which("rpicam-vid"):
        logger.info(f"cv2.VideoCapture({camera_index}) unusable, trying rpicam-vid")
        stream = RpicamVideoStream(camera_index, width, height)
        if stream.isOpened():
            ret, frame = stream.read()
            if ret and frame is not None:
                return stream
            logger.info(f"rpicam-vid started but no frames for camera {camera_index}")
        stream.release()

    return None


class PiTracServer:

    _BASE_DIR = Path(__file__).resolve().parent

    def __init__(self):
        self.app = FastAPI(title="PiTrac Dashboard")
        self.templates = Jinja2Templates(directory=str(self._BASE_DIR / "templates"))
        self.connection_manager = ConnectionManager()
        self.shot_store = ShotDataStore()
        self.parser = ShotDataParser()
        self.config_manager = ConfigurationManager()
        self.pitrac_manager = PiTracProcessManager(self.config_manager)
        self.calibration_manager = CalibrationManager(self.config_manager)
        self.testing_manager = TestingToolsManager(self.config_manager)
        self.strobe_calibration_manager = StrobeCalibrationManager(self.config_manager)
        self.update_manager = UpdateManager()
        self.update_manager.set_broadcast_callback(self.connection_manager.broadcast)
        self.shutdown_flag = False
        self.background_tasks: set[asyncio.Task] = set()
        self._active_cameras: Dict[int, str] = {}  # camera_index -> endpoint name
        IMAGES_DIR.mkdir(parents=True, exist_ok=True)

        self.app.mount("/static", StaticFiles(directory=str(self._BASE_DIR / "static")), name="static")
        self.app.mount("/images", StaticFiles(directory=str(IMAGES_DIR)), name="images")

        self._setup_routes()

        @self.app.on_event("startup")
        async def _startup():
            await self.startup_event()

        @self.app.on_event("shutdown")
        async def _shutdown():
            await self.shutdown_event()

    def _setup_routes(self) -> None:

        @self.app.get("/", response_class=HTMLResponse)
        async def dashboard(request: Request) -> Response:
            return self.templates.TemplateResponse(
                request,
                "dashboard.html",
                context={"shot": self.shot_store.get().to_dict()},
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

        @self.app.post("/api/internal/shot-result")
        async def receive_shot_result(request: Request) -> Dict[str, str]:
            """Receives shot results from the C++ pitrac_lm process via HTTP POST."""
            body = await request.json()

            result_type_int = int(body.get("result_type", 0))
            result_type_str = self.parser._get_result_type_string(result_type_int)
            speed_mps = float(body.get("speed_mps", 0))
            message = str(body.get("message", ""))

            is_status = result_type_str in self.parser._get_status_message_strings()
            is_fake_hit = result_type_int == 7 and message in [
                "Club type was set", "Test message", "Configuration update",
            ]

            if is_status or is_fake_hit:
                current = self.shot_store.get()
                shot_data = ShotData(
                    speed=current.speed,
                    carry=current.carry,
                    launch_angle=current.launch_angle,
                    side_angle=current.side_angle,
                    back_spin=current.back_spin,
                    side_spin=current.side_spin,
                    result_type=result_type_str,
                    message=message,
                    timestamp=datetime.now().isoformat(),
                )
            else:
                shot_data = ShotData(
                    speed=round(speed_mps * MPS_TO_MPH, 1),
                    carry=float(body.get("carry", 0)),
                    launch_angle=round(float(body.get("launch_angle", 0)), 1),
                    side_angle=round(float(body.get("side_angle", 0)), 1),
                    back_spin=int(body.get("back_spin", 0)),
                    side_spin=int(body.get("side_spin", 0)),
                    result_type=result_type_str,
                    message=message,
                    timestamp=datetime.now().isoformat(),
                )

            self.shot_store.update(shot_data)
            await self.connection_manager.broadcast(shot_data.to_dict())
            return {"status": "ok"}

        @self.app.post("/api/internal/image-ready")
        async def receive_image_ready(request: Request) -> Dict[str, str]:
            body = await request.json()
            filename = body.get("filename", "")
            if filename:
                await self.connection_manager.broadcast({
                    "type": "image_ready",
                    "filename": filename,
                })
            return {"status": "ok"}

        @self.app.get("/health")
        async def health_check() -> Dict[str, Union[str, bool, int]]:
            pitrac_running = False
            try:
                result = subprocess.run(
                    ["pgrep", "-f", "pitrac_lm"],
                    capture_output=True,
                    text=True,
                    timeout=1,
                )
                pitrac_running = result.returncode == 0
            except Exception:
                pass

            return {
                "status": "healthy",
                "pitrac_running": pitrac_running,
                "websocket_clients": self.connection_manager.connection_count,
            }

        @self.app.get("/api/stats")
        async def get_stats() -> Dict[str, Any]:
            return {
                "websocket_connections": self.connection_manager.connection_count,
                "transport": "http",
                "shot_history_count": len(self.shot_store.get_history(100)),
            }

        # Configuration API endpoints
        @self.app.get("/config", response_class=HTMLResponse)
        async def config_page(request: Request) -> Response:
            """Serve configuration UI page"""
            return self.templates.TemplateResponse(request, "config.html")

        @self.app.get("/api/config")
        async def get_config(key: Optional[str] = None):
            """Get merged configuration or specific key"""
            config = self.config_manager.get_config(key)
            if config is None and key:
                return JSONResponse(
                    status_code=404,
                    content={"error": f"Configuration key '{key}' not found"},
                )
            return {"data": config}

        @self.app.get("/api/config/defaults")
        async def get_defaults(key: Optional[str] = None) -> Dict[str, Any]:
            """Get system default configuration"""
            if key:
                config = self.config_manager.get_default(key)
                if config is None:
                    return JSONResponse(
                        status_code=404,
                        content={"error": f"Configuration key '{key}' not found"}
                    )
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
        async def update_config(key: str, request: Request):
            """Update a configuration value"""
            try:
                body = await request.json()
                value = body.get("value")

                # Check that the key exists in metadata
                metadata = self.config_manager.load_configurations_metadata()
                settings_metadata = metadata.get("settings", {})
                if key not in settings_metadata:
                    return JSONResponse(
                        status_code=400,
                        content={"error": "Unknown configuration key"},
                    )

                # Validate the value
                is_valid, error_msg = self.config_manager.validate_config(key, value)
                if not is_valid:
                    return JSONResponse(status_code=400, content={"error": error_msg})

                # Set the value
                success, message, requires_restart = self.config_manager.set_config(key, value)

                if success:
                    # Rebuild the generated config so the C++ process picks up changes
                    generated_config_path = self.config_manager.generate_golf_sim_config()
                    logger.info(f"Generated config file at: {generated_config_path}")

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
                    return JSONResponse(status_code=500, content={"error": message})

            except Exception as e:
                logger.error(f"Failed to update config: {e}")
                return JSONResponse(status_code=500, content={"error": str(e)})

        @self.app.post("/api/config/reset")
        async def reset_config():
            """Reset all user settings to defaults"""
            success, message = self.config_manager.reset_all()

            if success:
                self.config_manager.generate_golf_sim_config()
                await self.connection_manager.broadcast({"type": "config_reset"})
                return {"success": True, "message": message}

            return JSONResponse(status_code=500, content={"error": message})

        @self.app.post("/api/config/reload")
        async def reload_config():
            """Reload configuration from disk"""
            try:
                self.config_manager.reload()
                self.config_manager.generate_golf_sim_config()
                return {"status": "Configuration reloaded"}
            except Exception as e:
                logger.error(f"Failed to reload config: {e}")
                return JSONResponse(status_code=500, content={"error": str(e)})

        @self.app.get("/api/config/export")
        async def export_config() -> Dict[str, Any]:
            """Export configuration for backup/sharing"""
            return self.config_manager.export_config()

        @self.app.post("/api/config/import")
        async def import_config(request: Request):
            """Import configuration from exported data"""
            try:
                config_data = await request.json()
                success, message = self.config_manager.import_config(config_data)

                if success:
                    self.config_manager.generate_golf_sim_config()
                    await self.connection_manager.broadcast({"type": "config_import"})
                    return {"success": True, "message": message}

                return JSONResponse(status_code=400, content={"error": message})
            except Exception as e:
                logger.error(f"Failed to import config: {e}")
                return JSONResponse(status_code=500, content={"error": str(e)})

        # PiTrac process management endpoints
        @self.app.post("/api/pitrac/start")
        async def start_pitrac() -> Dict[str, Any]:
            """Start the PiTrac launch monitor process"""
            safety = self.strobe_calibration_manager.is_strobe_safe()
            if not safety["safe"]:
                return {"status": "error", "message": safety["reason"]}
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
            safety = self.strobe_calibration_manager.is_strobe_safe()
            if not safety["safe"]:
                return {"status": "error", "message": safety["reason"]}
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
            return self.templates.TemplateResponse(request, "calibration.html")

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
            if self.calibration_manager.loop is None:
                return {"status": "error", "message": "Server still starting up, please retry in a moment"}
            return await self.calibration_manager.check_ball_location(camera)

        @self.app.post("/api/calibration/auto/{camera}")
        async def run_auto_calibration(camera: str) -> Dict[str, Any]:
            """Run automatic calibration for specified camera"""
            if camera not in ["camera1", "camera2"]:
                return {"status": "error", "message": "Invalid camera"}
            safety = self.strobe_calibration_manager.is_strobe_safe()
            if not safety["safe"]:
                return {"status": "error", "message": safety["reason"]}
            if self.calibration_manager.loop is None:
                return {"status": "error", "message": "Server still starting up, please retry in a moment"}
            return await self.calibration_manager.run_auto_calibration(camera)

        @self.app.post("/api/calibration/manual/{camera}")
        async def run_manual_calibration(camera: str) -> Dict[str, Any]:
            """Run manual calibration for specified camera"""
            if camera not in ["camera1", "camera2"]:
                return {"status": "error", "message": "Invalid camera"}
            safety = self.strobe_calibration_manager.is_strobe_safe()
            if not safety["safe"]:
                return {"status": "error", "message": safety["reason"]}
            if self.calibration_manager.loop is None:
                return {"status": "error", "message": "Server still starting up, please retry in a moment"}
            return await self.calibration_manager.run_manual_calibration(camera)

        @self.app.post("/api/calibration/capture/{camera}")
        async def capture_still(camera: str) -> Dict[str, Any]:
            """Capture a still image for camera setup"""
            if camera not in ["camera1", "camera2"]:
                return {"status": "error", "message": "Invalid camera"}
            if camera == "camera2":
                safety = self.strobe_calibration_manager.is_strobe_safe()
                if not safety["safe"]:
                    return {"status": "error", "message": safety["reason"]}
            if self.calibration_manager.loop is None:
                return {"status": "error", "message": "Server still starting up, please retry in a moment"}
            return await self.calibration_manager.capture_still_image(camera)

        @self.app.get("/api/calibration/charuco-board")
        async def generate_charuco_board() -> Response:
            try:
                import cv2
                import numpy as np
            except ImportError as e:
                return Response(
                    content=json.dumps({"status": "error", "message": str(e)}),
                    media_type="application/json",
                    status_code=500,
                )

            # Match the board used by run_distortion_calibration
            aruco_dict = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50)
            board = cv2.aruco.CharucoBoard(
                (CHARUCO_SQUARES_X, CHARUCO_SQUARES_Y),
                CHARUCO_SQUARE_LENGTH, CHARUCO_MARKER_LENGTH, aruco_dict,
            )

            DPI = 300
            px_per_mm = DPI / 25.4
            square_px = round(CHARUCO_SQUARE_LENGTH * 1000 * px_per_mm)
            board_w = CHARUCO_SQUARES_X * square_px
            board_h = CHARUCO_SQUARES_Y * square_px
            board_img = board.generateImage((board_w, board_h), marginSize=0)

            a4_w, a4_h = round(210 * px_per_mm), round(297 * px_per_mm)
            page = np.full((a4_h, a4_w), 255, dtype=np.uint8)
            x_off = (a4_w - board_w) // 2
            y_off = (a4_h - board_h) // 2
            page[y_off:y_off + board_h, x_off:x_off + board_w] = board_img

            success, png_bytes = cv2.imencode(".png", page)
            if not success:
                return Response(
                    content=json.dumps({"status": "error", "message": "Failed to encode image"}),
                    media_type="application/json",
                    status_code=500,
                )

            return Response(
                content=png_bytes.tobytes(),
                media_type="image/png",
                headers={"Content-Disposition": "inline; filename=charuco_board_8x11.png"},
            )

        @self.app.websocket("/ws/distortion-feed")
        async def distortion_camera_feed(websocket: WebSocket) -> None:
            await websocket.accept()
            cap = None
            camera_index = None
            try:
                import cv2
                import numpy as np
                from charuco_detector import CompatibleCharucoDetector

                data = await websocket.receive_json()
                camera = data.get("camera", "camera1")
                camera_index = 0 if camera == "camera1" else 1

                if camera_index in self._active_cameras:
                    await websocket.send_json({
                        "error": f"Camera {camera_index} is already in use "
                                 f"by {self._active_cameras[camera_index]}"
                    })
                    await websocket.close()
                    return

                detector = CompatibleCharucoDetector(
                    squares_x=CHARUCO_SQUARES_X, squares_y=CHARUCO_SQUARES_Y,
                    square_length=CHARUCO_SQUARE_LENGTH, marker_length=CHARUCO_MARKER_LENGTH,
                )

                cap = await asyncio.to_thread(open_camera, camera_index, 1456, 1088)
                if cap is None:
                    await websocket.send_json({"error": f"Cannot open camera {camera_index}"})
                    await websocket.close()
                    return

                self._active_cameras[camera_index] = "distortion-feed"

                # Read actual resolution after first frame (accurate for rpicam-vid)
                ret, first_frame = await asyncio.to_thread(cap.read)
                if ret and first_frame is not None:
                    actual_h, actual_w = first_frame.shape[:2]
                    logger.info(f"Distortion feed camera resolution: {actual_w}x{actual_h}")
                else:
                    actual_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
                    actual_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

                STREAM_WIDTH = 960

                frame = first_frame
                while True:
                    if frame is None:
                        ret, frame = await asyncio.to_thread(cap.read)
                        if not ret:
                            await asyncio.sleep(0.1)
                            frame = None
                            continue

                    self.calibration_manager.set_shared_frame(camera_index, frame.copy())

                    scale = STREAM_WIDTH / frame.shape[1]
                    display = cv2.resize(frame, (STREAM_WIDTH, int(frame.shape[0] * scale)))

                    gray = cv2.cvtColor(display, cv2.COLOR_BGR2GRAY)
                    corners, ids, marker_corners, marker_ids = detector.detect_charuco_corners(gray)

                    if marker_corners and len(marker_corners) > 0:
                        cv2.aruco.drawDetectedMarkers(display, marker_corners, marker_ids)
                    if corners is not None and len(corners) > 0:
                        cv2.aruco.drawDetectedCornersCharuco(display, corners, ids)
                        quality = detector.assess_image_quality(gray, corners)
                        await websocket.send_json({
                            "type": "metrics",
                            "corners": int(quality["num_corners"]),
                            "blur": int(round(float(quality["blur_score"]))),
                            "is_good": bool(quality["is_good"]),
                        })
                    else:
                        await websocket.send_json({
                            "type": "metrics",
                            "corners": 0,
                            "blur": 0,
                            "is_good": False,
                        })

                    # Draw coverage grid overlay (single blend pass)
                    status = self.calibration_manager.calibration_status.get(camera, {})
                    cov = status.get("coverage")
                    if cov and cov.get("grid"):
                        dh, dw = display.shape[:2]
                        grid = cov["grid"]
                        rows, cols = len(grid), len(grid[0])
                        if any(grid[r][c] > 0 for r in range(rows) for c in range(cols)):
                            overlay = display.copy()
                            for r in range(rows):
                                for c in range(cols):
                                    if grid[r][c] > 0:
                                        x1 = int(c * dw / cols)
                                        y1 = int(r * dh / rows)
                                        x2 = int((c + 1) * dw / cols)
                                        y2 = int((r + 1) * dh / rows)
                                        cv2.rectangle(overlay, (x1, y1), (x2, y2), (0, 180, 0), -1)
                            cv2.addWeighted(overlay, 0.15, display, 0.85, 0, display)

                    _, jpg = cv2.imencode(".jpg", display, [cv2.IMWRITE_JPEG_QUALITY, 70])
                    await websocket.send_bytes(jpg.tobytes())
                    await asyncio.sleep(0.066)  # ~15 fps
                    frame = None  # read next frame on next iteration

            except WebSocketDisconnect:
                logger.debug("Distortion feed WebSocket disconnected")
            except Exception as e:
                logger.error(f"Distortion feed error: {e}")
                try:
                    await websocket.close()
                except Exception:
                    pass
            finally:
                if cap is not None:
                    await asyncio.to_thread(cap.release)
                if camera_index is not None:
                    self._active_cameras.pop(camera_index, None)
                    self.calibration_manager.clear_shared_frame(camera_index)

        @self.app.websocket("/ws/undistort-preview")
        async def undistort_preview_feed(websocket: WebSocket) -> None:
            await websocket.accept()
            cap = None
            camera_index = None
            listener = None
            try:
                import cv2
                import numpy as np

                data = await websocket.receive_json()
                camera = data.get("camera", "camera1")
                camera_index = 0 if camera == "camera1" else 1

                # Load calibration data
                camera_num = "1" if camera == "camera1" else "2"
                matrix_key = f"gs_config.cameras.kCamera{camera_num}CalibrationMatrix"
                dist_key = f"gs_config.cameras.kCamera{camera_num}DistortionVector"
                matrix_data = self.config_manager.get_config(matrix_key)
                dist_data = self.config_manager.get_config(dist_key)

                if matrix_data is None or dist_data is None:
                    await websocket.send_json({"error": "No calibration data found for this camera"})
                    await websocket.close()
                    return

                camera_matrix = np.array(matrix_data, dtype=np.float64)
                dist_coeffs = np.array(dist_data, dtype=np.float64)

                if camera_index in self._active_cameras:
                    await websocket.send_json({
                        "error": f"Camera {camera_index} is already in use "
                                 f"by {self._active_cameras[camera_index]}"
                    })
                    await websocket.close()
                    return

                cap = await asyncio.to_thread(open_camera, camera_index, 1456, 1088)
                if cap is None:
                    await websocket.send_json({"error": f"Cannot open camera {camera_index}"})
                    await websocket.close()
                    return

                self._active_cameras[camera_index] = "undistort-preview"

                # Read actual resolution and precompute undistort maps
                ret, first_frame = await asyncio.to_thread(cap.read)
                if not ret:
                    await websocket.send_json({"error": "Could not read from camera"})
                    await websocket.close()
                    return
                h, w = first_frame.shape[:2]
                new_matrix, roi = cv2.getOptimalNewCameraMatrix(
                    camera_matrix, dist_coeffs, (w, h), 1, (w, h)
                )
                map1, map2 = cv2.initUndistortRectifyMap(
                    camera_matrix, dist_coeffs, None, new_matrix, (w, h), cv2.CV_16SC2
                )

                mode = "side_by_side"  # or "raw" or "undistorted"

                # Listen for mode toggles in a background task
                mode_holder = {"mode": mode}

                async def listen_for_mode():
                    try:
                        while True:
                            msg = await websocket.receive_json()
                            mode_holder["mode"] = msg.get("mode", mode_holder["mode"])
                    except (WebSocketDisconnect, asyncio.CancelledError):
                        pass
                    except Exception as e:
                        logger.debug(f"Mode listener error: {e}")

                listener = asyncio.create_task(listen_for_mode())

                while True:
                    ret, frame = await asyncio.to_thread(cap.read)
                    if not ret:
                        await asyncio.sleep(0.1)
                        continue

                    mode = mode_holder["mode"]

                    if mode == "raw":
                        cv2.putText(frame, "RAW", (10, 30),
                                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)
                        output = frame
                    elif mode == "undistorted":
                        output = cv2.remap(frame, map1, map2, cv2.INTER_LINEAR)
                        cv2.putText(output, "UNDISTORTED", (10, 30),
                                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
                    else:
                        undistorted = cv2.remap(frame, map1, map2, cv2.INTER_LINEAR)
                        # Scale both to half size for side-by-side (preserve aspect ratio)
                        half_w = w // 2
                        half_h = h // 2
                        left = cv2.resize(frame, (half_w, half_h))
                        right = cv2.resize(undistorted, (half_w, half_h))
                        cv2.putText(left, "RAW", (10, 30),
                                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
                        cv2.putText(right, "UNDISTORTED", (10, 30),
                                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                        output = np.hstack([left, right])

                    _, jpg = cv2.imencode(".jpg", output, [cv2.IMWRITE_JPEG_QUALITY, 75])
                    await websocket.send_bytes(jpg.tobytes())
                    await asyncio.sleep(0.066)  # ~15 fps

            except WebSocketDisconnect:
                logger.debug("Undistort preview WebSocket disconnected")
            except Exception as e:
                logger.error(f"Undistort preview error: {e}")
                try:
                    await websocket.close()
                except Exception:
                    pass
            finally:
                if listener is not None:
                    listener.cancel()
                if cap is not None:
                    await asyncio.to_thread(cap.release)
                if camera_index is not None:
                    self._active_cameras.pop(camera_index, None)

        @self.app.post("/api/calibration/distortion/{camera}")
        async def run_distortion_calibration(camera: str, request: Request) -> Dict[str, Any]:
            """Start automated lens distortion calibration using ChArUco board"""
            if camera not in ["camera1", "camera2"]:
                return {"status": "error", "message": "Invalid camera"}
            if self.calibration_manager.loop is None:
                return {"status": "error", "message": "Server still starting up, please retry in a moment"}

            target_images = 40
            try:
                body = await request.json()
                target_images = body.get("target_images", 40)
            except Exception:
                pass  # No body or invalid JSON is fine, use default

            task = asyncio.create_task(
                self.calibration_manager.run_distortion_calibration(camera, target_images)
            )
            self.background_tasks.add(task)
            task.add_done_callback(self.background_tasks.discard)

            return {"status": "started", "message": f"Distortion calibration started for {camera}"}

        @self.app.post("/api/calibration/stop")
        async def stop_calibration() -> Dict[str, Any]:
            """Stop any running calibration process"""
            return await self.calibration_manager.stop_calibration()

        @self.app.get("/testing", response_class=HTMLResponse)
        async def testing_page(request: Request) -> Response:
            """Serve testing tools UI page"""
            return self.templates.TemplateResponse(request, "testing.html")

        @self.app.get("/api/testing/tools")
        async def get_testing_tools() -> Dict[str, Any]:
            """Get available testing tools organized by category"""
            return self.testing_manager.get_available_tools()

        @self.app.post("/api/testing/run/{tool_id}")
        async def run_testing_tool(tool_id: str) -> Dict[str, Any]:
            """Run a specific testing tool"""
            safety = self.strobe_calibration_manager.is_strobe_safe()
            if not safety["safe"]:
                return {"status": "error", "message": safety["reason"]}
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
                    "message": "Image uploaded successfully",
                    "filename": filename,
                    "path": str(file_path),
                }

            except Exception as e:
                logger.error(f"Error uploading test image: {e}")
                return {"status": "error", "message": str(e)}

        # Strobe safety check — used by frontend to gate strobe-triggering actions
        @self.app.get("/api/strobe-safety")
        async def strobe_safety() -> Dict[str, Any]:
            """Check if strobing is safe (V3 boards need DAC calibration first)"""
            return self.strobe_calibration_manager.is_strobe_safe()

        # Strobe calibration endpoints
        @self.app.get("/api/strobe-calibration/status")
        async def strobe_calibration_status() -> Dict[str, Any]:
            """Get current strobe calibration status"""
            return self.strobe_calibration_manager.get_status()

        @self.app.get("/api/strobe-calibration/settings")
        async def strobe_calibration_settings() -> Dict[str, Any]:
            """Get saved strobe calibration settings"""
            return await self.strobe_calibration_manager.get_saved_settings()

        @self.app.post("/api/strobe-calibration/start")
        async def start_strobe_calibration(request: Request) -> Dict[str, Any]:
            """Start strobe calibration as a background task"""
            body = await request.json()
            led_type = body.get("led_type", "v3")
            target_current = body.get("target_current")
            overwrite = body.get("overwrite", False)

            task = asyncio.create_task(
                self.strobe_calibration_manager.start_calibration(
                    led_type=led_type,
                    target_current=target_current,
                    overwrite=overwrite,
                )
            )
            self.background_tasks.add(task)
            task.add_done_callback(self.background_tasks.discard)

            return {"status": "started"}

        @self.app.post("/api/strobe-calibration/cancel")
        async def cancel_strobe_calibration() -> Dict[str, Any]:
            """Cancel a running strobe calibration"""
            self.strobe_calibration_manager.cancel()
            return {"status": "ok"}

        @self.app.get("/api/strobe-calibration/diagnostics")
        async def strobe_calibration_diagnostics() -> Dict[str, Any]:
            """Read strobe hardware diagnostics"""
            return await self.strobe_calibration_manager.read_diagnostics()

        @self.app.post("/api/strobe-calibration/set-dac")
        async def strobe_set_dac(request: Request) -> Dict[str, Any]:
            """Manually set the DAC value"""
            body = await request.json()
            value = body.get("value")
            if value is None or not isinstance(value, int):
                return {"status": "error", "message": "Integer 'value' is required"}
            return await self.strobe_calibration_manager.set_dac_manual(value)

        @self.app.get("/api/strobe-calibration/dac-start")
        async def strobe_dac_start() -> Dict[str, Any]:
            """Get the safe DAC starting value"""
            return await self.strobe_calibration_manager.get_dac_start()

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

        @self.app.get("/update", response_class=HTMLResponse)
        async def update_page(request: Request) -> Response:
            return self.templates.TemplateResponse(request, "update.html")

        @self.app.get("/api/update/branches")
        async def get_branches() -> Dict[str, Any]:
            return await self.update_manager.get_branches()

        @self.app.get("/api/update/check")
        async def check_for_updates() -> Dict[str, Any]:
            return await self.update_manager.check_for_updates()

        @self.app.post("/api/update/start")
        async def start_update(request: Request) -> Dict[str, Any]:
            content_type = request.headers.get("content-type", "")
            body = await request.json() if "application/json" in content_type else {}
            force = bool(body.get("force", False))
            branch = body.get("branch")
            if branch is not None:
                branch = str(branch)
            result = await self.update_manager.start_update(force=force, branch=branch)

            task = self.update_manager.get_update_task()
            if task:
                self.background_tasks.add(task)
                task.add_done_callback(self.background_tasks.discard)

            return result

        @self.app.post("/api/update/cancel")
        async def cancel_update() -> Dict[str, Any]:
            return await self.update_manager.cancel_update()

        @self.app.get("/api/update/status")
        async def update_status() -> Dict[str, Any]:
            return self.update_manager.get_status()

        @self.app.get("/logs", response_class=HTMLResponse)
        async def logs_page(request: Request) -> Response:
            """Serve logs viewer page"""
            return self.templates.TemplateResponse(request, "logs.html")

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

    async def startup_event(self) -> None:
        logger.info("Starting PiTrac Web Server...")
        loop = asyncio.get_event_loop()
        self.calibration_manager.loop = loop
        await self.calibration_manager._replay_pending_updates()

        # V3 boards: write calibrated DAC value to hardware before any strobe fires.
        # Without this, the MCP4801 is in shutdown after boot and strobing will blow the MOSFET.
        if not self.strobe_calibration_manager.apply_dac_setting():
            logger.warning(
                "V3 DAC not initialized — strobe calibration required before PiTrac can run"
            )

        logger.info("PiTrac Web Server ready — receiving results via HTTP POST")

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

        for ws in self.connection_manager.connections:
            try:
                await ws.close()
            except Exception:
                pass


server = PiTracServer()
app = server.app

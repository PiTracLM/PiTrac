"""
PiTrac Calibration Manager

Handles all calibration operations including:
- Ball location detection
- Manual calibration
- Auto calibration
- Still image capture
- Lens distortion calibration (ChArUco)
- Calibration data persistence
"""

import asyncio
import json
import logging
import os
import shutil
import uuid
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional

logger = logging.getLogger(__name__)

CAMERA1_CALIBRATION_TIMEOUT = 40.0  # Camera1 has faster hardware detection
CAMERA2_CALIBRATION_TIMEOUT = 140.0  # Camera2 needs background process initialization
CAMERA2_BACKGROUND_INIT_WAIT = 4.0  # Time to wait for background process to initialize

DISTORTION_DEFAULT_TARGET_IMAGES = 40  # Hagemann 2021: 40+ images for sub-0.2% std_fx/fx
DISTORTION_MAX_ATTEMPTS_MULTIPLIER = 5  # max_attempts = target * this
DISTORTION_CAPTURE_INTERVAL = 2.0  # seconds between captures
DISTORTION_CAPTURE_TIMEOUT = 10.0  # timeout for rpicam-still

RPICAM_TUNING_FILE = "/usr/share/libcamera/ipa/rpi/pisp/imx296_noir.json"
RPICAM_CAL_SHUTTER_US = 11000


class CalibrationManager:
    """Manages calibration processes for PiTrac cameras"""

    def __init__(self, config_manager, pitrac_binary: str = "/usr/lib/pitrac/pitrac_lm"):
        """
        Initialize calibration manager

        Args:
            config_manager: Configuration manager instance
            pitrac_binary: Path to pitrac_lm binary
        """
        self.config_manager = config_manager
        self.pitrac_binary = pitrac_binary
        self.current_processes: Dict[str, asyncio.subprocess.Process] = {}
        self._process_lock = asyncio.Lock()
        self.calibration_status = {
            "camera1": {"status": "idle", "message": "", "progress": 0, "last_run": None},
            "camera2": {"status": "idle", "message": "", "progress": 0, "last_run": None},
        }
        self.log_dir = Path.home() / ".pitrac" / "logs"
        self.log_dir.mkdir(parents=True, exist_ok=True)

        self.loop: Optional[asyncio.AbstractEventLoop] = None
        self._active_calibrations: Dict[str, Dict[str, Any]] = {}  # session_id -> {camera, expected_keys, futures}
        self._calibration_lock = asyncio.Lock()
        self._pending_updates: List[tuple[str, Any]] = []

        self.config_manager.register_callback("gs_config.cameras.kCamera", self._on_calibration_update)

        self._capture_backend = self._detect_capture_backend()

        # Shared frame buffer: the live-feed WebSocket writes here so the
        # calibration loop can grab frames without opening a second VideoCapture.
        # Keys: camera_index (int) -> latest BGR numpy frame
        self._shared_frames: Dict[int, Any] = {}

    def _on_calibration_update(self, key: str, value: Any) -> None:
        """
        Callback invoked when calibration config is updated via API.
        This runs in the FastAPI thread, so we must use run_coroutine_threadsafe
        to safely notify waiting calibration tasks.

        Args:
            key: Config key (e.g., "gs_config.cameras.kCamera1FocalLength")
            value: New value
        """
        if self.loop is None:
            self._pending_updates.append((key, value))
            logger.warning(f"Queuing calibration update (loop not ready yet): {key}")
            return

        logger.debug(f"Calibration config update: {key} = {value}")

        asyncio.run_coroutine_threadsafe(self._handle_calibration_update(key, value), self.loop)

    async def _replay_pending_updates(self) -> None:
        """
        Replay any calibration updates that arrived before the event loop was set.
        Called by server.py after setting the event loop.
        """
        if not self._pending_updates:
            return

        logger.info(f"Replaying {len(self._pending_updates)} pending calibration updates")
        for key, value in self._pending_updates:
            await self._handle_calibration_update(key, value)
        self._pending_updates.clear()

    async def _handle_calibration_update(self, key: str, value: Any) -> None:
        """
        Async handler for calibration updates. Checks if any active calibrations
        are waiting for this key and sets their futures.

        Args:
            key: Config key
            value: New value
        """
        async with self._calibration_lock:
            for session_id, session_data in self._active_calibrations.items():
                expected_keys = session_data["expected_keys"]
                futures = session_data["futures"]

                if key in expected_keys:
                    future = futures.get(key)
                    if future and not future.done():
                        logger.info(f"Session {session_id}: received expected key {key} = {value}")
                        future.set_result(value)
                    elif future:
                        logger.warning(f"Session {session_id}: duplicate calibration update for {key} = {value}")

    def _create_calibration_session(self, camera: str) -> tuple[str, Dict[str, Any]]:
        """
        Create a calibration session with futures for expected callback keys.
        This should be called BEFORE starting the calibration process to avoid
        race conditions where callbacks arrive before the session is registered.

        Args:
            camera: "camera1" or "camera2"

        Returns:
            Tuple of (session_id, session_data)
        """
        camera_num = "1" if camera == "camera1" else "2"
        focal_key = f"gs_config.cameras.kCamera{camera_num}FocalLength"
        angles_key = f"gs_config.cameras.kCamera{camera_num}Angles"

        session_id = str(uuid.uuid4())
        focal_future = asyncio.Future()
        angles_future = asyncio.Future()

        session_data = {
            "camera": camera,
            "expected_keys": {focal_key, angles_key},
            "futures": {focal_key: focal_future, angles_key: angles_future},
        }

        logger.debug(f"Created calibration session {session_id} for {camera}")
        return session_id, session_data

    async def wait_for_calibration_fields(self, session_id: str, timeout: float = 120.0) -> Dict[str, bool]:
        """
        Wait for calibration API callbacks to confirm completion.

        C++ sends two API calls when calibration succeeds:
        1. PUT /api/config/gs_config.cameras.kCamera{N}FocalLength
        2. PUT /api/config/gs_config.cameras.kCamera{N}Angles

        Args:
            session_id: The calibration session ID (from _create_calibration_session)
            timeout: Max time to wait for both fields

        Returns:
            Dict with keys:
                - focal_length_received: bool
                - angles_received: bool
                - completed: bool (both received)
        """
        async with self._calibration_lock:
            if session_id not in self._active_calibrations:
                logger.error(f"Session {session_id} not found in active calibrations")
                return {"focal_length_received": False, "angles_received": False, "completed": False}
            session_data = self._active_calibrations[session_id]
            futures = session_data["futures"]
            focal_future = list(futures.values())[0]
            angles_future = list(futures.values())[1]

        logger.info(f"Waiting for calibration API updates (session {session_id})")

        try:
            await asyncio.wait_for(asyncio.gather(focal_future, angles_future), timeout=timeout)
            logger.info(f"Session {session_id}: All calibration fields received")
            return {"focal_length_received": True, "angles_received": True, "completed": True}
        except asyncio.TimeoutError:
            if not focal_future.done():
                focal_future.cancel()
            if not angles_future.done():
                angles_future.cancel()

            logger.warning(
                f"Session {session_id}: Timeout after {timeout}s. "
                f"Received: {[k for k, f in futures.items() if f.done()]}"
            )
            return {
                "focal_length_received": focal_future.done(),
                "angles_received": angles_future.done(),
                "completed": False,
            }

    async def wait_for_calibration_completion(
        self, process: asyncio.subprocess.Process, session_id: str, timeout: float = 120.0
    ) -> Dict[str, Any]:
        """
        Hybrid completion detection: wait for EITHER API callbacks OR process exit.

        This provides robust detection by racing multiple completion methods:
        1. API callbacks (primary) - confirms success + provides data
        2. Process exit (fallback) - confirms process finished
        3. Timeout (safety) - prevents infinite waiting

        Args:
            process: The calibration process to monitor
            session_id: The calibration session ID
            timeout: Max time to wait for completion

        Returns:
            Dict with:
                - completed: bool - true if calibration finished
                - method: str - "api", "process", or "timeout"
                - api_success: bool - true if API callbacks received
                - process_exit_code: Optional[int] - process return code
                - focal_length_received: bool
                - angles_received: bool
        """
        logger.info(f"Starting hybrid completion detection (session {session_id}, timeout={timeout}s)")

        api_task = asyncio.create_task(self.wait_for_calibration_fields(session_id, timeout=timeout))

        process_task = asyncio.create_task(process.wait())

        try:
            done, pending = await asyncio.wait(
                {api_task, process_task}, return_when=asyncio.FIRST_COMPLETED, timeout=timeout
            )

            for task in pending:
                task.cancel()
                try:
                    await task
                except (asyncio.CancelledError, Exception) as e:
                    if not isinstance(e, asyncio.CancelledError):
                        logger.warning(f"Task raised during cancel: {e}")

            if api_task in done:
                api_result = api_task.result()
                if api_result["completed"]:
                    logger.info(f"Session {session_id}: Calibration completed via API callbacks")
                    CLEANUP_GRACE_PERIOD = 10.0  # seconds
                    logger.info(
                        f"Session {session_id}: Waiting up to {CLEANUP_GRACE_PERIOD}s for process to finish cleanup"
                    )

                    try:
                        exit_code = await asyncio.wait_for(process.wait(), timeout=CLEANUP_GRACE_PERIOD)
                        logger.info(f"Session {session_id}: Process exited naturally with code {exit_code}")
                    except asyncio.TimeoutError:
                        logger.warning(
                            f"Session {session_id}: Process did not exit within {CLEANUP_GRACE_PERIOD}s "
                            "after API callbacks (may be hung)"
                        )
                        exit_code = process.returncode

                    return {
                        "completed": True,
                        "method": "api",
                        "api_success": True,
                        "process_exit_code": exit_code,
                        "focal_length_received": api_result["focal_length_received"],
                        "angles_received": api_result["angles_received"],
                    }

            if process_task in done:
                exit_code = process_task.result()
                logger.info(f"Session {session_id}: Process exited with code {exit_code}")

                api_result = (
                    api_task.result()
                    if api_task.done()
                    else {"focal_length_received": False, "angles_received": False, "completed": False}
                )

                return {
                    "completed": exit_code == 0,
                    "method": "process",
                    "api_success": api_result["completed"],
                    "process_exit_code": exit_code,
                    "focal_length_received": api_result["focal_length_received"],
                    "angles_received": api_result["angles_received"],
                }

            logger.warning(f"Session {session_id}: Calibration timeout after {timeout}s")
            return {
                "completed": False,
                "method": "timeout",
                "api_success": False,
                "process_exit_code": process.returncode,
                "focal_length_received": False,
                "angles_received": False,
            }

        except Exception as e:
            logger.error(
                f"Session {session_id}: Error in hybrid completion detection: {e}. "
                f"Process state: {process.returncode}, API task done: {api_task.done()}",
                exc_info=True,
            )
            return {
                "completed": False,
                "method": "error",
                "api_success": False,
                "process_exit_code": process.returncode,
                "focal_length_received": False,
                "angles_received": False,
            }

    async def check_ball_location(self, camera: str = "camera1") -> Dict[str, Any]:
        """
        Run ball location detection to verify ball placement

        Args:
            camera: Which camera to use ("camera1" or "camera2")

        Returns:
            Dict with status and ball location info
        """
        logger.info(f"Starting ball location check for {camera}")

        self.calibration_status[camera] = {
            "status": "checking_ball",
            "message": "Detecting ball location...",
            "progress": 10,
            "last_run": datetime.now().isoformat(),
        }

        config = self.config_manager.get_config()

        cmd = [self.pitrac_binary, f"--system_mode={camera}_ball_location"]

        if camera == "camera1":
            search_x = self.config_manager.get_config("gs_config.cameras.kCamera1SearchCenterX")
            if search_x is None:
                search_x = 850  # Default from configurations.json
            search_y = self.config_manager.get_config("gs_config.cameras.kCamera1SearchCenterY")
            if search_y is None:
                search_y = 500  # Default from configurations.json
        else:
            search_x = 700
            search_y = 500

        logging_level = config.get("logging", {}).get("level", "warn")

        camera_gain_key = "kCamera1Gain" if camera == "camera1" else "kCamera2Gain"
        camera_gain = self.config_manager.get_config(f"gs_config.cameras.{camera_gain_key}")
        if camera_gain is None:
            camera_gain = 6.0

        cmd.extend(
            [
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                f"--logging_level={logging_level}",
                "--artifact_save_level=all",
                f"--camera_gain={camera_gain}",
            ]
        )
        cmd.extend(self._build_cli_args_from_metadata(camera))

        try:
            result = await self._run_calibration_command(cmd, camera, timeout=30)

            ball_info = self._parse_ball_location(result.get("output", ""))

            self.calibration_status[camera]["status"] = "ball_found" if ball_info else "ball_not_found"
            self.calibration_status[camera]["message"] = "Ball detected" if ball_info else "Ball not found"
            self.calibration_status[camera]["progress"] = 100

            return {
                "status": "success",
                "ball_found": bool(ball_info),
                "ball_info": ball_info,
                "output": result.get("output", ""),
            }

        except Exception as e:
            logger.error(f"Ball location check failed: {e}")
            self.calibration_status[camera]["status"] = "error"
            self.calibration_status[camera]["message"] = str(e)
            return {"status": "error", "message": str(e)}

    async def run_auto_calibration(self, camera: str = "camera1") -> Dict[str, Any]:
        """
        Run automatic calibration for specified camera

        Args:
            camera: Which camera to calibrate ("camera1" or "camera2")

        Returns:
            Dict with calibration results

        """

        generated_config_path = self.config_manager.generate_golf_sim_config()
        logger.info(f"Generated config file at: {generated_config_path}")

        return await self._run_auto_calibration(camera)

    async def _run_auto_calibration(self, camera: str = "camera1") -> Dict[str, Any]:
        """
        Run auto calibration with hybrid API + process completion detection.
        """
        timeout = CAMERA1_CALIBRATION_TIMEOUT if camera == "camera1" else CAMERA2_CALIBRATION_TIMEOUT

        logger.info(f"Starting {camera} auto calibration with hybrid detection (timeout={timeout}s)")

        self.calibration_status[camera] = {
            "status": "calibrating",
            "message": "Running auto calibration...",
            "progress": 20,
            "last_run": datetime.now().isoformat(),
        }

        session_id, session_data = self._create_calibration_session(camera)
        async with self._calibration_lock:
            self._active_calibrations[session_id] = session_data
            logger.info(f"Pre-registered calibration session {session_id} for {camera}")

        config = self.config_manager.get_config()
        cmd = [self.pitrac_binary, f"--system_mode={camera}AutoCalibrate"]

        search_x = self.config_manager.get_config("gs_config.cameras.kCamera1SearchCenterX")
        if search_x is None:
            search_x = 850  # Default from configurations.json

        search_y = self.config_manager.get_config("gs_config.cameras.kCamera1SearchCenterY")
        if search_y is None:
            search_y = 500  # Default from configurations.json

        logging_level = config.get("logging", {}).get("level", "warn")

        camera_gain = self.config_manager.get_config("gs_config.cameras.kCamera1Gain")
        if camera_gain is None:
            camera_gain = 6.0

        cmd.extend(
            [
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                f"--logging_level={logging_level}",
                "--artifact_save_level=all",
                "--show_images=0",
                f"--camera_gain={camera_gain}",
            ]
        )
        cmd.extend(self._build_cli_args_from_metadata(camera))

        log_file = self.log_dir / f"calibration_{camera}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"

        logger.info(f"Running command: {' '.join(cmd)}")
        logger.info(f"Log file: {log_file}")

        env = self._build_environment(camera)

        cmd = ["sudo", "-E"] + cmd

        async with self._process_lock:
            if camera in self.current_processes:
                raise Exception(f"A calibration process is already running for {camera}")

            try:
                process = await asyncio.create_subprocess_exec(
                    *cmd, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, env=env
                )

                self.current_processes[camera] = process
                logger.info(f"{camera}: Process started (PID {process.pid})")

            except Exception as e:
                logger.error(f"Failed to start calibration process: {e}")
                async with self._calibration_lock:
                    self._active_calibrations.pop(session_id, None)
                raise

        try:
            completion_result = await self.wait_for_calibration_completion(process, session_id, timeout=timeout)

            logger.info(f"{camera}: Completion result: {completion_result}")

            output = ""
            try:
                if process.stdout and not process.stdout.at_eof():
                    remaining_output = await asyncio.wait_for(process.stdout.read(), timeout=2.0)
                    output = remaining_output.decode() if remaining_output else ""
            except Exception as e:
                logger.debug(f"Could not read remaining output: {e}")

            # Check if output contains failure messages even if exit code was 0
            if output and self._check_calibration_failed(output):
                logger.warning(
                    f"{camera}: Detected calibration failure in output despite exit code {process.returncode}"
                )
                completion_result["completed"] = False
                completion_result["method"] = "output_parse"

            with open(log_file, "w") as f:
                f.write(f"Completion method: {completion_result['method']}\n")
                f.write(f"API success: {completion_result['api_success']}\n")
                f.write(f"Process exit code: {completion_result['process_exit_code']}\n")
                f.write(f"\n--- Output ---\n{output}")

            if completion_result["completed"]:
                self.calibration_status[camera]["status"] = "completed"
                self.calibration_status[camera][
                    "message"
                ] = f"Calibration successful (via {completion_result['method']})"
                self.calibration_status[camera]["progress"] = 100

                self.config_manager.reload()

                return {
                    "status": "success",
                    "completion_method": completion_result["method"],
                    "api_success": completion_result["api_success"],
                    "focal_length_received": completion_result["focal_length_received"],
                    "angles_received": completion_result["angles_received"],
                    "output": output,
                    "log_file": str(log_file),
                }
            else:
                self.calibration_status[camera]["status"] = "failed"
                self.calibration_status[camera]["message"] = f"Calibration failed ({completion_result['method']})"
                return {
                    "status": "failed",
                    "message": f"Calibration failed via {completion_result['method']}",
                    "completion_result": completion_result,
                    "output": output,
                    "log_file": str(log_file),
                }

        except Exception as e:
            logger.error(f"{camera} auto calibration failed: {e}", exc_info=True)
            self.calibration_status[camera]["status"] = "error"
            self.calibration_status[camera]["message"] = str(e)
            return {"status": "error", "message": str(e)}

        finally:
            async with self._calibration_lock:
                if session_id in self._active_calibrations:
                    logger.debug(f"Cleaning up calibration session {session_id}")
                    self._active_calibrations.pop(session_id, None)

            async with self._process_lock:
                if camera in self.current_processes:
                    proc = self.current_processes[camera]
                    if proc.returncode is None:
                        logger.info(f"{camera} process still running after completion, waiting for graceful exit...")
                        try:
                            await asyncio.wait_for(proc.wait(), timeout=5.0)
                            logger.info(f"{camera} process exited gracefully")
                        except asyncio.TimeoutError:
                            logger.warning(f"Terminating {camera} process after timeout")
                            try:
                                proc.terminate()
                                await asyncio.wait_for(proc.wait(), timeout=5.0)
                                logger.info(f"{camera} process terminated")
                            except asyncio.TimeoutError:
                                logger.warning(f"Force killing {camera} process")
                                proc.kill()
                                await proc.wait()
                    del self.current_processes[camera]

    # _run_camera2_auto_calibration and _run_standard_calibration_fallback removed
    # Camera2 auto-calibration now uses the same single-process path as camera1
    async def run_manual_calibration(self, camera: str = "camera1") -> Dict[str, Any]:
        """
        Run manual calibration for specified camera

        Args:
            camera: Which camera to calibrate ("camera1" or "camera2")

        Returns:
            Dict with calibration results
        """
        logger.info(f"Starting manual calibration for {camera}")

        self.calibration_status[camera] = {
            "status": "calibrating",
            "message": "Running manual calibration...",
            "progress": 20,
            "last_run": datetime.now().isoformat(),
        }

        config = self.config_manager.get_config()
        cmd = [self.pitrac_binary, f"--system_mode={camera}Calibrate"]

        if camera == "camera1":
            search_x = self.config_manager.get_config("gs_config.cameras.kCamera1SearchCenterX")
            if search_x is None:
                search_x = 850  # Default from configurations.json
            search_y = self.config_manager.get_config("gs_config.cameras.kCamera1SearchCenterY")
            if search_y is None:
                search_y = 500  # Default from configurations.json
        else:
            search_x = 700
            search_y = 500

        logging_level = config.get("logging", {}).get("level", "warn")

        camera_gain_key = "kCamera1Gain" if camera == "camera1" else "kCamera2Gain"
        camera_gain = self.config_manager.get_config(f"gs_config.cameras.{camera_gain_key}")
        if camera_gain is None:
            camera_gain = 6.0

        cmd.extend(
            [
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                f"--logging_level={logging_level}",
                "--artifact_save_level=all",
                f"--camera_gain={camera_gain}",
            ]
        )
        cmd.extend(self._build_cli_args_from_metadata(camera))

        try:
            result = await self._run_calibration_command(cmd, camera, timeout=180)
            output = result.get("output", "")

            # Check for failure messages in output
            if self._check_calibration_failed(output):
                self.calibration_status[camera]["status"] = "failed"
                self.calibration_status[camera]["message"] = "Manual calibration failed - no ball detected"
                return {"status": "failed", "message": "Manual calibration failed - no ball detected", "output": output}

            calibration_data = self._parse_calibration_results(output)

            if calibration_data:
                self.calibration_status[camera]["status"] = "completed"
                self.calibration_status[camera]["message"] = "Manual calibration successful"
                self.calibration_status[camera]["progress"] = 100

                self.config_manager.reload()

                return {"status": "success", "calibration_data": calibration_data, "output": output}
            else:
                self.calibration_status[camera]["status"] = "failed"
                self.calibration_status[camera]["message"] = "Manual calibration failed"
                return {"status": "failed", "message": "Manual calibration failed", "output": output}

        except Exception as e:
            logger.error(f"Manual calibration failed: {e}")
            self.calibration_status[camera]["status"] = "error"
            self.calibration_status[camera]["message"] = str(e)
            return {"status": "error", "message": str(e)}

    async def capture_still_image(self, camera: str = "camera1") -> Dict[str, Any]:
        """
        Capture a still image for camera setup verification

        Args:
            camera: Which camera to use ("camera1" or "camera2")

        Returns:
            Dict with image path and status
        """
        logger.info(f"Capturing still image for {camera}")

        config = self.config_manager.get_config()
        cmd = [self.pitrac_binary, f"--system_mode={camera}", "--cam_still_mode"]

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_file = f"calibration_{camera}_{timestamp}.png"
        images_dir = Path.home() / "LM_Shares" / "Images"
        images_dir.mkdir(parents=True, exist_ok=True)
        output_path = images_dir / output_file

        cmd.extend([f"--output_filename={output_path}", "--artifact_save_level=final_results_only"])
        cmd.extend(self._build_cli_args_from_metadata(camera))

        try:
            await self._run_calibration_command(cmd, camera, timeout=10)

            if output_path.exists():
                return {"status": "success", "image_path": str(output_path), "image_url": f"/api/images/{output_file}"}
            else:
                return {"status": "failed", "message": "Image capture failed"}

        except Exception as e:
            logger.error(f"Still image capture failed: {e}")
            return {"status": "error", "message": str(e)}

    def get_status(self) -> Dict[str, Any]:
        """Get current calibration status for all cameras"""
        return self.calibration_status

    def get_calibration_data(self) -> Dict[str, Any]:
        """Get current calibration data from config

        Returns calibration data including focal length, camera angles,
        and distortion calibration (matrix + distortion vector).
        """
        config = self.config_manager.get_config()
        cameras = config.get("gs_config", {}).get("cameras", {})

        return {
            "camera1": {
                "focal_length": cameras.get("kCamera1FocalLength"),
                "angles": cameras.get("kCamera1Angles"),
                "calibration_matrix": cameras.get("kCamera1CalibrationMatrix"),
                "distortion_vector": cameras.get("kCamera1DistortionVector"),
            },
            "camera2": {
                "focal_length": cameras.get("kCamera2FocalLength"),
                "angles": cameras.get("kCamera2Angles"),
                "calibration_matrix": cameras.get("kCamera2CalibrationMatrix"),
                "distortion_vector": cameras.get("kCamera2DistortionVector"),
            },
        }

    def _build_cli_args_from_metadata(self, camera: str = "camera1") -> list:
        """Build CLI arguments using metadata from configurations.json

        This method uses the passedVia metadata to automatically
        build CLI arguments, similar to pitrac_manager.py
        """
        args = []
        merged_config = self.config_manager.get_config()

        cli_params = self.config_manager.get_cli_parameters()

        # Skip args that we handle separately or need special handling
        skip_args = {
            "--system_mode",
            "--search_center_x",
            "--search_center_y",
            "--logging_level",
            "--artifact_save_level",
            "--cam_still_mode",
            "--output_filename",
            "--show_images",
            "--config_file",
        }  # We handle config_file specially below

        for param in cli_params:
            key = param["key"]
            cli_arg = param["cliArgument"]
            param_type = param["type"]

            if cli_arg in skip_args:
                continue

            value = merged_config
            for part in key.split("."):
                if isinstance(value, dict):
                    value = value.get(part)
                else:
                    value = None
                    break

            if value is None:
                continue

            # Skip empty string values for non-boolean parameters
            if param_type != "boolean" and value == "":
                continue

            if param_type == "boolean":
                if value:
                    args.append(cli_arg)
            else:
                if param_type == "path" and value:
                    value = str(value).replace("~", str(Path.home()))
                # Use --key=value format for consistency
                args.append(f"{cli_arg}={value}")

        # Always add the generated config file path
        args.append(f"--config_file={self.config_manager.generated_config_path}")

        return args

    def _build_environment(self, camera: str = "camera1") -> dict:
        """Build environment variables from config

        Args:
            camera: Which camera is being calibrated

        Returns:
            Environment dictionary with required variables
        """
        env = os.environ.copy()
        config = self.config_manager.get_config()

        # Set PITRAC_ROOT if not already set (required by camera discovery)
        if "PITRAC_ROOT" not in env:
            env["PITRAC_ROOT"] = "/usr/lib/pitrac"
        env["OMP_WAIT_POLICY"] = "PASSIVE"
        env.setdefault("LIBPISP_LOG_LEVEL", "4")
        env.setdefault("LIBCAMERA_LOG_LEVELS", "*:ERROR")

        # Camera types come from cameras.slot1.type and cameras.slot2.type (default 5 = InnoMaker IMX296)
        slot1_type = config.get("cameras", {}).get("slot1", {}).get("type", 5)
        slot2_type = config.get("cameras", {}).get("slot2", {}).get("type", 5)
        env["PITRAC_SLOT1_CAMERA_TYPE"] = str(slot1_type)
        env["PITRAC_SLOT2_CAMERA_TYPE"] = str(slot2_type)

        # Lens types come from cameras.slot1.lens and cameras.slot2.lens (default 1 = 6mm)
        slot1_lens = config.get("cameras", {}).get("slot1", {}).get("lens", 1)
        slot2_lens = config.get("cameras", {}).get("slot2", {}).get("lens", 1)
        env["PITRAC_SLOT1_LENS_TYPE"] = str(slot1_lens)
        env["PITRAC_SLOT2_LENS_TYPE"] = str(slot2_lens)

        # Orientation types come from cameras.slot1.orientation and cameras.slot2.orientation (default 1 = UpsideUp)
        slot1_orientation = config.get("cameras", {}).get("slot1", {}).get("orientation", 1)
        slot2_orientation = config.get("cameras", {}).get("slot2", {}).get("orientation", 1)
        env["PITRAC_SLOT1_CAMERA_ORIENTATION"] = str(slot1_orientation)
        env["PITRAC_SLOT2_CAMERA_ORIENTATION"] = str(slot2_orientation)

        base_dir = config.get("gs_config", {}).get("logging", {}).get("kPCBaseImageLoggingDir", "~/LM_Shares/Images/")
        env["PITRAC_BASE_IMAGE_LOGGING_DIR"] = str(base_dir).replace("~", str(Path.home()))

        web_share_dir = (
            config.get("gs_config", {})
            .get("ipc_interface", {})
            .get("kWebServerShareDirectory", "~/LM_Shares/WebShare/")
        )
        env["PITRAC_WEBSERVER_SHARE_DIR"] = str(web_share_dir).replace("~", str(Path.home()))

        return env

    async def _run_calibration_command(self, cmd: List[str], camera: str, timeout: int = 60) -> Dict[str, Any]:
        """
        Run a calibration command with timeout and progress updates

        Args:
            cmd: Command to run
            camera: Camera being calibrated
            timeout: Timeout in seconds

        Returns:
            Dict with command output and status
        """
        log_file = self.log_dir / f"calibration_{camera}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"

        logger.info(f"Running command: {' '.join(cmd)}")
        logger.info(f"Log file: {log_file}")

        # Build environment with required variables from config
        env = self._build_environment(camera)

        # Prepend sudo -E to preserve environment variables and run with elevated privileges
        # This is required for camera access in single-pi mode
        cmd = ["sudo", "-E"] + cmd

        async with self._process_lock:
            if camera in self.current_processes:
                raise Exception(f"A calibration process is already running for {camera}")

            try:
                process = await asyncio.create_subprocess_exec(
                    *cmd, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, env=env
                )

                self.current_processes[camera] = process

            except Exception as e:
                logger.error(f"Failed to start calibration process: {e}")
                raise

        try:
            output_lines = []
            try:
                stdout, _ = await asyncio.wait_for(process.communicate(), timeout=timeout)
                output = stdout.decode() if stdout else ""
                output_lines = output.split("\n")

                # Save to log file
                with open(log_file, "w") as f:
                    f.write(output)

            except asyncio.TimeoutError:
                logger.warning(f"Calibration timed out after {timeout} seconds, terminating process")

                try:
                    process.terminate()
                    await asyncio.wait_for(process.wait(), timeout=5.0)
                except asyncio.TimeoutError:
                    process.kill()
                    await process.wait()
                raise Exception(f"Calibration timed out after {timeout} seconds")

            if process.returncode != 0:
                raise Exception(f"Calibration failed with code {process.returncode}")

            return {"output": "\n".join(output_lines), "log_file": str(log_file), "return_code": process.returncode}

        finally:
            async with self._process_lock:
                if camera in self.current_processes:
                    del self.current_processes[camera]

    def _parse_ball_location(self, output: str) -> Optional[Dict[str, Any]]:
        """Parse ball location from command output"""
        import re

        for line in output.split("\n"):
            # Look for patterns like "ball found at (x, y)" or "ball location: x=123, y=456"
            if "ball found" in line.lower() or "ball location" in line.lower():
                coord_pattern = r"[\(,\s]?x[=:\s]+(\d+)[\),\s]+y[=:\s]+(\d+)"
                match = re.search(coord_pattern, line, re.IGNORECASE)
                if match:
                    x, y = int(match.group(1)), int(match.group(2))
                    return {"found": True, "x": x, "y": y, "confidence": 0.95}

                coord_pattern2 = r"\((\d+),\s*(\d+)\)"
                match2 = re.search(coord_pattern2, line)
                if match2:
                    x, y = int(match2.group(1)), int(match2.group(2))
                    return {"found": True, "x": x, "y": y, "confidence": 0.95}

                return {"found": True, "x": None, "y": None, "confidence": 0.95}

        return None

    def _parse_calibration_results(self, output: str) -> Optional[Dict[str, Any]]:
        """Parse calibration results from command output"""
        results = {}

        for line in output.split("\n"):
            if "focal length" in line.lower():
                pass
            elif "calibration complete" in line.lower():
                results["complete"] = True

        return results if results else None

    def _check_calibration_failed(self, output: str) -> bool:
        """Check if output contains calibration failure messages

        Args:
            output: Command output to check

        Returns:
            True if calibration failed
        """
        failure_indicators = [
            "Failed to AutoCalibrateCamera",
            "Model detection failed to find any balls",
            "GetBall() failed to get a ball",
            "Could not DetermineFocalLengthForAutoCalibration",
        ]

        for indicator in failure_indicators:
            if indicator in output:
                return True

        return False

    async def run_distortion_calibration(
        self, camera: str, target_images: int = DISTORTION_DEFAULT_TARGET_IMAGES
    ) -> Dict[str, Any]:
        """
        Run lens distortion calibration using ChArUco board detection.

        Captures images via rpicam-still, detects ChArUco corners with quality
        validation, tracks spatial coverage, and runs OpenCV calibration with
        iterative outlier rejection.

        Args:
            camera: "camera1" or "camera2"
            target_images: Number of good images to collect (default 40)

        Returns:
            Dict with calibration results or error
        """
        if camera not in ["camera1", "camera2"]:
            return {"status": "error", "message": "Invalid camera"}

        if self.calibration_status[camera]["status"] in ["distortion_calibrating", "calibrating"]:
            return {"status": "error", "message": "Calibration already running for this camera"}

        try:
            import cv2
            import numpy as np
            from charuco_detector import CompatibleCharucoDetector, CoverageTracker
        except ImportError as e:
            logger.error(f"Missing dependency for distortion calibration: {e}")
            return {"status": "error", "message": f"Missing dependency: {e}"}

        detector = CompatibleCharucoDetector(
            squares_x=8, squares_y=11,
            square_length=0.023, marker_length=0.017
        )

        image_dir = Path.home() / "LM_Shares" / "Images" / "distortion_calibration"
        image_dir.mkdir(parents=True, exist_ok=True)

        log_file = self.log_dir / (
            f"distortion_calibration_{camera}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
        )
        log_lines = []

        def log_msg(msg: str) -> None:
            log_lines.append(f"[{datetime.now().strftime('%H:%M:%S')}] {msg}")
            logger.info(f"Distortion({camera}): {msg}")

        def friendly_rejection(reasons: str) -> str:
            r = reasons.lower()
            if "blurry" in r:
                return "Too blurry -- hold the board steadier or improve lighting"
            if "too small" in r or "coverage" in r:
                return "Board is too far away -- move it closer to the camera"
            if "edge" in r or "margin" in r:
                return "Board is partially out of frame -- move it inward"
            if "corners" in r or "insufficient" in r:
                return "Board not fully visible -- make sure the full pattern is in frame and well-lit"
            return reasons

        self.calibration_status[camera] = {
            "status": "distortion_calibrating",
            "message": "Starting distortion calibration...",
            "progress": 0,
            "last_run": datetime.now().isoformat(),
            "images_captured": 0,
            "images_rejected": 0,
            "target_images": target_images,
            "coverage": None,
        }

        camera_index = 0 if camera == "camera1" else 1
        config = self.config_manager.get_config()
        slot_key = "slot1" if camera == "camera1" else "slot2"
        slot_config = config.get("cameras", {}).get(slot_key, {})
        gain_key = "kCamera1Gain" if camera == "camera1" else "kCamera2Gain"
        camera_gain = self.config_manager.get_config(f"gs_config.cameras.{gain_key}")
        if camera_gain is None:
            camera_gain = 6.0

        all_corners = []
        all_ids = []
        sample_params = []
        good_count = 0
        rejected_count = 0
        tilted_count = 0
        max_attempts = target_images * DISTORTION_MAX_ATTEMPTS_MULTIPLIER
        image_size = None
        coverage_tracker = None

        log_msg(f"Target: {target_images} images, camera index: {camera_index}")

        min_pixel_coverage = 0.80   # 10×10 pixel grid; reachable in ~40 frames per coupon-collector
        min_tilt_fraction = 0.40
        size_bin_min_samples = 3
        size_bin_edges = (0.10, 0.16)  # standard ball-tracking distance gives ~0.12 = medium
        size_bins = {"small": 0, "medium": 0, "large": 0}

        def bin_for(size: float) -> str:
            if size < size_bin_edges[0]:
                return "small"
            if size < size_bin_edges[1]:
                return "medium"
            return "large"

        try:
            for attempt in range(max_attempts):
                pixel_cov = (
                    coverage_tracker.get_pixel_coverage_fraction()
                    if coverage_tracker else 0.0
                )
                coverage_ok = pixel_cov >= min_pixel_coverage
                tilt_ok = good_count > 0 and (tilted_count / good_count) >= min_tilt_fraction
                size_ok = all(n >= size_bin_min_samples for n in size_bins.values())

                if good_count >= target_images and coverage_ok and tilt_ok and size_ok:
                    break

                # Check if calibration was stopped
                if self.calibration_status[camera]["status"] != "distortion_calibrating":
                    log_msg("Calibration stopped by user")
                    return {"status": "stopped", "message": "Calibration stopped"}

                hint = ""
                short_bin = next((b for b, n in size_bins.items() if n < size_bin_min_samples), None)
                if not coverage_ok and coverage_tracker:
                    suggested = coverage_tracker.get_suggested_region()
                    hint = f"Move the board toward the {suggested} of the frame."
                elif short_bin == "small":
                    hint = "Hold the board farther from the camera for a few shots."
                elif short_bin == "large":
                    hint = "Bring the board closer to the camera for a few shots."
                elif short_bin == "medium":
                    hint = "Try the board at mid-range distance."
                elif not tilt_ok:
                    hint = "Tilt the board at an angle for the next few shots."
                else:
                    hint = "Hold the board steady and visible."

                image_progress = min(good_count / target_images, 1.0)
                cov_progress = min(pixel_cov / min_pixel_coverage, 1.0)
                tilt_prog = min((tilted_count / good_count) / min_tilt_fraction, 1.0) if good_count > 0 else 0
                size_prog = min(
                    sum(min(n, size_bin_min_samples) for n in size_bins.values()) /
                    (size_bin_min_samples * 3), 1.0
                )
                progress = int(
                    (image_progress * 0.35 + cov_progress * 0.35 + tilt_prog * 0.15 + size_prog * 0.15) * 80
                )

                self.calibration_status[camera]["progress"] = progress
                self.calibration_status[camera]["hint"] = hint
                self.calibration_status[camera]["tilt_fraction"] = tilted_count / good_count if good_count > 0 else 0
                self.calibration_status[camera]["pixel_coverage"] = pixel_cov
                self.calibration_status[camera]["size_bins"] = dict(size_bins)

                if good_count >= target_images and not (coverage_ok and tilt_ok and size_ok):
                    self.calibration_status[camera]["message"] = (
                        f"All {target_images} images captured -- collecting a few more for better accuracy."
                    )
                else:
                    self.calibration_status[camera]["message"] = (
                        f"Captured {good_count} of {target_images} images"
                    )

                image_path = image_dir / f"calib_{good_count + 1:02d}.png"
                img = await self._capture_image(
                    camera_index, image_path, camera_gain)

                if img is None:
                    rejected_count += 1
                    log_msg(f"Attempt {attempt + 1}: Capture failed")
                    await asyncio.sleep(DISTORTION_CAPTURE_INTERVAL)
                    continue

                gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY) if len(img.shape) == 3 else img
                if image_size is None:
                    image_size = (gray.shape[1], gray.shape[0])
                    coverage_tracker = CoverageTracker(image_size[0], image_size[1])

                corners, ids, _, _ = detector.detect_charuco_corners(gray)
                quality = detector.assess_image_quality(gray, corners)

                if not quality["is_good"]:
                    rejected_count += 1
                    reasons = ", ".join(quality["reasons"])
                    log_msg(f"Attempt {attempt + 1}: Rejected - {reasons}")
                    self.calibration_status[camera]["message"] = (
                        f"Skipped: {friendly_rejection(reasons)}"
                    )
                    self.calibration_status[camera]["images_rejected"] = rejected_count
                    await asyncio.sleep(DISTORTION_CAPTURE_INTERVAL)
                    continue

                params = detector.compute_image_params(corners, image_size)
                if params is None:
                    rejected_count += 1
                    log_msg(f"Attempt {attempt + 1}: Could not compute image parameters")
                    await asyncio.sleep(DISTORTION_CAPTURE_INTERVAL)
                    continue
                if not detector.is_good_sample(params, sample_params):
                    rejected_count += 1
                    log_msg(f"Attempt {attempt + 1}: Too similar to existing sample")
                    self.calibration_status[camera]["message"] = (
                        "Skipped: board position too similar -- move it to a new area"
                    )
                    self.calibration_status[camera]["images_rejected"] = rejected_count
                    await asyncio.sleep(DISTORTION_CAPTURE_INTERVAL)
                    continue

                if quality["tilt_score"] > 0.20:
                    tilted_count += 1

                size_bucket = bin_for(quality["coverage"])
                size_bins[size_bucket] += 1

                all_corners.append(corners)
                all_ids.append(ids)
                sample_params.append(params)
                good_count += 1

                coverage_tracker.update(corners)
                coverage_data = coverage_tracker.to_dict()
                self.calibration_status[camera]["coverage"] = coverage_data
                self.calibration_status[camera]["images_captured"] = good_count
                self.calibration_status[camera]["images_rejected"] = rejected_count
                self.calibration_status[camera]["size_bins"] = dict(size_bins)

                tilt_label = "tilted" if quality["tilt_score"] > 0.20 else "flat"
                log_msg(
                    f"Image {good_count}/{target_images} accepted "
                    f"(sharpness: {quality['blur_score']:.0f}, "
                    f"tilt: {tilt_label}, "
                    f"size: {size_bucket} ({quality['coverage']:.0%}), "
                    f"pixel coverage: {coverage_data['pixel_coverage']:.0%})"
                )

                # Positive feedback with coaching hint
                if good_count < target_images and coverage_tracker:
                    suggested = coverage_tracker.get_suggested_region()
                    self.calibration_status[camera]["message"] = (
                        f"Got it! ({good_count}/{target_images}) -- Try the {suggested} area next."
                    )

                await asyncio.sleep(DISTORTION_CAPTURE_INTERVAL)

            if good_count < 3:
                msg = "Could not capture enough usable images. Check lighting and board visibility."
                log_msg(f"FAILED: Only captured {good_count} good images (need at least 3)")
                self.calibration_status[camera]["status"] = "failed"
                self.calibration_status[camera]["message"] = msg
                self._write_distortion_log(log_file, log_lines)
                return {"status": "failed", "message": msg, "log_file": str(log_file)}

            # Log if coverage/tilt conditions were not fully met
            tilt_fraction = tilted_count / good_count if good_count > 0 else 0
            if not (coverage_ok and tilt_ok):
                unmet = []
                if not coverage_ok and coverage_tracker:
                    unmet.append(f"coverage ({coverage_tracker.get_coverage_fraction():.0%})")
                if not tilt_ok:
                    unmet.append(f"tilt diversity ({tilt_fraction:.0%})")
                log_msg(f"Proceeding with calibration despite unmet conditions: {', '.join(unmet)}")

            # Run calibration with outlier rejection
            self.calibration_status[camera]["message"] = "Processing calibration..."
            self.calibration_status[camera]["progress"] = 85
            log_msg(f"Running calibration on {good_count} images...")

            # k3 is non-trivial on this lens (~-0.13); fixing it biases k1/k2.
            rms, camera_matrix, dist_coeffs, rejected_indices, diagnostics = \
                detector.calibrate_with_filtering(
                    all_corners, all_ids, image_size,
                    coverage_tracker=coverage_tracker,
                    fix_k3=False)

            if rejected_indices:
                log_msg(f"Filtering removed {len(rejected_indices)} frame(s)")

            log_msg(f"Calibration RMS error: {rms:.4f} pixels")
            log_msg(
                f"Per-view error: median {diagnostics['per_view_median']:.4f}px, "
                f"max {diagnostics['per_view_max']:.4f}px")
            log_msg(
                f"Parameter uncertainty: fx=±{diagnostics['std_fx']:.2f}px, "
                f"fy=±{diagnostics['std_fy']:.2f}px, "
                f"cx=±{diagnostics['std_cx']:.2f}px, "
                f"cy=±{diagnostics['std_cy']:.2f}px")
            # >5 px std on fx/fy means the pose set didn't constrain intrinsics.
            if max(diagnostics['std_fx'], diagnostics['std_fy']) > 5.0:
                log_msg(
                    "Warning: high focal-length uncertainty — capture more poses "
                    "with varied board distance and tilt.")

            if rms > 0.7:
                log_msg(f"Warning: RMS error ({rms:.4f}) is above target (0.7). "
                        "Consider recalibrating with more diverse board positions.")

            if rms > 1.2:
                msg = (f"Calibration quality too low (RMS {rms:.2f}px). "
                       "Try again with the board in more varied positions and angles.")
                log_msg(f"REJECTED: {msg}")
                self.calibration_status[camera]["status"] = "failed"
                self.calibration_status[camera]["message"] = msg
                self._write_distortion_log(log_file, log_lines)
                return {"status": "failed", "message": msg, "rms_error": float(rms),
                        "log_file": str(log_file)}

            self.calibration_status[camera]["message"] = "Saving calibration results..."
            self.calibration_status[camera]["progress"] = 95

            self._save_distortion_results(camera, camera_matrix, dist_coeffs, rms)
            log_msg("Calibration results saved to configuration")

            quality_label = (
                "Excellent" if rms < 0.4 else
                "Good" if rms < 0.6 else
                "Acceptable" if rms <= 0.9 else
                "Poor"
            )
            self.calibration_status[camera]["status"] = "completed"
            self.calibration_status[camera]["message"] = (
                f"Calibration complete -- accuracy: {quality_label} (error: {rms:.2f}px)"
            )
            self.calibration_status[camera]["progress"] = 100

            self._write_distortion_log(log_file, log_lines)

            return {
                "status": "success",
                "camera_matrix": camera_matrix.tolist(),
                "dist_coeffs": dist_coeffs.flatten().tolist(),
                "rms_error": float(rms),
                "images_used": good_count - len(rejected_indices),
                "images_rejected": rejected_count + len(rejected_indices),
                "coverage": coverage_tracker.to_dict() if coverage_tracker else None,
                "tilt_diversity": tilt_fraction,
                "log_file": str(log_file),
            }

        except Exception as e:
            logger.error(f"Distortion calibration failed: {e}", exc_info=True)
            log_msg(f"ERROR: {e}")
            self.calibration_status[camera]["status"] = "error"
            self.calibration_status[camera]["message"] = str(e)
            self._write_distortion_log(log_file, log_lines)
            return {"status": "error", "message": str(e), "log_file": str(log_file)}

    def _detect_capture_backend(self) -> str:
        """Auto-detect whether to use rpicam-still (Pi) or cv2.VideoCapture (webcam)."""
        if shutil.which("rpicam-still"):
            logger.info("Capture backend: rpicam-still")
            return "rpicam"
        logger.info("Capture backend: webcam (rpicam-still not found)")
        return "webcam"

    async def _capture_image(
        self, camera_index: int, output_path: Path, gain: float
    ) -> Optional[Any]:
        import cv2

        # Prefer the shared frame from the live feed — avoids conflicting with
        # rpicam-vid which holds exclusive libcamera access to the camera.
        frame = self._shared_frames.get(camera_index)
        if frame is not None:
            frame = frame.copy()
            cv2.imwrite(str(output_path), frame)
            return frame

        if self._capture_backend == "rpicam":
            return await self._capture_rpicam_image(camera_index, output_path, gain)
        return await self._capture_webcam_image(camera_index, output_path)

    def set_shared_frame(self, camera_index: int, frame) -> None:
        self._shared_frames[camera_index] = frame

    def clear_shared_frame(self, camera_index: int) -> None:
        self._shared_frames.pop(camera_index, None)

    async def _capture_webcam_image(
        self, camera_index: int, output_path: Path
    ) -> Optional[Any]:
        import cv2

        # Use shared frame from the live feed if available
        frame = self._shared_frames.get(camera_index)
        if frame is not None:
            logger.debug(f"Using shared frame for camera {camera_index}: shape={frame.shape}")
            frame = frame.copy()
            cv2.imwrite(str(output_path), frame)
            return frame
        logger.debug(f"No shared frame for camera {camera_index}, keys={list(self._shared_frames.keys())}")

        # No live feed — open camera directly at max resolution
        def _grab():
            cap = cv2.VideoCapture(camera_index)
            if not cap.isOpened():
                logger.warning(f"Could not open webcam at index {camera_index}")
                return None
            try:
                cap.set(cv2.CAP_PROP_FRAME_WIDTH, 3840)
                cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 2160)
                # Grab a few frames to let auto-exposure settle
                for _ in range(5):
                    cap.read()
                ret, frame = cap.read()
                if not ret or frame is None:
                    logger.warning("Webcam read() returned no frame")
                    return None
                cv2.imwrite(str(output_path), frame)
                return frame
            finally:
                cap.release()

        try:
            loop = asyncio.get_running_loop()
            return await asyncio.wait_for(
                loop.run_in_executor(None, _grab),
                timeout=DISTORTION_CAPTURE_TIMEOUT
            )
        except asyncio.TimeoutError:
            logger.warning("Webcam capture timed out")
            return None
        except Exception as e:
            logger.error(f"Webcam capture error: {e}")
            return None

    async def _capture_rpicam_image(
        self, camera_index: int, output_path: Path, gain: float
    ) -> Optional[Any]:
        cmd = [
            "rpicam-still",
            "--camera", str(camera_index),
            "-o", str(output_path),
            "--gain", str(gain),
            "--timeout", "1",
            "--nopreview",
            "--encoding", "png",
            "--shutter", str(RPICAM_CAL_SHUTTER_US),
            "--awbgains", "1.0,1.0",
            "--denoise", "cdn_off",
            "--tuning-file", RPICAM_TUNING_FILE,
        ]

        try:
            process = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE
            )
            stdout, stderr = await asyncio.wait_for(
                process.communicate(), timeout=DISTORTION_CAPTURE_TIMEOUT)

            if process.returncode != 0:
                logger.warning(f"rpicam-still failed (rc={process.returncode}): {stderr.decode()}")
                return None

            if not output_path.exists():
                logger.warning(f"rpicam-still produced no output file: {output_path}")
                return None

            import cv2
            img = cv2.imread(str(output_path))
            if img is None:
                logger.warning(f"Failed to read captured image: {output_path}")
            return img

        except asyncio.TimeoutError:
            logger.warning("rpicam-still capture timed out")
            return None
        except FileNotFoundError:
            logger.error("rpicam-still not found - is this running on a Raspberry Pi?")
            return None
        except Exception as e:
            logger.error(f"Error capturing image: {e}")
            return None

    def _save_distortion_results(
        self, camera: str, camera_matrix, dist_coeffs, rms_error: float
    ) -> None:
        """Atomically save camera matrix + distortion vector. C++ hardcodes 3×3 / 5-element."""
        camera_num = "1" if camera == "camera1" else "2"
        matrix_key = f"gs_config.cameras.kCamera{camera_num}CalibrationMatrix"
        distortion_key = f"gs_config.cameras.kCamera{camera_num}DistortionVector"

        matrix_list = camera_matrix.tolist()
        dist_list = dist_coeffs.flatten().tolist()

        if len(matrix_list) != 3 or any(len(row) != 3 for row in matrix_list):
            raise RuntimeError(
                f"Refusing to save: camera matrix shape is not 3×3 "
                f"(got {len(matrix_list)} rows). C++ consumer expects 3×3."
            )
        if len(dist_list) != 5:
            raise RuntimeError(
                f"Refusing to save: distortion vector has {len(dist_list)} "
                "elements, C++ consumer expects exactly 5."
            )

        success, message = self.config_manager.set_calibration_batch({
            matrix_key: matrix_list,
            distortion_key: dist_list,
        })
        if not success:
            raise RuntimeError(f"Failed to save calibration: {message}")

        logger.info(f"Distortion calibration saved for {camera} (RMS={rms_error:.4f})")

    def _write_distortion_log(self, log_file: Path, log_lines: List[str]) -> None:
        """Write distortion calibration log to file."""
        try:
            with open(log_file, "w") as f:
                f.write("\n".join(log_lines))
        except Exception as e:
            logger.error(f"Failed to write distortion log: {e}")

    async def stop_calibration(self, camera: Optional[str] = None) -> Dict[str, Any]:
        """Stop running calibration process(es)

        Handles both ball-based calibration (process termination) and
        distortion calibration (status flag to break capture loop).

        Args:
            camera: Specific camera to stop, or None to stop all

        Returns:
            Dict with stop status
        """
        # Signal distortion calibration to stop via status flag
        distortion_stopped = []
        if camera:
            if self.calibration_status[camera]["status"] == "distortion_calibrating":
                self.calibration_status[camera]["status"] = "stopping"
                self.calibration_status[camera]["message"] = "Stopping..."
                distortion_stopped.append(camera)
                logger.info(f"Signaled distortion calibration stop for {camera}")
        else:
            for cam in ["camera1", "camera2"]:
                if self.calibration_status[cam]["status"] == "distortion_calibrating":
                    self.calibration_status[cam]["status"] = "stopping"
                    self.calibration_status[cam]["message"] = "Stopping..."
                    distortion_stopped.append(cam)
                    logger.info(f"Signaled distortion calibration stop for {cam}")

        async with self._process_lock:
            if camera:
                if camera in self.current_processes:
                    try:
                        process = self.current_processes[camera]
                        await self._terminate_process_gracefully(process, camera)
                        del self.current_processes[camera]
                        logger.info(f"Calibration process stopped for {camera}")
                        return {"status": "stopped", "camera": camera}
                    except Exception as e:
                        logger.error(f"Failed to stop calibration for {camera}: {e}")
                        return {"status": "error", "message": str(e), "camera": camera}
                if distortion_stopped:
                    return {"status": "stopping", "cameras": distortion_stopped}
                return {"status": "not_running", "camera": camera}
            else:
                if not self.current_processes:
                    if distortion_stopped:
                        return {"status": "stopping", "cameras": distortion_stopped}
                    return {"status": "not_running"}

                stopped_cameras = []
                errors = []

                for cam, process in list(self.current_processes.items()):
                    try:
                        await self._terminate_process_gracefully(process, cam)
                        stopped_cameras.append(cam)
                    except Exception as e:
                        logger.error(f"Failed to stop calibration for {cam}: {e}")
                        errors.append(f"{cam}: {e}")

                self.current_processes.clear()

                if errors:
                    return {"status": "partial", "stopped": stopped_cameras, "errors": errors}
                return {"status": "stopped", "cameras": stopped_cameras}

    async def _terminate_process_gracefully(self, process: asyncio.subprocess.Process, camera: str) -> None:
        """Terminate a process gracefully with fallback to kill

        Args:
            process: Process to terminate
            camera: Camera name (for logging)
        """
        try:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), timeout=5.0)
                logger.info(f"Process for {camera} terminated gracefully")
            except asyncio.TimeoutError:
                logger.warning(f"Process for {camera} did not respond to SIGTERM, sending SIGKILL")
                process.kill()
                await process.wait()
                logger.info(f"Process for {camera} killed forcefully")
        except ProcessLookupError:
            logger.info(f"Process for {camera} already terminated")
        except Exception as e:
            logger.error(f"Error terminating process for {camera}: {e}")
            raise

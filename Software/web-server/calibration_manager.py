"""
PiTrac Calibration Manager

Handles all calibration operations including:
- Ball location detection
- Manual calibration
- Auto calibration
- Still image capture
- Calibration data persistence
"""

import asyncio
import logging
import os
import uuid
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional

logger = logging.getLogger(__name__)

CAMERA1_CALIBRATION_TIMEOUT = 30.0  # Camera1 has faster hardware detection
CAMERA2_CALIBRATION_TIMEOUT = 120.0  # Camera2 needs background process initialization
CAMERA2_BACKGROUND_INIT_WAIT = 3.0  # Time to wait for background process to initialize


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
                    return {
                        "completed": True,
                        "method": "api",
                        "api_success": True,
                        "process_exit_code": process.returncode,
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
        system_mode = config.get("system", {}).get("mode", "single")

        cmd = [self.pitrac_binary]

        if system_mode == "single":
            cmd.extend(["--run_single_pi", f"--system_mode={camera}_ball_location"])
        else:
            cmd.append(f"--system_mode={camera}_ball_location")

        search_x = config.get("calibration", {}).get(f"{camera}_search_center_x", 700)
        search_y = config.get("calibration", {}).get(f"{camera}_search_center_y", 500)
        logging_level = config.get("gs_config", {}).get("logging", {}).get("kLoggingLevel", "info")

        cmd.extend(
            [
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                f"--logging_level={logging_level}",
                "--artifact_save_level=all",
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
        if camera == "camera2":
            return await self._run_camera2_auto_calibration()
        else:
            return await self._run_camera1_auto_calibration()

    async def _run_camera1_auto_calibration(self) -> Dict[str, Any]:
        """
        Run camera1 auto calibration with hybrid API + process completion detection.
        Timeout: 30 seconds
        """
        camera = "camera1"
        timeout = CAMERA1_CALIBRATION_TIMEOUT

        logger.info(f"Starting camera1 auto calibration with hybrid detection (timeout={timeout}s)")

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
        system_mode = config.get("system", {}).get("mode", "single")

        cmd = [self.pitrac_binary]

        if system_mode == "single":
            cmd.extend(["--run_single_pi", "--system_mode=camera1AutoCalibrate"])
        else:
            cmd.append("--system_mode=camera1AutoCalibrate")

        search_x = config.get("calibration", {}).get("camera1_search_center_x", 750)
        search_y = config.get("calibration", {}).get("camera1_search_center_y", 500)
        logging_level = config.get("gs_config", {}).get("logging", {}).get("kLoggingLevel", "info")

        cmd.extend(
            [
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                f"--logging_level={logging_level}",
                "--artifact_save_level=all",
                "--show_images=0",
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
            logger.error(f"Camera1 auto calibration failed: {e}", exc_info=True)
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
                        logger.info(f"Terminating {camera} process")
                        try:
                            proc.terminate()
                            await asyncio.wait_for(proc.wait(), timeout=5.0)
                        except asyncio.TimeoutError:
                            logger.warning(f"Force killing {camera} process")
                            proc.kill()
                            await proc.wait()
                    del self.current_processes[camera]

    async def _run_camera2_auto_calibration(self) -> Dict[str, Any]:
        """
        Run camera2 auto calibration with two-process workflow.

        Camera2 in single-Pi mode requires:
        1. Background process: runCam2ProcessForPi1Processing (captures images via IPC)
        2. Foreground process: camera2AutoCalibrate (performs calibration)

        The background process must be started first, run indefinitely,
        and be manually killed after foreground completes.

        Timeout: 120 seconds
        """
        camera = "camera2"
        timeout = CAMERA2_CALIBRATION_TIMEOUT

        logger.info(f"Starting camera2 auto calibration with two-process workflow (timeout={timeout}s)")

        self.calibration_status[camera] = {
            "status": "calibrating",
            "message": "Starting camera2 background process...",
            "progress": 10,
            "last_run": datetime.now().isoformat(),
        }

        session_id, session_data = self._create_calibration_session(camera)
        async with self._calibration_lock:
            self._active_calibrations[session_id] = session_data
            logger.info(f"Pre-registered calibration session {session_id} for {camera}")

        config = self.config_manager.get_config()
        system_mode = config.get("system", {}).get("mode", "single")

        if system_mode != "single":
            logger.info("Dual-Pi mode detected, using standard calibration")
            return await self._run_standard_calibration_fallback(camera, timeout)

        env = self._build_environment(camera)
        logging_level = config.get("gs_config", {}).get("logging", {}).get("kLoggingLevel", "info")

        bg_cmd = [
            self.pitrac_binary,
            "--run_single_pi",
            "--system_mode",
            "runCam2ProcessForPi1Processing",
            "--camera_gain",
            "3.6",
            "--logging_level=info",
            "--artifact_save_level=final_results_only",
        ]
        bg_cmd.extend(self._build_cli_args_from_metadata(camera))

        log_file_bg = self.log_dir / f"calibration_camera2_bg_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
        log_file_fg = self.log_dir / f"calibration_camera2_fg_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"

        logger.info(f"Starting background process: {' '.join(bg_cmd)}")
        logger.info(f"Background log: {log_file_bg}")

        background_process = None
        foreground_process = None

        try:
            background_process = await asyncio.create_subprocess_exec(
                *bg_cmd, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, env=env
            )
            logger.info(f"Background process started (PID {background_process.pid})")

            logger.info(f"Waiting {CAMERA2_BACKGROUND_INIT_WAIT}s for background process initialization")
            await asyncio.sleep(CAMERA2_BACKGROUND_INIT_WAIT)

            if background_process.returncode is not None:
                raise Exception(f"Background process exited prematurely with code {background_process.returncode}")

            self.calibration_status[camera]["message"] = "Running camera2 calibration..."
            self.calibration_status[camera]["progress"] = 30

            search_x = config.get("calibration", {}).get("camera2_search_center_x", 720)
            search_y = config.get("calibration", {}).get("camera2_search_center_y", 500)

            fg_cmd = [
                "sudo",
                "-E",
                self.pitrac_binary,
                "--system_mode",
                "camera2AutoCalibrate",
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                f"--logging_level={logging_level}",
                "--artifact_save_level=final_results_only",
            ]
            fg_cmd.extend(self._build_cli_args_from_metadata(camera))

            logger.info(f"Starting foreground process: {' '.join(fg_cmd)}")
            logger.info(f"Foreground log: {log_file_fg}")

            async with self._process_lock:
                if camera in self.current_processes:
                    raise Exception(f"A calibration process is already running for {camera}")

                foreground_process = await asyncio.create_subprocess_exec(
                    *fg_cmd, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, env=env
                )
                self.current_processes[camera] = foreground_process
                logger.info(f"Foreground process started (PID {foreground_process.pid})")

            completion_result = await self.wait_for_calibration_completion(
                foreground_process, session_id, timeout=timeout
            )

            logger.info(f"{camera}: Completion result: {completion_result}")

            fg_output = ""
            try:
                if foreground_process.stdout and not foreground_process.stdout.at_eof():
                    remaining = await asyncio.wait_for(foreground_process.stdout.read(), timeout=2.0)
                    fg_output = remaining.decode() if remaining else ""
            except Exception as e:
                logger.debug(f"Could not read foreground output: {e}")

            bg_output = ""
            try:
                if background_process.stdout and not background_process.stdout.at_eof():
                    remaining = await asyncio.wait_for(background_process.stdout.read(), timeout=2.0)
                    bg_output = remaining.decode() if remaining else ""
            except Exception as e:
                logger.debug(f"Could not read background output: {e}")

            with open(log_file_fg, "w") as f:
                f.write(f"Completion method: {completion_result['method']}\n")
                f.write(f"API success: {completion_result['api_success']}\n")
                f.write(f"Process exit code: {completion_result['process_exit_code']}\n")
                f.write(f"\n--- Output ---\n{fg_output}")

            with open(log_file_bg, "w") as f:
                f.write("Background process log\n")
                f.write(f"\n--- Output ---\n{bg_output}")

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
                    "output": fg_output,
                    "log_file_fg": str(log_file_fg),
                    "log_file_bg": str(log_file_bg),
                }
            else:
                self.calibration_status[camera]["status"] = "failed"
                self.calibration_status[camera]["message"] = f"Calibration failed ({completion_result['method']})"
                return {
                    "status": "failed",
                    "message": f"Calibration failed via {completion_result['method']}",
                    "completion_result": completion_result,
                    "output": fg_output,
                    "log_file_fg": str(log_file_fg),
                    "log_file_bg": str(log_file_bg),
                }

        except Exception as e:
            logger.error(f"Camera2 auto calibration failed: {e}", exc_info=True)
            self.calibration_status[camera]["status"] = "error"
            self.calibration_status[camera]["message"] = str(e)
            return {"status": "error", "message": str(e)}

        finally:
            async with self._calibration_lock:
                if session_id in self._active_calibrations:
                    logger.debug(f"Cleaning up calibration session {session_id}")
                    self._active_calibrations.pop(session_id, None)

            if background_process is not None and background_process.returncode is None:
                logger.info("Terminating background process (runCam2ProcessForPi1Processing)")
                try:
                    background_process.terminate()
                    await asyncio.wait_for(background_process.wait(), timeout=5.0)
                    logger.info("Background process terminated gracefully")
                except asyncio.TimeoutError:
                    logger.warning("Force killing background process")
                    background_process.kill()
                    await background_process.wait()
                    logger.info("Background process killed")

            async with self._process_lock:
                if camera in self.current_processes:
                    proc = self.current_processes[camera]
                    if proc.returncode is None:
                        logger.info(f"Terminating {camera} foreground process")
                        try:
                            proc.terminate()
                            await asyncio.wait_for(proc.wait(), timeout=5.0)
                        except asyncio.TimeoutError:
                            logger.warning(f"Force killing {camera} foreground process")
                            proc.kill()
                            await proc.wait()
                    del self.current_processes[camera]

    async def _run_standard_calibration_fallback(self, camera: str, timeout: float) -> Dict[str, Any]:
        """
        Fallback for non-single-Pi modes (dual-Pi, etc).
        Uses the old _run_calibration_command logic.
        """
        logger.info(f"Using standard calibration for {camera}")

        config = self.config_manager.get_config()
        cmd = [self.pitrac_binary, f"--system_mode={camera}AutoCalibrate"]

        search_x = config.get("calibration", {}).get(f"{camera}_search_center_x", 700)
        search_y = config.get("calibration", {}).get(f"{camera}_search_center_y", 500)
        logging_level = config.get("gs_config", {}).get("logging", {}).get("kLoggingLevel", "info")

        cmd.extend(
            [
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                f"--logging_level={logging_level}",
                "--artifact_save_level=all",
                "--show_images=0",
            ]
        )
        cmd.extend(self._build_cli_args_from_metadata(camera))

        result = await self._run_calibration_command(cmd, camera, timeout=int(timeout))
        calibration_data = self._parse_calibration_results(result.get("output", ""))

        if calibration_data:
            self.calibration_status[camera]["status"] = "completed"
            self.calibration_status[camera]["message"] = "Calibration successful"
            self.calibration_status[camera]["progress"] = 100
            self.config_manager.reload()
            return {"status": "success", "calibration_data": calibration_data, "output": result.get("output", "")}
        else:
            self.calibration_status[camera]["status"] = "failed"
            self.calibration_status[camera]["message"] = "Calibration failed - check logs"
            return {"status": "failed", "message": "Calibration failed", "output": result.get("output", "")}

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
        system_mode = config.get("system", {}).get("mode", "single")

        cmd = [self.pitrac_binary]

        if system_mode == "single":
            cmd.extend(["--run_single_pi", f"--system_mode={camera}Calibrate"])
        else:
            cmd.append(f"--system_mode={camera}Calibrate")

        search_x = config.get("calibration", {}).get(f"{camera}_search_center_x", 700)
        search_y = config.get("calibration", {}).get(f"{camera}_search_center_y", 500)
        logging_level = config.get("gs_config", {}).get("logging", {}).get("kLoggingLevel", "info")

        cmd.extend(
            [
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                f"--logging_level={logging_level}",
                "--artifact_save_level=all",
            ]
        )
        cmd.extend(self._build_cli_args_from_metadata(camera))

        try:
            result = await self._run_calibration_command(cmd, camera, timeout=180)

            calibration_data = self._parse_calibration_results(result.get("output", ""))

            if calibration_data:
                self.calibration_status[camera]["status"] = "completed"
                self.calibration_status[camera]["message"] = "Manual calibration successful"
                self.calibration_status[camera]["progress"] = 100

                self.config_manager.reload()

                return {"status": "success", "calibration_data": calibration_data, "output": result.get("output", "")}
            else:
                self.calibration_status[camera]["status"] = "failed"
                self.calibration_status[camera]["message"] = "Manual calibration failed"
                return {"status": "failed", "message": "Manual calibration failed", "output": result.get("output", "")}

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
        system_mode = config.get("system", {}).get("mode", "single")

        cmd = [self.pitrac_binary]

        if system_mode == "single":
            cmd.extend(["--run_single_pi", f"--system_mode={camera}", "--cam_still_mode"])
        else:
            cmd.extend([f"--system_mode={camera}", "--cam_still_mode"])

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

        Returns calibration data including focal length and camera angles.
        The angles are stored as arrays [angle1, angle2] representing
        the camera's orientation angles.
        """
        config = self.config_manager.get_config()

        return {
            "camera1": {
                "focal_length": config.get("gs_config", {}).get("cameras", {}).get("kCamera1FocalLength"),
                "angles": config.get("gs_config", {}).get("cameras", {}).get("kCamera1Angles"),
            },
            "camera2": {
                "focal_length": config.get("gs_config", {}).get("cameras", {}).get("kCamera2FocalLength"),
                "angles": config.get("gs_config", {}).get("cameras", {}).get("kCamera2Angles"),
            },
        }

    def _build_cli_args_from_metadata(self, camera: str = "camera1") -> list:
        """Build CLI arguments using metadata from configurations.json

        This method uses the passedVia and passedTo metadata to automatically
        build CLI arguments, similar to pitrac_manager.py
        """
        args = []
        merged_config = self.config_manager.get_config()

        target = camera  # "camera1" or "camera2"

        cli_params = self.config_manager.get_cli_parameters(target)

        # Skip args that we handle separately or need special handling
        skip_args = {
            "--system_mode",
            "--run_single_pi",
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

    async def stop_calibration(self, camera: Optional[str] = None) -> Dict[str, Any]:
        """Stop running calibration process(es)

        Args:
            camera: Specific camera to stop, or None to stop all

        Returns:
            Dict with stop status
        """
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
                return {"status": "not_running", "camera": camera}
            else:
                if not self.current_processes:
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

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
from pathlib import Path
from typing import Dict, Any, Optional, List
from datetime import datetime

logger = logging.getLogger(__name__)


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
        logger.info(f"Starting auto calibration for {camera}")

        self.calibration_status[camera] = {
            "status": "calibrating",
            "message": "Running auto calibration...",
            "progress": 20,
            "last_run": datetime.now().isoformat(),
        }

        config = self.config_manager.get_config()
        system_mode = config.get("system", {}).get("mode", "single")

        cmd = [self.pitrac_binary]

        if system_mode == "single":
            cmd.extend(["--run_single_pi", f"--system_mode={camera}AutoCalibrate"])
        else:
            cmd.append(f"--system_mode={camera}AutoCalibrate")

        search_x = config.get("calibration", {}).get(f"{camera}_search_center_x", 750)
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

        try:
            result = await self._run_calibration_command(cmd, camera, timeout=120)
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

        except Exception as e:
            logger.error(f"Auto calibration failed: {e}")
            self.calibration_status[camera]["status"] = "error"
            self.calibration_status[camera]["message"] = str(e)
            return {"status": "error", "message": str(e)}

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

        cmd.extend([
            f"--output_filename={output_path}",
            "--artifact_save_level=final_results_only"
        ])
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
        """Get current calibration data from config"""
        config = self.config_manager.get_config()

        return {
            "camera1": {
                "focal_length": config.get("gs_config", {}).get("cameras", {}).get("kCamera1FocalLength"),
                "x_offset": config.get("gs_config", {}).get("cameras", {}).get("kCamera1XOffsetForTilt"),
                "y_offset": config.get("gs_config", {}).get("cameras", {}).get("kCamera1YOffsetForTilt"),
            },
            "camera2": {
                "focal_length": config.get("gs_config", {}).get("cameras", {}).get("kCamera2FocalLength"),
                "x_offset": config.get("gs_config", {}).get("cameras", {}).get("kCamera2XOffsetForTilt"),
                "y_offset": config.get("gs_config", {}).get("cameras", {}).get("kCamera2YOffsetForTilt"),
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
        skip_args = {"--system_mode", "--run_single_pi", "--search_center_x", "--search_center_y",
                     "--logging_level", "--artifact_save_level", "--cam_still_mode", "--output_filename",
                     "--show_images", "--config_file"}  # We handle config_file specially below

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

        slot1_type = config.get("gs_config", {}).get("cameras", {}).get("kSystemSlot1CameraType", 4)
        slot2_type = config.get("gs_config", {}).get("cameras", {}).get("kSystemSlot2CameraType", 4)
        env["PITRAC_SLOT1_CAMERA_TYPE"] = str(slot1_type)
        env["PITRAC_SLOT2_CAMERA_TYPE"] = str(slot2_type)

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

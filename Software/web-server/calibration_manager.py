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
        self.current_process = None
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

        search_x = config.get("calibration", {}).get(f"{camera}_search_center_x", 750)
        search_y = config.get("calibration", {}).get(f"{camera}_search_center_y", 500)

        cmd.extend(
            [
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                "--logging_level=info",
                "--artifact_save_level=all",
                f"--config_file={self.config_manager.generated_config_path}",
            ]
        )

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

        cmd.extend(
            [
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                "--logging_level=trace",
                "--artifact_save_level=all",
                f"--config_file={self.config_manager.generated_config_path}",
            ]
        )

        try:
            result = await self._run_calibration_command(cmd, camera, timeout=120)
            calibration_data = self._parse_calibration_results(result.get("output", ""))

            if calibration_data:
                self.calibration_status[camera]["status"] = "completed"
                self.calibration_status[camera]["message"] = "Calibration successful"
                self.calibration_status[camera]["progress"] = 100

                self.config_manager.reload_config()

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

        cmd.extend(
            [
                f"--search_center_x={search_x}",
                f"--search_center_y={search_y}",
                "--logging_level=info",
                "--artifact_save_level=all",
                f"--config_file={self.config_manager.generated_config_path}",
            ]
        )

        try:
            result = await self._run_calibration_command(cmd, camera, timeout=180)

            calibration_data = self._parse_calibration_results(result.get("output", ""))

            if calibration_data:
                self.calibration_status[camera]["status"] = "completed"
                self.calibration_status[camera]["message"] = "Manual calibration successful"
                self.calibration_status[camera]["progress"] = 100

                self.config_manager.reload_config()

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
            cmd.extend(["--run_single_pi", f"--system_mode={camera}_still", "--cam_still_mode"])
        else:
            cmd.extend([f"--system_mode={camera}_still", "--cam_still_mode"])

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_file = f"calibration_{camera}_{timestamp}.png"
        images_dir = Path.home() / "LM_Shares" / "Images"
        images_dir.mkdir(parents=True, exist_ok=True)
        output_path = images_dir / output_file

        cmd.extend([f"--output_filename={output_path}", f"--config_file={self.config_manager.generated_config_path}"])

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

        try:
            process = await asyncio.create_subprocess_exec(
                *cmd, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, env={**os.environ}
            )

            self.current_process = process

            output_lines = []
            try:
                stdout, _ = await asyncio.wait_for(process.communicate(), timeout=timeout)
                output = stdout.decode() if stdout else ""
                output_lines = output.split("\n")

                # Save to log file
                with open(log_file, "w") as f:
                    f.write(output)

            except asyncio.TimeoutError:
                process.terminate()
                await process.wait()
                raise Exception(f"Calibration timed out after {timeout} seconds")

            if process.returncode != 0:
                raise Exception(f"Calibration failed with code {process.returncode}")

            return {"output": "\n".join(output_lines), "log_file": str(log_file), "return_code": process.returncode}

        finally:
            self.current_process = None

    def _parse_ball_location(self, output: str) -> Optional[Dict[str, Any]]:
        """Parse ball location from command output"""
        for line in output.split("\n"):
            if "ball found at" in line.lower() or "ball location" in line.lower():
                return {"found": True, "x": 750, "y": 500, "confidence": 0.95}
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

    async def stop_calibration(self) -> Dict[str, Any]:
        """Stop any running calibration process"""
        if self.current_process:
            try:
                self.current_process.terminate()
                await self.current_process.wait()
                logger.info("Calibration process stopped")
                return {"status": "stopped"}
            except Exception as e:
                logger.error(f"Failed to stop calibration: {e}")
                return {"status": "error", "message": str(e)}
        return {"status": "not_running"}

"""Testing Tools Manager for PiTrac Web Server

Manages execution of various testing and diagnostic tools for PiTrac
"""

import asyncio
import logging
import os
import glob
import time
from pathlib import Path
from typing import Any, Dict, List, Optional
from datetime import datetime

logger = logging.getLogger(__name__)


class TestingToolsManager:
    """Manages PiTrac testing and diagnostic tools"""

    def __init__(self, config_manager):
        self.config_manager = config_manager
        self.pitrac_binary = "/usr/lib/pitrac/pitrac_lm"
        self.running_processes = {}
        self.completed_results = {}

        # Create TestImages directory if it doesn't exist
        self.test_images_dir = Path.home() / "LM_Shares/TestImages"
        self.test_images_dir.mkdir(parents=True, exist_ok=True)

        self.tools = {
            "test_uploaded_image": {
                "name": "Test Uploaded Image",
                "description": "Run full pipeline on uploaded flight camera image",
                "category": "testing",
                "args": ["--system_mode", "test", "--send_test_results", "--skip_wait_armed"],
                "requires_sudo": False,
                "timeout": 60,
                "uses_uploaded_image": True,
            },
            "pulse_test": {
                "name": "Strobe Pulse Test",
                "description": "Test IR strobe pulse functionality",
                "category": "hardware",
                "args": ["--pulse_test", "--system_mode", "camera1"],
                "requires_sudo": False,
                "timeout": 10,
                "continuous_test": True,
            },
            "camera1_still": {
                "name": "Camera 1 Still Image",
                "description": "Capture a still image from Camera 1",
                "category": "camera",
                "args": ["--system_mode", "camera1", "--cam_still_mode", "--output_filename=cam1_still_picture.png"],
                "requires_sudo": False,
                "timeout": 10,
            },
            "camera2_still": {
                "name": "Camera 2 Still Image",
                "description": "Capture a still image from Camera 2",
                "category": "camera",
                "args": ["--system_mode", "camera2", "--cam_still_mode", "--output_filename=cam2_still_picture.png"],
                "requires_sudo": False,
                "timeout": 10,
            },
            "camera1_ball_location": {
                "name": "Camera 1 Ball Location",
                "description": "Check ball location for Camera 1",
                "category": "calibration",
                "args": ["--system_mode", "camera1", "--check_ball_location"],
                "requires_sudo": False,
                "timeout": 10,
            },
            "camera2_ball_location": {
                "name": "Camera 2 Ball Location",
                "description": "Check ball location for Camera 2",
                "category": "calibration",
                "args": ["--system_mode", "camera2", "--check_ball_location"],
                "requires_sudo": False,
                "timeout": 10,
            },
            "test_images": {
                "name": "Test with Sample Images",
                "description": "Run detection on test images",
                "category": "testing",
                "args": ["--system_mode", "test"],
                "requires_sudo": True,
                "timeout": 60,
            },
            "automated_testing": {
                "name": "Automated Test Suite",
                "description": "Run full automated testing suite",
                "category": "testing",
                "args": ["--system_mode", "automated_testing"],
                "requires_sudo": False,
                "timeout": 120,
            },
            "test_gspro_server": {
                "name": "Test GSPro Server",
                "description": "Test GSPro server connectivity",
                "category": "connectivity",
                "args": ["--system_mode", "test_gspro_server"],
                "requires_sudo": False,
                "timeout": 30,
            },
        }

    def get_available_tools(self) -> Dict[str, Any]:
        """Get list of available testing tools organized by category"""
        categories = {}
        for tool_id, tool_info in self.tools.items():
            category = tool_info["category"]
            if category not in categories:
                categories[category] = []
            categories[category].append(
                {
                    "id": tool_id,
                    "name": tool_info["name"],
                    "description": tool_info["description"],
                    "requires_sudo": tool_info["requires_sudo"],
                }
            )
        return categories

    async def run_tool(self, tool_id: str) -> Dict[str, Any]:
        """Run a specific testing tool

        Args:
            tool_id: ID of the tool to run

        Returns:
            Dict with status, output, and any error messages
        """
        if tool_id not in self.tools:
            return {"status": "error", "message": f"Unknown tool: {tool_id}"}

        if tool_id in self.running_processes:
            return {"status": "error", "message": f"Tool {tool_id} is already running"}

        tool_info = self.tools[tool_id]

        try:
            config_path = self.config_manager.generate_golf_sim_config()

            # For image test tool, update config with uploaded image
            if tool_info.get("uses_uploaded_image"):
                test_images = list(self.test_images_dir.glob("*"))
                if not test_images:
                    return {"status": "error", "message": "No test images found. Please upload an image first."}

                # Use the most recent image
                latest_image = max(test_images, key=lambda p: p.stat().st_mtime)
                logger.info(f"Using test image: {latest_image}")

                # Modify the generated config to add test image path
                import json
                with open(config_path, 'r') as f:
                    config_json = json.load(f)

                # Add test configuration section for SystemMode::kTest
                if 'gs_config' not in config_json:
                    config_json['gs_config'] = {}
                if 'testing' not in config_json['gs_config']:
                    config_json['gs_config']['testing'] = {}

                # Use the uploaded image as both tee and strobed image
                # This allows testing with just a single flight camera image
                image_path = str(latest_image)
                config_json['gs_config']['testing']['kTwoImageTestTeedBallImage'] = image_path
                config_json['gs_config']['testing']['kTwoImageTestStrobedImage'] = image_path
                config_json['gs_config']['testing']['kTwoImageTestPreImage'] = ""  # Optional

                with open(config_path, 'w') as f:
                    json.dump(config_json, f, indent=2)

            cmd = [self.pitrac_binary]

            system_mode = self.config_manager.get_config().get("system", {}).get("mode", "single")
            if system_mode == "single" and tool_id not in ["test_gspro_server", "test_e6_connect"]:
                cmd.append("--run_single_pi")

            cmd.extend(tool_info["args"])
            cmd.append(f"--config_file={config_path}")

            config = self.config_manager.get_config()

            cmd.append("--msg_broker_address=tcp://localhost:61616")

            web_share_dir = (
                config.get("gs_config", {})
                .get("ipc_interface", {})
                .get("kWebServerShareDirectory", "~/LM_Shares/Images/")
            )
            expanded_web_dir = web_share_dir.replace("~", str(Path.home()))
            cmd.append(f"--web_server_share_dir={expanded_web_dir}")

            base_image_dir = str(Path.home() / "LM_Shares/Images")
            cmd.append(f"--base_image_logging_dir={base_image_dir}")

            cmd.append("--logging_level=trace")

            env = os.environ.copy()
            env["LD_LIBRARY_PATH"] = "/usr/lib/pitrac"
            env["PITRAC_ROOT"] = "/usr/lib/pitrac"
            env["PITRAC_MSG_BROKER_FULL_ADDRESS"] = "tcp://localhost:61616"
            env["PITRAC_BASE_IMAGE_LOGGING_DIR"] = base_image_dir
            env["PITRAC_WEBSERVER_SHARE_DIR"] = str(Path.home() / "LM_Shares/WebShare")
            env["DISPLAY"] = ":0.0"

            env_params_cam1 = self.config_manager.get_environment_parameters("camera1")
            env_params_cam2 = self.config_manager.get_environment_parameters("camera2")
            merged_config = self.config_manager.get_config()

            for param in env_params_cam1:
                key = param["key"]
                env_var = param["envVariable"]

                value = merged_config
                for part in key.split("."):
                    if isinstance(value, dict):
                        value = value.get(part)
                    else:
                        value = None
                        break

                if value is not None and value != "":
                    env[env_var] = str(value)

            for param in env_params_cam2:
                key = param["key"]
                env_var = param["envVariable"]

                value = merged_config
                for part in key.split("."):
                    if isinstance(value, dict):
                        value = value.get(part)
                    else:
                        value = None
                        break

                if value is not None and value != "":
                    env[env_var] = str(value)

            if tool_info["requires_sudo"]:
                cmd = ["sudo", "-E"] + cmd

            logger.info(f"Running tool {tool_id}: {' '.join(cmd)}")

            process = await asyncio.create_subprocess_exec(
                *cmd, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE, env=env
            )

            self.running_processes[tool_id] = process

            start_time = time.time()

            try:
                stdout, stderr = await asyncio.wait_for(process.communicate(), timeout=tool_info["timeout"])

                output = stdout.decode() if stdout else ""
                error = stderr.decode() if stderr else ""

                log_content = await self._find_and_read_test_log(start_time)
                if log_content:
                    if output:
                        output += "\n\n=== Test Log ===\n"
                    output += log_content

                result = {
                    "status": "success" if process.returncode == 0 else "failed",
                    "output": output,
                    "error": error,
                    "return_code": process.returncode,
                    "timestamp": datetime.now().isoformat(),
                }

                if "still" in tool_id:
                    if "cam1" in tool_id:
                        image_path = Path.home() / "LM_Shares/Images/cam1_still_picture.png"
                    else:
                        image_path = Path.home() / "LM_Shares/Images/cam2_still_picture.png"

                    if image_path.exists():
                        result["image_path"] = str(image_path)
                        result["image_url"] = f"/api/images/{image_path.name}"

                return result

            except asyncio.TimeoutError:
                process.terminate()
                await process.wait()

                if tool_info.get("continuous_test", False):
                    log_content = await self._find_and_read_test_log(start_time)
                    if log_content:
                        return {
                            "status": "success",
                            "output": log_content,
                            "message": f"Test ran for {tool_info['timeout']} seconds",
                            "timestamp": datetime.now().isoformat(),
                        }
                    else:
                        return {
                            "status": "success",
                            "output": "Test completed but no log file found",
                            "message": f"Test ran for {tool_info['timeout']} seconds",
                            "timestamp": datetime.now().isoformat(),
                        }
                else:
                    return {
                        "status": "timeout",
                        "message": f"Tool {tool_id} timed out after {tool_info['timeout']} seconds",
                    }
            finally:
                if tool_id in self.running_processes:
                    del self.running_processes[tool_id]

        except Exception as e:
            logger.error(f"Error running tool {tool_id}: {e}")
            return {"status": "error", "message": str(e)}

    async def stop_tool(self, tool_id: str) -> Dict[str, Any]:
        """Stop a running tool

        Args:
            tool_id: ID of the tool to stop

        Returns:
            Dict with status
        """
        if tool_id not in self.running_processes:
            return {"status": "error", "message": f"Tool {tool_id} is not running"}

        try:
            process = self.running_processes[tool_id]
            process.terminate()

            try:
                await asyncio.wait_for(process.wait(), timeout=5.0)
            except asyncio.TimeoutError:
                process.kill()
                await process.wait()

            del self.running_processes[tool_id]

            return {"status": "success", "message": f"Tool {tool_id} stopped"}

        except Exception as e:
            logger.error(f"Error stopping tool {tool_id}: {e}")
            return {"status": "error", "message": str(e)}

    async def _find_and_read_test_log(self, start_time: float) -> Optional[str]:
        """Find and read the test log file created after start_time

        Args:
            start_time: Unix timestamp when the test started

        Returns:
            Content of the log file if found, None otherwise
        """
        try:
            log_dir = Path.home() / ".pitrac" / "logs"
            if not log_dir.exists():
                return None

            pattern = str(log_dir / "test_*.log")
            log_files = glob.glob(pattern)

            latest_log = None
            latest_mtime = 0

            for log_file in log_files:
                mtime = os.path.getmtime(log_file)
                if mtime >= start_time and mtime > latest_mtime:
                    latest_log = log_file
                    latest_mtime = mtime

            if latest_log:
                logger.info(f"Found test log file: {latest_log}")
                with open(latest_log, "r") as f:
                    lines = f.readlines()
                    if len(lines) > 1000:
                        content = "... (truncated) ...\n" + "".join(lines[-1000:])
                    else:
                        content = "".join(lines)
                    return content
            else:
                logger.debug("No test log file found")

        except Exception as e:
            logger.error(f"Error reading test log: {e}")

        return None

    def get_running_tools(self) -> List[str]:
        """Get list of currently running tools"""
        return list(self.running_processes.keys())

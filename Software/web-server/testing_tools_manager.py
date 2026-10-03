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

from constants import SERVER_PORT

logger = logging.getLogger(__name__)


class TestingToolsManager:
    """Manages PiTrac testing and diagnostic tools"""

    def __init__(self, config_manager):
        self.config_manager = config_manager
        self.pitrac_binary = "/usr/lib/pitrac/pitrac_lm"
        self.running_processes = {}
        self.started_at: Dict[str, float] = {}
        self.stopping: set = set()
        self.last_results: Dict[str, Dict[str, Any]] = {}

        # Create TestImages directory if it doesn't exist
        self.test_images_dir = Path.home() / "LM_Shares/TestImages"
        self.test_images_dir.mkdir(parents=True, exist_ok=True)

        self.tools = {
            "test_uploaded_image": {
                "name": "Test Uploaded Image",
                "description": "Run full pipeline on uploaded flight camera image",
                "before": "Upload a strobed flight camera image above.",
                "success": "Shows the detection log and timing in the output.",
                "category": "testing",
                "args": ["--system_mode", "test"],
                "requires_sudo": False,
                "timeout": 60,
                "uses_uploaded_image": True,
            },
            "pulse_test": {
                "name": "Strobe Pulse Test",
                "description": "Test IR strobe pulse functionality",
                "before": "Runs for 60 seconds.",
                "success": "The strobe pulses until the test ends.",
                "category": "hardware",
                "args": ["--pulse_test", "--system_mode", "camera1"],
                "requires_sudo": False,
                "timeout": 60,
                "continuous_test": True,
            },
            "camera1_still": {
                "name": "Camera 1 Still Image",
                "description": "Capture a still image from Camera 1",
                "before": "Takes up to 10 seconds.",
                "success": "Shows a picture from Camera 1.",
                "category": "camera",
                "args": ["--system_mode", "camera1", "--cam_still_mode", "--output_filename=cam1_still_picture.png"],
                "output_image": "cam1_still_picture.png",
                "requires_sudo": False,
                "timeout": 10,
            },
            "camera2_still": {
                "name": "Camera 2 Still Image",
                "description": "Capture a still image from Camera 2",
                "before": "Takes up to 10 seconds.",
                "success": "Shows a picture from Camera 2.",
                "category": "camera",
                "args": ["--system_mode", "camera2", "--cam_still_mode", "--output_filename=cam2_still_picture.png"],
                "output_image": "cam2_still_picture.png",
                "requires_sudo": False,
                "timeout": 10,
            },
            "camera1_ball_location": {
                "name": "Camera 1 Ball Location",
                "description": "Check ball location for Camera 1",
                "before": "Place a ball on the tee and stop PiTrac.",
                "success": "Prints the ball position in the output.",
                "category": "calibration",
                "args": ["--system_mode", "camera1_ball_location"],
                "requires_sudo": False,
                "timeout": 10,
            },
            "camera2_ball_location": {
                "name": "Camera 2 Ball Location",
                "description": "Check ball location for Camera 2",
                "before": "Place a ball on the tee and stop PiTrac.",
                "success": "Prints the ball position in the output.",
                "category": "calibration",
                "args": ["--system_mode", "camera2_ball_location"],
                "requires_sudo": False,
                "timeout": 10,
            },
            "test_images": {
                "name": "Test with Sample Images",
                "description": "Run detection on test images",
                "before": "Uses the sample images installed with PiTrac.",
                "success": "Shows the detection log and timing in the output.",
                "category": "testing",
                "args": ["--system_mode", "test"],
                "requires_sudo": True,
                "timeout": 60,
            },
            "automated_testing": {
                "name": "Automated Test Suite",
                "description": "Run full automated testing suite",
                "before": "Uses the sample test suite installed with PiTrac. Takes up to 2 minutes.",
                "success": "Prints how each sample shot compares with the expected results.",
                "category": "testing",
                "args": ["--system_mode", "automated_testing"],
                "requires_sudo": False,
                "timeout": 120,
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
                    "before": tool_info["before"],
                    "success": tool_info["success"],
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

        # One tool at a time: they share config_manager.transient_overrides
        running = next(iter(self.running_processes), None)
        if running:
            return {"status": "error", "message": f"Tool {running} is already running"}
        # Reserved with no await since the check, so a run arriving during the spawn sees it
        self.running_processes[tool_id] = None
        self.started_at[tool_id] = time.time()

        tool_info = self.tools[tool_id]

        try:
            testing: Dict[str, str] = {}

            if tool_info.get("uses_uploaded_image"):
                test_images = list(self.test_images_dir.glob("*"))
                if not test_images:
                    return {"status": "error", "message": "No test images found. Upload an image first."}

                latest_image = max(test_images, key=lambda p: p.stat().st_mtime)
                logger.info(f"Using test image: {latest_image}")

                testing["kBaseTestImageDir"] = str(self.test_images_dir) + "/"
                testing["kTwoImageTestTeedBallImage"] = latest_image.name
                testing["kTwoImageTestStrobedBallImage"] = latest_image.name
                testing["kTwoImageTestPreImage"] = ""

            if tool_id in ("test_images", "automated_testing"):
                test_suite_dir = Path("/usr/share/pitrac/test-suites/TestSuite_2025_02_07")

                if tool_id == "test_images" and test_suite_dir.exists():
                    teed_files = sorted(test_suite_dir.glob("*log_ball_final_found_ball_img_Shot_1_*"))
                    strobed_files = sorted(test_suite_dir.glob("*log_cam2_last_strobed_img_Shot_1_*"))
                    if teed_files and strobed_files:
                        testing["kBaseTestImageDir"] = str(test_suite_dir) + "/"
                        testing["kTwoImageTestTeedBallImage"] = teed_files[0].name
                        testing["kTwoImageTestStrobedBallImage"] = strobed_files[0].name

                if tool_id == "automated_testing":
                    testing["kAutomatedTestSuiteDirectory"] = str(test_suite_dir) + "/"
                    testing["kAutomatedTestExpectedResultsCSV"] = "Uneekor Comparison 2025-02-07_Small_Test.csv"

            # Trace keeps the info-level lines _parse_timing_output reads, whatever the UI level is
            self.config_manager.transient_overrides = {"gs_config": {"testing": testing}, "logging": {"level": "trace"}}

            cmd = [self.pitrac_binary]

            cmd.extend(tool_info["args"])
            cmd.append(f"--web_server_port={SERVER_PORT}")

            config = self.config_manager.get_config()

            web_share_dir = (
                config.get("gs_config", {})
                .get("ipc_interface", {})
                .get("kWebServerShareDirectory", "~/LM_Shares/Images/")
            )
            expanded_web_dir = web_share_dir.replace("~", str(Path.home()))
            cmd.append(f"--web_server_share_dir={expanded_web_dir}")

            base_image_dir = str(Path.home() / "LM_Shares/Images")
            cmd.append(f"--base_image_logging_dir={base_image_dir}")

            env = os.environ.copy()
            env["LD_LIBRARY_PATH"] = "/usr/lib/pitrac"
            env["PITRAC_ROOT"] = "/usr/lib/pitrac"
            env["DISPLAY"] = ":0.0"
            env["OMP_WAIT_POLICY"] = "PASSIVE"

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
                    "status": "stopped" if tool_id in self.stopping
                    else "success" if process.returncode == 0 else "failed",
                    "output": output,
                    "error": error,
                    "return_code": process.returncode,
                    "timestamp": datetime.now().isoformat(),
                }

                if image_name := tool_info.get("output_image"):
                    image_path = Path.home() / "LM_Shares/Images" / image_name
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
                        "message": f"{tool_info['name']} timed out after {tool_info['timeout']} seconds",
                    }

        except Exception as e:
            logger.error(f"Error running tool {tool_id}: {e}")
            return {"status": "error", "message": str(e)}
        finally:
            self.running_processes.pop(tool_id, None)
            self.started_at.pop(tool_id, None)
            self.stopping.discard(tool_id)
            self.config_manager.transient_overrides = {}

    async def stop_tool(self, tool_id: str) -> Dict[str, Any]:
        """Stop a running tool

        Args:
            tool_id: ID of the tool to stop

        Returns:
            Dict with status
        """
        if tool_id not in self.running_processes:
            return {"status": "error", "message": f"Tool {tool_id} is not running"}
        process = self.running_processes[tool_id]
        if process is None:
            return {"status": "error", "message": f"Tool {tool_id} is still starting"}

        try:
            self.stopping.add(tool_id)
            process.terminate()

            try:
                await asyncio.wait_for(process.wait(), timeout=5.0)
            except asyncio.TimeoutError:
                process.kill()
                await process.wait()

            # run_tool's finally releases the slot and the overrides once its cleanup is done
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

                    timing_summary = self._extract_timing_summary(lines)
                    if timing_summary:
                        content += "\n\n" + timing_summary

                    return content
            else:
                logger.debug("No test log file found")

        except Exception as e:
            logger.error(f"Error reading test log: {e}")

        return None

    def _extract_timing_summary(self, log_lines: List[str]) -> Optional[str]:
        """Extract timing information from log lines and create a summary

        Args:
            log_lines: List of log file lines

        Returns:
            Formatted timing summary string, or None if no timing data found
        """
        import re

        timing_data = {
            "grayscale": [],
            "ncnn_preload": None,
            "ncnn_warmup": None,
            "ncnn_detection": [],
            "opencv_fallback": [],
            "getball": [],
            "spin_detection": [],
        }

        for line in log_lines:
            # Grayscale conversion (microseconds)
            if "Grayscale conversion completed in" in line:
                match = re.search(r"(\d+)us", line)
                if match:
                    timing_data["grayscale"].append(int(match.group(1)))

            # NCNN preload
            elif "NCNN model preloaded in" in line:
                match = re.search(r"in (\d+)ms", line)
                if match:
                    timing_data["ncnn_preload"] = int(match.group(1))

            # NCNN warmup
            elif "NCNN warmup complete" in line:
                match = re.search(r"\((\d+) iterations\)", line)
                if match:
                    timing_data["ncnn_warmup"] = int(match.group(1))

            # NCNN detection
            elif "NCNN detected" in line and "balls in" in line:
                match = re.search(r"in (\d+)ms", line)
                if match:
                    timing_data["ncnn_detection"].append(int(match.group(1)))

            # OpenCV DNN fallback
            elif "OpenCV DNN completed processing in" in line:
                match = re.search(r"in (\d+) ms", line)
                if match:
                    timing_data["opencv_fallback"].append(int(match.group(1)))

            # GetBall (ball detection)
            elif "GetBall (ball detection) completed in" in line:
                match = re.search(r"in (\d+)ms", line)
                if match:
                    timing_data["getball"].append(int(match.group(1)))

            # Spin detection
            elif "Spin detection completed in" in line:
                match = re.search(r"in (\d+)ms", line)
                if match:
                    timing_data["spin_detection"].append(int(match.group(1)))

        # Check if we have any timing data
        has_data = (
            timing_data["ncnn_preload"]
            or timing_data["ncnn_detection"]
            or timing_data["opencv_fallback"]
            or timing_data["getball"]
            or timing_data["spin_detection"]
            or timing_data["grayscale"]
        )

        if not has_data:
            return None

        # Build summary
        summary = ["=" * 80]
        summary.append("PERFORMANCE TIMING SUMMARY")
        summary.append("=" * 80)

        if timing_data["ncnn_preload"]:
            summary.append(f"\n Initialization:")
            summary.append(f"  NCNN Model Preload: {timing_data['ncnn_preload']}ms")
            if timing_data["ncnn_warmup"]:
                summary.append(f"  NCNN Warmup: {timing_data['ncnn_warmup']} iterations")

        if timing_data["grayscale"]:
            avg_gray = sum(timing_data["grayscale"]) / len(timing_data["grayscale"])
            summary.append(f"\n Image Preprocessing:")
            summary.append(f"  Grayscale Conversion: {avg_gray:.0f}μs (avg of {len(timing_data['grayscale'])} ops)")

        if timing_data["ncnn_detection"]:
            avg_ncnn = sum(timing_data["ncnn_detection"]) / len(timing_data["ncnn_detection"])
            summary.append(f"\n Ball Detection (NCNN):")
            summary.append(f"  Average: {avg_ncnn:.0f}ms")
            summary.append(f"  Count: {len(timing_data['ncnn_detection'])} detections")
            if len(timing_data["ncnn_detection"]) > 1:
                summary.append(
                    f"  Range: {min(timing_data['ncnn_detection'])}ms - {max(timing_data['ncnn_detection'])}ms"
                )

        if timing_data["opencv_fallback"]:
            avg_opencv = sum(timing_data["opencv_fallback"]) / len(timing_data["opencv_fallback"])
            summary.append(f"\n OpenCV DNN Fallback:")
            summary.append(f"  Average: {avg_opencv:.0f}ms")
            summary.append(f"  Fallback Count: {len(timing_data['opencv_fallback'])}")

        if timing_data["getball"]:
            avg_getball = sum(timing_data["getball"]) / len(timing_data["getball"])
            summary.append(f"\n GetBall (Legacy Detection):")
            summary.append(f"  Average: {avg_getball:.0f}ms")
            summary.append(f"  Count: {len(timing_data['getball'])}")

        if timing_data["spin_detection"]:
            avg_spin = sum(timing_data["spin_detection"]) / len(timing_data["spin_detection"])
            summary.append(f"\n Spin Analysis:")
            summary.append(f"  Average: {avg_spin:.0f}ms")
            summary.append(f"  Count: {len(timing_data['spin_detection'])}")

        # Calculate total per-shot time if we have detection + spin
        if timing_data["ncnn_detection"] and timing_data["spin_detection"]:
            avg_detection = sum(timing_data["ncnn_detection"]) / len(timing_data["ncnn_detection"])
            avg_spin = sum(timing_data["spin_detection"]) / len(timing_data["spin_detection"])
            total_per_shot = avg_detection + avg_spin
            summary.append(f"\n  Total Per-Shot Time:")
            summary.append(f"  Detection + Spin: ~{total_per_shot:.0f}ms ({total_per_shot/1000:.2f}s)")

        summary.append("\n" + "=" * 80)

        return "\n".join(summary)

    def get_running_tools(self) -> List[str]:
        """Get list of currently running tools"""
        return list(self.running_processes.keys())

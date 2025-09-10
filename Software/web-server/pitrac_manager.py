"""
PiTrac Process Manager - Manages the lifecycle of the pitrac_lm process
"""

import asyncio
import logging
import os
import signal
import subprocess
from pathlib import Path
from typing import Optional, Dict, Any
import json
from config_manager import ConfigurationManager

logger = logging.getLogger(__name__)


class PiTracProcessManager:
    """Manages the PiTrac launch monitor process"""

    def __init__(self, config_manager: Optional[ConfigurationManager] = None):
        self.process: Optional[subprocess.Popen] = None
        self.camera2_process: Optional[subprocess.Popen] = (
            None
        )
        self.pitrac_binary = "/usr/lib/pitrac/pitrac_lm"
        self.config_file = "/etc/pitrac/golf_sim_config.json"
        self.log_file = Path.home() / ".pitrac" / "logs" / "pitrac.log"
        self.camera2_log_file = Path.home() / ".pitrac" / "logs" / "pitrac_camera2.log"
        self.pid_file = Path.home() / ".pitrac" / "run" / "pitrac.pid"
        self.camera2_pid_file = Path.home() / ".pitrac" / "run" / "pitrac_camera2.pid"

        self.config_manager = config_manager or ConfigurationManager()

        self.log_file.parent.mkdir(parents=True, exist_ok=True)
        self.pid_file.parent.mkdir(parents=True, exist_ok=True)

    def _load_pitrac_config(self) -> Dict[str, Any]:
        """Load PiTrac configuration from JSON config manager"""
        config = self.config_manager.get_config()

        transformed = {
            "system": {
                "mode": config.get("system.mode", "single"),
                "camera_role": config.get("system.camera_role", "camera1"),
            },
            "logging": {"level": config.get("logging.level", "info")},
            "network": {
                "broker_address": config.get(
                    "gs_config.ipc_interface.kWebActiveMQHostAddress",
                    "tcp://localhost:61616",
                )
            },
            "storage": {
                "image_dir": config.get("storage.image_dir"),
                "web_share_dir": config.get("storage.web_share_dir"),
            },
            "simulators": {
                "e6_host": config.get(
                    "gs_config.golf_simulator_interfaces.E6.kE6ConnectAddress"
                ),
                "gspro_host": config.get(
                    "gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress"
                ),
            },
            "cameras": {
                "camera1_gain": config.get("gs_config.cameras.kCamera1Gain", 1.0),
                "camera2_gain": config.get("gs_config.cameras.kCamera2Gain", 4.0),
            },
        }

        return transformed

    def _build_command(self, camera: str = "camera1") -> list:
        """Build the command to run pitrac_lm with proper arguments"""
        cmd = [self.pitrac_binary]

        config = self._load_pitrac_config()

        system_config = config.get("system") or {}

        is_single_pi = system_config.get("mode", "single") == "single"

        if is_single_pi:
            cmd.append(f"--system_mode={camera}")
            cmd.append("--run_single_pi")
        else:
            camera_role = system_config.get("camera_role", "camera1")
            cmd.append(f"--system_mode={camera_role}")

        logging_config = config.get("logging") or {}
        log_level = logging_config.get("level", "info")
        cmd.append(f"--logging_level={log_level}")

        network_config = config.get("network") or {}
        msg_broker = network_config.get("broker_address")
        if not msg_broker:
            msg_broker = "tcp://localhost:61616"
        cmd.append(f"--msg_broker_address={msg_broker}")

        storage_config = config.get("storage") or {}
        base_image_dir = storage_config.get("image_dir")
        if not base_image_dir:
            base_image_dir = str(Path.home() / "LM_Shares" / "Images")
        if not base_image_dir.endswith("/"):
            base_image_dir += "/"
        cmd.append(f"--base_image_logging_dir={base_image_dir}")

        web_share_dir = storage_config.get("web_share_dir")
        if not web_share_dir:
            web_share_dir = str(Path.home() / "LM_Shares" / "WebShare")
        if not web_share_dir.endswith("/"):
            web_share_dir += "/"
        cmd.append(f"--web_server_share_dir={web_share_dir}")

        simulators_config = config.get("simulators") or {}
        e6_host = simulators_config.get("e6_host")
        if e6_host:
            cmd.append(f"--e6_host_address={e6_host}")

        gspro_host = simulators_config.get("gspro_host")
        if gspro_host:
            cmd.append(f"--gspro_host_address={gspro_host}")

        if Path(self.config_file).exists():
            cmd.append(f"--config_file={self.config_file}")

        cameras_config = config.get("cameras") or {}
        if camera == "camera1" and "camera1_gain" in cameras_config:
            cmd.append(f"--camera_gain={cameras_config['camera1_gain']}")
        elif camera == "camera2" and "camera2_gain" in cameras_config:
            cmd.append(f"--camera_gain={cameras_config['camera2_gain']}")

        merged_config = self.config_manager.get_config()

        golfer_orientation = (
            merged_config.get("gs_config", {})
            .get("player", {})
            .get("kGolferOrientation")
        )
        if golfer_orientation:
            cmd.append(f"--golfer_orientation={golfer_orientation}")

        use_practice_balls = (
            merged_config.get("gs_config", {})
            .get("player", {})
            .get("kUsePracticeBalls")
        )
        if use_practice_balls:
            cmd.append("--practice_ball")

        artifact_save_level = (
            merged_config.get("gs_config", {})
            .get("logging", {})
            .get("kArtifactSaveLevel")
        )
        if artifact_save_level:
            cmd.append(f"--artifact_save_level={artifact_save_level}")

        show_debug_images = (
            merged_config.get("gs_config", {}).get("debug", {}).get("kShowDebugImages")
        )
        if show_debug_images:
            cmd.append("--show_images")

        wait_for_key = (
            merged_config.get("gs_config", {})
            .get("debug", {})
            .get("kWaitForKeyOnImages")
        )
        if wait_for_key:
            cmd.append("--wait_keys")

        logger.info(f"Built command: {' '.join(cmd)}")
        return cmd

    async def start(self) -> Dict[str, Any]:
        """Start the PiTrac process"""
        if self.is_running():
            return {
                "status": "already_running",
                "message": "PiTrac is already running",
                "pid": self.get_pid(),
            }

        try:
            Path(self.log_file).parent.mkdir(parents=True, exist_ok=True)
            Path(self.pid_file).parent.mkdir(parents=True, exist_ok=True)

            config = self._load_pitrac_config()
            system_config = config.get("system") or {}
            is_single_pi = system_config.get("mode", "single") == "single"

            cmd = self._build_command("camera1")

            env = os.environ.copy()
            env["LD_LIBRARY_PATH"] = "/usr/lib/pitrac"
            env["PITRAC_ROOT"] = (
                "/usr/lib/pitrac"
            )
            home_dir = str(Path.home())
            env["PITRAC_BASE_IMAGE_LOGGING_DIR"] = f"{home_dir}/LM_Shares/Images/"
            env["PITRAC_WEBSERVER_SHARE_DIR"] = f"{home_dir}/LM_Shares/WebShare/"
            env["PITRAC_MSG_BROKER_FULL_ADDRESS"] = "tcp://localhost:61616"

            config = self._load_pitrac_config()
            cameras_config = config.get("cameras") or {}

            slot1 = cameras_config.get("slot1") or {}
            if "type" in slot1:
                env["PITRAC_SLOT1_CAMERA_TYPE"] = str(slot1["type"])
            if "lens" in slot1:
                env["PITRAC_SLOT1_LENS_TYPE"] = str(slot1["lens"])

            slot2 = cameras_config.get("slot2") or {}
            if "type" in slot2:
                env["PITRAC_SLOT2_CAMERA_TYPE"] = str(slot2["type"])
            if "lens" in slot2:
                env["PITRAC_SLOT2_LENS_TYPE"] = str(slot2["lens"])

            Path(f"{home_dir}/LM_Shares/Images").mkdir(parents=True, exist_ok=True)
            Path(f"{home_dir}/LM_Shares/WebShare").mkdir(parents=True, exist_ok=True)

            with open(self.log_file, "a") as log:
                self.process = subprocess.Popen(
                    cmd,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    env=env,
                    cwd=str(Path.home()),
                    preexec_fn=os.setsid,
                )

                with open(self.pid_file, "w") as f:
                    f.write(str(self.process.pid))

                await asyncio.sleep(3)

                if self.process.poll() is None:
                    logger.info(
                        f"PiTrac camera1 started successfully with PID {self.process.pid}"
                    )

                    if is_single_pi:
                        logger.info(
                            "Starting camera2 process for single-Pi dual camera mode..."
                        )

                        cmd2 = self._build_command("camera2")

                        with open(self.camera2_log_file, "a") as log2:
                            self.camera2_process = subprocess.Popen(
                                cmd2,
                                stdout=log2,
                                stderr=subprocess.STDOUT,
                                env=env,
                                cwd=str(Path.home()),
                                preexec_fn=os.setsid,
                            )

                            with open(self.camera2_pid_file, "w") as f:
                                f.write(str(self.camera2_process.pid))

                            await asyncio.sleep(3)

                            if self.camera2_process.poll() is None:
                                logger.info(
                                    f"PiTrac camera2 started successfully with PID {self.camera2_process.pid}"
                                )
                                return {
                                    "status": "started",
                                    "message": "PiTrac started successfully (both cameras)",
                                    "camera1_pid": self.process.pid,
                                    "camera2_pid": self.camera2_process.pid,
                                }
                            else:
                                logger.error("Camera2 process exited immediately")
                                if self.camera2_pid_file.exists():
                                    self.camera2_pid_file.unlink()
                                self.camera2_process = None

                                try:
                                    os.kill(self.process.pid, signal.SIGTERM)
                                except ProcessLookupError:
                                    pass
                                if self.pid_file.exists():
                                    self.pid_file.unlink()
                                self.process = None

                                return {
                                    "status": "failed",
                                    "message": "Camera2 failed to start - check logs",
                                    "log_file": str(self.camera2_log_file),
                                }
                    else:
                        return {
                            "status": "started",
                            "message": "PiTrac started successfully",
                            "pid": self.process.pid,
                        }
                else:
                    logger.error("PiTrac process exited immediately")
                    if self.pid_file.exists():
                        self.pid_file.unlink()
                    self.process = None
                    return {
                        "status": "failed",
                        "message": "PiTrac failed to start - check logs",
                        "log_file": str(self.log_file),
                    }

        except Exception as e:
            logger.error(f"Failed to start PiTrac: {e}")
            return {"status": "error", "message": f"Failed to start PiTrac: {str(e)}"}

    async def stop(self) -> Dict[str, Any]:
        """Stop the PiTrac process(es) gracefully"""
        if not self.is_running():
            return {"status": "not_running", "message": "PiTrac is not running"}

        try:
            stopped_cameras = []

            pid = self.get_pid()
            if pid:
                os.kill(pid, signal.SIGTERM)
                logger.info(f"Sent SIGTERM to PiTrac camera1 process {pid}")

                max_wait = 5
                for _ in range(max_wait * 10):
                    await asyncio.sleep(0.1)
                    if not self.is_running():
                        break

                if self.is_running():
                    logger.warning("PiTrac camera1 didn't stop gracefully, forcing...")
                    os.kill(pid, signal.SIGKILL)
                    await asyncio.sleep(0.5)

                if self.pid_file.exists():
                    self.pid_file.unlink()

                self.process = None
                stopped_cameras.append("camera1")

            camera2_pid = self.get_camera2_pid()
            if camera2_pid:
                os.kill(camera2_pid, signal.SIGTERM)
                logger.info(f"Sent SIGTERM to PiTrac camera2 process {camera2_pid}")

                max_wait = 5
                for _ in range(max_wait * 10):
                    await asyncio.sleep(0.1)
                    try:
                        os.kill(camera2_pid, 0)
                    except ProcessLookupError:
                        break

                try:
                    os.kill(camera2_pid, signal.SIGKILL)
                    await asyncio.sleep(0.5)
                except ProcessLookupError:
                    pass

                if self.camera2_pid_file.exists():
                    self.camera2_pid_file.unlink()

                self.camera2_process = None
                stopped_cameras.append("camera2")

            if stopped_cameras:
                cameras_msg = " and ".join(stopped_cameras)
                logger.info(f"PiTrac stopped successfully ({cameras_msg})")
                return {
                    "status": "stopped",
                    "message": f"PiTrac stopped successfully ({cameras_msg})",
                }
            else:
                return {
                    "status": "error",
                    "message": "Could not find PiTrac process ID",
                }

        except Exception as e:
            logger.error(f"Failed to stop PiTrac: {e}")
            return {"status": "error", "message": f"Failed to stop PiTrac: {str(e)}"}

    def is_running(self) -> bool:
        """Check if PiTrac is currently running (any camera process)"""
        pid = self.get_pid()
        if pid:
            try:
                os.kill(pid, 0)
                return True
            except ProcessLookupError:
                if self.pid_file.exists():
                    self.pid_file.unlink()

        camera2_pid = self.get_camera2_pid()
        if camera2_pid:
            try:
                os.kill(camera2_pid, 0)
                return True
            except ProcessLookupError:
                if self.camera2_pid_file.exists():
                    self.camera2_pid_file.unlink()

        return False

    def get_pid(self) -> Optional[int]:
        """Get the PID of the running PiTrac camera1 process"""
        if self.process and self.process.poll() is None:
            return self.process.pid

        if self.pid_file.exists():
            try:
                with open(self.pid_file, "r") as f:
                    pid = int(f.read().strip())
                    os.kill(pid, 0)
                    with open(f"/proc/{pid}/cmdline", "r") as cmdline:
                        if "pitrac_lm" in cmdline.read():
                            return pid
            except (ValueError, IOError, ProcessLookupError, FileNotFoundError):
                if self.pid_file.exists():
                    self.pid_file.unlink()

        return None

    def get_camera2_pid(self) -> Optional[int]:
        """Get the PID of the running PiTrac camera2 process"""
        if self.camera2_process and self.camera2_process.poll() is None:
            return self.camera2_process.pid

        if self.camera2_pid_file.exists():
            try:
                with open(self.camera2_pid_file, "r") as f:
                    pid = int(f.read().strip())
                    os.kill(pid, 0)
                    with open(f"/proc/{pid}/cmdline", "r") as cmdline:
                        if "pitrac_lm" in cmdline.read():
                            return pid
            except (ValueError, IOError, ProcessLookupError, FileNotFoundError):
                if self.camera2_pid_file.exists():
                    self.camera2_pid_file.unlink()

        return None

    def get_status(self) -> Dict[str, Any]:
        """Get detailed status of PiTrac process(es)"""
        camera1_pid = self.get_pid()
        camera2_pid = self.get_camera2_pid()

        status = {
            "running": camera1_pid is not None or camera2_pid is not None,
            "camera1_pid": camera1_pid,
            "camera2_pid": camera2_pid,
            "camera1_log_file": str(self.log_file),
            "camera2_log_file": str(self.camera2_log_file),
            "config_file": self.config_file,
            "binary": self.pitrac_binary,
        }

        config = self._load_pitrac_config()
        system_config = config.get("system") or {}
        status["mode"] = system_config.get("mode", "single")

        if self.log_file.exists():
            try:
                with open(self.log_file, "r") as f:
                    lines = f.readlines()
                    status["camera1_recent_logs"] = (
                        lines[-10:] if len(lines) > 10 else lines
                    )
            except Exception as e:
                status["camera1_log_error"] = str(e)

        if self.camera2_log_file.exists():
            try:
                with open(self.camera2_log_file, "r") as f:
                    lines = f.readlines()
                    status["camera2_recent_logs"] = (
                        lines[-10:] if len(lines) > 10 else lines
                    )
            except Exception as e:
                status["camera2_log_error"] = str(e)

        return status

    async def restart(self) -> Dict[str, Any]:
        """Restart the PiTrac process"""
        logger.info("Restarting PiTrac...")

        if self.is_running():
            stop_result = await self.stop()
            if stop_result["status"] == "error":
                return stop_result

            await asyncio.sleep(1)

        return await self.start()

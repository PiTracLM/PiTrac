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
from config_manager import ConfigurationManager

logger = logging.getLogger(__name__)


class PiTracProcessManager:
    """Manages the PiTrac launch monitor process"""

    def __init__(self, config_manager: Optional[ConfigurationManager] = None):
        self.process: Optional[subprocess.Popen] = None
        self.camera2_process: Optional[subprocess.Popen] = None
        self.config_manager = config_manager or ConfigurationManager()
        
        metadata = self.config_manager.load_configurations_metadata()
        sys_paths = metadata.get("systemPaths", {})
        proc_mgmt = metadata.get("processManagement", {})
        
        def expand_path(path_str: str) -> Path:
            return Path(path_str.replace("~", str(Path.home())))
        
        self.pitrac_binary = sys_paths.get("pitracBinary", {}).get("default", "/usr/lib/pitrac/pitrac_lm")
        self.config_file = sys_paths.get("configFile", {}).get("default", "/etc/pitrac/golf_sim_config.json")
        
        log_dir = expand_path(sys_paths.get("logDirectory", {}).get("default", "~/.pitrac/logs"))
        pid_dir = expand_path(sys_paths.get("pidDirectory", {}).get("default", "~/.pitrac/run"))
        
        self.log_file = log_dir / proc_mgmt.get("camera1LogFile", {}).get("default", "pitrac.log")
        self.camera2_log_file = log_dir / proc_mgmt.get("camera2LogFile", {}).get("default", "pitrac_camera2.log")
        self.pid_file = pid_dir / proc_mgmt.get("camera1PidFile", {}).get("default", "pitrac.pid")
        self.camera2_pid_file = pid_dir / proc_mgmt.get("camera2PidFile", {}).get("default", "pitrac_camera2.pid")
        
        self.process_check_command = proc_mgmt.get("processCheckCommand", {}).get("default", "pitrac_lm")
        self.startup_delay_camera2 = proc_mgmt.get("startupDelayCamera2", {}).get("default", 2)
        self.startup_wait_camera2_ready = proc_mgmt.get("startupWaitCamera2Ready", {}).get("default", 1)
        self.startup_delay_camera1 = proc_mgmt.get("startupDelayCamera1", {}).get("default", 3)
        self.shutdown_grace_period = proc_mgmt.get("shutdownGracePeriod", {}).get("default", 5)
        self.shutdown_check_interval = proc_mgmt.get("shutdownCheckInterval", {}).get("default", 0.1)
        self.post_kill_delay = proc_mgmt.get("postKillDelay", {}).get("default", 0.5)
        self.restart_delay = proc_mgmt.get("restartDelay", {}).get("default", 1)
        self.recent_log_lines = proc_mgmt.get("recentLogLines", {}).get("default", 10)
        
        self.termination_signal = getattr(signal, proc_mgmt.get("terminationSignal", {}).get("default", "SIGTERM"))
        self.kill_signal = getattr(signal, proc_mgmt.get("killSignal", {}).get("default", "SIGKILL"))

        self.log_file.parent.mkdir(parents=True, exist_ok=True)
        self.pid_file.parent.mkdir(parents=True, exist_ok=True)

    def _load_pitrac_config(self) -> Dict[str, Any]:
        """Load PiTrac configuration from JSON config manager"""

        config = self.config_manager.get_config()
        metadata = self.config_manager.load_configurations_metadata()
        system_defaults = metadata.get("systemDefaults", {})
        camera_defs = metadata.get("cameraDefinitions", {})
        
        struct = system_defaults.get("configStructure", {})
        system_key = struct.get("systemKey", "system")
        cameras_key = struct.get("camerasKey", "cameras")
        
        transformed = {
            system_key: {
                "mode": config.get(system_key, {}).get("mode", system_defaults.get("mode", "single")),
                "camera_role": config.get(system_key, {}).get("camera_role", system_defaults.get("cameraRole", "camera1")),
            },
            cameras_key: {}
        }
        
        for cam_name, cam_def in camera_defs.items():
            slot = cam_def.get("slot", f"slot{cam_name[-1]}")
            default_idx = cam_def.get("defaultIndex", 0)
            transformed[cameras_key][slot] = config.get(cameras_key, {}).get(slot, {"index": default_idx})
        
        return transformed

    def _build_cli_args_from_metadata(self, camera: str = "camera1") -> list:
        """Build CLI arguments using metadata from configurations.json
        
        This method uses the passedVia and passedTo metadata to automatically
        build CLI arguments instead of manual hardcoding.
        """
        args = []
        merged_config = self.config_manager.get_config()
        
        target = camera  # "camera1" or "camera2"
        
        cli_params = self.config_manager.get_cli_parameters(target)
        
        for param in cli_params:
            key = param["key"]
            cli_arg = param["cliArgument"]
            param_type = param["type"]
            
            value = merged_config
            for part in key.split("."):
                if isinstance(value, dict):
                    value = value.get(part)
                else:
                    value = None
                    break
            
            if value is None:
                continue
                
            if param_type == "boolean":
                if value:
                    args.append(cli_arg)
            else:
                args.extend([cli_arg, str(value)])
        
        return args
    
    def _set_environment_from_metadata(self, camera: str = "camera1") -> dict:
        """Set environment variables using metadata from configurations.json
        
        This method uses the passedVia and passedTo metadata to automatically
        set environment variables instead of manual hardcoding.
        """
        env = os.environ.copy()
        merged_config = self.config_manager.get_config()
        
        target = camera  # "camera1" or "camera2"
        
        # Get environment parameters for this target
        env_params = self.config_manager.get_environment_parameters(target)
        
        for param in env_params:
            key = param["key"]
            env_var = param["envVariable"]
            
            value = merged_config
            for part in key.split("."):
                if isinstance(value, dict):
                    value = value.get(part)
                else:
                    value = None
                    break
            
            if value is not None:
                env[env_var] = str(value)
        
        return env

    def _build_command(self, camera: str = "camera1") -> list:
        """Build the command to run pitrac_lm with proper arguments"""

        cmd = [self.pitrac_binary]
        
        config = self._load_pitrac_config()
        metadata = self.config_manager.load_configurations_metadata()
        system_defaults = metadata.get("systemDefaults", {})
        camera_defs = metadata.get("cameraDefinitions", {})
        
        struct = system_defaults.get("configStructure", {})
        system_key = struct.get("systemKey", "system")
        cameras_key = struct.get("camerasKey", "cameras")
        
        system_config = config.get(system_key) or {}
        cameras_config = config.get(cameras_key) or {}
        
        default_mode = system_defaults.get("mode", "single")
        is_single_pi = system_config.get("mode", default_mode) == "single"
        if is_single_pi:
            cmd.append(f"--system_mode={camera}")
            cmd.append("--run_single_pi")
        else:
            default_role = system_defaults.get("cameraRole", "camera1")
            camera_role = system_config.get("camera_role", default_role)
            cmd.append(f"--system_mode={camera_role}")

        if camera in camera_defs:
            cam_def = camera_defs[camera]
            slot = cam_def.get("slot")
            default_idx = cam_def.get("defaultIndex", 0)
            slot_config = cameras_config.get(slot) or {}
            camera_index = slot_config.get("index", str(default_idx))
            cmd.append(f"--camera={camera_index}")

        if Path(self.config_file).exists():
            cmd.append(f"--config_file={self.config_file}")

        metadata_args = self._build_cli_args_from_metadata(camera)
        cmd.extend(metadata_args)
        
        # Add web server share directory argument (with expanded path)
        home_dir = str(Path.home())
        metadata = self.config_manager.load_configurations_metadata()
        env_defaults = metadata.get("environmentDefaults", {})
        web_share_dir = env_defaults.get("webserverShareDir", {}).get("default", "~/LM_Shares/WebShare/")
        expanded_web_share_dir = web_share_dir.replace("~", home_dir)
        cmd.append(f"--web_server_share_dir={expanded_web_share_dir}")

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
            metadata = self.config_manager.load_configurations_metadata()
            system_defaults = metadata.get("systemDefaults", {})
            struct = system_defaults.get("configStructure", {})
            system_key = struct.get("systemKey", "system")
            
            system_config = config.get(system_key) or {}
            default_mode = system_defaults.get("mode", "single")
            is_single_pi = system_config.get("mode", default_mode) == "single"

            metadata = self.config_manager.load_configurations_metadata()
            env_defaults = metadata.get("environmentDefaults", {})
            env = os.environ.copy()
            home_dir = str(Path.home())
            
            env["LD_LIBRARY_PATH"] = env_defaults.get("ldLibraryPath", {}).get("default", "/usr/lib/pitrac")
            env["PITRAC_ROOT"] = env_defaults.get("pitracRoot", {}).get("default", "/usr/lib/pitrac")
            
            base_img_dir = env_defaults.get("baseImageLoggingDir", {}).get("default", "~/LM_Shares/Images/")
            env["PITRAC_BASE_IMAGE_LOGGING_DIR"] = base_img_dir.replace("~", home_dir)
            
            web_share_dir = env_defaults.get("webserverShareDir", {}).get("default", "~/LM_Shares/WebShare/")
            env["PITRAC_WEBSERVER_SHARE_DIR"] = web_share_dir.replace("~", home_dir)
            
            env["PITRAC_MSG_BROKER_FULL_ADDRESS"] = env_defaults.get("msgBrokerFullAddress", {}).get("default", "tcp://localhost:61616")
            
            camera_defs = metadata.get("cameraDefinitions", {})
            
            if is_single_pi:
                for cam_name, cam_def in camera_defs.items():
                    env_cam = self._set_environment_from_metadata(cam_name)
                    env_prefix = cam_def.get("envPrefix", f"PITRAC_SLOT{cam_name[-1]}")
                    
                    for key, value in env_cam.items():
                        if key.startswith(env_prefix):
                            env[key] = value
                    
                    logger.info(f"{cam_def.get('displayName', cam_name)} env: {[(k,v) for k,v in env.items() if k.startswith(env_prefix)]}")
            else:
                first_camera = list(camera_defs.keys())[0] if camera_defs else "camera1"
                env_cam1 = self._set_environment_from_metadata(first_camera)
                env.update(env_cam1)
                cam_def = camera_defs.get(first_camera, {})
                env_prefix = cam_def.get("envPrefix", "PITRAC_SLOT1")
                logger.info(f"{cam_def.get('displayName', first_camera)} env: {[(k,v) for k,v in env.items() if k.startswith(env_prefix)]}")

            Path(env["PITRAC_BASE_IMAGE_LOGGING_DIR"]).mkdir(parents=True, exist_ok=True)
            Path(env["PITRAC_WEBSERVER_SHARE_DIR"]).mkdir(parents=True, exist_ok=True)

            if is_single_pi:
                camera_defs = metadata.get("cameraDefinitions", {})
                camera_names = list(camera_defs.keys())
                second_camera = camera_names[1] if len(camera_names) > 1 else "camera2"
                
                logger.info(
                    f"Starting {second_camera} process FIRST for single-Pi dual camera mode..."
                )

                cmd2 = self._build_command(second_camera)

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

                    await asyncio.sleep(self.startup_delay_camera2)

                    if self.camera2_process.poll() is None:
                        logger.info(
                            f"PiTrac camera2 started successfully with PID {self.camera2_process.pid}"
                        )

                        logger.info(
                            "Waiting for camera2 to be ready before starting camera1..."
                        )
                        await asyncio.sleep(self.startup_wait_camera2_ready)
                    else:
                        logger.error("Camera2 process exited immediately")
                        if self.camera2_pid_file.exists():
                            self.camera2_pid_file.unlink()
                        self.camera2_process = None
                        return {
                            "status": "failed",
                            "message": "Camera2 failed to start - check logs",
                            "log_file": str(self.camera2_log_file),
                        }

            camera_defs = metadata.get("cameraDefinitions", {})
            first_camera = list(camera_defs.keys())[0] if camera_defs else "camera1"
            
            logger.info(f"Starting {first_camera} process...")
            cmd = self._build_command(first_camera)

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

                await asyncio.sleep(self.startup_delay_camera1)

                if self.process.poll() is None:
                    logger.info(
                        f"PiTrac camera1 started successfully with PID {self.process.pid}"
                    )

                    if is_single_pi:
                        if self.camera2_process and self.camera2_process.poll() is None:
                            return {
                                "status": "started",
                                "message": "PiTrac started successfully (both cameras)",
                                "camera1_pid": self.process.pid,
                                "camera2_pid": self.camera2_process.pid,
                            }
                        else:
                            logger.error("Camera2 process died during camera1 startup")

                            try:
                                os.kill(self.process.pid, signal.SIGTERM)
                            except ProcessLookupError:
                                pass
                            if self.pid_file.exists():
                                self.pid_file.unlink()
                            self.process = None

                            if self.camera2_pid_file.exists():
                                self.camera2_pid_file.unlink()
                            self.camera2_process = None

                            return {
                                "status": "failed",
                                "message": "Camera2 died during startup - check logs",
                                "log_file": str(self.camera2_log_file),
                            }
                    else:
                        return {
                            "status": "started",
                            "message": "PiTrac started successfully",
                            "pid": self.process.pid,
                        }
                else:
                    logger.error("PiTrac camera1 process exited immediately")

                    if self.pid_file.exists():
                        self.pid_file.unlink()
                    self.process = None

                    if is_single_pi and self.camera2_process:
                        try:
                            os.kill(self.camera2_process.pid, signal.SIGTERM)
                        except ProcessLookupError:
                            pass
                        if self.camera2_pid_file.exists():
                            self.camera2_pid_file.unlink()
                        self.camera2_process = None

                    return {
                        "status": "failed",
                        "message": "PiTrac camera1 failed to start - check logs",
                        "log_file": str(self.log_file),
                    }

        except Exception as e:
            logger.error(f"Failed to start PiTrac: {e}")
            return {"status": "error", "message": f"Failed to start PiTrac: {str(e)}"}

    async def stop(self) -> Dict[str, Any]:
        """Stop the PiTrac process(es) gracefully - stop camera1 first, then camera2"""
        if not self.is_running():
            return {"status": "not_running", "message": "PiTrac is not running"}

        try:
            stopped_cameras = []

            pid = self.get_pid()
            if pid:
                os.kill(pid, self.termination_signal)
                logger.info(f"Sent {self.termination_signal} to PiTrac camera1 process {pid}")

                max_wait = self.shutdown_grace_period
                for _ in range(int(max_wait / self.shutdown_check_interval)):
                    await asyncio.sleep(self.shutdown_check_interval)
                    try:
                        os.kill(pid, 0)
                    except ProcessLookupError:
                        break

                try:
                    os.kill(pid, 0)
                    logger.warning("PiTrac camera1 didn't stop gracefully, forcing...")
                    os.kill(pid, self.kill_signal)
                    await asyncio.sleep(self.post_kill_delay)
                except ProcessLookupError:
                    pass

                if self.pid_file.exists():
                    self.pid_file.unlink()

                self.process = None
                stopped_cameras.append("camera1")

            camera2_pid = self.get_camera2_pid()
            if camera2_pid:
                os.kill(camera2_pid, self.termination_signal)
                logger.info(f"Sent {self.termination_signal} to PiTrac camera2 process {camera2_pid}")

                max_wait = self.shutdown_grace_period
                for _ in range(int(max_wait / self.shutdown_check_interval)):
                    await asyncio.sleep(self.shutdown_check_interval)
                    try:
                        os.kill(camera2_pid, 0)
                    except ProcessLookupError:
                        break

                try:
                    os.kill(camera2_pid, 0)
                    logger.warning("PiTrac camera2 didn't stop gracefully, forcing...")
                    os.kill(camera2_pid, self.kill_signal)
                    await asyncio.sleep(self.post_kill_delay)
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
        if self.process:
            poll_result = self.process.poll()
            if poll_result is None:
                return self.process.pid
            else:
                logger.debug(f"Camera1 process terminated with code {poll_result}")
                self.process = None
                if self.pid_file.exists():
                    self.pid_file.unlink()

        if self.pid_file.exists():
            try:
                with open(self.pid_file, "r") as f:
                    pid = int(f.read().strip())
                    os.kill(pid, 0)
                    with open(f"/proc/{pid}/cmdline", "r") as cmdline:
                        if self.process_check_command in cmdline.read():
                            return pid
            except (ValueError, IOError, ProcessLookupError, FileNotFoundError):
                if self.pid_file.exists():
                    self.pid_file.unlink()

        return None

    def get_camera2_pid(self) -> Optional[int]:
        """Get the PID of the running PiTrac camera2 process"""
        if self.camera2_process:
            poll_result = self.camera2_process.poll()
            if poll_result is None:
                return self.camera2_process.pid
            else:
                logger.debug(f"Camera2 process terminated with code {poll_result}")
                self.camera2_process = None
                if self.camera2_pid_file.exists():
                    self.camera2_pid_file.unlink()

        if self.camera2_pid_file.exists():
            try:
                with open(self.camera2_pid_file, "r") as f:
                    pid = int(f.read().strip())
                    os.kill(pid, 0)
                    with open(f"/proc/{pid}/cmdline", "r") as cmdline:
                        if self.process_check_command in cmdline.read():
                            return pid
            except (ValueError, IOError, ProcessLookupError, FileNotFoundError):
                if self.camera2_pid_file.exists():
                    self.camera2_pid_file.unlink()

        return None

    def get_status(self) -> Dict[str, Any]:
        """Get detailed status of PiTrac process(es)"""
        camera1_pid = self.get_pid()
        camera2_pid = self.get_camera2_pid()

        config = self._load_pitrac_config()
        metadata = self.config_manager.load_configurations_metadata()
        system_defaults = metadata.get("systemDefaults", {})
        struct = system_defaults.get("configStructure", {})
        system_key = struct.get("systemKey", "system")
        
        system_config = config.get(system_key) or {}
        default_mode = system_defaults.get("mode", "single")
        is_single_pi = system_config.get("mode", default_mode) == "single"

        status = {
            "is_running": camera1_pid is not None or camera2_pid is not None,
            "pid": camera1_pid,  # For backward compatibility
            "camera1_pid": camera1_pid,
            "camera2_pid": camera2_pid,
            "camera1_running": camera1_pid is not None,
            "camera2_running": camera2_pid is not None,
            "is_dual_camera": is_single_pi,  # Single Pi with dual cameras
            "camera1_log_file": str(self.log_file),
            "camera2_log_file": str(self.camera2_log_file),
            "config_file": self.config_file,
            "binary": self.pitrac_binary,
            "mode": system_config.get("mode", "single"),
        }

        if self.log_file.exists():
            try:
                with open(self.log_file, "r") as f:
                    lines = f.readlines()
                    status["camera1_recent_logs"] = (
                        lines[-self.recent_log_lines:] if len(lines) > self.recent_log_lines else lines
                    )
            except Exception as e:
                status["camera1_log_error"] = str(e)

        if self.camera2_log_file.exists():
            try:
                with open(self.camera2_log_file, "r") as f:
                    lines = f.readlines()
                    status["camera2_recent_logs"] = (
                        lines[-self.recent_log_lines:] if len(lines) > self.recent_log_lines else lines
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

            await asyncio.sleep(self.restart_delay)

        return await self.start()

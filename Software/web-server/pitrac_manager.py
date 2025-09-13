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

    def _get_system_mode(self) -> str:
        """Get the system mode (single or dual Pi)"""
        config = self.config_manager.get_config()
        return config.get("system", {}).get("mode", "single")
    
    def _get_camera_role(self) -> str:
        """Get the camera role for dual Pi mode"""
        config = self.config_manager.get_config()
        return config.get("system", {}).get("camera_role", "camera1")

    def _build_cli_args_from_metadata(self, camera: str = "camera1") -> list:
        """Build CLI arguments using metadata from configurations.json
        
        This method uses the passedVia and passedTo metadata to automatically
        build CLI arguments instead of manual hardcoding.
        """
        args = []
        merged_config = self.config_manager.get_config()
        
        target = camera  # "camera1" or "camera2"
        
        cli_params = self.config_manager.get_cli_parameters(target)
        
        skip_args = {"--system_mode", "--run_single_pi", "--web_server_share_dir"}
        
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
                # Use --key=value format for consistency
                args.append(f"{cli_arg}={value}")
        
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
            
            if value is not None and value != "":
                env[env_var] = str(value)
        
        return env

    def _build_command(self, camera: str = "camera1", config_file_path: Optional[Path] = None) -> list:
        """Build the command to run pitrac_lm with proper arguments
        
        Args:
            camera: Which camera to build command for ("camera1" or "camera2")
            config_file_path: Path to the generated config file
        """
        cmd = [self.pitrac_binary]
        
        # Add system mode arguments
        system_mode = self._get_system_mode()
        if system_mode == "single":
            cmd.append(f"--system_mode={camera}")
            cmd.append("--run_single_pi")
        else:
            camera_role = self._get_camera_role()
            cmd.append(f"--system_mode={camera_role}")
        

        # Add the generated config file
        if config_file_path and Path(config_file_path).exists():
            cmd.append(f"--config_file={config_file_path}")
        else:
            logger.error("No config file path provided!")
        
        # Add CLI arguments from metadata
        cli_args = self._build_cli_args_from_metadata(camera)
        cmd.extend(cli_args)

        # Add web server share directory (from config)
        config = self.config_manager.get_config()
        web_share_dir = config.get("gs_config", {}).get("ipc_interface", {}).get("kWebServerShareDirectory", "~/LM_Shares/Images/")
        expanded_dir = web_share_dir.replace("~", str(Path.home()))
        cmd.append(f"--web_server_share_dir={expanded_dir}")

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
            
            # Generate the golf_sim_config.json from configurations metadata
            try:
                generated_config_path = self.config_manager.generate_golf_sim_config()
                logger.info(f"Generated config file at: {generated_config_path}")
            except RuntimeError as e:
                logger.error(f"Failed to generate config: {e}")
                return {
                    "status": "error",
                    "message": f"Failed to generate configuration: {e}",
                    "error": str(e)
                }

            # Get system mode to determine single vs dual Pi
            system_mode = self._get_system_mode()
            is_single_pi = system_mode == "single"

            # Set up environment variables
            env = os.environ.copy()
            home_dir = str(Path.home())
            
            # Set standard environment variables
            env["LD_LIBRARY_PATH"] = "/usr/lib/pitrac"
            env["PITRAC_ROOT"] = "/usr/lib/pitrac"
            env["PITRAC_BASE_IMAGE_LOGGING_DIR"] = "~/LM_Shares/Images/".replace("~", home_dir)
            env["PITRAC_WEBSERVER_SHARE_DIR"] = "~/LM_Shares/WebShare/".replace("~", home_dir)
            env["PITRAC_MSG_BROKER_FULL_ADDRESS"] = "tcp://localhost:61616"
            
            # Set camera-specific environment variables
            if is_single_pi:
                # Single Pi mode: set env vars for both cameras
                env_cam1 = self._set_environment_from_metadata("camera1")
                env_cam2 = self._set_environment_from_metadata("camera2")
                env.update(env_cam1)
                env.update(env_cam2)
                logger.info(f"Set environment for both cameras")
            else:
                # Dual Pi mode: only set env for camera1
                env_cam1 = self._set_environment_from_metadata("camera1")
                env.update(env_cam1)
                logger.info(f"Set environment for camera1 only")

            Path(env["PITRAC_BASE_IMAGE_LOGGING_DIR"]).mkdir(parents=True, exist_ok=True)
            Path(env["PITRAC_WEBSERVER_SHARE_DIR"]).mkdir(parents=True, exist_ok=True)

            if is_single_pi:
                # In single Pi mode, start camera2 first
                second_camera = "camera2"
                
                logger.info(
                    f"Starting {second_camera} process FIRST for single-Pi dual camera mode..."
                )

                cmd2 = self._build_command(second_camera, config_file_path=generated_config_path)

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
                        try:
                            pid = self.camera2_process.pid
                            logger.info(
                                f"PiTrac camera2 started successfully with PID {pid}"
                            )
                        except (AttributeError, OSError) as e:
                            logger.warning(f"Race condition getting camera2 PID: {e}")

                        logger.info(
                            "Waiting for camera2 to be ready before starting camera1..."
                        )
                        await asyncio.sleep(self.startup_wait_camera2_ready)
                    else:
                        logger.error("Camera2 process exited immediately")
                        if self.camera2_process:
                            try:
                                self.camera2_process.wait(timeout=1.0)
                            except subprocess.TimeoutExpired:
                                self.camera2_process.kill()
                                try:
                                    self.camera2_process.wait(timeout=5.0)
                                except subprocess.TimeoutExpired:
                                    logger.warning("Camera2 process did not terminate after kill signal")
                        if self.camera2_pid_file.exists():
                            self.camera2_pid_file.unlink()
                        self.camera2_process = None
                        return {
                            "status": "failed",
                            "message": "Camera2 failed to start - check logs",
                            "log_file": str(self.camera2_log_file),
                        }

            # Start camera1 (primary camera)
            first_camera = "camera1"
            
            logger.info(f"Starting {first_camera} process...")
            cmd = self._build_command(first_camera, config_file_path=generated_config_path)

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
                    try:
                        pid = self.process.pid
                        logger.info(
                            f"PiTrac camera1 started successfully with PID {pid}"
                        )
                    except (AttributeError, OSError) as e:
                        logger.warning(f"Race condition getting camera1 PID: {e}")

                    if is_single_pi:
                        if self.camera2_process and self.camera2_process.poll() is None:
                            try:
                                cam1_pid = self.process.pid
                                cam2_pid = self.camera2_process.pid
                                return {
                                    "status": "started",
                                    "message": "PiTrac started successfully (both cameras)",
                                    "camera1_pid": cam1_pid,
                                    "camera2_pid": cam2_pid,
                                }
                            except (AttributeError, OSError) as e:
                                logger.warning(f"Race condition getting PIDs: {e}")
                        else:
                            logger.error("Camera2 process died during camera1 startup")
                            if self.process:
                                try:
                                    os.kill(self.process.pid, signal.SIGTERM)
                                    self.process.wait(timeout=2.0)
                                except (ProcessLookupError, subprocess.TimeoutExpired):
                                    if self.process.poll() is None:
                                        self.process.kill()
                                        try:
                                            self.process.wait(timeout=5.0)
                                        except subprocess.TimeoutExpired:
                                            logger.warning("Camera1 process did not terminate after kill signal")
                                except Exception:
                                    pass
                            if self.pid_file.exists():
                                self.pid_file.unlink()
                            self.process = None

                            if self.camera2_process and self.camera2_process.poll() is not None:
                                try:
                                    self.camera2_process.wait(timeout=2.0)
                                except subprocess.TimeoutExpired:
                                    pass
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
                    if self.process:
                        try:
                            self.process.wait(timeout=1.0)
                        except subprocess.TimeoutExpired:
                            self.process.kill()
                            try:
                                self.process.wait(timeout=5.0)
                            except subprocess.TimeoutExpired:
                                logger.warning("Camera1 process did not terminate after kill signal")

                    if self.pid_file.exists():
                        self.pid_file.unlink()
                    self.process = None

                    if is_single_pi and self.camera2_process:
                        try:
                            os.kill(self.camera2_process.pid, signal.SIGTERM)
                            self.camera2_process.wait(timeout=2.0)
                        except (ProcessLookupError, subprocess.TimeoutExpired):
                            if self.camera2_process.poll() is None:
                                self.camera2_process.kill()
                                try:
                                    self.camera2_process.wait(timeout=5.0)
                                except subprocess.TimeoutExpired:
                                    logger.warning("Camera2 process did not terminate after kill signal")
                        except Exception:
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
                try:
                    os.killpg(os.getpgid(pid), self.termination_signal)
                    logger.info(f"Sent {self.termination_signal} to PiTrac camera1 process group {pid}")
                except (ProcessLookupError, PermissionError):
                    try:
                        os.kill(pid, self.termination_signal)
                        logger.info(f"Sent {self.termination_signal} to PiTrac camera1 process {pid}")
                    except ProcessLookupError:
                        pass

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
                    try:
                        os.killpg(os.getpgid(pid), self.kill_signal)
                    except (ProcessLookupError, PermissionError):
                        os.kill(pid, self.kill_signal)
                    await asyncio.sleep(self.post_kill_delay)
                except ProcessLookupError:
                    pass

                if self.process:
                    try:
                        self.process.wait(timeout=1.0)
                    except (subprocess.TimeoutExpired, AttributeError):
                        pass

                if self.pid_file.exists():
                    self.pid_file.unlink()

                self.process = None
                stopped_cameras.append("camera1")

            camera2_pid = self.get_camera2_pid()
            if camera2_pid:
                try:
                    os.killpg(os.getpgid(camera2_pid), self.termination_signal)
                    logger.info(f"Sent {self.termination_signal} to PiTrac camera2 process group {camera2_pid}")
                except (ProcessLookupError, PermissionError):
                    try:
                        os.kill(camera2_pid, self.termination_signal)
                        logger.info(f"Sent {self.termination_signal} to PiTrac camera2 process {camera2_pid}")
                    except ProcessLookupError:
                        pass

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
                    try:
                        os.killpg(os.getpgid(camera2_pid), self.kill_signal)
                    except (ProcessLookupError, PermissionError):
                        os.kill(camera2_pid, self.kill_signal)
                    await asyncio.sleep(self.post_kill_delay)
                except ProcessLookupError:
                    pass

                if self.camera2_process:
                    try:
                        self.camera2_process.wait(timeout=1.0)
                    except (subprocess.TimeoutExpired, AttributeError):
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
            try:
                poll_result = self.process.poll()
                if poll_result is None:
                    pid = self.process.pid
                    return pid
                else:
                    logger.debug(f"Camera1 process terminated with code {poll_result}")
                    self.process = None
                    if self.pid_file.exists():
                        self.pid_file.unlink()
            except (AttributeError, OSError) as e:
                logger.warning(f"Race condition in get_pid: {e}")
                self.process = None

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
            try:
                poll_result = self.camera2_process.poll()
                if poll_result is None:
                    pid = self.camera2_process.pid
                    return pid
                else:
                    logger.debug(f"Camera2 process terminated with code {poll_result}")
                    self.camera2_process = None
                    if self.camera2_pid_file.exists():
                        self.camera2_pid_file.unlink()
            except (AttributeError, OSError) as e:
                logger.warning(f"Race condition in get_camera2_pid: {e}")
                self.camera2_process = None

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

        # Get system mode
        system_mode = self._get_system_mode()
        is_single_pi = system_mode == "single"

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
            "mode": system_mode,
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

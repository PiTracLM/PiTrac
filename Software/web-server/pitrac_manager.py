"""
PiTrac Process Manager - Manages the single pitrac_lm process lifecycle
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

    def __init__(self, config_manager: Optional[ConfigurationManager] = None):
        self.process: Optional[subprocess.Popen] = None
        self.config_manager = config_manager or ConfigurationManager()

        metadata = self.config_manager.load_configurations_metadata()
        sys_paths = metadata.get("systemPaths", {})
        proc_mgmt = metadata.get("processManagement", {})

        def expand_path(path_str: str) -> Path:
            return Path(path_str.replace("~", str(Path.home())))

        self.pitrac_binary = sys_paths.get("pitracBinary", {}).get("default", "/usr/lib/pitrac/pitrac_lm")

        log_dir = expand_path(sys_paths.get("logDirectory", {}).get("default", "~/.pitrac/logs"))
        pid_dir = expand_path(sys_paths.get("pidDirectory", {}).get("default", "~/.pitrac/run"))

        self.log_file = log_dir / proc_mgmt.get("camera1LogFile", {}).get("default", "pitrac.log")
        self.pid_file = pid_dir / proc_mgmt.get("camera1PidFile", {}).get("default", "pitrac.pid")

        self.process_check_command = proc_mgmt.get("processCheckCommand", {}).get("default", "pitrac_lm")
        self.startup_delay = proc_mgmt.get("startupDelayCamera1", {}).get("default", 3)
        self.shutdown_grace_period = proc_mgmt.get("shutdownGracePeriod", {}).get("default", 5)
        self.shutdown_check_interval = proc_mgmt.get("shutdownCheckInterval", {}).get("default", 0.1)
        self.post_kill_delay = proc_mgmt.get("postKillDelay", {}).get("default", 0.5)
        self.restart_delay = proc_mgmt.get("restartDelay", {}).get("default", 1)
        self.recent_log_lines = proc_mgmt.get("recentLogLines", {}).get("default", 10)

        self.termination_signal = getattr(signal, proc_mgmt.get("terminationSignal", {}).get("default", "SIGTERM"))
        self.kill_signal = getattr(signal, proc_mgmt.get("killSignal", {}).get("default", "SIGKILL"))

        self.log_file.parent.mkdir(parents=True, exist_ok=True)
        self.pid_file.parent.mkdir(parents=True, exist_ok=True)

    def _build_cli_args_from_metadata(self) -> list:
        args = []
        merged_config = self.config_manager.get_config()
        skip_args = {"--system_mode", "--web_server_share_dir"}

        for param in self.config_manager.get_cli_parameters():
            cli_arg = param["cliArgument"]
            param_type = param["type"]
            if cli_arg in skip_args:
                continue
            key = param["key"]
            value = merged_config
            for part in key.split("."):
                if isinstance(value, dict):
                    value = value.get(part)
                else:
                    value = None
                    break
            if value is None:
                continue
            if param_type != "boolean" and value == "":
                continue
            if param_type == "boolean":
                if value:
                    args.append(cli_arg)
            else:
                if param_type == "path" and value:
                    value = str(value).replace("~", str(Path.home()))
                args.append(f"{cli_arg}={value}")
        return args

    def _build_command(self, config_file_path: Optional[Path] = None) -> list:
        cmd = [self.pitrac_binary]
        cmd.append("--system_mode=camera1")

        if config_file_path and Path(config_file_path).exists():
            cmd.append(f"--config_file={config_file_path}")
        else:
            logger.error("No config file path provided!")

        cmd.extend(self._build_cli_args_from_metadata())

        config = self.config_manager.get_config()
        web_share_dir = (
            config.get("gs_config", {}).get("ipc_interface", {}).get("kWebServerShareDirectory", "~/LM_Shares/Images/")
        )
        expanded_dir = web_share_dir.replace("~", str(Path.home()))
        cmd.append(f"--web_server_share_dir={expanded_dir}")

        logger.info(f"Built command: {' '.join(cmd)}")
        return cmd

    async def start(self) -> Dict[str, Any]:
        if self.is_running():
            return {
                "status": "already_running",
                "message": "PiTrac is already running",
                "pid": self.get_pid(),
            }

        try:
            Path(self.log_file).parent.mkdir(parents=True, exist_ok=True)
            Path(self.pid_file).parent.mkdir(parents=True, exist_ok=True)

            try:
                generated_config_path = self.config_manager.generate_golf_sim_config()
                logger.info(f"Generated config file at: {generated_config_path}")
            except RuntimeError as e:
                logger.error(f"Failed to generate config: {e}")
                return {"status": "error", "message": f"Failed to generate configuration: {e}", "error": str(e)}

            env = os.environ.copy()
            home_dir = str(Path.home())
            env["LD_LIBRARY_PATH"] = "/usr/lib/pitrac"
            env["PITRAC_ROOT"] = "/usr/lib/pitrac"
            env["PITRAC_BASE_IMAGE_LOGGING_DIR"] = "~/LM_Shares/Images/".replace("~", home_dir)
            env["PITRAC_WEBSERVER_SHARE_DIR"] = "~/LM_Shares/WebShare/".replace("~", home_dir)

            merged_config = self.config_manager.get_config()
            for param in self.config_manager.get_environment_parameters():
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

            Path(env["PITRAC_BASE_IMAGE_LOGGING_DIR"]).mkdir(parents=True, exist_ok=True)
            Path(env["PITRAC_WEBSERVER_SHARE_DIR"]).mkdir(parents=True, exist_ok=True)

            cmd = self._build_command(config_file_path=generated_config_path)

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

                await asyncio.sleep(self.startup_delay)

                if self.process.poll() is None:
                    pid = self.process.pid
                    logger.info(f"PiTrac started with PID {pid}")
                    return {"status": "started", "message": "PiTrac started successfully", "pid": pid}
                else:
                    logger.error("PiTrac process exited immediately")
                    self._cleanup_process()
                    return {"status": "failed", "message": "PiTrac failed to start - check logs", "log_file": str(self.log_file)}

        except Exception as e:
            logger.error(f"Failed to start PiTrac: {e}")
            return {"status": "error", "message": f"Failed to start PiTrac: {str(e)}"}

    async def stop(self) -> Dict[str, Any]:
        if not self.is_running():
            return {"status": "not_running", "message": "PiTrac is not running"}

        try:
            pid = self.get_pid()
            if not pid:
                return {"status": "error", "message": "Could not find PiTrac process ID"}

            try:
                os.killpg(os.getpgid(pid), self.termination_signal)
                logger.info(f"Sent {self.termination_signal} to PiTrac process group {pid}")
            except (ProcessLookupError, PermissionError):
                try:
                    os.kill(pid, self.termination_signal)
                except ProcessLookupError:
                    pass

            for _ in range(int(self.shutdown_grace_period / self.shutdown_check_interval)):
                await asyncio.sleep(self.shutdown_check_interval)
                try:
                    os.kill(pid, 0)
                except ProcessLookupError:
                    break

            try:
                os.kill(pid, 0)
                logger.warning("PiTrac didn't stop gracefully, forcing...")
                try:
                    os.killpg(os.getpgid(pid), self.kill_signal)
                except (ProcessLookupError, PermissionError):
                    os.kill(pid, self.kill_signal)
                await asyncio.sleep(self.post_kill_delay)
            except ProcessLookupError:
                pass

            self._cleanup_process()
            logger.info("PiTrac stopped successfully")
            return {"status": "stopped", "message": "PiTrac stopped successfully"}

        except Exception as e:
            logger.error(f"Failed to stop PiTrac: {e}")
            return {"status": "error", "message": f"Failed to stop PiTrac: {str(e)}"}

    def _cleanup_process(self):
        if self.process:
            try:
                self.process.wait(timeout=1.0)
            except (subprocess.TimeoutExpired, AttributeError):
                pass
        if self.pid_file.exists():
            self.pid_file.unlink(missing_ok=True)
        self.process = None

    def is_running(self) -> bool:
        pid = self.get_pid()
        if pid:
            try:
                os.kill(pid, 0)
                return True
            except ProcessLookupError:
                self.pid_file.unlink(missing_ok=True)
        return False

    def get_pid(self) -> Optional[int]:
        if self.process:
            try:
                if self.process.poll() is None:
                    return self.process.pid
                else:
                    self.process = None
                    self.pid_file.unlink(missing_ok=True)
            except (AttributeError, OSError):
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
                self.pid_file.unlink(missing_ok=True)
        return None

    def get_status(self) -> Dict[str, Any]:
        pid = self.get_pid()
        status = {
            "is_running": pid is not None,
            "pid": pid,
            "log_file": str(self.log_file),
            "generated_config_path": str(Path.home() / ".pitrac/config/generated_golf_sim_config.json"),
            "binary": self.pitrac_binary,
        }

        if self.log_file.exists():
            try:
                with open(self.log_file, "r") as f:
                    lines = f.readlines()
                    status["recent_logs"] = lines[-self.recent_log_lines:] if len(lines) > self.recent_log_lines else lines
            except Exception as e:
                status["log_error"] = str(e)

        return status

    async def restart(self) -> Dict[str, Any]:
        logger.info("Restarting PiTrac...")
        if self.is_running():
            stop_result = await self.stop()
            if stop_result["status"] == "error":
                return stop_result
            await asyncio.sleep(self.restart_delay)
        return await self.start()

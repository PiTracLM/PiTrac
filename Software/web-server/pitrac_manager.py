"""
PiTrac Process Manager - Manages the single pitrac_lm process lifecycle
"""

import asyncio
import logging
import os
import signal
import subprocess
from datetime import datetime
from pathlib import Path
from typing import Optional, Dict, Any
from config_manager import ConfigurationManager
from log_files import run_log_path, latest_run_log, prune_run_logs, tail_lines
from constants import SERVER_PORT

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

        self.log_dir = log_dir
        self.log_file = latest_run_log(log_dir) or (log_dir / "pitrac.log")
        self.pid_file = pid_dir / proc_mgmt.get("camera1PidFile", {}).get("default", "pitrac.pid")

        storage = metadata.get("storage", {})
        self.log_dir_cap_bytes = storage.get("logDirectoryCapMB", {}).get("default", 30) * 1024 * 1024
        self.run_log_cap_bytes = storage.get("runLogCapMB", {}).get("default", 50) * 1024 * 1024

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

    def _build_command(self) -> list:
        cmd = [self.pitrac_binary]
        cmd.append("--system_mode=camera1")
        cmd.append(f"--web_server_port={SERVER_PORT}")

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

            env = os.environ.copy()
            home_dir = str(Path.home())
            env["LD_LIBRARY_PATH"] = "/usr/lib/pitrac"
            env["PITRAC_ROOT"] = "/usr/lib/pitrac"
            env["OMP_WAIT_POLICY"] = "PASSIVE"
            env["PITRAC_BASE_IMAGE_LOGGING_DIR"] = "~/LM_Shares/Images/".replace("~", home_dir)
            env["PITRAC_WEBSERVER_SHARE_DIR"] = "~/LM_Shares/WebShare/".replace("~", home_dir)

            Path(env["PITRAC_BASE_IMAGE_LOGGING_DIR"]).mkdir(parents=True, exist_ok=True)
            Path(env["PITRAC_WEBSERVER_SHARE_DIR"]).mkdir(parents=True, exist_ok=True)

            cmd = self._build_command()

            self.log_file = run_log_path(self.log_dir, datetime.now())
            prune_run_logs(self.log_dir, self.log_dir_cap_bytes)

            with open(self.log_file, "a") as log:
                process = subprocess.Popen(
                    cmd,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    env=env,
                    cwd=str(Path.home()),
                    preexec_fn=os.setsid,
                )
                self.process = process

                with open(self.pid_file, "w") as f:
                    f.write(str(process.pid))

                await asyncio.sleep(self.startup_delay)

                if process.poll() is None:
                    logger.info(f"PiTrac started with PID {process.pid}")
                    return {"status": "started", "message": "PiTrac started successfully", "pid": process.pid}
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
            except (AttributeError, OSError):
                pass

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
            "binary": self.pitrac_binary,
        }

        if self.log_file.exists():
            try:
                status["recent_logs"] = tail_lines(self.log_file, self.recent_log_lines)[0]
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

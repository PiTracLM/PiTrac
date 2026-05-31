"""PiTrac Update Manager - git-based update checking and build.sh dev execution"""

import asyncio
import json
import logging
import os
import signal
import subprocess
from datetime import datetime
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

logger = logging.getLogger(__name__)

ENVIRONMENT_FILE = "/etc/pitrac/environment"
STATUS_FILE = "/etc/pitrac/update-status.json"
MAX_LOG_LINES = 500
BUILD_TIMEOUT_SECONDS = 600  # 10 minutes


class UpdateManager:

    def __init__(self):
        self._env: Dict[str, str] = {}
        self._update_process: Optional[asyncio.subprocess.Process] = None
        self._update_task: Optional[asyncio.Task] = None
        self._update_lock = asyncio.Lock()
        self._update_log: List[str] = []
        self._update_status: str = "idle"  # idle, checking, updating, restarting, failed
        self._update_error: Optional[str] = None
        self._last_check: Optional[str] = None
        self._last_update: Optional[str] = None
        self._available_commits: List[Dict[str, str]] = []
        self._broadcast_callback: Optional[Callable] = None

        self._load_environment()
        self._load_persisted_status()

    def set_broadcast_callback(self, callback: Callable) -> None:
        self._broadcast_callback = callback

    def _load_environment(self) -> None:
        env_path = Path(ENVIRONMENT_FILE)
        if not env_path.exists():
            logger.warning(f"{ENVIRONMENT_FILE} not found — update features disabled. "
                           "Run 'sudo ./build.sh dev' to enable.")
            return

        try:
            for line in env_path.read_text().splitlines():
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" in line:
                    key, value = line.split("=", 1)
                    self._env[key.strip()] = value.strip()
            logger.info(f"Loaded build environment: repo={self.repo_root}, user={self.build_user}")
        except Exception as e:
            logger.error(f"Failed to read {ENVIRONMENT_FILE}: {e}")

    def _load_persisted_status(self) -> None:
        status_path = Path(STATUS_FILE)
        if not status_path.exists():
            return
        try:
            data = json.loads(status_path.read_text())
            prev_status = data.get("status")
            if prev_status == "updating":
                # If we're alive after a mid-build kill, the build succeeded
                self._last_update = data.get("timestamp")
                logger.info("Previous update completed successfully (service was restarted by build.sh)")
            elif prev_status == "failed":
                self._update_error = data.get("message")
                self._last_update = data.get("timestamp")
            status_path.unlink(missing_ok=True)
        except Exception as e:
            logger.warning(f"Could not read persisted update status: {e}")

    def _persist_status(self, status: str, message: str = "") -> None:
        try:
            payload = {
                "status": status,
                "message": message,
                "timestamp": datetime.now().isoformat(),
            }
            Path(STATUS_FILE).write_text(json.dumps(payload))
        except Exception as e:
            logger.warning(f"Could not persist update status: {e}")

    @property
    def is_configured(self) -> bool:
        return bool(self.repo_root and self.build_script)

    @property
    def repo_root(self) -> Optional[str]:
        return self._env.get("PITRAC_REPO_ROOT")

    @property
    def build_script(self) -> Optional[str]:
        return self._env.get("PITRAC_BUILD_SCRIPT")

    @property
    def build_user(self) -> Optional[str]:
        return self._env.get("PITRAC_BUILD_USER")

    @property
    def last_build_time(self) -> Optional[str]:
        return self._env.get("PITRAC_LAST_BUILD")

    @property
    def is_busy(self) -> bool:
        return self._update_status in ("checking", "updating", "restarting")

    async def _run_git(self, *args: str, timeout: int = 30) -> subprocess.CompletedProcess:
        cmd = ["git", "-C", self.repo_root, *args]
        return await asyncio.to_thread(
            subprocess.run, cmd, capture_output=True, text=True, timeout=timeout
        )

    async def get_branches(self, remote: str = "origin") -> Dict[str, Any]:
        if not self.is_configured:
            return {"status": "error", "message": "Update system not configured. Run 'sudo ./build.sh dev' first."}

        if self._update_lock.locked():
            return {"status": "error", "message": "An update or check is already in progress."}

        async with self._update_lock:
            self._update_status = "checking"
            try:
                fetch = await self._run_git("fetch", "--prune", remote, timeout=60)
                if fetch.returncode != 0:
                    self._update_status = "idle"
                    return {"status": "error", "message": f"git fetch failed: {fetch.stderr.strip()}"}

                refs = await self._run_git("for-each-ref",
                                           f"--format=%(refname:short)%00%(committerdate:relative)",
                                           f"refs/remotes/{remote}/")
                if refs.returncode != 0:
                    self._update_status = "idle"
                    return {"status": "error", "message": "Failed to list branches"}

                branch_result = await self._run_git("rev-parse", "--abbrev-ref", "HEAD")
                current_branch = branch_result.stdout.strip() if branch_result.returncode == 0 else "unknown"

                prefix = f"{remote}/"
                branches = []
                for line in refs.stdout.strip().splitlines():
                    parts = line.split("\x00", 1)
                    if len(parts) != 2:
                        continue
                    ref, age = parts
                    if ref in (f"{remote}/HEAD", remote):
                        continue
                    name = ref[len(prefix):] if ref.startswith(prefix) else ref
                    branches.append({"name": name, "last_commit": age})

                self._update_status = "idle"
                return {
                    "status": "ok",
                    "current_branch": current_branch,
                    "branches": branches,
                }

            except subprocess.TimeoutExpired:
                self._update_status = "idle"
                return {"status": "error", "message": "git fetch timed out — check network connectivity."}
            except Exception as e:
                self._update_status = "idle"
                logger.error(f"Branch list failed: {e}")
                return {"status": "error", "message": str(e)}

    async def check_for_updates(self, remote: str = "origin") -> Dict[str, Any]:
        if not self.is_configured:
            return {"status": "error", "message": "Update system not configured. Run 'sudo ./build.sh dev' first."}

        if self._update_lock.locked():
            return {"status": "error", "message": "An update or check is already in progress."}

        async with self._update_lock:
            self._update_status = "checking"
            try:
                # Check for local modifications that would block git pull --ff-only
                dirty = await self._run_git("status", "--porcelain")
                has_local_changes = bool(dirty.stdout.strip()) if dirty.returncode == 0 else False

                branch_result = await self._run_git("rev-parse", "--abbrev-ref", "HEAD")
                current_branch = branch_result.stdout.strip() if branch_result.returncode == 0 else "unknown"
                is_detached = current_branch == "HEAD"

                fetch = await self._run_git("fetch", remote, timeout=60)
                if fetch.returncode != 0:
                    self._update_status = "idle"
                    return {"status": "error", "message": f"git fetch failed: {fetch.stderr.strip()}"}

                tracking = f"{remote}/{current_branch}"
                count_result = await self._run_git("rev-list", "--count", f"HEAD..{tracking}")
                if count_result.returncode != 0:
                    self._update_status = "idle"
                    return {"status": "error", "message": f"Failed to compare branches: {count_result.stderr.strip()}"}

                commit_count = int(count_result.stdout.strip())

                commits = []
                if commit_count > 0:
                    log_result = await self._run_git(
                        "log", "--oneline", f"--format=%H%x00%s%x00%an%x00%ar",
                        f"HEAD..{tracking}"
                    )
                    if log_result.returncode == 0:
                        for line in log_result.stdout.strip().splitlines():
                            parts = line.split("\x00", 3)
                            if len(parts) == 4:
                                commits.append({
                                    "hash": parts[0][:8],
                                    "message": parts[1],
                                    "author": parts[2],
                                    "time": parts[3],
                                })

                head_result = await self._run_git("rev-parse", "--short", "HEAD")
                current_hash = head_result.stdout.strip() if head_result.returncode == 0 else "unknown"

                self._available_commits = commits
                self._last_check = datetime.now().isoformat()
                self._update_status = "idle"

                result = {
                    "status": "ok",
                    "updates_available": commit_count > 0,
                    "commit_count": commit_count,
                    "commits": commits,
                    "current_hash": current_hash,
                    "current_branch": current_branch,
                    "last_check": self._last_check,
                    "last_build": self.last_build_time,
                    "has_local_changes": has_local_changes,
                    "is_detached": is_detached,
                }

                if has_local_changes and commit_count > 0:
                    result["warning"] = ("Local uncommitted changes detected. "
                                         "Update may fail — commit or stash changes first.")
                if is_detached:
                    result["warning"] = ("Repo is in detached HEAD state. "
                                         "Checkout a branch to enable updates.")

                return result

            except subprocess.TimeoutExpired:
                self._update_status = "idle"
                return {"status": "error", "message": "git fetch timed out — check network connectivity."}
            except Exception as e:
                self._update_status = "idle"
                logger.error(f"Update check failed: {e}")
                return {"status": "error", "message": str(e)}

    async def start_update(self, force: bool = False, branch: Optional[str] = None) -> Dict[str, Any]:
        if not self.is_configured:
            return {"status": "error", "message": "Update system not configured. Run 'sudo ./build.sh dev' first."}

        if self._update_lock.locked():
            return {"status": "error", "message": "An update is already in progress."}

        if not Path(self.repo_root).is_dir():
            return {"status": "error", "message": f"Repo not found at {self.repo_root}"}
        if not Path(self.build_script).is_file():
            return {"status": "error", "message": f"Build script not found at {self.build_script}"}

        self._update_log = []
        self._update_error = None

        self._update_task = asyncio.create_task(self._run_update(force, branch))

        return {
            "status": "started",
            "message": "Update started. The server will restart when the build completes.",
        }

    async def _run_update(self, force: bool, branch: Optional[str] = None) -> None:
        async with self._update_lock:
            self._update_status = "updating"
            try:
                # Switch branch if requested
                if branch:
                    current = await self._run_git("rev-parse", "--abbrev-ref", "HEAD")
                    current_branch = current.stdout.strip() if current.returncode == 0 else None
                    if current_branch != branch:
                        await self._log_line(f"[UPDATE] Switching to branch: {branch}")
                        checkout = await self._run_git("checkout", branch, timeout=30)
                        if checkout.returncode != 0:
                            error = f"git checkout failed: {checkout.stderr.strip()}"
                            await self._log_line(f"[ERROR] {error}")
                            self._update_status = "failed"
                            self._update_error = error
                            self._persist_status("failed", error)
                            return

                await self._log_line("[UPDATE] Pulling latest changes...")
                pull = await self._run_git("pull", "--ff-only", timeout=120)
                if pull.returncode != 0:
                    error = f"git pull failed: {pull.stderr.strip()}"
                    await self._log_line(f"[ERROR] {error}")
                    self._update_status = "failed"
                    self._update_error = error
                    self._persist_status("failed", error)
                    return

                for line in pull.stdout.strip().splitlines():
                    await self._log_line(f"[GIT] {line}")

                build_args = ["sudo", self.build_script, "dev"]
                if force:
                    build_args.append("force")

                await self._log_line(f"[UPDATE] Running: {' '.join(build_args)}")
                await self._log_line("[UPDATE] This will rebuild PiTrac and restart the web server...")

                # Persisted so the next process (after build.sh restarts the service)
                # can infer the update succeeded
                self._persist_status("updating", "build in progress")

                self._update_process = await asyncio.create_subprocess_exec(
                    *build_args,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.STDOUT,
                    cwd=self.repo_root,
                    start_new_session=True,
                )

                try:
                    await asyncio.wait_for(
                        self._stream_build_output(),
                        timeout=BUILD_TIMEOUT_SECONDS,
                    )
                except asyncio.TimeoutError:
                    await self._log_line(f"[ERROR] Build timed out after {BUILD_TIMEOUT_SECONDS}s")
                    await self._kill_build_process()
                    self._update_status = "failed"
                    self._update_error = "Build timed out"
                    self._persist_status("failed", "Build timed out")
                    return

                returncode = await self._update_process.wait()
                self._update_process = None

                if returncode == 0:
                    self._last_update = datetime.now().isoformat()
                    # Unlikely to reach here — build.sh restarts this service
                    await self._log_line("[UPDATE] Build complete. Service restarting...")
                    self._update_status = "restarting"
                else:
                    error = f"Build failed with exit code {returncode}"
                    await self._log_line(f"[ERROR] {error}")
                    self._update_status = "failed"
                    self._update_error = error
                    self._persist_status("failed", error)

            except asyncio.CancelledError:
                await self._log_line("[UPDATE] Update cancelled.")
                await self._kill_build_process()
                self._update_status = "idle"
                self._persist_status("failed", "Cancelled by user")
            except Exception as e:
                error = f"Update failed: {e}"
                logger.error(error)
                await self._log_line(f"[ERROR] {error}")
                self._update_status = "failed"
                self._update_error = str(e)
                self._persist_status("failed", str(e))

    async def _stream_build_output(self) -> None:
        if self._update_process and self._update_process.stdout:
            async for raw_line in self._update_process.stdout:
                line = raw_line.decode("utf-8", errors="replace").rstrip()
                await self._log_line(f"[BUILD] {line}")

    async def _kill_build_process(self) -> None:
        if not self._update_process:
            return
        try:
            pgid = os.getpgid(self._update_process.pid)
            os.killpg(pgid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            try:
                self._update_process.terminate()
            except ProcessLookupError:
                pass
        try:
            await asyncio.wait_for(self._update_process.wait(), timeout=10)
        except asyncio.TimeoutError:
            try:
                pgid = os.getpgid(self._update_process.pid)
                os.killpg(pgid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                try:
                    self._update_process.kill()
                except ProcessLookupError:
                    pass
        self._update_process = None

    async def cancel_update(self) -> Dict[str, Any]:
        if not self.is_busy:
            return {"status": "error", "message": "No update in progress."}

        if self._update_task and not self._update_task.done():
            self._update_task.cancel()
            try:
                await self._update_task
            except asyncio.CancelledError:
                pass
            self._update_task = None

        self._update_status = "idle"
        self._update_error = None
        await self._log_line("[UPDATE] Update cancelled by user.")
        return {"status": "cancelled", "message": "Update cancelled."}

    def get_update_task(self) -> Optional[asyncio.Task]:
        return self._update_task

    def get_status(self) -> Dict[str, Any]:
        return {
            "configured": self.is_configured,
            "repo_root": self.repo_root,
            "build_script": self.build_script,
            "build_user": self.build_user,
            "status": self._update_status,
            "error": self._update_error,
            "last_check": self._last_check,
            "last_build": self.last_build_time,
            "last_update": self._last_update,
            "available_commits": self._available_commits,
            "log_tail": self._update_log[-50:],
        }

    async def _log_line(self, line: str) -> None:
        timestamped = f"[{datetime.now().strftime('%H:%M:%S')}] {line}"
        self._update_log.append(timestamped)
        if len(self._update_log) > MAX_LOG_LINES:
            self._update_log = self._update_log[-MAX_LOG_LINES:]
        logger.info(line)

        if self._broadcast_callback:
            try:
                await self._broadcast_callback({
                    "type": "update_log",
                    "line": timestamped,
                    "status": self._update_status,
                })
            except Exception as e:
                logger.debug(f"Failed to broadcast update log: {e}")

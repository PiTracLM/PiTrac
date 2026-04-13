"""Tests for UpdateManager environment loading, status persistence, and accessors."""

import asyncio
import json
import pytest
from pathlib import Path
from unittest.mock import patch

import update_manager
from update_manager import UpdateManager


@pytest.fixture
def isolated_paths(tmp_path):
    """Patch the module-level paths so tests don't touch /etc/pitrac."""
    env_file = tmp_path / "environment"
    status_file = tmp_path / "update-status.json"
    with patch.object(update_manager, "ENVIRONMENT_FILE", str(env_file)), \
         patch.object(update_manager, "STATUS_FILE", str(status_file)):
        yield env_file, status_file


class TestEnvironmentLoading:
    def test_missing_env_file_yields_unconfigured(self, isolated_paths):
        env_file, _ = isolated_paths
        assert not env_file.exists()
        m = UpdateManager()
        assert m.is_configured is False
        assert m.repo_root is None
        assert m.build_script is None
        assert m.build_user is None
        assert m.last_build_time is None

    def test_env_file_parses_key_value_pairs(self, isolated_paths):
        env_file, _ = isolated_paths
        env_file.write_text(
            "# comment\n"
            "PITRAC_REPO_ROOT=/srv/pitrac\n"
            "PITRAC_BUILD_SCRIPT=/srv/pitrac/build.sh\n"
            "PITRAC_BUILD_USER=pitrac\n"
            "PITRAC_LAST_BUILD=2026-04-12T10:00:00\n"
            "\n"
        )
        m = UpdateManager()
        assert m.is_configured is True
        assert m.repo_root == "/srv/pitrac"
        assert m.build_script == "/srv/pitrac/build.sh"
        assert m.build_user == "pitrac"
        assert m.last_build_time == "2026-04-12T10:00:00"

    def test_env_file_skips_comments_and_blank_lines(self, isolated_paths):
        env_file, _ = isolated_paths
        env_file.write_text("# only comments\n\n   \n# more\n")
        m = UpdateManager()
        assert m.is_configured is False


class TestPersistedStatus:
    def test_no_persisted_status_is_safe(self, isolated_paths):
        m = UpdateManager()
        assert m._update_error is None
        assert m._last_update is None

    def test_previous_updating_marked_complete(self, isolated_paths):
        _, status_file = isolated_paths
        status_file.write_text(json.dumps({
            "status": "updating",
            "message": "in progress",
            "timestamp": "2026-04-13T08:00:00",
        }))
        m = UpdateManager()
        assert m._last_update == "2026-04-13T08:00:00"
        # File is consumed once read
        assert not status_file.exists()

    def test_previous_failed_carries_error(self, isolated_paths):
        _, status_file = isolated_paths
        status_file.write_text(json.dumps({
            "status": "failed",
            "message": "git pull conflict",
            "timestamp": "2026-04-13T07:30:00",
        }))
        m = UpdateManager()
        assert m._update_error == "git pull conflict"
        assert m._last_update == "2026-04-13T07:30:00"
        assert not status_file.exists()

    def test_persist_writes_status_file(self, isolated_paths):
        _, status_file = isolated_paths
        m = UpdateManager()
        m._persist_status("updating", "starting")
        saved = json.loads(status_file.read_text())
        assert saved["status"] == "updating"
        assert saved["message"] == "starting"
        assert "timestamp" in saved


class TestStatusAndAccessors:
    def test_get_status_initial(self, isolated_paths):
        m = UpdateManager()
        s = m.get_status()
        for key in ("configured", "repo_root", "build_script", "build_user",
                    "status", "error", "last_check", "last_build", "last_update",
                    "available_commits", "log_tail"):
            assert key in s
        assert s["status"] == "idle"
        assert s["available_commits"] == []
        assert s["log_tail"] == []

    def test_is_busy_reflects_active_status(self, isolated_paths):
        m = UpdateManager()
        assert m.is_busy is False
        for active in ("checking", "updating", "restarting"):
            m._update_status = active
            assert m.is_busy is True
        m._update_status = "idle"
        assert m.is_busy is False
        m._update_status = "failed"
        assert m.is_busy is False

    def test_get_update_task_initial_none(self, isolated_paths):
        assert UpdateManager().get_update_task() is None

    def test_set_broadcast_callback(self, isolated_paths):
        m = UpdateManager()
        cb = lambda *a, **kw: None
        m.set_broadcast_callback(cb)
        assert m._broadcast_callback is cb


class TestLogLine:
    def test_log_line_appends_with_timestamp(self, isolated_paths):
        m = UpdateManager()

        async def go():
            await m._log_line("hello")

        asyncio.run(go())
        assert len(m._update_log) == 1
        assert "hello" in m._update_log[0]
        assert m._update_log[0].startswith("[")  # timestamp prefix

    def test_log_line_caps_at_max(self, isolated_paths):
        m = UpdateManager()

        async def go():
            for i in range(update_manager.MAX_LOG_LINES + 50):
                await m._log_line(f"line {i}")

        asyncio.run(go())
        assert len(m._update_log) == update_manager.MAX_LOG_LINES

    def test_log_line_invokes_broadcast(self, isolated_paths):
        m = UpdateManager()
        seen = []

        async def cb(payload):
            seen.append(payload)

        m.set_broadcast_callback(cb)

        async def go():
            await m._log_line("broadcast me")

        asyncio.run(go())
        assert len(seen) == 1
        assert seen[0]["type"] == "update_log"
        assert "broadcast me" in seen[0]["line"]

    def test_log_line_swallows_broadcast_errors(self, isolated_paths):
        m = UpdateManager()

        async def bad_cb(_):
            raise RuntimeError("boom")

        m.set_broadcast_callback(bad_cb)

        async def go():
            await m._log_line("still works")

        # Must not raise
        asyncio.run(go())
        assert len(m._update_log) == 1


class TestGuardClauses:
    """Public methods refuse work when not configured or lock is held."""

    def test_get_branches_without_config(self, isolated_paths):
        m = UpdateManager()
        assert not m.is_configured
        result = asyncio.run(m.get_branches())
        assert result["status"] == "error"
        assert "not configured" in result["message"]

    def test_check_for_updates_without_config(self, isolated_paths):
        m = UpdateManager()
        result = asyncio.run(m.check_for_updates())
        assert result["status"] == "error"
        assert "not configured" in result["message"]

    def test_start_update_without_config(self, isolated_paths):
        m = UpdateManager()
        result = asyncio.run(m.start_update())
        assert result["status"] == "error"
        assert "not configured" in result["message"]

    def test_cancel_update_when_idle(self, isolated_paths):
        m = UpdateManager()
        result = asyncio.run(m.cancel_update())
        assert result["status"] == "error" or result["status"] == "idle" or "no update" in result.get("message", "").lower()

    def test_get_branches_lock_busy(self, isolated_paths, tmp_path):
        env_file, _ = isolated_paths
        env_file.write_text(
            "PITRAC_REPO_ROOT=/tmp/repo\n"
            "PITRAC_BUILD_SCRIPT=/tmp/repo/build.sh\n"
        )
        m = UpdateManager()

        async def go():
            async with m._update_lock:
                return await m.get_branches()

        result = asyncio.run(go())
        assert result["status"] == "error"
        assert "in progress" in result["message"]


class TestGitDrivenFlows:
    """Mock _run_git to drive get_branches / check_for_updates code paths."""

    @pytest.fixture
    def configured(self, isolated_paths):
        env_file, _ = isolated_paths
        env_file.write_text(
            "PITRAC_REPO_ROOT=/tmp/repo\n"
            "PITRAC_BUILD_SCRIPT=/tmp/repo/build.sh\n"
        )
        return UpdateManager()

    def _completed(self, returncode=0, stdout="", stderr=""):
        import subprocess
        return subprocess.CompletedProcess(args=[], returncode=returncode,
                                           stdout=stdout, stderr=stderr)

    def test_get_branches_fetch_fails(self, configured):
        from unittest.mock import AsyncMock
        configured._run_git = AsyncMock(return_value=self._completed(1, stderr="net down"))
        result = asyncio.run(configured.get_branches())
        assert result["status"] == "error"
        assert "git fetch failed" in result["message"]
        assert configured._update_status == "idle"

    def test_get_branches_parses_refs(self, configured):
        from unittest.mock import AsyncMock
        side = [
            self._completed(0),                                         # fetch
            self._completed(0, stdout="origin/main\x002 days ago\n"
                                       "origin/dev\x004 hours ago\n"
                                       "origin/HEAD\x00\n"),            # for-each-ref
            self._completed(0, stdout="main\n"),                        # rev-parse HEAD
        ]
        configured._run_git = AsyncMock(side_effect=side)
        result = asyncio.run(configured.get_branches())
        assert result["status"] == "ok"
        assert result["current_branch"] == "main"
        names = [b["name"] for b in result["branches"]]
        assert "main" in names and "dev" in names
        assert "HEAD" not in names

    def test_check_for_updates_fetch_fails(self, configured):
        from unittest.mock import AsyncMock
        side = [
            self._completed(0, stdout=""),         # status --porcelain (clean)
            self._completed(0, stdout="main\n"),   # rev-parse HEAD
            self._completed(1, stderr="no net"),   # fetch
        ]
        configured._run_git = AsyncMock(side_effect=side)
        result = asyncio.run(configured.check_for_updates())
        assert result["status"] == "error"
        assert "git fetch failed" in result["message"]


class TestStatusFile:
    def test_persisted_status_unreadable_is_swallowed(self, isolated_paths):
        _, status_file = isolated_paths
        status_file.write_text("{not valid json")
        # Should not raise
        m = UpdateManager()
        assert m._update_error is None

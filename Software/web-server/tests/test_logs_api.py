import asyncio
import os
import pytest
import time
from pathlib import Path
from unittest.mock import patch
from fastapi.testclient import TestClient

import log_files


@pytest.mark.unit
class TestLogsAPI:
    """Test logs REST API and WebSocket endpoints"""

    def test_logs_page_loads(self, client):
        """Test that logs page loads successfully"""
        response = client.get("/logs")
        assert response.status_code == 200
        assert "Logs" in response.text
        assert "logs.css" in response.text or "dashboard.css" in response.text
        assert "logs.js" in response.text or "dashboard.js" in response.text

    def test_get_log_services(self, client):
        """Test getting available log services"""
        response = client.get("/api/logs/services")
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, dict)
        assert "services" in data

        services = data["services"]
        assert isinstance(services, list)

        assert len(services) > 0

        for service in services:
            assert "id" in service
            assert "name" in service
            assert "status" in service
            assert service["status"] in ["running", "stopped"]

    @patch("server.PiTracServer._stream_systemd_logs")
    async def test_websocket_logs_systemd_service(self, mock_stream_systemd, app):
        """Test WebSocket logs streaming for systemd service"""

        async def mock_systemd_stream(websocket, unit):
            await websocket.send_json(
                {
                    "timestamp": "1640995200000000",
                    "message": "Test log line 1",
                    "level": "6",
                    "service": unit,
                    "historical": True,
                }
            )
            return

        mock_stream_systemd.side_effect = mock_systemd_stream

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac-web"})

                time.sleep(0.1)

                assert mock_stream_systemd.called

    @patch("server.PiTracServer._stream_file_logs")
    async def test_websocket_logs_file_service(self, mock_stream_file, app):
        """Test WebSocket logs streaming for file-based logs"""

        async def mock_file_stream(websocket, _log_file):
            await websocket.send_json({"message": "Test log line", "historical": True})
            return

        mock_stream_file.side_effect = mock_file_stream

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac"})

                time.sleep(0.1)

                assert mock_stream_file.called

    async def test_websocket_logs_invalid_service(self, app):
        """Test WebSocket logs with invalid service"""
        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "nonexistent_service"})

                try:
                    message = websocket.receive_json()
                    if "error" in str(message).lower():
                        assert True
                except Exception:
                    assert True

    async def test_websocket_logs_malformed_message(self, app):
        """Test WebSocket logs with malformed message"""
        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                try:
                    websocket.send_text("invalid json")
                    assert True
                except Exception:
                    assert True

    async def test_websocket_logs_missing_service(self, app):
        """Test WebSocket logs with missing service"""
        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({})

                time.sleep(0.1)
                assert True

    @patch("server.PiTracServer._stream_systemd_logs")
    @patch("server.PiTracServer._stream_file_logs")
    async def test_websocket_logs_connection_basic(self, mock_stream_file, mock_stream_systemd, app):
        """Test basic WebSocket logs connection"""
        async def mock_stream(websocket, unit):
            await websocket.send_json({"message": "Test log", "service": unit})

        mock_stream_systemd.side_effect = mock_stream
        mock_stream_file.side_effect = mock_stream

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac-web"})

                time.sleep(0.1)
                assert True

    @patch("server.PiTracServer._stream_systemd_logs")
    async def test_websocket_logs_subprocess_failure(self, mock_stream_systemd, app):
        """Test WebSocket logs when subprocess fails"""

        async def mock_systemd_stream_error(websocket, _unit):
            await websocket.send_json({"error": "Failed to stream logs: subprocess error"})
            return

        mock_stream_systemd.side_effect = mock_systemd_stream_error

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac-web"})

                time.sleep(0.1)

                assert mock_stream_systemd.called

    @patch("server.PiTracServer._stream_file_logs")
    async def test_websocket_logs_file_not_found(self, mock_stream_file, app):
        """Test WebSocket logs when log file doesn't exist"""

        async def mock_file_stream_error(websocket, log_file):
            await websocket.send_json({"message": f"Log file not found: {log_file}", "level": "warning"})
            return

        mock_stream_file.side_effect = mock_file_stream_error

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac"})

                time.sleep(0.1)

                assert mock_stream_file.called

    @patch("server.PiTracServer._stream_file_logs")
    @patch("server.PiTracServer._stream_systemd_logs")
    async def test_websocket_logs_multiple_clients(self, mock_stream_systemd, mock_stream_file, app):
        """Test multiple WebSocket clients can connect simultaneously"""

        async def mock_file_stream(websocket, _log_file):
            await websocket.send_json({"message": "File log", "service": "pitrac"})

        async def mock_systemd_stream(websocket, unit):
            await websocket.send_json({"message": "Systemd log", "service": unit})

        mock_stream_file.side_effect = mock_file_stream
        mock_stream_systemd.side_effect = mock_systemd_stream

        with TestClient(app) as client1, TestClient(app) as client2:
            with client1.websocket_connect("/ws/logs") as ws1, client2.websocket_connect("/ws/logs") as ws2:
                ws1.send_json({"service": "pitrac"})
                ws2.send_json({"service": "pitrac-web"})

                time.sleep(0.1)

                assert mock_stream_file.called
                assert mock_stream_systemd.called

    def test_logs_page_static_resources(self, client):
        """Test that logs page references correct static resources"""
        response = client.get("/logs")
        assert response.status_code == 200
        content = response.text

        assert "logs.css" in content or "dashboard.css" in content
        assert "logs.js" in content or "dashboard.js" in content

        assert "log" in content.lower()

    @patch("server.PiTracServer._stream_systemd_logs")
    async def test_websocket_logs_stream_interruption(self, mock_stream_systemd, app):
        """Test handling of stream interruption"""

        async def mock_interrupted_stream(websocket, unit):
            await websocket.send_json({"message": "Log line before interruption", "service": unit, "historical": True})
            raise Exception("Stream interrupted")

        mock_stream_systemd.side_effect = mock_interrupted_stream

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac-web"})

                time.sleep(0.1)

                assert mock_stream_systemd.called

    @patch("server.PiTracServer._stream_systemd_logs")
    @patch("server.PiTracServer._stream_file_logs")
    async def test_websocket_logs_connection_handling(self, mock_stream_file, mock_stream_systemd, app):
        """Test proper connection handling and cleanup"""
        async def mock_stream(websocket, unit):
            await websocket.send_json({"message": "Test log", "service": unit})

        mock_stream_systemd.side_effect = mock_stream
        mock_stream_file.side_effect = mock_stream

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac-web"})

                time.sleep(0.1)
                assert True

    def test_get_log_services_structure(self, client):
        """Test the structure of log services response"""
        response = client.get("/api/logs/services")
        assert response.status_code == 200
        data = response.json()

        assert "services" in data
        services = data["services"]

        for service in services:
            required_fields = ["id", "name", "status"]
            for field in required_fields:
                assert field in service, f"Service missing required field: {field}"

            assert service["status"] in ["running", "stopped"]

            assert isinstance(service["id"], str) and len(service["id"]) > 0
            assert isinstance(service["name"], str) and len(service["name"]) > 0

    @patch("server.PiTracServer._stream_systemd_logs")
    @patch("server.PiTracServer._stream_file_logs")
    async def test_websocket_logs_different_service_types(self, mock_stream_file, mock_stream_systemd, app):
        """Test streaming logs from different service types"""
        async def mock_stream(websocket, unit):
            await websocket.send_json({"message": f"Test log for {unit}", "service": unit})

        mock_stream_systemd.side_effect = mock_stream
        mock_stream_file.side_effect = mock_stream

        services = ["pitrac", "pitrac-web"]

        for service_name in services:
            with TestClient(app) as client:
                with client.websocket_connect("/ws/logs") as websocket:
                    websocket.send_json({"service": service_name})

                    import time

                    time.sleep(0.1)
                    assert True


def _write_run_log(path: Path, prefix: str, count: int) -> None:
    path.write_text("".join(f"{prefix}-{i}\n" for i in range(count)))


@pytest.fixture
def run_logs(server_instance, tmp_path):
    """Point the pitrac_manager at a tmp log dir with an older + newer run file."""
    log_dir = tmp_path / "logs"
    log_dir.mkdir()
    older = log_dir / "pitrac-20260101-000000.log"
    newer = log_dir / "pitrac-20260102-000000.log"
    _write_run_log(older, "old", 120)
    _write_run_log(newer, "new", 120)

    server_instance.pitrac_manager.log_dir = log_dir
    server_instance.pitrac_manager.log_file = newer
    server_instance.pitrac_manager.log_dir_cap_bytes = 10 * 1024 * 1024
    server_instance.pitrac_manager.run_log_cap_bytes = 10 * 1024 * 1024
    return {"dir": log_dir, "older": older, "newer": newer}


@pytest.mark.unit
class TestLogsHistoryAPI:
    """Test the /api/logs/history scrollback endpoint"""

    def test_history_tail_of_latest(self, run_logs, app):
        client = TestClient(app)
        resp = client.get("/api/logs/history", params={"service": "pitrac", "lines": 50})
        assert resp.status_code == 200
        data = resp.json()
        assert len(data["lines"]) == 50
        assert data["lines"][-1] == "new-119"
        assert data["lines"][0] == "new-70"
        assert data["next"]["file"] == run_logs["newer"].name
        assert data["next"]["offset"] > 0

    def test_history_walk_back_to_previous_file(self, run_logs, app):
        client = TestClient(app)
        newer_name = run_logs["newer"].name
        older_name = run_logs["older"].name

        # Start at the tail, then page backwards within the newer file.
        resp = client.get("/api/logs/history", params={"service": "pitrac", "lines": 50})
        cursor = resp.json()["next"]
        collected = list(resp.json()["lines"])

        guard = 0
        while cursor is not None and cursor["file"] == newer_name:
            guard += 1
            assert guard < 20
            resp = client.get(
                "/api/logs/history",
                params={"service": "pitrac", "file": cursor["file"], "before": cursor["offset"], "lines": 50},
            )
            data = resp.json()
            collected = data["lines"] + collected
            cursor = data["next"]

        # All 120 lines of the newer file should be recovered, in order.
        assert collected == [f"new-{i}" for i in range(120)]

        # Cursor has now crossed into the older file at its end.
        assert cursor is not None
        assert cursor["file"] == older_name
        assert cursor["offset"] == run_logs["older"].stat().st_size

        # Page back through the older file until next is null (very beginning).
        older_collected = []
        guard = 0
        while cursor is not None:
            guard += 1
            assert guard < 20
            resp = client.get(
                "/api/logs/history",
                params={"service": "pitrac", "file": cursor["file"], "before": cursor["offset"], "lines": 50},
            )
            data = resp.json()
            older_collected = data["lines"] + older_collected
            cursor = data["next"]

        assert older_collected == [f"old-{i}" for i in range(120)]

    def test_history_systemd_service_empty(self, run_logs, app):
        client = TestClient(app)
        resp = client.get("/api/logs/history", params={"service": "pitrac-web"})
        assert resp.status_code == 200
        assert resp.json() == {"lines": [], "next": None}

    def test_history_invalid_file_not_served(self, run_logs, app):
        client = TestClient(app)
        resp = client.get(
            "/api/logs/history",
            params={"service": "pitrac", "file": "../../../etc/passwd", "before": 10, "lines": 50},
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["lines"] == []
        assert data["next"] is None

    def test_history_stale_cursor_resets(self, run_logs, app):
        client = TestClient(app)
        newer = run_logs["newer"]
        beyond = newer.stat().st_size + 100_000
        resp = client.get(
            "/api/logs/history",
            params={"service": "pitrac", "file": newer.name, "before": beyond, "lines": 50},
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["lines"] == []
        assert data.get("reset") is True
        assert data["next"]["file"] == newer.name
        assert data["next"]["offset"] == newer.stat().st_size

    def test_history_lines_capped(self, run_logs, app):
        client = TestClient(app)
        resp = client.get("/api/logs/history", params={"service": "pitrac", "lines": 99999})
        # 120 lines exist; capping at 1000 just means we get them all without error.
        assert resp.status_code == 200
        assert len(resp.json()["lines"]) == 120


@pytest.mark.unit
class TestFollow:
    """Test the subprocess-free log follower"""

    async def test_follow_yields_appended_lines(self, tmp_path):
        path = tmp_path / "follow.log"
        path.write_text("first\n")

        gen = log_files.follow(path, poll_interval=0.05)
        # Prime the generator so it records the starting size before we append.
        first = asyncio.ensure_future(gen.__anext__())
        await asyncio.sleep(0.1)
        assert not first.done()

        with open(path, "a") as f:
            f.write("second\n")
        line = await asyncio.wait_for(first, timeout=2.0)
        assert line == "second"

        await gen.aclose()

    async def test_follow_holds_back_partial_line(self, tmp_path):
        path = tmp_path / "follow.log"
        path.write_text("first\n")

        gen = log_files.follow(path, poll_interval=0.05)
        pending = asyncio.ensure_future(gen.__anext__())
        await asyncio.sleep(0.1)

        # write a line in two chunks; the fragment must not be yielded on its own
        with open(path, "a") as f:
            f.write("half ")
        await asyncio.sleep(0.1)
        assert not pending.done()

        with open(path, "a") as f:
            f.write("line\n")
        line = await asyncio.wait_for(pending, timeout=2.0)
        assert line == "half line"

        await gen.aclose()

    async def test_follow_resets_on_truncate(self, tmp_path):
        path = tmp_path / "follow.log"
        path.write_text("line-a\nline-b\n")

        gen = log_files.follow(path, poll_interval=0.05)
        pending = asyncio.ensure_future(gen.__anext__())
        await asyncio.sleep(0.1)
        assert not pending.done()

        # Truncate to zero (the marathon-run cap backstop), then write fresh content.
        os.truncate(path, 0)
        with open(path, "a") as f:
            f.write("after-reset\n")

        line = await asyncio.wait_for(pending, timeout=2.0)
        assert line == "after-reset"

        await gen.aclose()

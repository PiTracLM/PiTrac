"""HTTP-level tests for the /api/pico/* endpoints. The PicoManager itself
has full unit coverage in test_pico_manager.py; here we just verify the
routes wire the right calls through and surface the right status codes."""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock

import pytest


class TestPicoPageRoute:
    def test_pico_page_renders(self, client):
        response = client.get("/pico")
        assert response.status_code == 200
        assert "text/html" in response.headers["content-type"]


class TestStatusEndpoint:
    def test_returns_status_when_present(self, server_instance, client):
        server_instance.pico_manager.status = AsyncMock(
            return_value={"present": True, "armed": 0, "threshold": 4096}
        )
        response = client.get("/api/pico/status")
        assert response.status_code == 200
        assert response.json()["present"] is True
        assert response.json()["threshold"] == 4096

    def test_returns_503_when_absent(self, server_instance, client):
        server_instance.pico_manager.status = AsyncMock(
            return_value={"present": False, "error": "no device"}
        )
        response = client.get("/api/pico/status")
        assert response.status_code == 503
        assert response.json()["error"] == "no device"


class TestSelftestEndpoint:
    def test_runs_selftest(self, server_instance, client):
        server_instance.pico_manager.selftest = AsyncMock(
            return_value={"ok": True, "vsys_mv": 4900, "fw": "0.5.0"}
        )
        response = client.post("/api/pico/selftest")
        assert response.status_code == 200
        body = response.json()
        assert body["fw"] == "0.5.0"

    def test_selftest_failure_returns_503(self, server_instance, client):
        server_instance.pico_manager.selftest = AsyncMock(
            return_value={"ok": False, "error": "no reply"}
        )
        response = client.post("/api/pico/selftest")
        assert response.status_code == 503


class TestConfigEndpoint:
    def test_set_threshold_dispatches(self, server_instance, client):
        server_instance.pico_manager.set_threshold = AsyncMock(
            return_value={"ok": True, "threshold": 8000}
        )
        response = client.post("/api/pico/config", json={"threshold": 8000})
        assert response.status_code == 200
        server_instance.pico_manager.set_threshold.assert_awaited_once_with(8000)

    def test_set_armed_dispatches(self, server_instance, client):
        server_instance.pico_manager.set_armed = AsyncMock(
            return_value={"ok": True, "armed": 1}
        )
        response = client.post("/api/pico/config", json={"armed": True})
        assert response.status_code == 200
        server_instance.pico_manager.set_armed.assert_awaited_once_with(True)

    def test_set_min_inter_shot_dispatches(self, server_instance, client):
        server_instance.pico_manager.set_min_inter_shot = AsyncMock(
            return_value={"ok": True, "min_inter_shot_ms": 20}
        )
        response = client.post(
            "/api/pico/config", json={"min_inter_shot_ms": 5}
        )
        assert response.status_code == 200
        server_instance.pico_manager.set_min_inter_shot.assert_awaited_once_with(5)

    def test_threshold_garbage_returns_400(self, server_instance, client):
        async def _raise(_value):
            raise ValueError("threshold out of range")

        server_instance.pico_manager.set_threshold = AsyncMock(side_effect=_raise)
        response = client.post("/api/pico/config", json={"threshold": "abc"})
        assert response.status_code == 400


class TestRmsStreamEndpoint:
    def test_rms_stream_emits_sse_lines(self, server_instance, client):
        async def fake_iter():
            yield {"value": 10, "timestamp": 100}
            yield {"value": 20, "timestamp": 200}

        async def fake_start(hz):
            return fake_iter()

        server_instance.pico_manager.start_rms_stream = fake_start
        server_instance.pico_manager.stop_rms_stream = AsyncMock()

        with client.stream("GET", "/api/pico/rms-stream?hz=20") as resp:
            assert resp.status_code == 200
            assert "text/event-stream" in resp.headers["content-type"]
            chunks = []
            for line in resp.iter_lines():
                chunks.append(line)
                if len(chunks) >= 4:
                    break

        payload = "\n".join(chunks)
        assert "value" in payload
        assert "100" in payload or "200" in payload



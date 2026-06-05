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

    def test_set_putt_threshold_dispatches(self, server_instance, client):
        server_instance.pico_manager.set_putt_threshold = AsyncMock(
            return_value={"ok": True, "threshold_putt": 2_500_000}
        )
        response = client.post("/api/pico/config", json={"putt_threshold": 2_500_000})
        assert response.status_code == 200
        server_instance.pico_manager.set_putt_threshold.assert_awaited_once_with(2_500_000)

    def test_putt_threshold_garbage_returns_400(self, server_instance, client):
        async def _raise(_value):
            raise ValueError("putt threshold out of range")

        server_instance.pico_manager.set_putt_threshold = AsyncMock(side_effect=_raise)
        response = client.post("/api/pico/config", json={"putt_threshold": "abc"})
        assert response.status_code == 400

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

    def test_set_cam_xtr_setup_dispatches(self, server_instance, client):
        server_instance.pico_manager.set_cam_xtr_setup = AsyncMock(
            return_value={"ok": True}
        )
        response = client.post(
            "/api/pico/config", json={"cam_xtr_setup_us": 200}
        )
        assert response.status_code == 200
        server_instance.pico_manager.set_cam_xtr_setup.assert_awaited_once_with(200)

    def test_cam_xtr_setup_garbage_returns_400(self, server_instance, client):
        async def _raise(_value):
            raise ValueError("cam_xtr_setup_us out of range")

        server_instance.pico_manager.set_cam_xtr_setup = AsyncMock(side_effect=_raise)
        response = client.post("/api/pico/config", json={"cam_xtr_setup_us": "abc"})
        assert response.status_code == 400

    def test_threshold_garbage_returns_400(self, server_instance, client):
        async def _raise(_value):
            raise ValueError("threshold out of range")

        server_instance.pico_manager.set_threshold = AsyncMock(side_effect=_raise)
        response = client.post("/api/pico/config", json={"threshold": "abc"})
        assert response.status_code == 400

    def test_set_pulse_width_dispatches(self, server_instance, client):
        server_instance.pico_manager.set_pulse_width_us = AsyncMock(
            return_value={"ok": True, "pulse_us": 12}
        )
        response = client.post("/api/pico/config", json={"pulse_width_us": 12})
        assert response.status_code == 200
        server_instance.pico_manager.set_pulse_width_us.assert_awaited_once_with(12)

    def test_set_pulse_intervals_dispatches(self, server_instance, client):
        server_instance.pico_manager.set_pulse_intervals = AsyncMock(
            return_value={"ok": True, "intervals": [0.7, 1.8]}
        )
        response = client.post(
            "/api/pico/config", json={"pulse_intervals": [0.7, 1.8]}
        )
        assert response.status_code == 200
        server_instance.pico_manager.set_pulse_intervals.assert_awaited_once_with([0.7, 1.8])

    def test_pulse_intervals_bad_ratio_returns_400(self, server_instance, client):
        async def _raise(_value):
            raise ValueError("consecutive ratio repeats")

        server_instance.pico_manager.set_pulse_intervals = AsyncMock(side_effect=_raise)
        response = client.post(
            "/api/pico/config", json={"pulse_intervals": [1.0, 2.0, 4.0]}
        )
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


class TestFlashEndpoint:
    def test_flash_streams_progress_lines(self, server_instance, client):
        async def fake_flash(path, on_progress=None):
            for line in ("Loading...", "100%", "Reboot complete"):
                if on_progress:
                    on_progress(line)
            return {"ok": True, "uf2": path}

        server_instance.pico_manager.flash = fake_flash

        valid_uf2 = b"\x55\x46\x32\x0a" + b"\x00" * 508
        response = client.post(
            "/api/pico/flash",
            files={"uf2": ("fake.uf2", valid_uf2, "application/octet-stream")},
        )
        assert response.status_code == 200
        body = response.text
        assert "Loading..." in body
        assert "Reboot complete" in body
        assert "DONE" in body or "ok" in body.lower()

    def test_flash_error_emits_error_line(self, server_instance, client):
        async def fake_flash(path, on_progress=None):
            raise RuntimeError("picotool missing")

        server_instance.pico_manager.flash = fake_flash

        valid_uf2 = b"\x55\x46\x32\x0a" + b"\x00" * 508
        response = client.post(
            "/api/pico/flash",
            files={"uf2": ("x.uf2", valid_uf2, "application/octet-stream")},
        )
        assert response.status_code == 200
        assert "ERROR" in response.text

    def test_flash_rejects_non_uf2_extension(self, server_instance, client):
        called = False

        async def fake_flash(path, on_progress=None):
            nonlocal called
            called = True
            return {"ok": True, "uf2": path}

        server_instance.pico_manager.flash = fake_flash

        response = client.post(
            "/api/pico/flash",
            files={"uf2": ("malware.bin", b"\x55\x46\x32\x0a" + b"\x00" * 508,
                           "application/octet-stream")},
        )
        assert response.status_code == 400
        assert called is False

    def test_flash_rejects_bad_magic(self, server_instance, client):
        called = False

        async def fake_flash(path, on_progress=None):
            nonlocal called
            called = True
            return {"ok": True, "uf2": path}

        server_instance.pico_manager.flash = fake_flash

        response = client.post(
            "/api/pico/flash",
            files={"uf2": ("fw.uf2", b"this is not a uf2 payload at all",
                           "application/octet-stream")},
        )
        assert response.status_code == 400
        assert called is False


class TestLmStartReleasesRmsStream:
    """The LM drives the Pico over the same CDC port, so /api/pitrac/start and
    /restart must tear down the mic stream BEFORE launching the LM -- otherwise a
    trailing EVENT RMS line poisons the LM's STATUS handshake into legacy fallback."""

    def test_start_stops_rms_stream_before_launching_lm(self, server_instance, client):
        server_instance.strobe_calibration_manager.is_strobe_safe = MagicMock(
            return_value={"safe": True}
        )
        order = []
        server_instance.pico_manager.stop_rms_stream = AsyncMock(
            side_effect=lambda *a, **k: order.append("stop_rms")
        )
        server_instance.pitrac_manager.start = AsyncMock(
            side_effect=lambda *a, **k: (order.append("start_lm"), {"status": "started"})[1]
        )

        resp = client.post("/api/pitrac/start")
        assert resp.status_code == 200
        server_instance.pico_manager.stop_rms_stream.assert_awaited_once()
        server_instance.pitrac_manager.start.assert_awaited_once()
        assert order == ["stop_rms", "start_lm"]

    def test_restart_stops_rms_stream_before_relaunching_lm(self, server_instance, client):
        server_instance.strobe_calibration_manager.is_strobe_safe = MagicMock(
            return_value={"safe": True}
        )
        order = []
        server_instance.pico_manager.stop_rms_stream = AsyncMock(
            side_effect=lambda *a, **k: order.append("stop_rms")
        )
        server_instance.pitrac_manager.restart = AsyncMock(
            side_effect=lambda *a, **k: (order.append("restart_lm"), {"status": "restarted"})[1]
        )

        resp = client.post("/api/pitrac/restart")
        assert resp.status_code == 200
        server_instance.pico_manager.stop_rms_stream.assert_awaited_once()
        server_instance.pitrac_manager.restart.assert_awaited_once()
        assert order == ["stop_rms", "restart_lm"]

    def test_start_blocked_by_unsafe_strobe_leaves_stream_alone(self, server_instance, client):
        server_instance.strobe_calibration_manager.is_strobe_safe = MagicMock(
            return_value={"safe": False, "reason": "strobe live"}
        )
        server_instance.pico_manager.stop_rms_stream = AsyncMock()
        server_instance.pitrac_manager.start = AsyncMock()

        resp = client.post("/api/pitrac/start")
        assert resp.status_code == 200
        assert resp.json()["status"] == "error"
        server_instance.pico_manager.stop_rms_stream.assert_not_awaited()
        server_instance.pitrac_manager.start.assert_not_awaited()

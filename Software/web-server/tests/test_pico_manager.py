"""Tests for the PicoManager class — patches `serial.Serial` and feeds
canned reply lines through `read.side_effect`. Mirrors the structure of
test_pico_bridge_calibration.py."""

from __future__ import annotations

import asyncio
from unittest.mock import MagicMock, patch

import pytest


def _build(serial_mock, *, get_config=None):
    """Helper: monkeypatched pyserial + a fresh manager bound to a Lock."""
    from pico_manager import PicoManager

    cm = MagicMock()
    cm.get_config.side_effect = get_config or (lambda key: None)
    lock = asyncio.Lock()
    return PicoManager(cm, lock)


# --------------------------------------------------------------------- probe

class TestProbe:
    """`probe()` opens a fresh fd, sends STATUS, parses, closes."""

    @patch("pico_manager.serial")
    def test_probe_parses_status_reply(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [
            b"STATUS armed=0 threshold=4096 pulse_us=8.68 "
            b"min_inter_shot_ms=200 pre_trigger_delay_ms=0 "
            b"decay_confirm_ms=5 strobe_hold=0 vsys_mv=4900 vbus=1 "
            b"intervals=0.70,1.80\n",
            b"",
        ]

        mgr = _build(mock_serial_mod)
        data = asyncio.run(mgr.probe())

        mock_serial_mod.Serial.assert_called_once_with(
            "/dev/ttyACM0", 115200, timeout=1, exclusive=True
        )
        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"STATUS\n" in writes
        assert data["present"] is True
        assert data["armed"] == 0
        assert data["threshold"] == 4096
        assert data["vbus"] == 1
        assert data["intervals"] == [0.70, 1.80]
        assert data["device"] == "/dev/ttyACM0"
        ser.close.assert_called_once()

    @patch("pico_manager.serial")
    def test_probe_marks_absent_when_no_reply(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.return_value = b""

        mgr = _build(mock_serial_mod)
        data = asyncio.run(mgr.probe())

        assert data["present"] is False
        assert "error" in data

    @patch("pico_manager.serial")
    def test_probe_uses_config_manager_device_override(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0\n", b""]

        mgr = _build(
            mock_serial_mod,
            get_config=lambda key: "/dev/ttyACM2"
            if key == "gs_config.pico.device"
            else None,
        )
        asyncio.run(mgr.probe())

        mock_serial_mod.Serial.assert_called_once_with(
            "/dev/ttyACM2", 115200, timeout=1, exclusive=True
        )


# --------------------------------------------------------------------- status

class TestStatus:
    """`status()` keeps the fd open across calls; reuses for back-to-back polls."""

    @patch("pico_manager.serial")
    def test_status_reuses_handle(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [
            b"STATUS armed=1 threshold=8000\n", b"",
            b"STATUS armed=0 threshold=8000\n", b"",
        ]

        mgr = _build(mock_serial_mod)
        first = asyncio.run(mgr.status())
        second = asyncio.run(mgr.status())

        mock_serial_mod.Serial.assert_called_once()
        assert first["armed"] == 1
        assert second["armed"] == 0


# --------------------------------------------------------------------- selftest

class TestSelftest:
    @patch("pico_manager.serial")
    def test_selftest_parses_reply(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [
            b"SELFTEST vsys_mv=4900 vbus=1 mic_rms=120 armed=0 fw=0.5.0\n",
            b"",
        ]

        mgr = _build(mock_serial_mod)
        data = asyncio.run(mgr.selftest())

        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"SELFTEST\n" in writes
        assert data["ok"] is True
        assert data["vsys_mv"] == 4900
        assert data["mic_rms"] == 120
        assert data["fw"] == "0.5.0"

    @patch("pico_manager.serial")
    def test_selftest_times_out(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.return_value = b""

        mgr = _build(mock_serial_mod)
        import pico_manager
        old = pico_manager.SELFTEST_DEADLINE_S
        pico_manager.SELFTEST_DEADLINE_S = 0.05
        try:
            data = asyncio.run(mgr.selftest())
        finally:
            pico_manager.SELFTEST_DEADLINE_S = old

        assert data["ok"] is False


# --------------------------------------------------------------------- CFG

class TestSetters:
    @patch("pico_manager.serial")
    def test_set_threshold_writes_cfg_and_reads_status(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 threshold=4096\n", b""]

        mgr = _build(mock_serial_mod)
        data = asyncio.run(mgr.set_threshold(4096))

        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"CFG MIC_THRESHOLD=4096\n" in writes
        assert data["ok"] is True
        assert data["threshold"] == 4096

    @patch("pico_manager.serial")
    def test_set_threshold_rejects_garbage(self, mock_serial_mod):
        mgr = _build(mock_serial_mod)
        with pytest.raises(ValueError):
            asyncio.run(mgr.set_threshold(-1))
        with pytest.raises(ValueError):
            asyncio.run(mgr.set_threshold(2**40))

    @patch("pico_manager.serial")
    def test_set_armed_writes_correct_cfg(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=1\n", b"", b"STATUS armed=0\n", b""]

        mgr = _build(mock_serial_mod)
        asyncio.run(mgr.set_armed(True))
        asyncio.run(mgr.set_armed(False))

        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"CFG ARMED=1\n" in writes
        assert b"CFG ARMED=0\n" in writes

    @patch("pico_manager.serial")
    def test_set_min_inter_shot_clamps_to_floor(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 min_inter_shot_ms=20\n", b""]

        mgr = _build(mock_serial_mod)
        asyncio.run(mgr.set_min_inter_shot(5))  # below floor

        writes = [c.args[0] for c in ser.write.call_args_list]
        assert any(w == b"CFG MIN_INTER_SHOT_MS=20\n" for w in writes)


# --------------------------------------------------------------------- RMS stream

class TestRmsStream:
    @patch("pico_manager.serial")
    def test_rms_stream_yields_parsed_events(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [
            b"EVENT RMS value=42 timestamp=100\n",
            b"EVENT RMS value=51 timestamp=200\n",
            b"EVENT RMS value=39 timestamp=300\n",
            b"", b"", b"",
        ]

        async def drive():
            mgr = _build(mock_serial_mod)
            stream = await mgr.start_rms_stream(hz=10)
            samples = []
            async for evt in stream:
                samples.append(evt)
                if len(samples) >= 3:
                    await mgr.stop_rms_stream()
                    await stream.aclose()
                    break
            return samples

        samples = asyncio.run(drive())
        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"CFG STREAM_RMS=10\n" in writes
        assert b"CFG STREAM_RMS=0\n" in writes
        assert samples == [
            {"value": 42, "timestamp": 100},
            {"value": 51, "timestamp": 200},
            {"value": 39, "timestamp": 300},
        ]

    @patch("pico_manager.serial")
    def test_rms_stream_clamps_hz_to_max(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [
            b"EVENT RMS value=1 timestamp=10\n",
            b"", b"", b"",
        ]

        async def drive():
            mgr = _build(mock_serial_mod)
            stream = await mgr.start_rms_stream(hz=9999)
            async for _ in stream:
                await mgr.stop_rms_stream()
                await stream.aclose()
                break

        asyncio.run(drive())
        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"CFG STREAM_RMS=100\n" in writes

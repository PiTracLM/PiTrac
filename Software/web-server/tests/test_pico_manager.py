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


def _build_with_lm(serial_mock, *, running=True):
    """A manager wired to a pitrac_manager whose is_running() we control, so we
    can exercise the "LM owns the CDC port" gate."""
    from pico_manager import PicoManager

    cm = MagicMock()
    cm.get_config.side_effect = lambda key: None
    lock = asyncio.Lock()
    lm = MagicMock()
    lm.is_running.return_value = running
    return PicoManager(cm, lock, pitrac_manager=lm)


# --------------------------------------------------------------------- probe

class TestProbe:
    """`probe()` opens a fresh fd, sends STATUS, parses, closes."""

    @patch("pico_manager.serial")
    def test_probe_parses_status_reply(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [
            b"STATUS armed=0 threshold=4096 pulse_us=8.68 "
            b"min_inter_shot_ms=200 pre_trigger_delay_ms=0 "
            b"strobe_hold=0 vsys_mv=4900 vbus=1 "
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
    """`status()` opens the shared owner per transaction and parses the reply."""

    @patch("pico_manager.serial")
    def test_status_opens_and_closes_per_call(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [
            b"STATUS armed=1 threshold=8000\n", b"",
            b"STATUS armed=0 threshold=8000\n", b"",
        ]

        mgr = _build(mock_serial_mod)
        first = asyncio.run(mgr.status())
        second = asyncio.run(mgr.status())

        assert first["armed"] == 1
        assert second["armed"] == 0
        # No borrow outstanding once both polls finish — the owner closed it.
        assert mgr.serial_owner.handle is None
        assert ser.close.call_count == 2


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
    def test_set_armed_surfaces_firmware_refusal(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        # Firmware refuses the arm (room too loud) — STATUS still reports armed=0.
        ser.read.side_effect = [b"STATUS armed=0 threshold=4096\n", b""]

        mgr = _build(mock_serial_mod)
        result = asyncio.run(mgr.set_armed(True))

        assert result["ok"] is False
        assert "raise the threshold" in result["error"]

    @patch("pico_manager.serial")
    def test_set_min_inter_shot_clamps_to_floor(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 min_inter_shot_ms=20\n", b""]

        mgr = _build(mock_serial_mod)
        asyncio.run(mgr.set_min_inter_shot(5))  # below floor

        writes = [c.args[0] for c in ser.write.call_args_list]
        assert any(w == b"CFG MIN_INTER_SHOT_MS=20\n" for w in writes)

    @patch("pico_manager.serial")
    def test_set_pulse_width_emits_cfg_and_confirms_echo(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 pulse_us=12.00\n", b""]

        mgr = _build(mock_serial_mod)
        data = asyncio.run(mgr.set_pulse_width_us(12))

        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"CFG PULSE_WIDTH_US=12\n" in writes
        assert data["ok"] is True
        assert data["pulse_us"] == 12

    @patch("pico_manager.serial")
    def test_set_pulse_width_reports_not_ok_when_echo_mismatch(self, mock_serial_mod):
        # Firmware clamped/rejected the request: STATUS echoes a different width.
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 pulse_us=8.68\n", b""]

        mgr = _build(mock_serial_mod)
        data = asyncio.run(mgr.set_pulse_width_us(12))

        assert data["ok"] is False
        assert "error" in data

    @patch("pico_manager.serial")
    def test_set_pulse_width_accepts_float(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 pulse_us=8.68\n", b""]

        mgr = _build(mock_serial_mod)
        data = asyncio.run(mgr.set_pulse_width_us(8.68))

        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"CFG PULSE_WIDTH_US=8.68\n" in writes
        assert data["ok"] is True

    @patch("pico_manager.serial")
    def test_set_pulse_width_rejects_out_of_range(self, mock_serial_mod):
        mgr = _build(mock_serial_mod)
        with pytest.raises(ValueError):
            asyncio.run(mgr.set_pulse_width_us(0))
        with pytest.raises(ValueError):
            asyncio.run(mgr.set_pulse_width_us(501))

    @patch("pico_manager.serial")
    def test_set_pulse_intervals_emits_cfg(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 intervals=0.70,1.80,0.50\n", b""]

        mgr = _build(mock_serial_mod)
        data = asyncio.run(mgr.set_pulse_intervals([0.70, 1.80, 0.50]))

        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"CFG PULSE_INTERVALS=0.7,1.8,0.5\n" in writes
        assert data["ok"] is True
        assert data["intervals"] == [0.70, 1.80, 0.50]

    @patch("pico_manager.serial")
    def test_set_pulse_intervals_rejects_repeated_ratio(self, mock_serial_mod):
        # 1.0,2.0,4.0 has consecutive ratios 2.0 then 2.0 — ambiguous, must reject.
        mgr = _build(mock_serial_mod)
        with pytest.raises(ValueError, match="ratio"):
            asyncio.run(mgr.set_pulse_intervals([1.0, 2.0, 4.0]))

    @patch("pico_manager.serial")
    def test_set_pulse_intervals_rejects_empty_and_overlong(self, mock_serial_mod):
        mgr = _build(mock_serial_mod)
        with pytest.raises(ValueError):
            asyncio.run(mgr.set_pulse_intervals([]))
        with pytest.raises(ValueError, match="too many"):
            asyncio.run(mgr.set_pulse_intervals([0.1 * (i + 1) for i in range(40)]))

    @patch("pico_manager.serial")
    def test_set_pulse_intervals_rejects_interval_above_firmware_ceiling(self, mock_serial_mod):
        # Firmware rejects any interval > STROBE_MAX_INTERVAL_MS (1000 ms).
        mgr = _build(mock_serial_mod)
        with pytest.raises(ValueError, match="firmware max"):
            asyncio.run(mgr.set_pulse_intervals([1.0, 1500.0]))

    @patch("pico_manager.serial")
    def test_set_pulse_intervals_reports_not_ok_when_echo_mismatch(self, mock_serial_mod):
        # Firmware rejected the list (WP-C): STATUS echoes the OLD intervals.
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 intervals=0.70,1.80\n", b""]

        mgr = _build(mock_serial_mod)
        data = asyncio.run(mgr.set_pulse_intervals([0.50, 1.30, 2.20]))

        assert data["ok"] is False
        assert "intervals" in data


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
                    await stream.aclose()
                    await mgr.stop_rms_stream()
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

    def test_stop_rms_stream_returns_when_no_stream(self):
        # Nothing streaming -> the closed-event is pre-set, so stop returns at once
        # rather than blocking the LM-start handler that awaits it.
        from pico_manager import PicoManager

        cm = MagicMock()
        cm.get_config.return_value = None
        mgr = PicoManager(cm, asyncio.Lock())
        asyncio.run(asyncio.wait_for(mgr.stop_rms_stream(), timeout=1.0))

    @patch("pico_manager.serial")
    def test_stop_rms_stream_waits_for_teardown(self, mock_serial_mod):
        # Drive the generator as a concurrent task (like StreamingResponse does):
        # stop_rms_stream must not return until the generator's finally has run and
        # released the port, so the LM gets a clean handshake.
        ser = mock_serial_mod.Serial.return_value
        reads = [b"EVENT RMS value=9 timestamp=1\n"]
        ser.read.side_effect = lambda _n: reads.pop(0) if reads else b""

        async def drive():
            mgr = _build(mock_serial_mod)
            stream = await mgr.start_rms_stream(hz=10)

            async def consume():
                async for _ in stream:
                    pass

            task = asyncio.create_task(consume())
            await asyncio.sleep(0.05)  # let it emit, then park on empty reads
            assert mgr.serial_owner.handle is not None
            await asyncio.wait_for(mgr.stop_rms_stream(), timeout=2.0)
            await task
            return mgr

        mgr = asyncio.run(drive())
        assert mgr._stream_active is False
        assert mgr.serial_owner.handle is None
        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"CFG STREAM_RMS=0\n" in writes

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
                await stream.aclose()
                await mgr.stop_rms_stream()
                break

        asyncio.run(drive())
        writes = [c.args[0] for c in ser.write.call_args_list]
        assert b"CFG STREAM_RMS=100\n" in writes


# --------------------------------------------------------------------- lock sharing

class TestSharedLock:
    """The shared lock serializes transactions across both managers, and the
    RMS stream must not hold it for its whole lifetime."""

    @patch("pico_manager.serial")
    def test_status_awaits_until_held_lock_releases(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0\n", b""]

        async def drive():
            lock = asyncio.Lock()
            from pico_manager import PicoManager

            cm = MagicMock()
            cm.get_config.return_value = None
            mgr = PicoManager(cm, lock)

            await lock.acquire()
            try:
                # Status call should be pending because someone else holds the lock.
                task = asyncio.create_task(mgr.status())
                await asyncio.sleep(0.05)
                assert not task.done(), "status returned while calibration owned the lock"
            finally:
                lock.release()

            return await task

        data = asyncio.run(drive())
        assert data["present"] is True
        assert data["armed"] == 0

    @patch("pico_manager.serial")
    def test_rms_stream_does_not_hold_lock_across_reads(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [
            b"EVENT RMS value=7 timestamp=10\n",
            b"", b"", b"", b"", b"", b"", b"", b"", b"",
        ]

        async def drive():
            lock = asyncio.Lock()
            from pico_manager import PicoManager

            cm = MagicMock()
            cm.get_config.return_value = None
            mgr = PicoManager(cm, lock)

            stream = await mgr.start_rms_stream(hz=10)
            first = await stream.__anext__()

            # With the stream parked on an empty read, a status() must be able
            # to grab the lock and complete — the stream releases between reads.
            ser.read.side_effect = [b"STATUS armed=1\n", b""]
            status_data = await asyncio.wait_for(mgr.status(), timeout=1.0)

            await stream.aclose()
            await mgr.stop_rms_stream()
            return first, status_data

        first, status_data = asyncio.run(drive())
        assert first == {"value": 7, "timestamp": 10}
        assert status_data["armed"] == 1

    @patch("pico_manager.serial")
    def test_status_oserror_does_not_close_handle_under_live_stream(self, mock_serial_mod):
        # A mid-stream transaction error must release its own borrow only; the
        # RMS stream still holds a borrow, so the shared fd must stay open.
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [
            b"EVENT RMS value=7 timestamp=10\n",
            b"", b"", b"", b"", b"", b"", b"", b"", b"",
        ]

        async def drive():
            lock = asyncio.Lock()
            from pico_manager import PicoManager

            cm = MagicMock()
            cm.get_config.return_value = None
            mgr = PicoManager(cm, lock)

            stream = await mgr.start_rms_stream(hz=10)
            first = await stream.__anext__()

            # status() blows up reading STATUS while the stream is parked.
            ser.reset_input_buffer.side_effect = OSError("device dropped")
            status_data = await asyncio.wait_for(mgr.status(), timeout=1.0)

            handle_after = mgr.serial_owner.handle
            close_calls = ser.close.call_count

            ser.reset_input_buffer.side_effect = None
            await stream.aclose()
            await mgr.stop_rms_stream()
            return first, status_data, handle_after, close_calls

        first, status_data, handle_after, close_calls = asyncio.run(drive())
        assert first == {"value": 7, "timestamp": 10}
        assert status_data["present"] is False
        # The stream's borrow kept the port open through the failed transaction.
        assert handle_after is not None
        assert close_calls == 0

    @patch("pico_manager.serial")
    def test_rms_stream_ends_gracefully_on_read_error(self, mock_serial_mod):
        # A device error mid-stream should stop the generator and release the
        # borrow in finally, not raise out of the SSE generator.
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [
            b"EVENT RMS value=5 timestamp=10\n",
            OSError("device dropped"),
        ]

        async def drive():
            mgr = _build(mock_serial_mod)
            stream = await mgr.start_rms_stream(hz=10)
            samples = [evt async for evt in stream]
            return mgr, samples

        mgr, samples = asyncio.run(drive())
        assert samples == [{"value": 5, "timestamp": 10}]
        assert mgr.serial_owner.handle is None

    @patch("strobe_calibration_manager.serial")
    @patch("strobe_calibration_manager.spidev")
    @patch("strobe_calibration_manager.DigitalOutputDevice")
    @patch("pico_manager.serial")
    def test_both_managers_share_one_serial_owner(
        self, mock_pico_serial, mock_led_cls, mock_spidev_mod, mock_strobe_serial, monkeypatch
    ):
        """Calibration and a /api/pico call must serialize through the same
        owner so neither holds a conflicting exclusive fd on /dev/ttyACM0."""
        monkeypatch.setenv("PITRAC_PICO_ENABLED", "required")
        mock_spidev_mod.SpiDev.side_effect = [MagicMock(), MagicMock()]

        from pico_manager import PicoManager
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = MagicMock()
        cm.get_config.return_value = None
        lock = asyncio.Lock()

        pico = PicoManager(cm, lock)
        strobe = StrobeCalibrationManager(cm, lock, serial_owner=pico.serial_owner)

        # Calibration borrows the shared owner instead of opening its own fd.
        strobe._open_hardware()
        assert strobe._serial is pico.serial_owner.handle
        mock_strobe_serial.Serial.assert_not_called()

        strobe._close_hardware()
        # Owner closed by the borrow's release; status() can reopen cleanly.
        ser = mock_pico_serial.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0\n", b""]
        data = asyncio.run(pico.status())
        assert data["present"] is True


# --------------------------------------------------------------------- flash

class TestFlash:
    @patch("pico_manager.serial")
    @patch("pico_manager.shutil.which")
    @patch("pico_manager.asyncio.create_subprocess_exec")
    def test_flash_invokes_picotool_load(
        self, mock_exec, mock_which, mock_serial_mod, tmp_path
    ):
        mock_which.return_value = "/usr/bin/picotool"

        proc = MagicMock()
        proc.stdout = MagicMock()

        async def _readline_seq():
            for chunk in (b"Loading into Flash: 100%\n", b"Reboot complete\n", b""):
                yield chunk

        seq = _readline_seq()

        async def _next_line():
            try:
                return await seq.__anext__()
            except StopAsyncIteration:
                return b""

        proc.stdout.readline = _next_line

        async def _wait():
            return 0

        proc.wait = _wait

        async def _exec(*args, **kwargs):
            return proc

        mock_exec.side_effect = _exec

        uf2 = tmp_path / "fake.uf2"
        uf2.write_bytes(b"FAKE")

        captured = []
        mgr = _build(mock_serial_mod)
        result = asyncio.run(mgr.flash(str(uf2), on_progress=captured.append))

        assert result["ok"] is True
        assert result["uf2"] == str(uf2)
        called_args = [call.args for call in mock_exec.call_args_list]
        assert any("load" in argv for argv in called_args)
        assert any("Reboot" in line or "Loading" in line for line in captured)

    @patch("pico_manager.serial")
    @patch("pico_manager.shutil.which")
    def test_flash_raises_when_picotool_missing(
        self, mock_which, mock_serial_mod, tmp_path
    ):
        mock_which.return_value = None
        uf2 = tmp_path / "x.uf2"
        uf2.write_bytes(b"")

        mgr = _build(mock_serial_mod)
        with pytest.raises(RuntimeError, match="picotool"):
            asyncio.run(mgr.flash(str(uf2)))


# ------------------------------------------------------------------ detect/bundled

class TestDetectTarget:
    def _exec_returning(self, text: bytes):
        proc = MagicMock()

        async def _communicate():
            return (text, b"")

        proc.communicate = _communicate
        return proc

    @patch("pico_manager.serial")
    @patch("pico_manager.asyncio.create_subprocess_exec")
    def test_detect_rp2350_maps_to_pico2_w(self, mock_exec, mock_serial_mod):
        proc = self._exec_returning(b"Program Information\n type: RP2350\n")

        async def _exec(*a, **k):
            return proc

        mock_exec.side_effect = _exec
        mgr = _build(mock_serial_mod)
        assert asyncio.run(mgr._detect_target(None)) == "pico2_w"

    @patch("pico_manager.serial")
    @patch("pico_manager.asyncio.create_subprocess_exec")
    def test_detect_rp2040_maps_to_pico_w(self, mock_exec, mock_serial_mod):
        proc = self._exec_returning(b"chip: RP2040 rev B2\n")

        async def _exec(*a, **k):
            return proc

        mock_exec.side_effect = _exec
        mgr = _build(mock_serial_mod)
        assert asyncio.run(mgr._detect_target(None)) == "pico_w"

    @patch("pico_manager.serial")
    @patch("pico_manager.asyncio.create_subprocess_exec")
    def test_detect_no_match_raises(self, mock_exec, mock_serial_mod):
        proc = self._exec_returning(b"no device connected\n")

        async def _exec(*a, **k):
            return proc

        mock_exec.side_effect = _exec
        mgr = _build(mock_serial_mod)
        with pytest.raises(RuntimeError, match="detect"):
            asyncio.run(mgr._detect_target(None))


class TestFlashBundled:
    @patch("pico_manager.serial")
    def test_unknown_target_rejected(self, mock_serial_mod, tmp_path):
        mgr = _build(mock_serial_mod)
        with pytest.raises(ValueError, match="target"):
            asyncio.run(mgr.flash_bundled(str(tmp_path), target="../../etc/passwd"))

    @patch("pico_manager.serial")
    def test_missing_bundled_file_raises(self, mock_serial_mod, tmp_path):
        mgr = _build(mock_serial_mod)
        with pytest.raises(RuntimeError, match="no bundled firmware"):
            asyncio.run(mgr.flash_bundled(str(tmp_path), target="pico_w"))


# ------------------------------------------------------------------ uf2 validation

class TestUf2Validation:
    def test_accepts_valid_magic(self):
        from pico_manager import is_valid_uf2

        block = b"\x55\x46\x32\x0a" + b"\x00" * 508
        assert is_valid_uf2(block) is True

    def test_rejects_bad_magic(self):
        from pico_manager import is_valid_uf2

        assert is_valid_uf2(b"not a uf2 file at all............") is False

    def test_rejects_too_short(self):
        from pico_manager import is_valid_uf2

        assert is_valid_uf2(b"\x55\x46\x32\x0a") is False


# ----------------------------------------------------------------- LM ownership

class TestLmRunningGuard:
    """While pitrac_lm runs it owns /dev/ttyACM0 (its own PicoStrobeClient arms
    the Pico), so the /pico background pollers must defer and never open the port."""

    @staticmethod
    def _build_with_lm(*, lm_running):
        from pico_manager import PicoManager

        cm = MagicMock()
        cm.get_config.side_effect = lambda key: None
        lm = MagicMock()
        lm.is_running.return_value = lm_running
        return PicoManager(cm, asyncio.Lock(), pitrac_manager=lm)

    @patch("pico_manager.serial")
    def test_status_defers_to_lm_without_opening_port(self, mock_serial_mod):
        mgr = self._build_with_lm(lm_running=True)
        data = asyncio.run(mgr.status())

        assert data["present"] is False
        assert "pitrac_lm" in data["error"]
        mock_serial_mod.Serial.assert_not_called()

    @patch("pico_manager.serial")
    def test_rms_stream_yields_nothing_while_lm_running(self, mock_serial_mod):
        async def drive():
            mgr = self._build_with_lm(lm_running=True)
            stream = await mgr.start_rms_stream(hz=10)
            return [evt async for evt in stream]

        samples = asyncio.run(drive())
        assert samples == []
        mock_serial_mod.Serial.assert_not_called()

    @patch("pico_manager.serial")
    def test_status_opens_port_when_lm_idle(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 threshold=4096\n", b""]

        mgr = self._build_with_lm(lm_running=False)
        data = asyncio.run(mgr.status())

        assert data["present"] is True
        mock_serial_mod.Serial.assert_called_once()


# ----------------------------------------------------------- DSP config persistence

class TestDspPersistence:
    """Tuned threshold persists to config so the LM can re-push it to a
    power-cycled Pico."""

    @staticmethod
    def _mgr(serial_mock):
        from pico_manager import PicoManager

        cm = MagicMock()
        cm.get_config.side_effect = lambda key=None: None
        return PicoManager(cm, asyncio.Lock()), cm

    @patch("pico_manager.serial")
    def test_set_threshold_persists_on_accept(self, mock_serial_mod):
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 threshold=8000\n", b""]
        mgr, cm = self._mgr(mock_serial_mod)
        asyncio.run(mgr.set_threshold(8000))
        cm.set_config.assert_any_call("gs_config.pico.mic_threshold", 8000)


class TestLmOwnsPortGate:
    """While pitrac_lm runs it owns /dev/ttyACM0, so every web transaction that
    would open the port must refuse WITHOUT touching the device."""

    @patch("pico_manager.serial")
    def test_probe_refuses_when_lm_running(self, mock_serial_mod):
        from pico_manager import LM_OWNS_PORT_MSG

        mgr = _build_with_lm(mock_serial_mod)
        data = asyncio.run(mgr.probe())
        assert data["present"] is False
        assert data["error"] == LM_OWNS_PORT_MSG
        mock_serial_mod.Serial.assert_not_called()

    @patch("pico_manager.serial")
    def test_status_refuses_when_lm_running(self, mock_serial_mod):
        from pico_manager import LM_OWNS_PORT_MSG

        mgr = _build_with_lm(mock_serial_mod)
        data = asyncio.run(mgr.status())
        assert data["present"] is False
        assert data["error"] == LM_OWNS_PORT_MSG
        mock_serial_mod.Serial.assert_not_called()

    @patch("pico_manager.serial")
    def test_selftest_refuses_when_lm_running(self, mock_serial_mod):
        from pico_manager import LM_OWNS_PORT_MSG

        mgr = _build_with_lm(mock_serial_mod)
        data = asyncio.run(mgr.selftest())
        assert data["ok"] is False
        assert data["error"] == LM_OWNS_PORT_MSG
        mock_serial_mod.Serial.assert_not_called()

    @patch("pico_manager.serial")
    def test_setters_refuse_when_lm_running(self, mock_serial_mod):
        from pico_manager import LM_OWNS_PORT_MSG

        mgr = _build_with_lm(mock_serial_mod)
        data = asyncio.run(mgr.set_threshold(8000))
        assert data["ok"] is False
        assert data["error"] == LM_OWNS_PORT_MSG
        mock_serial_mod.Serial.assert_not_called()

    @patch("pico_manager.shutil.which", return_value="/usr/bin/picotool")
    @patch("pico_manager.serial")
    def test_flash_refuses_when_lm_running(self, mock_serial_mod, _which):
        from pico_manager import LM_OWNS_PORT_MSG

        mgr = _build_with_lm(mock_serial_mod)
        data = asyncio.run(mgr.flash("/tmp/whatever.uf2"))
        assert data["ok"] is False
        assert data["error"] == LM_OWNS_PORT_MSG
        mock_serial_mod.Serial.assert_not_called()

    @patch("pico_manager.serial")
    def test_flash_bundled_refuses_when_lm_running(self, mock_serial_mod):
        from pico_manager import LM_OWNS_PORT_MSG

        mgr = _build_with_lm(mock_serial_mod)
        data = asyncio.run(mgr.flash_bundled("/tmp/fw"))
        assert data["ok"] is False
        assert data["error"] == LM_OWNS_PORT_MSG
        mock_serial_mod.Serial.assert_not_called()

    @patch("pico_manager.serial")
    def test_status_opens_port_when_lm_not_running(self, mock_serial_mod):
        # Contrast: with the LM down, the same call opens the port normally.
        ser = mock_serial_mod.Serial.return_value
        ser.read.side_effect = [b"STATUS armed=0 threshold=4096\n", b""]
        mgr = _build_with_lm(mock_serial_mod, running=False)
        data = asyncio.run(mgr.status())
        assert data["present"] is True
        mock_serial_mod.Serial.assert_called_once()

    @patch("pico_manager.serial")
    def test_status_reports_lm_running_flag(self, mock_serial_mod):
        # The /pico page distinguishes "LM owns the port" from "USB unplugged" via
        # this flag so it can show the right warning and disable Start.
        mgr = _build_with_lm(mock_serial_mod)
        data = asyncio.run(mgr.status())
        assert data["present"] is False
        assert data["lm_running"] is True
        mock_serial_mod.Serial.assert_not_called()


class TestValidateIntervals:
    """The validator must accept the firmware's optional trailing-0 terminator
    (present in the shipped vectors) while still rejecting interior zeros,
    negatives, and repeated consecutive ratios over the non-zero prefix."""

    def test_accepts_trailing_zero_terminator(self):
        from pico_manager import _validate_intervals

        # The shipped driver vector: distinct ratios, ends in the 0 terminator.
        out = _validate_intervals([0.7, 1.8, 3.0, 2.2, 3.0, 7.1, 4.0, 0])
        assert out == [0.7, 1.8, 3.0, 2.2, 3.0, 7.1, 4.0, 0.0]

    def test_accepts_vector_without_terminator(self):
        from pico_manager import _validate_intervals

        assert _validate_intervals([0.7, 1.8, 0.5]) == [0.7, 1.8, 0.5]

    def test_rejects_interior_zero(self):
        from pico_manager import _validate_intervals

        with pytest.raises(ValueError, match="positive"):
            _validate_intervals([0.7, 0.0, 1.8])

    def test_rejects_negative(self):
        from pico_manager import _validate_intervals

        with pytest.raises(ValueError, match="positive"):
            _validate_intervals([0.7, -1.0])

    def test_rejects_lone_zero(self):
        from pico_manager import _validate_intervals

        with pytest.raises(ValueError):
            _validate_intervals([0])

    def test_rejects_repeated_consecutive_ratio_over_prefix(self):
        from pico_manager import _validate_intervals

        # The putter vector [5,30,30,30,50] has ratios 6,1,1,1.67 -> repeated 1.0,
        # and the trailing terminator must not hide that.
        with pytest.raises(ValueError, match="ratio"):
            _validate_intervals([5.0, 30.0, 30.0, 30.0, 50.0, 0])

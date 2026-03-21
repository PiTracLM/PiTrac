"""Tests for StrobeCalibrationManager"""

import asyncio
import gc
import os
import time
import pytest
from unittest.mock import Mock, MagicMock, patch, PropertyMock


# ---------------------------------------------------------------------------
# Increment 1: Skeleton + hardware open/close
# ---------------------------------------------------------------------------

class TestStrobeCalibrationInit:
    """Initialization and default state"""

    def test_init_stores_config_manager(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        mgr = StrobeCalibrationManager(cm)
        assert mgr.config_manager is cm

    def test_init_status_is_idle(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        assert mgr.status["state"] == "idle"
        assert mgr.status["progress"] == 0
        assert mgr.status["message"] == ""

    def test_init_hardware_refs_are_none(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        assert mgr._spi_dac is None
        assert mgr._spi_adc is None
        assert mgr._diag_pin is None

    def test_init_cancel_flag_false(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        assert mgr._cancel_requested is False


class TestOpenHardware:
    """_open_hardware sets up SPI and GPIO"""

    @patch("strobe_calibration_manager.spidev")
    @patch("strobe_calibration_manager.DigitalOutputDevice")
    def test_open_creates_spi_and_gpio(self, mock_led_cls, mock_spidev_mod):
        from strobe_calibration_manager import StrobeCalibrationManager

        mock_dac = MagicMock()
        mock_adc = MagicMock()
        mock_spidev_mod.SpiDev.side_effect = [mock_dac, mock_adc]

        mgr = StrobeCalibrationManager(Mock())
        mgr._open_hardware()

        assert mgr._spi_dac is mock_dac
        mock_dac.open.assert_called_once_with(1, 0)
        assert mock_dac.max_speed_hz == 1_000_000
        assert mock_dac.mode == 0

        assert mgr._spi_adc is mock_adc
        mock_adc.open.assert_called_once_with(1, 1)
        assert mock_adc.max_speed_hz == 1_000_000
        assert mock_adc.mode == 0

        mock_led_cls.assert_called_once_with(10)
        assert mgr._diag_pin is mock_led_cls.return_value

    @patch("strobe_calibration_manager.spidev", None)
    def test_open_raises_when_spidev_missing(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        with pytest.raises(RuntimeError, match="spidev"):
            mgr._open_hardware()


class TestCloseHardware:
    """_close_hardware tears down SPI and GPIO safely"""

    def test_close_calls_close_on_all(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mock_dac = MagicMock()
        mock_adc = MagicMock()
        mock_diag = MagicMock()
        mgr._spi_dac = mock_dac
        mgr._spi_adc = mock_adc
        mgr._diag_pin = mock_diag

        mgr._close_hardware()

        mock_dac.close.assert_called_once()
        mock_adc.close.assert_called_once()
        mock_diag.close.assert_called_once()
        assert mgr._spi_dac is None
        assert mgr._spi_adc is None
        assert mgr._diag_pin is None

    def test_close_tolerates_none_refs(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        # all refs are None by default -- should not raise
        mgr._close_hardware()

    def test_close_turns_diag_off_first(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        pin = MagicMock()
        mgr._diag_pin = pin

        mgr._close_hardware()
        # off() should be called before close()
        pin.off.assert_called_once()
        pin.close.assert_called_once()


# ---------------------------------------------------------------------------
# Increment 2: DAC/ADC primitives
# ---------------------------------------------------------------------------

class TestSetDac:
    """_set_dac encodes value into MCP4801 protocol and sends via SPI"""

    def test_set_dac_zero(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_dac = MagicMock()

        mgr._set_dac(0)
        mgr._spi_dac.xfer2.assert_called_once_with([0x30, 0x00])

    def test_set_dac_max(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_dac = MagicMock()

        mgr._set_dac(0xFF)
        # 0x30 | (0xFF >> 4 & 0x0F) = 0x30 | 0x0F = 0x3F
        # (0xFF << 4) & 0xF0 = 0xF0
        mgr._spi_dac.xfer2.assert_called_once_with([0x3F, 0xF0])

    def test_set_dac_midrange(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_dac = MagicMock()

        # 0x96 = 150
        # msb: 0x30 | (0x96 >> 4 & 0x0F) = 0x30 | 0x09 = 0x39
        # lsb: (0x96 << 4) & 0xF0 = 0x60
        mgr._set_dac(0x96)
        mgr._spi_dac.xfer2.assert_called_once_with([0x39, 0x60])


class TestReadAdc:
    """_read_adc sends command and parses 12-bit response"""

    def test_read_adc_parses_12bit(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_adc = MagicMock()
        # response: first byte ignored, upper nibble in byte 1, full byte 2
        # 0x0A << 8 | 0xBC = 0xABC = 2748
        mgr._spi_adc.xfer2.return_value = [0x00, 0x0A, 0xBC]

        result = mgr._read_adc(0x80)
        mgr._spi_adc.xfer2.assert_called_once_with([0x01, 0x80, 0x00])
        assert result == 2748

    def test_read_adc_masks_upper_nibble(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_adc = MagicMock()
        # byte 1 has extra bits above nibble -- should be masked
        mgr._spi_adc.xfer2.return_value = [0xFF, 0xFF, 0xFF]

        result = mgr._read_adc(0xC0)
        # (0xFF & 0x0F) << 8 | 0xFF = 0x0FFF = 4095
        assert result == 4095

    def test_read_adc_zero(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_adc = MagicMock()
        mgr._spi_adc.xfer2.return_value = [0x00, 0x00, 0x00]

        assert mgr._read_adc(0x80) == 0


class TestGetLdoVoltage:
    """get_ldo_voltage reads CH1 and applies resistor divider formula"""

    def test_ldo_voltage_calculation(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_adc = MagicMock()
        # pick adc value that gives a nice voltage
        # LDO = (3.3 / 4096) * adc * 3.0
        # for adc = 2048: (3.3/4096)*2048*3 = 4.95
        mgr._spi_adc.xfer2.return_value = [0x00, 0x08, 0x00]  # 2048

        voltage = mgr.get_ldo_voltage()
        assert abs(voltage - 4.95) < 0.01

    def test_ldo_voltage_zero(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_adc = MagicMock()
        mgr._spi_adc.xfer2.return_value = [0x00, 0x00, 0x00]

        assert mgr.get_ldo_voltage() == 0.0


class TestGetLedCurrent:
    """get_led_current pulses DIAG, reads CH0, converts to amps"""

    def test_led_current_calculation(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_adc = MagicMock()
        mgr._diag_pin = MagicMock()
        # LED current = (3.3 / 4096) * adc * 10.0
        # for adc = 1240: (3.3/4096)*1240*10 = ~9.98
        mgr._spi_adc.xfer2.return_value = [0x00, 0x04, 0xD8]  # 0x04D8 = 1240

        with patch("strobe_calibration_manager.gc"), \
             patch("strobe_calibration_manager.time.sleep"):
            current = mgr.get_led_current()

        expected = (3.3 / 4096) * 1240 * 10.0
        assert abs(current - expected) < 0.01

    def test_led_current_always_turns_diag_off(self):
        """Even if SPI read raises, DIAG must be turned off"""
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_adc = MagicMock()
        mgr._spi_adc.xfer2.side_effect = RuntimeError("SPI failure")
        mgr._diag_pin = MagicMock()

        with patch("strobe_calibration_manager.gc"), \
             patch("strobe_calibration_manager.time.sleep"):
            with pytest.raises(RuntimeError):
                mgr.get_led_current()

        mgr._diag_pin.off.assert_called_once()

    def test_led_current_pulses_diag_on_then_off(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._spi_adc = MagicMock()
        mgr._spi_adc.xfer2.return_value = [0x00, 0x04, 0x00]
        mgr._diag_pin = MagicMock()

        call_order = []
        mgr._diag_pin.on.side_effect = lambda: call_order.append("on")
        mgr._diag_pin.off.side_effect = lambda: call_order.append("off")

        with patch("strobe_calibration_manager.gc"), \
             patch("strobe_calibration_manager.time.sleep"):
            mgr.get_led_current()

        assert call_order == ["on", "off"]


# ---------------------------------------------------------------------------
# Increment 3: Calibration algorithm
# ---------------------------------------------------------------------------

def _make_mgr_with_hw():
    """Helper: create a manager with mocked hardware attached."""
    from strobe_calibration_manager import StrobeCalibrationManager

    mgr = StrobeCalibrationManager(Mock())
    mgr._spi_dac = MagicMock()
    mgr._spi_adc = MagicMock()
    mgr._diag_pin = MagicMock()
    return mgr


class TestFindDacStart:
    """_find_dac_start sweeps DAC 0->255, returns last value where LDO >= 4.5V"""

    @patch("strobe_calibration_manager.time.sleep")
    def test_finds_boundary(self, _sleep):
        mgr = _make_mgr_with_hw()

        # LDO drops below 4.5V at DAC=5, so start should be 4
        voltages = [8.0, 7.5, 6.5, 5.5, 4.8, 4.3]
        call_idx = [0]

        def fake_ldo():
            v = voltages[min(call_idx[0], len(voltages) - 1)]
            call_idx[0] += 1
            return v

        mgr.get_ldo_voltage = fake_ldo

        dac_start, ldo = mgr._find_dac_start()
        assert dac_start == 4

    @patch("strobe_calibration_manager.time.sleep")
    def test_dac_zero_already_unsafe(self, _sleep):
        mgr = _make_mgr_with_hw()

        mgr.get_ldo_voltage = lambda: 3.0  # always below 4.5V

        dac_start, ldo = mgr._find_dac_start()
        assert dac_start == -1

    @patch("strobe_calibration_manager.time.sleep")
    def test_never_drops_returns_255(self, _sleep):
        mgr = _make_mgr_with_hw()

        mgr.get_ldo_voltage = lambda: 9.0  # always safe

        dac_start, ldo = mgr._find_dac_start()
        assert dac_start == 255

    @patch("strobe_calibration_manager.time.sleep")
    def test_sets_dac_at_each_step(self, _sleep):
        mgr = _make_mgr_with_hw()

        calls = []
        mgr._set_dac = lambda v: calls.append(v)
        mgr.get_ldo_voltage = lambda: 3.0  # immediately unsafe

        mgr._find_dac_start()
        # Should have set DAC=0, then LDO was bad, so start=-1
        assert calls[0] == 0


class TestCalibrate:
    """_calibrate runs all 3 phases: find_start, main sweep, averaging"""

    @patch("strobe_calibration_manager.time.sleep")
    def test_succeeds_with_realistic_data(self, _sleep):
        mgr = _make_mgr_with_hw()

        # Phase 1: LDO drops at DAC=100 so start=99
        phase1_voltages = [9.0] * 100 + [4.0]
        phase1_idx = [0]

        def phase1_ldo():
            v = phase1_voltages[min(phase1_idx[0], len(phase1_voltages) - 1)]
            phase1_idx[0] += 1
            return v

        # Phase 2: sweep DAC from 99 down, LED current rises as DAC decreases.
        # Current crosses 10A (V3 target) at DAC=50
        phase2_ldo = [7.0]  # always safe during phase 2
        phase2_currents = {}
        for d in range(100):
            # current goes from ~5A at DAC=99 up to ~12A at DAC=0
            phase2_currents[d] = 5.0 + (99 - d) * (7.0 / 99)

        # Phase 3: averaging returns just under target
        avg_current = [9.8]

        # Wire up the mock methods
        phase = [1]
        ldo_call = [0]
        current_call = [0]
        current_dac_val = [99]

        def smart_set_dac(v):
            current_dac_val[0] = v

        def smart_ldo():
            if phase[0] == 1:
                return phase1_ldo()
            return phase2_ldo[0]

        def smart_current():
            return phase2_currents.get(current_dac_val[0], 5.0)

        def smart_avg_current():
            return avg_current[0]

        # Patch _find_dac_start to return a known start
        mgr._find_dac_start = lambda: (99, 7.0)
        mgr._set_dac = smart_set_dac
        mgr.get_ldo_voltage = lambda: 7.0  # always safe
        mgr.get_led_current = smart_current
        phase[0] = 2

        success, final_dac, current = mgr._calibrate(10.0)
        assert success is True
        assert final_dac > 0
        assert final_dac <= 99

    @patch("strobe_calibration_manager.time.sleep")
    def test_fails_when_dac_zero_unsafe(self, _sleep):
        mgr = _make_mgr_with_hw()

        mgr._find_dac_start = lambda: (-1, 3.0)

        success, dac, current = mgr._calibrate(10.0)
        assert success is False
        assert dac == -1

    @patch("strobe_calibration_manager.time.sleep")
    def test_fails_when_ldo_too_high(self, _sleep):
        mgr = _make_mgr_with_hw()

        mgr._find_dac_start = lambda: (200, 7.0)
        mgr._set_dac = lambda v: None
        mgr.get_ldo_voltage = lambda: 12.0  # above LDO_MAX_V of 11.0

        success, dac, current = mgr._calibrate(10.0)
        assert success is False

    @patch("strobe_calibration_manager.time.sleep")
    def test_fails_when_min_dac_reached(self, _sleep):
        mgr = _make_mgr_with_hw()

        mgr._find_dac_start = lambda: (10, 7.0)
        mgr._set_dac = lambda v: None
        mgr.get_ldo_voltage = lambda: 7.0
        mgr.get_led_current = lambda: 5.0  # never reaches target

        success, dac, current = mgr._calibrate(10.0)
        assert success is False

    @patch("strobe_calibration_manager.time.sleep")
    def test_cancel_during_main_sweep(self, _sleep):
        mgr = _make_mgr_with_hw()

        mgr._find_dac_start = lambda: (200, 7.0)
        mgr._set_dac = lambda v: None
        mgr.get_ldo_voltage = lambda: 7.0
        mgr.get_led_current = lambda: 5.0

        # Cancel after a few iterations
        counter = [0]
        original_set_dac = mgr._set_dac
        def counting_set_dac(v):
            counter[0] += 1
            if counter[0] > 5:
                mgr._cancel_requested = True
            original_set_dac(v)
        mgr._set_dac = counting_set_dac

        success, dac, current = mgr._calibrate(10.0)
        assert success is False

    @patch("strobe_calibration_manager.time.sleep")
    def test_hard_cap_rejects_excessive_current(self, _sleep):
        """If LED current ever exceeds HARD_CAP_CURRENT, calibration should fail"""
        mgr = _make_mgr_with_hw()

        mgr._find_dac_start = lambda: (50, 7.0)
        mgr._set_dac = lambda v: None
        mgr.get_ldo_voltage = lambda: 7.0
        mgr.get_led_current = lambda: 13.0  # above 12A hard cap

        success, dac, current = mgr._calibrate(10.0)
        assert success is False

    @patch("strobe_calibration_manager.time.sleep")
    def test_skips_ldo_below_min_during_sweep(self, _sleep):
        """When LDO drops below min during sweep, that step is skipped"""
        mgr = _make_mgr_with_hw()

        mgr._find_dac_start = lambda: (5, 7.0)
        dac_calls = []
        mgr._set_dac = lambda v: dac_calls.append(v)

        # LDO goes below min at DAC=4, then back up for DAC=3..0
        ldo_values = {5: 7.0, 4: 4.0, 3: 7.0, 2: 7.0, 1: 7.0, 0: 7.0}
        mgr.get_ldo_voltage = lambda: ldo_values.get(dac_calls[-1] if dac_calls else 5, 7.0)
        mgr.get_led_current = lambda: 5.0  # never reaches target

        success, dac, current = mgr._calibrate(10.0)
        # Should have continued past the low LDO step
        assert success is False  # reaches MIN_DAC
        assert 4 in dac_calls  # did try DAC=4

    @patch("strobe_calibration_manager.time.sleep")
    def test_averaging_steps_up_when_still_over_target(self, _sleep):
        """Phase 3: if average current still above target, increment DAC"""
        mgr = _make_mgr_with_hw()

        dac_val = [0]

        def track_dac(v):
            dac_val[0] = v

        mgr._find_dac_start = lambda: (50, 7.0)
        mgr._set_dac = track_dac
        mgr.get_ldo_voltage = lambda: 7.0

        def smart_current():
            d = dac_val[0]
            # Sweep phase: crosses target at DAC=25 (current > 10A)
            if d <= 25:
                return 10.5
            # Averaging phase at DAC=26: first time over target, second time under
            if d == 26:
                # Return slightly above target so it steps up to 27
                smart_current._avg_call_count = getattr(smart_current, '_avg_call_count', 0) + 1
                if smart_current._avg_call_count <= 10:
                    return 10.1
                return 9.8
            if d == 27:
                return 9.8
            return 5.0

        mgr.get_led_current = smart_current

        success, final_dac, current = mgr._calibrate(10.0)
        assert success is True
        # Should have stepped up from 26 to 27 during averaging
        assert final_dac == 27

    @patch("strobe_calibration_manager.time.sleep")
    def test_updates_status_progress(self, _sleep):
        """Status dict should be updated during calibration sweep"""
        mgr = _make_mgr_with_hw()

        mgr._find_dac_start = lambda: (10, 7.0)
        mgr._set_dac = lambda v: None
        mgr.get_ldo_voltage = lambda: 7.0

        # Have it cross the target at DAC=5
        current_dac = [10]
        original_set = mgr._set_dac
        def tracking_set(v):
            current_dac[0] = v
        mgr._set_dac = tracking_set

        def current_fn():
            if current_dac[0] <= 5:
                return 10.5  # above target
            return 5.0
        mgr.get_led_current = current_fn

        progress_values = []
        original_status = mgr.status
        class StatusProxy(dict):
            def __setitem__(self, k, v):
                super().__setitem__(k, v)
                if k == "progress":
                    progress_values.append(v)
        proxy = StatusProxy(original_status)
        mgr.status = proxy

        mgr._calibrate(10.0)
        # Should have updated progress at least once
        assert len(progress_values) > 0


# ---------------------------------------------------------------------------
# Increment 4: Async public API
# ---------------------------------------------------------------------------

class TestStartCalibration:
    """start_calibration validates inputs, checks board version, runs _calibrate"""

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_rejects_non_v3_board(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.side_effect = lambda key=None: {
            "gs_config.strobing.kConnectionBoardVersion": "2",
            "gs_config.strobing.kDAC_setting": None,
        }.get(key)

        mgr = StrobeCalibrationManager(cm)

        result = await mgr.start_calibration(led_type="v3")
        assert result["status"] == "error"
        assert "version" in result["message"].lower() or "V3" in result["message"]

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_rejects_overwrite_without_flag(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.side_effect = lambda key=None: {
            "gs_config.strobing.kConnectionBoardVersion": "3",
            "gs_config.strobing.kDAC_setting": 150,
        }.get(key)

        mgr = StrobeCalibrationManager(cm)

        result = await mgr.start_calibration(led_type="v3", overwrite=False)
        assert result["status"] == "error"
        assert "exist" in result["message"].lower() or "overwrite" in result["message"].lower()

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_allows_overwrite_with_flag(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.side_effect = lambda key=None: {
            "gs_config.strobing.kConnectionBoardVersion": "3",
            "gs_config.strobing.kDAC_setting": 150,
        }.get(key)
        cm.set_config.return_value = (True, "ok", False)

        mgr = StrobeCalibrationManager(cm)

        # Mock out the hardware and calibration
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        mgr._calibrate = Mock(return_value=(True, 0x80, 9.5))

        result = await mgr.start_calibration(led_type="v3", overwrite=True)
        assert result["status"] == "success"

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_uses_v3_target_current(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.side_effect = lambda key=None: {
            "gs_config.strobing.kConnectionBoardVersion": "3",
            "gs_config.strobing.kDAC_setting": None,
        }.get(key)
        cm.set_config.return_value = (True, "ok", False)

        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()

        captured_target = []
        def fake_calibrate(target):
            captured_target.append(target)
            return (True, 0x80, 9.5)
        mgr._calibrate = fake_calibrate

        await mgr.start_calibration(led_type="v3")
        assert captured_target[0] == 10.0

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_uses_legacy_target_current(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.side_effect = lambda key=None: {
            "gs_config.strobing.kConnectionBoardVersion": "3",
            "gs_config.strobing.kDAC_setting": None,
        }.get(key)
        cm.set_config.return_value = (True, "ok", False)

        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()

        captured_target = []
        def fake_calibrate(target):
            captured_target.append(target)
            return (True, 0x80, 8.5)
        mgr._calibrate = fake_calibrate

        await mgr.start_calibration(led_type="legacy")
        assert captured_target[0] == 9.0

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_uses_custom_target(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.side_effect = lambda key=None: {
            "gs_config.strobing.kConnectionBoardVersion": "3",
            "gs_config.strobing.kDAC_setting": None,
        }.get(key)
        cm.set_config.return_value = (True, "ok", False)

        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()

        captured_target = []
        def fake_calibrate(target):
            captured_target.append(target)
            return (True, 0x80, 7.5)
        mgr._calibrate = fake_calibrate

        await mgr.start_calibration(led_type="v3", target_current=7.5)
        assert captured_target[0] == 7.5

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_saves_result_on_success(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.side_effect = lambda key=None: {
            "gs_config.strobing.kConnectionBoardVersion": "3",
            "gs_config.strobing.kDAC_setting": None,
        }.get(key)
        cm.set_config.return_value = (True, "ok", False)

        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        mgr._calibrate = Mock(return_value=(True, 0x80, 9.5))

        await mgr.start_calibration(led_type="v3")
        cm.set_config.assert_called_once_with("gs_config.strobing.kDAC_setting", 0x80)

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_sets_safe_dac_on_failure(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.side_effect = lambda key=None: {
            "gs_config.strobing.kConnectionBoardVersion": "3",
            "gs_config.strobing.kDAC_setting": None,
        }.get(key)

        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        mgr._calibrate = Mock(return_value=(False, -1, -1))
        dac_calls = []
        mgr._set_dac = lambda v: dac_calls.append(v)

        result = await mgr.start_calibration(led_type="v3")
        assert result["status"] == "failed"
        assert 0x96 in dac_calls

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_always_closes_hardware(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.side_effect = lambda key=None: {
            "gs_config.strobing.kConnectionBoardVersion": "3",
            "gs_config.strobing.kDAC_setting": None,
        }.get(key)

        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        mgr._calibrate = Mock(side_effect=RuntimeError("kaboom"))

        result = await mgr.start_calibration(led_type="v3")
        assert result["status"] == "error"
        mgr._close_hardware.assert_called_once()

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_status_transitions(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.side_effect = lambda key=None: {
            "gs_config.strobing.kConnectionBoardVersion": "3",
            "gs_config.strobing.kDAC_setting": None,
        }.get(key)
        cm.set_config.return_value = (True, "ok", False)

        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        mgr._calibrate = Mock(return_value=(True, 0x80, 9.5))

        assert mgr.status["state"] == "idle"
        await mgr.start_calibration(led_type="v3")
        assert mgr.status["state"] == "complete"
        assert mgr.status["progress"] == 100


class TestCancel:
    """cancel sets the flag and resets status"""

    def test_cancel_sets_flag(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr.status["state"] = "calibrating"
        mgr.cancel()
        assert mgr._cancel_requested is True

    def test_cancel_when_idle(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr.cancel()
        # should not error, just a no-op
        assert mgr._cancel_requested is True


class TestGetStatus:
    """get_status returns a copy of the status dict"""

    def test_returns_status_copy(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr.status["state"] = "calibrating"
        mgr.status["progress"] = 42
        mgr.status["message"] = "sweeping"

        result = mgr.get_status()
        assert result["state"] == "calibrating"
        assert result["progress"] == 42
        assert result["message"] == "sweeping"
        # should be a copy
        result["state"] = "mutated"
        assert mgr.status["state"] == "calibrating"


class TestReadDiagnostics:
    """read_diagnostics bundles LDO + current + raw ADC reads"""

    @pytest.mark.asyncio
    async def test_returns_all_readings(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        mgr.get_ldo_voltage = Mock(return_value=7.5)
        mgr.get_led_current = Mock(return_value=9.2)
        mgr._read_adc = Mock(side_effect=[1234, 2345])

        result = await mgr.read_diagnostics()

        assert result["ldo_voltage"] == 7.5
        assert result["led_current"] == 9.2
        assert result["adc_ch0_raw"] == 1234
        assert result["adc_ch1_raw"] == 2345

    @pytest.mark.asyncio
    async def test_skips_current_when_ldo_unsafe(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        mgr.get_ldo_voltage = Mock(return_value=3.0)
        mgr._read_adc = Mock(side_effect=[100, 200])

        result = await mgr.read_diagnostics()

        assert result["ldo_voltage"] == 3.0
        assert result["led_current"] is None
        assert "unsafe" in result.get("warning", "").lower()

    @pytest.mark.asyncio
    async def test_closes_hardware_on_error(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        mgr.get_ldo_voltage = Mock(side_effect=RuntimeError("SPI gone"))

        result = await mgr.read_diagnostics()
        assert result["status"] == "error"
        mgr._close_hardware.assert_called_once()


class TestSetDacManual:
    """set_dac_manual validates range and returns LDO check"""

    @pytest.mark.asyncio
    async def test_rejects_out_of_range(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())

        result = await mgr.set_dac_manual(256)
        assert result["status"] == "error"

        result = await mgr.set_dac_manual(-1)
        assert result["status"] == "error"

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_sets_dac_and_checks_ldo(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        dac_calls = []
        mgr._set_dac = lambda v: dac_calls.append(v)
        mgr.get_ldo_voltage = Mock(return_value=7.5)

        result = await mgr.set_dac_manual(0x80)
        assert result["status"] == "success"
        assert 0x80 in dac_calls
        assert result["ldo_voltage"] == 7.5

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_warns_when_ldo_below_min(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        mgr._set_dac = lambda v: None
        mgr.get_ldo_voltage = Mock(return_value=3.5)

        result = await mgr.set_dac_manual(0x10)
        assert "warning" in result


class TestGetDacStart:
    """get_dac_start runs the safe-start sweep"""

    @pytest.mark.asyncio
    @patch("strobe_calibration_manager.time.sleep")
    async def test_returns_start_and_ldo(self, _sleep):
        from strobe_calibration_manager import StrobeCalibrationManager

        mgr = StrobeCalibrationManager(Mock())
        mgr._open_hardware = Mock()
        mgr._close_hardware = Mock()
        mgr._find_dac_start = Mock(return_value=(99, 7.0))
        mgr._set_dac = Mock()
        mgr.get_ldo_voltage = Mock(return_value=7.0)

        result = await mgr.get_dac_start()
        assert result["dac_start"] == 99
        assert result["ldo_voltage"] == 7.0


class TestGetSavedSettings:
    """get_saved_settings reads kDAC_setting from config"""

    @pytest.mark.asyncio
    async def test_returns_saved_value(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.return_value = 150

        mgr = StrobeCalibrationManager(cm)
        result = await mgr.get_saved_settings()
        assert result["dac_setting"] == 150

    @pytest.mark.asyncio
    async def test_returns_none_when_unset(self):
        from strobe_calibration_manager import StrobeCalibrationManager

        cm = Mock()
        cm.get_config.return_value = None

        mgr = StrobeCalibrationManager(cm)
        result = await mgr.get_saved_settings()
        assert result["dac_setting"] is None

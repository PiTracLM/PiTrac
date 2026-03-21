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
    @patch("strobe_calibration_manager.LED")
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
        mgr._spi_dac = MagicMock()
        mgr._spi_adc = MagicMock()
        mgr._diag_pin = MagicMock()

        mgr._close_hardware()

        mgr._spi_dac.close.assert_called_once()
        mgr._spi_adc.close.assert_called_once()
        mgr._diag_pin.close.assert_called_once()

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

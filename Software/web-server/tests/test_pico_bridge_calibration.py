"""Tests for the Pico bridge in StrobeCalibrationManager.

The Pico-mode calibration path fires a single `FIRE` command per DAC step
(microsecond pulse train via PIO) and oversamples ADC CH0 during the train
window. This matches the legacy diag.on/off duty cycle and is safe for the
IR LED string.
"""

from unittest.mock import MagicMock, patch


class TestPicoModeOpenHardware:
    """`_open_hardware` opens a serial handle when PITRAC_PICO_ENABLED is set."""

    @patch("strobe_calibration_manager.serial")
    @patch("strobe_calibration_manager.spidev")
    @patch("strobe_calibration_manager.DigitalOutputDevice")
    def test_pico_enabled_required_opens_serial(self, mock_led_cls, mock_spidev_mod, mock_serial_mod, monkeypatch):
        from strobe_calibration_manager import StrobeCalibrationManager

        monkeypatch.setenv("PITRAC_PICO_ENABLED", "required")
        mock_spidev_mod.SpiDev.side_effect = [MagicMock(), MagicMock()]

        cm = MagicMock()
        cm.get_config.return_value = None
        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware()

        mock_serial_mod.Serial.assert_called_once_with(
            "/dev/ttyACM0", 115200, timeout=2, exclusive=True
        )
        # Legacy GPIO 10 path must NOT have been opened.
        mock_led_cls.assert_not_called()
        assert mgr._diag_pin is None
        assert mgr._serial is mock_serial_mod.Serial.return_value
        # MIN_INTER_SHOT_MS must be lowered for the sweep.
        writes = [c.args[0] for c in mock_serial_mod.Serial.return_value.write.call_args_list]
        assert b"CFG MIN_INTER_SHOT_MS=20\n" in writes

    @patch("strobe_calibration_manager.serial")
    @patch("strobe_calibration_manager.spidev")
    @patch("strobe_calibration_manager.DigitalOutputDevice")
    def test_pico_enabled_false_uses_legacy(self, mock_led_cls, mock_spidev_mod, mock_serial_mod, monkeypatch):
        from strobe_calibration_manager import StrobeCalibrationManager

        monkeypatch.setenv("PITRAC_PICO_ENABLED", "legacy")
        mock_spidev_mod.SpiDev.side_effect = [MagicMock(), MagicMock()]

        cm = MagicMock()
        cm.get_config.return_value = None  # force env-var fallback in _pico_setting
        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware()

        # Legacy: DigitalOutputDevice(10) is what gets used.
        mock_led_cls.assert_called_once_with(10)
        mock_serial_mod.Serial.assert_not_called()
        assert mgr._serial is None

    @patch("strobe_calibration_manager.serial")
    @patch("strobe_calibration_manager.spidev")
    @patch("strobe_calibration_manager.DigitalOutputDevice")
    def test_pico_device_env_var_overrides_default_path(self, mock_led_cls, mock_spidev_mod, mock_serial_mod, monkeypatch):
        from strobe_calibration_manager import StrobeCalibrationManager

        monkeypatch.setenv("PITRAC_PICO_ENABLED", "required")
        monkeypatch.setenv("PITRAC_PICO_DEVICE", "/dev/ttyACM1")
        mock_spidev_mod.SpiDev.side_effect = [MagicMock(), MagicMock()]

        cm = MagicMock()
        cm.get_config.return_value = None
        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware()

        mock_serial_mod.Serial.assert_called_once_with(
            "/dev/ttyACM1", 115200, timeout=2, exclusive=True
        )


class TestPicoConfigManagerWiring:
    """The UI value via config_manager beats the env var (UI is authoritative)."""

    @patch("strobe_calibration_manager.serial")
    @patch("strobe_calibration_manager.spidev")
    @patch("strobe_calibration_manager.DigitalOutputDevice")
    def test_config_manager_false_overrides_env_var_true(
        self, mock_led_cls, mock_spidev_mod, mock_serial_mod, monkeypatch
    ):
        from strobe_calibration_manager import StrobeCalibrationManager

        # Env var would normally enable Pico; UI value says disable.
        monkeypatch.setenv("PITRAC_PICO_ENABLED", "required")
        mock_spidev_mod.SpiDev.side_effect = [MagicMock(), MagicMock()]

        cm = MagicMock()
        cm.get_config.side_effect = lambda key: {
            "gs_config.pico.enabled": "legacy",
        }.get(key)

        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware()

        mock_serial_mod.Serial.assert_not_called()
        mock_led_cls.assert_called_once_with(10)

    @patch("strobe_calibration_manager.serial")
    @patch("strobe_calibration_manager.spidev")
    @patch("strobe_calibration_manager.DigitalOutputDevice")
    def test_config_manager_device_override_used(
        self, mock_led_cls, mock_spidev_mod, mock_serial_mod, monkeypatch
    ):
        from strobe_calibration_manager import StrobeCalibrationManager

        monkeypatch.delenv("PITRAC_PICO_ENABLED", raising=False)
        monkeypatch.delenv("PITRAC_PICO_DEVICE", raising=False)
        mock_spidev_mod.SpiDev.side_effect = [MagicMock(), MagicMock()]

        cm = MagicMock()
        cm.get_config.side_effect = lambda key: {
            "gs_config.pico.enabled": "required",
            "gs_config.pico.device": "/dev/ttyACM3",
        }.get(key)

        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware()

        mock_serial_mod.Serial.assert_called_once_with(
            "/dev/ttyACM3", 115200, timeout=2, exclusive=True
        )


class TestPicoModeGetLedCurrent:
    """`get_led_current` swaps `diag.on/off` for `CFG STROBE_HOLD=1/0` writes."""

    @patch("strobe_calibration_manager.serial")
    @patch("strobe_calibration_manager.spidev")
    @patch("strobe_calibration_manager.DigitalOutputDevice")
    def test_pico_mode_uses_fire_peak(self, mock_led_cls, mock_spidev_mod, mock_serial_mod, monkeypatch):
        from strobe_calibration_manager import StrobeCalibrationManager

        monkeypatch.setenv("PITRAC_PICO_ENABLED", "required")
        mock_dac = MagicMock()
        mock_adc = MagicMock()
        mock_spidev_mod.SpiDev.side_effect = [mock_dac, mock_adc]
        serial_instance = mock_serial_mod.Serial.return_value
        # Simulate three firmware replies with noise — one spike, two stable.
        # Median should pick the stable value.
        serial_instance.read.side_effect = [
            b"EVENT PEAK timestamp=1000 adc=1240 samples=6100\n", b"",
            b"EVENT PEAK timestamp=2000 adc=1500 samples=6100\n", b"",  # outlier
            b"EVENT PEAK timestamp=3000 adc=1235 samples=6100\n", b"",
        ]

        cm = MagicMock()
        cm.get_config.return_value = None
        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware()
        current = mgr.get_led_current()

        # Pico path uses FIRE_PEAK and sends it 3 times for median.
        writes = [c.args[0] for c in serial_instance.write.call_args_list]
        fire_peak_count = sum(1 for w in writes if w == b"FIRE_PEAK\n")
        assert fire_peak_count == 3
        assert b"CFG STROBE_HOLD=1\n" not in writes
        # ADC SPI not used in Pico path — Pico samples its own ADC.
        mock_adc.xfer2.assert_not_called()
        # Median of [1240, 1500, 1235] sorted = [1235, 1240, 1500] -> 1240
        # Current at adc=1240: (3.3/4096) * 1240 * 10 = 9.99 A
        assert 9.9 < current < 10.1

    @patch("strobe_calibration_manager.serial")
    @patch("strobe_calibration_manager.spidev")
    @patch("strobe_calibration_manager.DigitalOutputDevice")
    def test_legacy_mode_uses_diag_pin(self, mock_led_cls, mock_spidev_mod, mock_serial_mod, monkeypatch):
        from strobe_calibration_manager import StrobeCalibrationManager

        monkeypatch.setenv("PITRAC_PICO_ENABLED", "legacy")
        mock_dac = MagicMock()
        mock_adc = MagicMock()
        mock_adc.xfer2.return_value = [0x00, 0x00, 0x00]
        mock_spidev_mod.SpiDev.side_effect = [mock_dac, mock_adc]
        diag = mock_led_cls.return_value

        cm = MagicMock()
        cm.get_config.return_value = None  # force env-var fallback in _pico_setting
        mgr = StrobeCalibrationManager(cm)
        mgr._open_hardware()
        mgr.get_led_current()

        diag.on.assert_called_once()
        diag.off.assert_called()
        mock_serial_mod.Serial.assert_not_called()

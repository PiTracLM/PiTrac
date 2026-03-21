"""Strobe Calibration Manager for PiTrac Web Server

Controls MCP4801 DAC and MCP3202 ADC over SPI1 to calibrate the IR strobe
LED current on the V3 Connector Board.
"""

import asyncio
import gc
import logging
import os
import time
from typing import Any, Dict, Optional

logger = logging.getLogger(__name__)

try:
    import spidev
except ImportError:
    spidev = None

try:
    from gpiozero import LED
except ImportError:
    LED = None


class StrobeCalibrationManager:
    """Manages strobe LED calibration via SPI hardware on the Connector Board"""

    # SPI bus 1 (auxiliary), CS0 = DAC, CS1 = ADC
    SPI_BUS = 1
    SPI_DAC_DEVICE = 0
    SPI_ADC_DEVICE = 1
    SPI_MAX_SPEED_HZ = 1_000_000

    # DIAG pin gates the strobe LED (BCM numbering)
    DIAG_GPIO_PIN = 10

    # MCP4801 8-bit DAC write command (1x gain, active output)
    MCP4801_WRITE_CMD = 0x30

    # MCP3202 12-bit ADC channel commands (single-ended)
    ADC_CH0_CMD = 0x80  # LED current sense
    ADC_CH1_CMD = 0xC0  # LDO voltage

    # DAC range
    DAC_MIN = 0
    DAC_MAX = 0xFF

    # Safe fallback DAC value if calibration fails
    SAFE_DAC_VALUE = 0x96

    # LDO voltage bounds
    LDO_MIN_V = 4.5
    LDO_MAX_V = 11.0

    # Target LED currents (amps)
    V3_TARGET_CURRENT = 10.0
    LEGACY_TARGET_CURRENT = 9.0
    HARD_CAP_CURRENT = 12.0

    # Config key for persisting the result
    DAC_CONFIG_KEY = "gs_config.strobing.kDAC_setting"

    def __init__(self, config_manager):
        self.config_manager = config_manager

        self._spi_dac = None
        self._spi_adc = None
        self._diag_pin = None

        self._cancel_requested = False

        self.status: Dict[str, Any] = {
            "state": "idle",
            "progress": 0,
            "message": "",
        }

    # ------------------------------------------------------------------
    # Hardware lifecycle
    # ------------------------------------------------------------------

    def _open_hardware(self):
        if spidev is None:
            raise RuntimeError("spidev library not available -- not running on a Raspberry Pi?")
        if LED is None:
            raise RuntimeError("gpiozero library not available -- not running on a Raspberry Pi?")

        self._spi_dac = spidev.SpiDev()
        self._spi_dac.open(self.SPI_BUS, self.SPI_DAC_DEVICE)
        self._spi_dac.max_speed_hz = self.SPI_MAX_SPEED_HZ
        self._spi_dac.mode = 0

        self._spi_adc = spidev.SpiDev()
        self._spi_adc.open(self.SPI_BUS, self.SPI_ADC_DEVICE)
        self._spi_adc.max_speed_hz = self.SPI_MAX_SPEED_HZ
        self._spi_adc.mode = 0

        self._diag_pin = LED(self.DIAG_GPIO_PIN)

    def _close_hardware(self):
        if self._diag_pin is not None:
            self._diag_pin.off()
            self._diag_pin.close()
        if self._spi_dac is not None:
            self._spi_dac.close()
        if self._spi_adc is not None:
            self._spi_adc.close()

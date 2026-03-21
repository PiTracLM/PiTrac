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

    # ------------------------------------------------------------------
    # DAC / ADC primitives
    # ------------------------------------------------------------------

    def _set_dac(self, value: int):
        """Write an 8-bit value to the MCP4801 DAC."""
        msb = self.MCP4801_WRITE_CMD | ((value >> 4) & 0x0F)
        lsb = (value << 4) & 0xF0
        self._spi_dac.xfer2([msb, lsb])

    def _read_adc(self, channel_cmd: int) -> int:
        """Read a 12-bit value from the MCP3202 ADC."""
        response = self._spi_adc.xfer2([0x01, channel_cmd, 0x00])
        return ((response[1] & 0x0F) << 8) | response[2]

    def get_ldo_voltage(self) -> float:
        """Read the LDO gate voltage via ADC CH1 (2k/1k resistor divider)."""
        adc_value = self._read_adc(self.ADC_CH1_CMD)
        return (3.3 / 4096) * adc_value * 3.0

    def get_led_current(self) -> float:
        """Pulse DIAG, read LED current sense via ADC CH0 (0.1 ohm sense resistor).

        Uses real-time scheduling and GC disable for deterministic timing.
        DIAG is always turned off in the finally block.
        """
        msg = [0x01, self.ADC_CH0_CMD, 0x00]
        spi = self._spi_adc
        diag = self._diag_pin

        gc.disable()
        try:
            param = os.sched_param(os.sched_get_priority_max(os.SCHED_FIFO))
            os.sched_setscheduler(0, os.SCHED_FIFO, param)
        except (PermissionError, AttributeError, OSError):
            pass

        time.sleep(0)

        try:
            diag.on()
            response = spi.xfer2(msg)
        finally:
            diag.off()
            try:
                param = os.sched_param(0)
                os.sched_setscheduler(0, os.SCHED_OTHER, param)
            except (PermissionError, AttributeError, OSError):
                pass
            gc.enable()

        adc_value = ((response[1] & 0x0F) << 8) | response[2]
        return (3.3 / 4096) * adc_value * 10.0

    # ------------------------------------------------------------------
    # Calibration algorithm
    # ------------------------------------------------------------------

    def _find_dac_start(self):
        """Sweep DAC 0->255, return last value where LDO stays >= LDO_MIN_V.

        Returns:
            (dac_value, ldo_voltage) — dac_value is -1 if even DAC 0 is unsafe.
        """
        dac_start = 0
        ldo = 0.0

        for i in range(self.DAC_MAX + 1):
            self._set_dac(i)
            time.sleep(0.1)
            ldo = self.get_ldo_voltage()
            logger.debug(f"DAC={i:#04x}, LDO={ldo:.2f}V")

            if ldo < self.LDO_MIN_V:
                dac_start = i - 1
                return dac_start, ldo

            dac_start = i

        return dac_start, ldo

    def _calibrate(self, target_current: float):
        """Run full calibration: find safe start, sweep down to target, average.

        Returns:
            (success, final_dac, led_current)
        """
        # Phase 1: find safe starting DAC
        dac_start, ldo = self._find_dac_start()

        if dac_start < 0:
            logger.debug(f"DAC 0 already below LDO minimum ({ldo:.2f}V)")
            return False, -1, -1

        logger.debug(f"Calibrating: target={target_current}A, dac_start={dac_start:#04x}")

        # Phase 2: sweep from dac_start downward, looking for target crossing
        current_dac = dac_start
        final_dac = self.DAC_MIN
        total_steps = dac_start - self.DAC_MIN + 1

        while current_dac >= self.DAC_MIN:
            if self._cancel_requested:
                logger.info("Calibration cancelled by user")
                return False, -1, -1

            self._set_dac(current_dac)
            time.sleep(0.1)

            # Update progress for UI polling
            steps_done = dac_start - current_dac
            if total_steps > 0:
                self.status["progress"] = int(20 + (steps_done / total_steps) * 60)

            ldo = self.get_ldo_voltage()

            if ldo < self.LDO_MIN_V:
                logger.debug(f"LDO {ldo:.2f}V below min at DAC={current_dac:#04x}, skipping")
                final_dac = current_dac
                current_dac -= 1
                continue

            if ldo > self.LDO_MAX_V:
                logger.debug(f"LDO {ldo:.2f}V above max — something is wrong")
                return False, -1, -1

            led_current = self.get_led_current()
            logger.debug(f"DAC={current_dac:#04x}, current={led_current:.2f}A")

            # Hard safety cap (bug fix over original script)
            if led_current > self.HARD_CAP_CURRENT:
                logger.error(f"LED current {led_current:.2f}A exceeds hard cap {self.HARD_CAP_CURRENT}A")
                return False, -1, -1

            if led_current > target_current:
                logger.debug(f"Crossed target at DAC={current_dac:#04x} ({led_current:.2f}A)")
                final_dac = current_dac + 1
                break

            final_dac = current_dac
            current_dac -= 1

        # Edge cases — sweep ran off either end
        if current_dac < self.DAC_MIN:
            logger.debug(f"Reached DAC_MIN without crossing target")
            return False, -1, -1
        if current_dac >= self.DAC_MAX:
            logger.debug(f"DAC_MAX still above target — hardware problem")
            return False, -1, -1

        # Phase 3: average readings at the final setting to refine
        led_current = 0.0
        n_avg = 10

        while True:
            if self._cancel_requested:
                return False, -1, -1

            self._set_dac(final_dac)
            time.sleep(0.1)

            ldo = self.get_ldo_voltage()
            if ldo < self.LDO_MIN_V:
                final_dac -= 1
                break

            current_sum = 0.0
            for _ in range(n_avg):
                current_sum += self.get_led_current()
                time.sleep(0.1)
            led_current = current_sum / n_avg

            if led_current > target_current:
                final_dac += 1
                if final_dac > self.DAC_MAX:
                    logger.error("Averaging loop exceeded DAC_MAX")
                    return False, -1, -1
            else:
                break

        self.status["progress"] = 90
        logger.debug(f"Calibration result: DAC={final_dac:#04x}, current={led_current:.2f}A")
        return True, final_dac, led_current

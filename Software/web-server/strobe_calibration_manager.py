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
    from gpiozero import DigitalOutputDevice
except ImportError:
    DigitalOutputDevice = None


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

    # If ADC CH0 reads above this with strobe off, something is wrong (blown MOSFET, shorted gate driver)
    PREFLIGHT_CURRENT_THRESHOLD = 6

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
        if DigitalOutputDevice is None:
            raise RuntimeError("gpiozero library not available -- not running on a Raspberry Pi?")

        self._spi_dac = spidev.SpiDev()
        self._spi_dac.open(self.SPI_BUS, self.SPI_DAC_DEVICE)
        self._spi_dac.max_speed_hz = self.SPI_MAX_SPEED_HZ
        self._spi_dac.mode = 0

        self._spi_adc = spidev.SpiDev()
        self._spi_adc.open(self.SPI_BUS, self.SPI_ADC_DEVICE)
        self._spi_adc.max_speed_hz = self.SPI_MAX_SPEED_HZ
        self._spi_adc.mode = 0

        self._diag_pin = DigitalOutputDevice(self.DIAG_GPIO_PIN)

    def _close_hardware(self):
        for name, resource in [("diag", self._diag_pin),
                               ("dac", self._spi_dac),
                               ("adc", self._spi_adc)]:
            if resource is None:
                continue
            try:
                if name == "diag":
                    resource.off()
                    time.sleep(0.1)
                resource.close()
            except Exception:
                logger.debug(f"Error closing {name}", exc_info=True)

        self._diag_pin = None
        self._spi_dac = None
        self._spi_adc = None

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
                    os.sched_setscheduler(0, os.SCHED_OTHER, os.sched_param(0))
                except (PermissionError, AttributeError, OSError):
                    pass
        finally:
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
        # Pre-flight: check for current with strobe off — indicates blown MOSFET or gate driver
        idle_adc = self._read_adc(self.ADC_CH0_CMD)
        if idle_adc > self.PREFLIGHT_CURRENT_THRESHOLD:
            self.status["message"] = f"Current detected with strobe off (ADC CH0={idle_adc}). Likely blown MOSFET or gate driver — check V3 Connector Board."
            return False, -1, -1

        # Phase 1: find safe starting DAC
        dac_start, ldo = self._find_dac_start()

        if dac_start < 0:
            self.status["message"] = f"DAC value of 0 is below minimum LDO voltage ({self.LDO_MIN_V:.2f}V): {ldo:.2f}V. This indicates a problem with the controller board."
            return False, -1, -1

        logger.debug(f"Calibrating: target={target_current}A, dac_start={dac_start:#04x}")

        # Phase 2: sweep from dac_start downward, looking for target crossing
        final_dac = self.DAC_MIN
        crossed = False

        for dac in range(dac_start, self.DAC_MIN - 1, -1):
            if self._cancel_requested:
                logger.info("Calibration cancelled by user")
                return False, -1, -1

            self._set_dac(dac)
            time.sleep(0.1)

            steps_done = dac_start - dac
            total_steps = dac_start - self.DAC_MIN + 1
            if total_steps > 0:
                self.status["progress"] = int(20 + (steps_done / total_steps) * 60)

            ldo = self.get_ldo_voltage()

            if ldo < self.LDO_MIN_V:
                logger.debug(f"LDO {ldo:.2f}V below min at DAC={dac:#04x}, skipping")
                final_dac = dac
                continue

            if ldo > self.LDO_MAX_V:
                self.status["message"] = f"LDO voltage ({ldo:.2f}V) above maximum ({self.LDO_MAX_V}V). Stopping calibration, as something is wrong."
                return False, -1, -1

            led_current = self.get_led_current()
            logger.debug(f"DAC={dac:#04x}, current={led_current:.2f}A")

            if led_current > self.HARD_CAP_CURRENT:
                self.status["message"] = f"LED current ({led_current:.2f}A) exceeds hard cap ({self.HARD_CAP_CURRENT}A). This strongly indicates the LED is shorted."
                return False, -1, -1

            if led_current > target_current:
                logger.debug(f"Crossed target at DAC={dac:#04x} ({led_current:.2f}A)")
                final_dac = dac + 1
                crossed = True
                break

            final_dac = dac

        if not crossed:
            self.status["message"] = f"Reached MIN_DAC without reaching target ({target_current}A). This generally indicates a problem."
            return False, -1, -1
        if final_dac >= self.DAC_MAX:
            self.status["message"] = "MAX_DAC resulted in current above target. This generally indicates a problem."
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

    # ------------------------------------------------------------------
    # Public async API (called from web server endpoints)
    # ------------------------------------------------------------------

    async def start_calibration(self, led_type: str = "v3",
                                target_current: Optional[float] = None,
                                overwrite: bool = False) -> Dict[str, Any]:
        """Run full strobe calibration. Blocking I/O is offloaded to a thread."""

        if self.status.get("state") == "calibrating":
            return {"status": "error", "message": "Calibration already in progress"}

        # Validate board version
        board_version = self.config_manager.get_config("gs_config.strobing.kConnectionBoardVersion")
        if board_version is None or int(board_version) != 3:
            msg = f"Board version must be V3 (got {board_version})"
            self.status = {"state": "error", "progress": 0, "message": msg}
            return {"status": "error", "message": msg}

        # Check for existing calibration
        existing = self.config_manager.get_config(self.DAC_CONFIG_KEY)
        if existing is not None and not overwrite:
            msg = "Calibration already exists. Set overwrite=true to replace."
            self.status = {"state": "error", "progress": 0, "message": msg}
            return {"status": "error", "message": msg}

        # Resolve target
        if target_current is not None:
            target = target_current
        elif led_type == "legacy":
            target = self.LEGACY_TARGET_CURRENT
        else:
            target = self.V3_TARGET_CURRENT

        self._cancel_requested = False
        self.status = {"state": "calibrating", "progress": 0, "message": "Starting calibration"}

        loop = asyncio.get_event_loop()
        try:
            result = await loop.run_in_executor(None, self._run_calibration_sync, target)
            return result
        except Exception as e:
            logger.error(f"Calibration error: {e}")
            self.status = {"state": "error", "progress": 0, "message": str(e)}
            return {"status": "error", "message": str(e)}

    def _run_calibration_sync(self, target: float) -> Dict[str, Any]:
        """Synchronous calibration wrapper — runs in executor thread."""
        try:
            self._open_hardware()

            success, final_dac, led_current = self._calibrate(target)

            if success and final_dac > 0:
                self.config_manager.set_config(self.DAC_CONFIG_KEY, final_dac)
                self.status = {
                    "state": "complete", "progress": 100,
                    "message": f"DAC=0x{final_dac:02X}, current={led_current:.2f}A",
                    "dac_setting": final_dac,
                    "led_current": round(led_current, 2),
                }
                return self.status
            elif self._cancel_requested:
                self.status = {"state": "cancelled", "progress": 0,
                               "message": "Calibration cancelled by user"}
                return self.status
            else:
                self._set_dac(self.SAFE_DAC_VALUE)
                reason = self.status.get("message", "Calibration failed")
                self.status = {"state": "failed", "progress": 0,
                               "message": f"{reason} DAC set to safe fallback."}
                return self.status

        except Exception as e:
            logger.error(f"Calibration exception: {e}")
            self.status = {"state": "error", "progress": 0, "message": str(e)}
            return {"status": "error", "message": str(e)}
        finally:
            self._close_hardware()

    def cancel(self):
        """Request cancellation of a running calibration."""
        self._cancel_requested = True

    def get_status(self) -> Dict[str, Any]:
        """Return a snapshot of the current calibration status."""
        return dict(self.status)

    async def read_diagnostics(self) -> Dict[str, Any]:
        """Read LDO voltage, LED current, and raw ADC values."""
        loop = asyncio.get_event_loop()
        try:
            return await loop.run_in_executor(None, self._read_diagnostics_sync)
        except Exception as e:
            return {"status": "error", "message": str(e)}

    def _read_diagnostics_sync(self) -> Dict[str, Any]:
        try:
            self._open_hardware()

            ldo = self.get_ldo_voltage()
            adc_ch0 = self._read_adc(self.ADC_CH0_CMD)
            adc_ch1 = self._read_adc(self.ADC_CH1_CMD)

            result: Dict[str, Any] = {
                "ldo_voltage": round(ldo, 2),
                "adc_ch0_raw": adc_ch0,
                "adc_ch1_raw": adc_ch1,
            }

            if ldo >= self.LDO_MIN_V:
                led_current = self.get_led_current()
                result["led_current"] = round(led_current, 2)
            else:
                result["led_current"] = None
                result["warning"] = f"LDO unsafe ({ldo:.2f}V) — skipped LED current read"

            return result

        except Exception as e:
            logger.error(f"Diagnostics error: {e}")
            return {"status": "error", "message": str(e)}
        finally:
            self._close_hardware()

    async def set_dac_manual(self, value: int) -> Dict[str, Any]:
        """Set DAC to a specific value and report LDO voltage."""
        if value < self.DAC_MIN or value > self.DAC_MAX:
            return {"status": "error",
                    "message": f"DAC value must be {self.DAC_MIN}-{self.DAC_MAX}"}

        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(None, self._set_dac_manual_sync, value)

    def _set_dac_manual_sync(self, value: int) -> Dict[str, Any]:
        try:
            self._open_hardware()
            self._set_dac(value)
            time.sleep(0.1)
            ldo = self.get_ldo_voltage()

            result: Dict[str, Any] = {
                "status": "success",
                "dac_value": value,
                "ldo_voltage": round(ldo, 2),
            }

            if ldo < self.LDO_MIN_V:
                result["warning"] = f"LDO voltage {ldo:.2f}V is below minimum {self.LDO_MIN_V}V"

            return result
        except Exception as e:
            return {"status": "error", "message": str(e)}
        finally:
            self._close_hardware()

    async def get_dac_start(self) -> Dict[str, Any]:
        """Run the safe-start sweep and return the boundary DAC value."""
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(None, self._get_dac_start_sync)

    def _get_dac_start_sync(self) -> Dict[str, Any]:
        try:
            self._open_hardware()
            dac_start, ldo = self._find_dac_start()

            if dac_start >= 0:
                self._set_dac(dac_start)
                time.sleep(0.1)
                ldo = self.get_ldo_voltage()

            return {
                "dac_start": dac_start,
                "ldo_voltage": round(ldo, 2),
            }
        except Exception as e:
            return {"status": "error", "message": str(e)}
        finally:
            self._close_hardware()

    async def get_saved_settings(self) -> Dict[str, Any]:
        """Read the saved kDAC_setting from config."""
        value = self.config_manager.get_config(self.DAC_CONFIG_KEY)
        return {"dac_setting": value}

"""Strobe Calibration Manager for PiTrac Web Server

Controls MCP4801 DAC and MCP3202 ADC over SPI1 to calibrate the IR strobe
LED current on the V3 Connector Board.
"""

import asyncio
import gc
import logging
import os
import subprocess
import time
from typing import Any, Dict, Optional

logger = logging.getLogger(__name__)

try:
    import spidev
except ImportError:
    spidev = None

try:
    os.environ.setdefault('LG_WD', '/tmp')
    from gpiozero import DigitalOutputDevice
except ImportError:
    DigitalOutputDevice = None

try:
    import serial  # type: ignore
except ImportError:
    serial = None


def _pico_setting(config_manager, key: str, env_var: str, default: str) -> str:
    """Return the effective value for a Pico-bridge setting.

    Reads the live UI value via config_manager first; falls back to the env var
    that pitrac_manager.py would have used; finally falls back to the default.
    The UI value is authoritative because the user can flip the Pico bridge
    knob from the web UI without a process restart, while os.environ is fixed
    at web-server-process startup.
    """
    if config_manager is not None:
        try:
            value = config_manager.get_config(key)
            if value is not None and value != "":
                return str(value).lower() if key.endswith(".enabled") else str(value)
        except Exception:
            pass
    return os.environ.get(env_var, default).lower() if env_var.endswith("_ENABLED") \
        else os.environ.get(env_var, default)


def _pico_enabled(config_manager=None) -> bool:
    """Return True if the Pico bridge should own the strobe-hold signal.

    Honours gs_config.pico.enabled / PITRAC_PICO_ENABLED in
    {auto, required, legacy} (default auto). The full auto-probe semantics
    live in the C++ layer; here we treat auto as a soft enable that lets the
    serial open fail gracefully and fall back to legacy.
    """
    val = _pico_setting(config_manager, "gs_config.pico.enabled",
                        "PITRAC_PICO_ENABLED", "auto")
    return val in ("required", "auto")


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

    # Failure fallback. DAC is inverse (higher code = less current), so DAC_MAX = min current.
    SAFE_DAC_VALUE = DAC_MAX

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

    def __init__(self, config_manager, pico_lock: Optional[asyncio.Lock] = None,
                 serial_owner=None, pitrac_manager=None):
        self.config_manager = config_manager
        # When pitrac_lm runs it owns the strobe hardware (the Pico CDC in Pico
        # mode, BCM 10 DIAG in legacy), so a calibration sweep must stand down
        # rather than fire the strobe and grab the port out from under it.
        self._pitrac_manager = pitrac_manager
        # Shared across both managers so concurrent /api/calibration and
        # /api/pico requests don't fight over /dev/ttyACM0. Constructed on
        # demand if none was injected — keeps the existing test wiring valid.
        self._pico_lock = pico_lock or asyncio.Lock()
        # The single /dev/ttyACM0 holder shared with PicoManager. When present
        # we borrow its handle instead of opening our own exclusive fd, so a
        # mic-page visit no longer poisons the next calibration's serial open.
        self._serial_owner = serial_owner

        self._spi_dac = None
        self._spi_adc = None
        self._diag_pin = None
        self._serial = None  # Pico USB-CDC handle (replaces _diag_pin in Pico mode)

        self._cancel_requested = False
        self._dac_applied = False

        self.status: Dict[str, Any] = {
            "state": "idle",
            "progress": 0,
            "message": "",
        }

    # ------------------------------------------------------------------
    # Hardware lifecycle
    # ------------------------------------------------------------------

    def _lm_is_running(self) -> bool:
        """True when pitrac_lm holds the strobe hardware; calibration defers to it."""
        if self._pitrac_manager is None:
            return False
        try:
            return bool(self._pitrac_manager.is_running())
        except Exception:
            return False

    def _open_hardware(self):
        # The launch monitor owns the strobe + the CDC port while it runs; a
        # sweep here would fire the strobe mid-round and fight it for the device.
        # Every calibration entry point funnels through _open_hardware and
        # catches this, so one guard covers them all.
        if self._lm_is_running():
            raise RuntimeError(
                "pitrac_lm is running; stop the launch monitor before calibrating the strobe")
        if spidev is None:
            raise RuntimeError("spidev library not available -- not running on a Raspberry Pi?")

        saved_cwd = os.getcwd()

        self._spi_dac = spidev.SpiDev()
        self._spi_dac.open(self.SPI_BUS, self.SPI_DAC_DEVICE)
        self._spi_dac.max_speed_hz = self.SPI_MAX_SPEED_HZ
        self._spi_dac.mode = 0

        self._spi_adc = spidev.SpiDev()
        self._spi_adc.open(self.SPI_BUS, self.SPI_ADC_DEVICE)
        self._spi_adc.max_speed_hz = self.SPI_MAX_SPEED_HZ
        self._spi_adc.mode = 0

        # In Pico mode the strobe pulse flows over USB-CDC via the
        # existing safe FIRE command (microsecond pulse train, same duty
        # cycle as a normal shot). Legacy DigitalOutputDevice(BCM 10) is the
        # documented fallback when Pico is disabled.
        if _pico_enabled(self.config_manager) and serial is not None:
            device = _pico_setting(self.config_manager, "gs_config.pico.device",
                                   "PITRAC_PICO_DEVICE", "/dev/ttyACM0")
            try:
                if self._serial_owner is not None:
                    self._serial = self._serial_owner.open()
                else:
                    self._serial = serial.Serial(device, 115200, timeout=2, exclusive=True)
                self._diag_pin = None
                logger.info(
                    "Strobe calibration: Pico bridge OPEN on %s (gate=%s)",
                    device,
                    _pico_setting(self.config_manager, "gs_config.pico.enabled",
                                  "PITRAC_PICO_ENABLED", "auto"),
                )
                # Lower the min-inter-shot floor so the calibration sweep doesn't
                # wait 200 ms between DAC steps. 20 ms is the firmware floor.
                # _close_hardware restores 200 ms.
                try:
                    self._serial.write(b"CFG MIN_INTER_SHOT_MS=20\n")
                    self._serial.flush()
                except Exception:
                    logger.warning("Could not lower MIN_INTER_SHOT_MS", exc_info=True)
                os.chdir(saved_cwd)
                return
            except (serial.SerialException, FileNotFoundError, OSError) as e:
                gate = _pico_setting(self.config_manager, "gs_config.pico.enabled",
                                     "PITRAC_PICO_ENABLED", "auto")
                if gate == "required":
                    logger.error("Pico open REQUIRED but failed on %s: %s", device, e)
                    raise  # explicit user requirement; surface the error
                logger.info("Pico open failed on %s (%s); falling back to legacy GPIO 10", device, e)
                self._serial = None
        else:
            logger.info("Strobe calibration: using LEGACY GPIO 10 path (pico bridge not enabled)")

        if DigitalOutputDevice is None:
            raise RuntimeError("gpiozero library not available -- not running on a Raspberry Pi?")
        self._diag_pin = DigitalOutputDevice(self.DIAG_GPIO_PIN)

        os.chdir(saved_cwd)

    def _close_hardware(self):
        # Release the Pico serial handle first; restore the production
        # min-inter-shot floor and clear any stale strobe-hold flag.
        if self._serial is not None:
            for cmd in (b"CFG STROBE_HOLD=0\n", b"CFG MIN_INTER_SHOT_MS=200\n"):
                try:
                    self._serial.write(cmd)
                    self._serial.flush()
                except Exception:
                    logger.debug("Error restoring Pico state on close", exc_info=True)
            if self._serial_owner is not None:
                # Hand the borrow back; the owner closes the fd once no other
                # component holds it. Closing it ourselves would yank the port
                # out from under a concurrent mic stream.
                self._serial_owner.release()
            else:
                try:
                    self._serial.close()
                except Exception:
                    logger.debug("Error closing serial", exc_info=True)
            self._serial = None

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

        # gpiozero leaves GPIO10 in GPIO mode after close(); restore SPI0 MOSI.
        try:
            result = subprocess.run(
                ["pinctrl", "set", str(self.DIAG_GPIO_PIN), "a0"],
                capture_output=True, text=True, timeout=5,
            )
            if result.returncode != 0:
                logger.warning("pinctrl restore SPI0 MOSI: %s", result.stderr.strip())
        except FileNotFoundError:
            logger.warning("pinctrl not found — reboot required to restore SPI0")
        except Exception:
            logger.warning("Failed to restore SPI0 MOSI", exc_info=True)

    def _park_dac_safe(self) -> None:
        """Set the DAC to min current so an aborted sweep can't leave the LEDs at a
        high-current code. The SPI handle may be gone on the exception path."""
        if self._spi_dac is None:
            return
        try:
            self._set_dac(self.SAFE_DAC_VALUE)
        except Exception:
            logger.warning("Failed to park DAC at safe (min-current) value", exc_info=True)

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

    def get_led_current(self) -> Optional[float]:
        """Read LED current sense at the current DAC value.

        Two paths:
          * Pico mode: send FIRE so the Pico fires its configured pulse train
            (microsecond pulses, ~21 ms total window) and oversample the ADC
            CH0 during the train. The peak ADC reading represents the LED
            current during a pulse. Duty cycle matches a normal shot --
            inherently safe for the IR LEDs.
          * Legacy mode: pulse DIAG via GPIO 10 across a single xfer2, same
            ~100 us LED-on duration the algorithm has always assumed.
        """
        msg = [0x01, self.ADC_CH0_CMD, 0x00]
        spi = self._spi_adc
        diag = self._diag_pin
        ser = self._serial

        if ser is not None:
            return self._get_led_current_pico(spi, ser, msg)

        # Legacy diag.on/off path: SCHED_FIFO + GC-off for deterministic
        # short-pulse timing.
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
        current_a = (3.3 / 4096) * adc_value * 10.0
        logger.info(
            "get_led_current: path=legacy resp=[%#x,%#x,%#x] adc=%d current=%.3fA",
            response[0], response[1], response[2], adc_value, current_a,
        )
        return current_a

    # Number of FIRE_PEAK measurements averaged per DAC step. Single-sample
    # peaks have ~10-15% noise from ADC sample-timing alignment, boost rail
    # transients, and pulse-edge ringing. Taking the median of 3 reads
    # collapses that to <2% while still completing each DAC step in ~250 ms.
    PICO_PEAK_REPEATS = 3

    def _get_led_current_pico(self, spi, ser, msg) -> Optional[float]:
        """Ask the Pico to fire a few pulse trains and report median peak ADC.

        The Pico's onboard ADC samples GP26 (wired to V3 CUR-SENSE / TP4)
        with single-cycle PIO determinism during the strobe train, completely
        sidestepping USB-CDC and Python timing jitter. We fire FIRE_PEAK
        `PICO_PEAK_REPEATS` times and take the median peak so a single noise
        spike (pulse-edge ringing, boost rail transient, ADC sample landing
        on an edge) doesn't trip HARD_CAP_CURRENT.
        """
        peaks = []
        samples_total = 0
        for _ in range(self.PICO_PEAK_REPEATS):
            adc_v, samples = self._fire_peak_once(ser)
            if adc_v > 0:
                peaks.append(adc_v)
                samples_total += samples

        if not peaks:
            # No telemetry (serial drop / Pico not replying). Return None, NOT 0.0 --
            # a fake 0 A reads as "below target" and the sweep would keep raising current.
            logger.warning("get_led_current: no PEAK replies from Pico")
            return None

        peaks.sort()
        median_adc = peaks[len(peaks) // 2]

        # Pico ADC is 12-bit at 3.3 V reference. Same 0.1 ohm shunt + 10x
        # scaling as the legacy MCP3202 reading on Pi-side.
        current_a = (3.3 / 4096) * median_adc * 10.0
        logger.info(
            "get_led_current: path=pico reads=%d peaks=%s median_adc=%d current=%.3fA",
            len(peaks), peaks, median_adc, current_a,
        )
        return current_a

    def _fire_peak_once(self, ser) -> tuple:
        """Send one FIRE_PEAK, parse the EVENT PEAK reply, return (adc, samples).

        Returns (0, 0) if the reply doesn't arrive within the deadline or
        the line can't be parsed. Filtering of "no reply" cases is the
        caller's job.
        """
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
        try:
            ser.write(b"FIRE_PEAK\n")
            ser.flush()
        except Exception:
            logger.warning("_fire_peak_once: write failed", exc_info=True)
            return (0, 0)

        deadline = time.monotonic() + 0.150
        buf = b""
        while time.monotonic() < deadline:
            chunk = ser.read(256)
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, _, buf = buf.partition(b"\n")
                    line_s = line.decode(errors="replace").strip()
                    if line_s.startswith("EVENT PEAK"):
                        adc = 0
                        samples = 0
                        for kv in line_s.split():
                            if kv.startswith("adc="):
                                try: adc = int(kv.split("=", 1)[1])
                                except ValueError: pass
                            elif kv.startswith("samples="):
                                try: samples = int(kv.split("=", 1)[1])
                                except ValueError: pass
                        return (adc, samples)
        return (0, 0)

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
            if self._cancel_requested:
                return -1, 0.0

            self._set_dac(i)
            time.sleep(0.1)
            ldo = self.get_ldo_voltage()
            logger.debug(f"DAC={i:#04x}, LDO={ldo:.2f}V")

            self.status["progress"] = int((i / self.DAC_MAX) * 20)
            self.status["message"] = f"Finding safe start point... DAC {i}/{self.DAC_MAX}"

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

        logger.info(f"Calibrating: target={target_current}A, dac_start={dac_start:#04x}")

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
            if led_current is None:
                self.status["message"] = "Lost current telemetry from the Pico mid-sweep. Aborting so the DAC isn't driven blind."
                return False, -1, -1
            logger.info(f"DAC={dac:#04x}, current={led_current:.2f}A")

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
                reading = self.get_led_current()
                if reading is None:
                    self.status["message"] = "Lost current telemetry from the Pico during averaging. Aborting."
                    return False, -1, -1
                current_sum += reading
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
            async with self._pico_lock:
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
                ldo = self.get_ldo_voltage()
                self.config_manager.set_config(self.DAC_CONFIG_KEY, final_dac)
                self._dac_applied = True
                self.status = {
                    "state": "complete", "progress": 100,
                    "message": f"DAC=0x{final_dac:02X}, current={led_current:.2f}A",
                    "dac_setting": final_dac,
                    "led_current": round(led_current, 2),
                    "ldo_voltage": round(ldo, 2),
                }
                return self.status
            elif self._cancel_requested:
                self._park_dac_safe()
                self.status = {"state": "cancelled", "progress": 0,
                               "message": "Calibration cancelled by user"}
                return self.status
            else:
                self._park_dac_safe()
                reason = self.status.get("message", "Calibration failed")
                self.status = {"state": "failed", "progress": 0,
                               "message": f"{reason} DAC set to safe fallback."}
                return self.status

        except Exception as e:
            logger.error(f"Calibration exception: {e}")
            self._park_dac_safe()
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
            async with self._pico_lock:
                return await loop.run_in_executor(None, self._read_diagnostics_sync)
        except Exception as e:
            return {"status": "error", "message": str(e)}

    def _read_diagnostics_sync(self) -> Dict[str, Any]:
        try:
            # Gate on board version
            board_version = self.config_manager.get_config(
                "gs_config.strobing.kConnectionBoardVersion"
            )
            if board_version is None or int(board_version) != 3:
                return {"status": "error", "message": "Diagnostics only available on V3 boards"}

            self._open_hardware()

            # Set calibrated DAC value before pulsing DIAG — without this,
            # the current regulator has no reference and LED current is unregulated
            dac_value = self.config_manager.get_config(self.DAC_CONFIG_KEY)
            if dac_value is not None and int(dac_value) > 0:
                self._set_dac(int(dac_value))
                time.sleep(0.05)
            else:
                logger.warning("No calibrated DAC value — skipping LED current measurement")

            ldo = self.get_ldo_voltage()
            adc_ch0 = self._read_adc(self.ADC_CH0_CMD)
            adc_ch1 = self._read_adc(self.ADC_CH1_CMD)

            result: Dict[str, Any] = {
                "ldo_voltage": round(ldo, 2),
                "adc_ch0_raw": adc_ch0,
                "adc_ch1_raw": adc_ch1,
            }

            if dac_value is None or int(dac_value) <= 0:
                result["led_current"] = None
                result["warning"] = "No DAC calibration — LED current measurement skipped to protect hardware"
            elif ldo >= self.LDO_MIN_V:
                led_current = self.get_led_current()
                if led_current is None:
                    result["led_current"] = None
                    result["warning"] = "No current telemetry from the Pico"
                else:
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
        async with self._pico_lock:
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
        async with self._pico_lock:
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

    def is_strobe_safe(self) -> Dict[str, Any]:
        """Check if the system is safe to fire strobes.

        Returns a dict with 'safe' (bool), 'reason' (str if unsafe),
        and 'board_version' (int or None).
        """
        board_version = self.config_manager.get_config(
            "gs_config.strobing.kConnectionBoardVersion"
        )

        # V1/V2 boards use fixed resistors — always safe
        if board_version is None or int(board_version) != 3:
            return {"safe": True, "board_version": board_version}

        dac_value = self.config_manager.get_config(self.DAC_CONFIG_KEY)
        if dac_value is None or int(dac_value) < 0:
            return {
                "safe": False,
                "board_version": 3,
                "reason": "V3 board requires strobe calibration before use. "
                          "Run strobe calibration from the Calibration page.",
            }

        if not self._dac_applied:
            return {
                "safe": False,
                "board_version": 3,
                "reason": "V3 DAC not yet initialized. Restart the web server.",
            }

        return {"safe": True, "board_version": 3, "dac_setting": int(dac_value)}

    async def get_saved_settings(self) -> Dict[str, Any]:
        """Read the saved kDAC_setting from config."""
        value = self.config_manager.get_config(self.DAC_CONFIG_KEY)
        return {"dac_setting": value}

    def apply_dac_setting(self) -> bool:
        """Write the saved calibrated DAC value to hardware via SPI1.

        Must be called on boot before any strobe fires. Without this,
        the MCP4801 powers up in shutdown (high-Z) and the current regulator
        has no reference — strobing in that state blows the MOSFET.

        Returns True if the DAC was successfully set, False otherwise.
        """
        board_version = self.config_manager.get_config(
            "gs_config.strobing.kConnectionBoardVersion"
        )
        if board_version is None or int(board_version) != 3:
            logger.debug("Not a V3 board, skipping DAC initialization")
            return True

        dac_value = self.config_manager.get_config(self.DAC_CONFIG_KEY)
        if dac_value is None or int(dac_value) < 0:
            logger.warning(
                "V3 board detected but no DAC calibration found. "
                "Strobe will be BLOCKED until calibration is run."
            )
            return False

        dac_value = int(dac_value)

        if spidev is None:
            logger.warning("spidev not available, cannot set DAC")
            return False

        try:
            spi = spidev.SpiDev()
            spi.open(self.SPI_BUS, self.SPI_DAC_DEVICE)
            spi.max_speed_hz = self.SPI_MAX_SPEED_HZ
            spi.mode = 0

            msb = self.MCP4801_WRITE_CMD | ((dac_value >> 4) & 0x0F)
            lsb = (dac_value << 4) & 0xF0
            spi.xfer2([msb, lsb])

            spi.close()
            self._dac_applied = True
            logger.info(
                f"V3 DAC initialized to calibrated value 0x{dac_value:02X}"
            )
            return True
        except Exception as e:
            logger.error(f"Failed to initialize V3 DAC: {e}")
            return False

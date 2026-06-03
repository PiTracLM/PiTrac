from __future__ import annotations

import asyncio
import logging
import shutil
import time
from pathlib import Path
from typing import Any, AsyncIterator, Callable, Dict, Optional

logger = logging.getLogger(__name__)

try:
    import serial  # type: ignore
except ImportError:
    serial = None  # type: ignore


DEFAULT_DEVICE = "/dev/ttyACM0"
BAUDRATE = 115200
DEFAULT_DEADLINE_S = 0.5
SELFTEST_DEADLINE_S = 3.0
READ_CHUNK = 256

THRESHOLD_MIN = 0
THRESHOLD_MAX = 1_000_000_000
MIN_INTER_SHOT_FLOOR_MS = 20
MIN_INTER_SHOT_CEIL_MS = 60_000
# Firmware clamps to 1..200 ms (impact_detect.c).
DECAY_CONFIRM_MIN_MS = 1
DECAY_CONFIRM_MAX_MS = 200
STREAM_RMS_MAX_HZ = 100

# Mirrors the strobe grammar in pico/include/config.h: PULSE_WIDTH_US float capped
# at STROBE_MAX_PULSE_WIDTH_US; interval vector capped at STROBE_MAX_PULSES entries,
# each <= STROBE_MAX_INTERVAL_MS.
PULSE_WIDTH_US_MIN = 0.0  # exclusive; firmware rejects v <= 0
PULSE_WIDTH_US_MAX = 500.0
PULSE_INTERVALS_MAX = 32
INTERVAL_MS_MAX = 1000.0

# UF2 magic: little-endian 0x0A324655 ("UF2\n") at offset 0.
UF2_MAGIC = b"\x55\x46\x32\x0a"
UF2_MIN_BYTES = 512
MAX_UF2_BYTES = 8 * 1024 * 1024
UPLOAD_CHUNK_BYTES = 64 * 1024

# Gates the bundled-firmware filename so a stray target can't become path traversal.
VALID_TARGETS = frozenset({"pico", "pico_w", "pico2", "pico2_w"})

# pitrac_lm drives the Pico over /dev/ttyACM0 without O_EXCL, so this app-level gate
# (not the OS) is the real arbiter: web transactions stand down with this message.
LM_OWNS_PORT_MSG = "pitrac_lm is running; the Pico is owned by the launch monitor"


def is_valid_uf2(data: bytes) -> bool:
    """Return True if ``data`` looks like a UF2 image (magic + plausible size)."""
    return len(data) >= UF2_MIN_BYTES and data[:4] == UF2_MAGIC


class PicoSerialOwner:
    """Single holder of the /dev/ttyACM0 handle, borrowed by every Pico component
    (mic tuning, flashing, strobe calibration).

    pyserial opens with exclusive=True; before this owner, the calibration manager
    and mic page each kept their own exclusive fd and clobbered each other. One
    handle, opened lazily on first borrow and closed when no borrow is outstanding.
    """

    def __init__(self, device_resolver: Callable[[], str]):
        self._device_resolver = device_resolver
        self.handle: Any = None
        self._borrows = 0

    def open(self) -> Any:
        if self.handle is None:
            if serial is None:
                raise RuntimeError("pyserial not installed; cannot reach the Pico")
            path = self._device_resolver()
            self.handle = serial.Serial(path, BAUDRATE, timeout=1, exclusive=True)
            logger.info("PicoSerialOwner: opened %s", path)
        self._borrows += 1
        return self.handle

    def release(self) -> None:
        """Drop one borrow; close once nobody holds it."""
        if self._borrows > 0:
            self._borrows -= 1
        if self._borrows == 0:
            self.close()

    def close(self) -> None:
        self._borrows = 0
        if self.handle is not None:
            try:
                self.handle.close()
            except Exception:
                logger.debug("PicoSerialOwner: close error", exc_info=True)
            self.handle = None


class PicoManager:
    def __init__(
        self,
        config_manager: Any,
        lock: asyncio.Lock,
        serial_owner: Optional[PicoSerialOwner] = None,
        pitrac_manager: Optional[Any] = None,
    ):
        self._config_manager = config_manager
        self._lock = lock
        self.serial_owner = serial_owner or PicoSerialOwner(self._device_path)
        self._stream_active = False
        # Set whenever no RMS stream is running. stop_rms_stream() awaits this so a
        # caller handing the port to pitrac_lm knows the stream has truly released
        # it (CFG STREAM_RMS=0 sent, borrow dropped). Otherwise a lingering EVENT RMS
        # line poisons the LM's STATUS handshake and it falls back to legacy strobe.
        self._stream_closed = asyncio.Event()
        self._stream_closed.set()
        # When pitrac_lm runs it owns the port, so /pico background pollers stand down.
        self._pitrac_manager = pitrac_manager

    def _device_path(self) -> str:
        if self._config_manager is None:
            return DEFAULT_DEVICE
        try:
            value = self._config_manager.get_config("gs_config.pico.device")
            if value:
                return str(value)
        except Exception:
            pass
        return DEFAULT_DEVICE

    def _lm_is_running(self) -> bool:
        """True when pitrac_lm holds the port; the web defers to it."""
        if self._pitrac_manager is None:
            return False
        try:
            return bool(self._pitrac_manager.is_running())
        except Exception:
            return False

    def _persist(self, key: str, value: Any) -> None:
        """Persist a tuned DSP value so the LM can re-push it to a power-cycled
        Pico, which otherwise reverts to compiled defaults."""
        if self._config_manager is None:
            return
        try:
            self._config_manager.set_config(key, value)
        except Exception:
            logger.debug("failed to persist %s", key, exc_info=True)

    def close(self) -> None:
        self.serial_owner.close()

    def _write_line(self, ser: Any, line: str) -> None:
        payload = line if line.endswith("\n") else line + "\n"
        ser.write(payload.encode("ascii"))
        ser.flush()

    def _read_until(
        self,
        ser: Any,
        predicate: Callable[[str], bool],
        deadline_s: float,
    ) -> Optional[str]:
        deadline = time.monotonic() + deadline_s
        buf = b""
        while time.monotonic() < deadline:
            chunk = ser.read(READ_CHUNK)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, _, buf = buf.partition(b"\n")
                text = line.decode("ascii", errors="replace").strip()
                if predicate(text):
                    return text
        return None

    @staticmethod
    def _parse_status_line(line: str) -> Dict[str, Any]:
        out: Dict[str, Any] = {"raw": line}
        if not line.startswith("STATUS"):
            return out
        for token in line.split()[1:]:
            if "=" not in token:
                continue
            key, _, value = token.partition("=")
            if key == "intervals":
                vals = []
                for piece in value.split(","):
                    piece = piece.strip()
                    if not piece:
                        continue
                    try:
                        vals.append(float(piece))
                    except ValueError:
                        pass
                out["intervals"] = vals
                continue
            out[key] = _coerce_scalar(value)
        return out

    async def probe(self) -> Dict[str, Any]:
        async with self._lock:
            return await asyncio.to_thread(self._probe_sync)

    def _probe_sync(self) -> Dict[str, Any]:
        if self._lm_is_running():
            return {"present": False, "lm_running": True, "error": LM_OWNS_PORT_MSG}
        try:
            ser = self.serial_owner.open()
        except (RuntimeError, OSError) as exc:
            return {"present": False, "error": str(exc)}
        try:
            ser.reset_input_buffer()
            self._write_line(ser, "STATUS")
            line = self._read_until(
                ser, lambda t: t.startswith("STATUS"), DEFAULT_DEADLINE_S
            )
            if line is None:
                return {"present": False, "error": "no STATUS reply"}
            data = self._parse_status_line(line)
            data["present"] = True
            data["device"] = self._device_path()
            return data
        finally:
            self.serial_owner.release()

    async def status(self) -> Dict[str, Any]:
        async with self._lock:
            return await asyncio.to_thread(self._status_sync)

    def _status_sync(self) -> Dict[str, Any]:
        if self._lm_is_running():
            return {"present": False, "lm_running": True, "error": LM_OWNS_PORT_MSG}
        try:
            ser = self.serial_owner.open()
        except (RuntimeError, OSError) as exc:
            return {"present": False, "error": str(exc)}
        try:
            ser.reset_input_buffer()
            self._write_line(ser, "STATUS")
            line = self._read_until(
                ser, lambda t: t.startswith("STATUS"), DEFAULT_DEADLINE_S
            )
            if line is None:
                return {"present": False, "error": "no STATUS reply"}
            data = self._parse_status_line(line)
            data["present"] = True
            data["device"] = self._device_path()
            return data
        except OSError as exc:
            # finally releases only this borrow; port stays open if the RMS stream still holds it.
            return {"present": False, "error": str(exc)}
        finally:
            self.serial_owner.release()

    async def selftest(self) -> Dict[str, Any]:
        async with self._lock:
            return await asyncio.to_thread(self._selftest_sync)

    def _selftest_sync(self) -> Dict[str, Any]:
        if self._lm_is_running():
            return {"ok": False, "error": LM_OWNS_PORT_MSG}
        try:
            ser = self.serial_owner.open()
        except (RuntimeError, OSError) as exc:
            return {"ok": False, "error": str(exc)}
        try:
            ser.reset_input_buffer()
            self._write_line(ser, "SELFTEST")
            result = self._read_until(
                ser,
                lambda t: t.startswith("SELFTEST"),
                SELFTEST_DEADLINE_S,
            )
            if result is None:
                return {"ok": False, "error": "no SELFTEST reply"}
            out: Dict[str, Any] = {"ok": True, "raw": result}
            for token in result.split()[1:]:
                if "=" not in token:
                    continue
                key, _, value = token.partition("=")
                out[key] = _coerce_scalar(value)
            return out
        except OSError as exc:
            return {"ok": False, "error": str(exc)}
        finally:
            self.serial_owner.release()

    async def set_threshold(self, value: int) -> Dict[str, Any]:
        if not isinstance(value, int) or value < THRESHOLD_MIN or value > THRESHOLD_MAX:
            raise ValueError(f"threshold out of range: {value!r}")
        result = await self._send_cfg(f"MIC_THRESHOLD={value}")
        if result.get("threshold") == value:
            self._persist("gs_config.pico.mic_threshold", value)
        return result

    async def set_armed(self, armed: bool) -> Dict[str, Any]:
        """Arm/disarm, confirming via the STATUS echo that firmware took it.

        Firmware refuses ARMED=1 when the room exceeds mic_threshold /
        DSP_ARM_QUIET_FACTOR, so a bare ACK can show armed while the device isn't.
        """
        want = 1 if armed else 0
        result = await self._send_cfg(f"ARMED={want}")
        echoed = result.get("armed")
        if echoed is not None and echoed != want:
            result["ok"] = False
            result.setdefault(
                "error",
                "arm refused — raise the threshold (room too loud relative to it)"
                if want else "firmware did not disarm",
            )
        return result

    async def set_min_inter_shot(self, ms: int) -> Dict[str, Any]:
        if not isinstance(ms, int):
            raise ValueError(f"min_inter_shot must be int: {ms!r}")
        clamped = max(MIN_INTER_SHOT_FLOOR_MS, min(ms, MIN_INTER_SHOT_CEIL_MS))
        return await self._send_cfg(f"MIN_INTER_SHOT_MS={clamped}")

    async def set_decay_confirm(self, ms: int) -> Dict[str, Any]:
        """Set the impact decay-confirm window (ms). Shorter = lower hit->strobe
        latency but less margin against impulsive false triggers (claps, turf).
        Firmware clamps to [1, 200]; confirm via STATUS echo, then persist.
        """
        if isinstance(ms, bool) or not isinstance(ms, int):
            raise ValueError(f"decay_confirm_ms must be int: {ms!r}")
        if ms < DECAY_CONFIRM_MIN_MS or ms > DECAY_CONFIRM_MAX_MS:
            raise ValueError(
                f"decay_confirm_ms out of range "
                f"[{DECAY_CONFIRM_MIN_MS}, {DECAY_CONFIRM_MAX_MS}]: {ms}"
            )
        result = await self._send_cfg(f"DECAY_CONFIRM_MS={ms}")
        echoed = result.get("decay_confirm_ms")
        if echoed is not None and echoed != ms:
            result["ok"] = False
            result.setdefault("error", "firmware did not accept the decay-confirm window")
        elif echoed == ms:
            self._persist("gs_config.pico.decay_confirm_ms", ms)
        return result

    async def set_pulse_width_us(self, value: float) -> Dict[str, Any]:
        """Set strobe pulse width, confirming via the STATUS echo (pulse_us).

        Firmware treats PULSE_WIDTH_US as a float in (0, STROBE_MAX_PULSE_WIDTH_US]
        and silently keeps the prior value when out of range.
        """
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"pulse_width_us must be a number: {value!r}")
        if value <= PULSE_WIDTH_US_MIN or value > PULSE_WIDTH_US_MAX:
            raise ValueError(
                f"pulse_width_us out of range "
                f"({PULSE_WIDTH_US_MIN}, {PULSE_WIDTH_US_MAX}]: {value}"
            )
        result = await self._send_cfg(f"PULSE_WIDTH_US={_format_interval(value)}")
        echoed = result.get("pulse_us")
        if not _scalar_matches(echoed, float(value)):
            result["ok"] = False
            result.setdefault("error", "firmware did not accept the pulse width")
        return result

    async def set_pulse_intervals(self, intervals: list) -> Dict[str, Any]:
        """Push the strobe interval vector (CFG PULSE_INTERVALS=<comma floats>).

        Must be non-empty, within the length cap, and have unique consecutive
        ratios so each gap is distinguishable on playback (a repeated ratio is
        ambiguous to decode). Confirm via STATUS echo; firmware re-validates too.
        """
        values = _validate_intervals(intervals)
        encoded = ",".join(_format_interval(v) for v in values)
        result = await self._send_cfg(f"PULSE_INTERVALS={encoded}")
        echoed = result.get("intervals")
        if not _intervals_match(echoed, values):
            result["ok"] = False
            result.setdefault("error", "firmware did not accept the interval list")
        return result

    async def start_rms_stream(self, hz: int) -> AsyncIterator[Dict[str, int]]:
        clamped = max(1, min(int(hz), STREAM_RMS_MAX_HZ))
        return self._rms_iter(clamped)

    async def _rms_iter(self, hz: int) -> AsyncIterator[Dict[str, int]]:
        # Handle stays borrowed for the stream's life (no port churn); lock is held
        # only around each read/write, so between reads other transactions can grab
        # the lock and tune the very mic this chart displays.
        if self._lm_is_running():
            # LM owns the port; yield nothing so the stream closes cleanly.
            logger.info("RMS stream not started: pitrac_lm is running")
            return
        self._stream_closed.clear()
        async with self._lock:
            ser = await asyncio.to_thread(self.serial_owner.open)
            try:
                await asyncio.to_thread(ser.reset_input_buffer)
            except Exception:
                pass
            self._stream_active = True
            await asyncio.to_thread(self._write_line, ser, f"CFG STREAM_RMS={hz}")

        buf = b""
        try:
            while self._stream_active:
                async with self._lock:
                    try:
                        chunk = await asyncio.to_thread(ser.read, READ_CHUNK)
                    except OSError:
                        # Device dropped mid-stream; end cleanly so SSE closes instead of raising.
                        logger.info("RMS stream read failed; stopping", exc_info=True)
                        break
                if not chunk:
                    await asyncio.sleep(0)
                    continue
                buf += chunk
                while b"\n" in buf:
                    line, _, buf = buf.partition(b"\n")
                    text = line.decode("ascii", errors="replace").strip()
                    if not text.startswith("EVENT RMS"):
                        continue
                    parsed = _parse_rms_event(text)
                    if parsed is not None:
                        yield parsed
        finally:
            self._stream_active = False
            async with self._lock:
                try:
                    await asyncio.to_thread(self._write_line, ser, "CFG STREAM_RMS=0")
                except Exception:
                    logger.debug("RMS stop write failed", exc_info=True)
                self.serial_owner.release()
            self._stream_closed.set()

    async def stop_rms_stream(self, timeout_s: float = 2.0) -> None:
        """Signal the stream to stop and wait until it has actually released the
        port (CFG STREAM_RMS=0 sent, borrow dropped). Callers that then hand the
        port to pitrac_lm depend on this completing so the LM's STATUS handshake
        isn't poisoned by a trailing EVENT RMS line."""
        self._stream_active = False
        try:
            await asyncio.wait_for(self._stream_closed.wait(), timeout=timeout_s)
        except asyncio.TimeoutError:
            logger.warning("RMS stream did not stop within %.1fs", timeout_s)

    async def _send_cfg(self, suffix: str) -> Dict[str, Any]:
        async with self._lock:
            return await asyncio.to_thread(self._send_cfg_sync, suffix)

    def _send_cfg_sync(self, suffix: str) -> Dict[str, Any]:
        if self._lm_is_running():
            return {"ok": False, "error": LM_OWNS_PORT_MSG}
        try:
            ser = self.serial_owner.open()
        except (RuntimeError, OSError) as exc:
            return {"ok": False, "error": str(exc)}
        try:
            ser.reset_input_buffer()
            self._write_line(ser, f"CFG {suffix}")
            ser.flush()
            self._write_line(ser, "STATUS")
            line = self._read_until(
                ser, lambda t: t.startswith("STATUS"), DEFAULT_DEADLINE_S
            )
            if line is None:
                return {"ok": False, "error": "no STATUS reply"}
            data = self._parse_status_line(line)
            data["ok"] = True
            return data
        except OSError as exc:
            return {"ok": False, "error": str(exc)}
        finally:
            self.serial_owner.release()


    async def flash(
        self,
        uf2_path: str,
        on_progress: Optional[Callable[[str], None]] = None,
    ) -> Dict[str, Any]:
        # Flashing reboots into BOOTSEL -- catastrophic mid-round if the LM is driving it.
        if self._lm_is_running():
            return {"ok": False, "error": LM_OWNS_PORT_MSG}
        if shutil.which("picotool") is None:
            raise RuntimeError(
                "picotool not installed; rerun packaging/build.sh dev"
            )

        async with self._lock:
            sent_bootsel = False
            try:
                ser = await asyncio.to_thread(self.serial_owner.open)
                await asyncio.to_thread(self._write_line, ser, "BOOTSEL")
                sent_bootsel = True
                if on_progress is not None:
                    on_progress("sent BOOTSEL over CDC, waiting for re-enumeration")
            except Exception as exc:
                if on_progress is not None:
                    on_progress(f"BOOTSEL via CDC failed ({exc}); will try picotool reboot")

            self.serial_owner.close()
            await asyncio.sleep(2.5)

            if not sent_bootsel:
                try:
                    await self._picotool(["reboot", "-f", "-u"], on_progress)
                    await asyncio.sleep(1.5)
                except Exception:
                    pass

            ok = await self._picotool(["load", "-f", "-x", uf2_path], on_progress)
            await asyncio.sleep(2)
            return {"ok": ok, "uf2": uf2_path}

    async def _picotool(
        self,
        args: list,
        on_progress: Optional[Callable[[str], None]],
    ) -> bool:
        proc = await asyncio.create_subprocess_exec(
            "picotool",
            *args,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
        )
        assert proc.stdout is not None
        while True:
            line = await proc.stdout.readline()
            if not line:
                break
            text = line.decode("utf-8", errors="replace").rstrip()
            if on_progress is not None:
                try:
                    on_progress(text)
                except Exception:
                    logger.debug("on_progress callback raised", exc_info=True)
        rc = await proc.wait()
        return rc == 0

    async def flash_bundled(
        self,
        fw_dir: str,
        target: Optional[str] = None,
        on_progress: Optional[Callable[[str], None]] = None,
    ) -> Dict[str, Any]:
        # Fail fast before _detect_target spawns picotool against an LM-owned device.
        if self._lm_is_running():
            return {"ok": False, "error": LM_OWNS_PORT_MSG}
        directory = Path(fw_dir).expanduser().resolve()
        if not directory.is_dir():
            raise RuntimeError(f"firmware dir not found at {directory}")

        chosen = target
        if not chosen:
            chosen = await self._detect_target(on_progress)
        if chosen not in VALID_TARGETS:
            raise ValueError(
                f"unknown target board {chosen!r}; expected one of {sorted(VALID_TARGETS)}"
            )
        uf2 = directory / f"pitrac_{chosen}.uf2"
        if not uf2.is_file():
            raise RuntimeError(f"no bundled firmware for board={chosen} at {uf2}")
        if on_progress is not None:
            on_progress(f"target={chosen} -> {uf2} ({uf2.stat().st_size} bytes)")
        return await self.flash(str(uf2), on_progress=on_progress)

    async def _detect_target(
        self,
        on_progress: Optional[Callable[[str], None]],
    ) -> str:
        proc = await asyncio.create_subprocess_exec(
            "picotool", "info",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
        )
        out, _ = await proc.communicate()
        text = out.decode("utf-8", errors="replace") if out else ""
        if on_progress is not None:
            for line in text.splitlines():
                on_progress(line)
        if "RP2350" in text:
            return "pico2_w"
        if "RP2040" in text:
            return "pico_w"
        raise RuntimeError(
            "could not detect chip family via picotool; set gs_config.pico.target_board"
        )


INTERVAL_MATCH_TOL = 1e-3
RATIO_MATCH_TOL = 1e-3


def _validate_intervals(intervals: Any) -> list:
    """Coerce, range-check, and enforce the unique-consecutive-ratio rule.

    Raises ValueError on anything firmware would reject, sparing a doomed round-trip.
    """
    if not isinstance(intervals, (list, tuple)):
        raise ValueError(f"intervals must be a list: {intervals!r}")
    if not intervals:
        raise ValueError("intervals must not be empty")
    if len(intervals) > PULSE_INTERVALS_MAX:
        raise ValueError(
            f"too many intervals ({len(intervals)} > {PULSE_INTERVALS_MAX})"
        )

    coerced = []
    for raw in intervals:
        try:
            coerced.append(float(raw))
        except (TypeError, ValueError):
            raise ValueError(f"interval is not a number: {raw!r}") from None

    # A single trailing 0 is the firmware terminator sentinel (caps the train,
    # present in shipped driver/putter vectors). Strip for validation, re-attach
    # on return; any other non-positive value (interior 0, negative) is rejected.
    has_terminator = len(coerced) > 1 and coerced[-1] == 0.0
    gaps = coerced[:-1] if has_terminator else coerced
    if not gaps:
        raise ValueError("intervals must contain at least one positive gap")

    values = []
    for v in gaps:
        if v <= 0:
            raise ValueError(
                "interval must be positive "
                f"(only a single trailing 0 terminator is allowed): {v}"
            )
        if v > INTERVAL_MS_MAX:
            raise ValueError(f"interval exceeds firmware max {INTERVAL_MS_MAX} ms: {v}")
        values.append(v)

    ratios = [values[i + 1] / values[i] for i in range(len(values) - 1)]
    for i in range(len(ratios)):
        for j in range(i + 1, len(ratios)):
            if abs(ratios[i] - ratios[j]) <= RATIO_MATCH_TOL:
                raise ValueError(
                    "consecutive interval ratios must be unique "
                    f"(ratio {ratios[i]:.3f} repeats)"
                )
    return values + [0.0] if has_terminator else values


def _format_interval(value: float) -> str:
    """Render an interval the way the firmware grammar expects (trim zeros)."""
    return f"{value:g}"


def _intervals_match(echoed: Any, expected: list) -> bool:
    if not isinstance(echoed, list) or len(echoed) != len(expected):
        return False
    return all(abs(a - b) <= INTERVAL_MATCH_TOL for a, b in zip(echoed, expected))


def _scalar_matches(echoed: Any, expected: float) -> bool:
    if not isinstance(echoed, (int, float)) or isinstance(echoed, bool):
        return False
    return abs(echoed - expected) <= INTERVAL_MATCH_TOL


def _coerce_scalar(value: str) -> Any:
    if value == "":
        return value
    try:
        return int(value)
    except ValueError:
        pass
    try:
        return float(value)
    except ValueError:
        return value


def _parse_rms_event(text: str) -> Optional[Dict[str, int]]:
    out: Dict[str, int] = {}
    for token in text.split()[2:]:
        if "=" not in token:
            continue
        key, _, value = token.partition("=")
        if key not in ("value", "timestamp"):
            continue
        try:
            out[key] = int(value)
        except ValueError:
            return None
    if "value" not in out or "timestamp" not in out:
        return None
    return out

"""Async PicoManager — owns the USB CDC handle to /dev/ttyACM0.

The Pico bridge already takes the serial port whenever a strobe calibration
sweep is in flight. PicoManager runs alongside that flow: it shares a single
asyncio.Lock with the calibration manager so neither one writes while the
other is mid-command. Hold times for any one round-trip stay well under a
human-perceptible window so the calibration sweep is never starved.
"""

from __future__ import annotations

import asyncio
import logging
import shutil
import time
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
STREAM_RMS_MAX_HZ = 100


class PicoManager:
    """Wraps the Pico's USB CDC line protocol behind asyncio-friendly helpers.

    All public methods take the shared lock for the duration of one
    write+read round-trip. The lock is module-shared with the calibration
    manager so concurrent /api/pico/* and /api/calibration/* requests don't
    fight over the serial fd.
    """

    def __init__(self, config_manager: Any, lock: asyncio.Lock):
        self._config_manager = config_manager
        self._lock = lock
        self._serial = None  # type: ignore[assignment]
        self._stream_active = False

    # ------------------------------------------------------------------
    # serial fd lifecycle
    # ------------------------------------------------------------------

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

    def _open(self) -> Any:
        if serial is None:
            raise RuntimeError("pyserial not installed; cannot reach the Pico")
        path = self._device_path()
        ser = serial.Serial(path, BAUDRATE, timeout=1, exclusive=True)
        logger.info("PicoManager: opened %s", path)
        return ser

    def close(self) -> None:
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                logger.debug("PicoManager: close error", exc_info=True)
            self._serial = None

    # ------------------------------------------------------------------
    # primitive read/write
    # ------------------------------------------------------------------

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
        """Turn `STATUS armed=0 threshold=4096 ...` into a dict.

        Anything past the leading `STATUS` token is parsed as key=value pairs,
        with numerics coerced. The trailing `intervals=` CSV is split into a
        list of floats so the UI can show / round-trip it without re-parsing.
        """
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

    # ------------------------------------------------------------------
    # public API
    # ------------------------------------------------------------------

    async def probe(self) -> Dict[str, Any]:
        """One-shot: open, ask STATUS, close. Use for the connection card."""
        async with self._lock:
            return await asyncio.to_thread(self._probe_sync)

    def _probe_sync(self) -> Dict[str, Any]:
        try:
            ser = self._open()
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
            try:
                ser.close()
            except Exception:
                pass

    async def status(self) -> Dict[str, Any]:
        """Reuses the internal serial handle so back-to-back polls don't pay
        the open/close cost on every tick. Falls back to probe semantics on
        first call or after close()."""
        async with self._lock:
            return await asyncio.to_thread(self._status_sync)

    def _status_sync(self) -> Dict[str, Any]:
        if self._serial is None:
            try:
                self._serial = self._open()
            except (RuntimeError, OSError) as exc:
                return {"present": False, "error": str(exc)}
        try:
            self._serial.reset_input_buffer()
            self._write_line(self._serial, "STATUS")
            line = self._read_until(
                self._serial, lambda t: t.startswith("STATUS"), DEFAULT_DEADLINE_S
            )
            if line is None:
                return {"present": False, "error": "no STATUS reply"}
            data = self._parse_status_line(line)
            data["present"] = True
            data["device"] = self._device_path()
            return data
        except OSError as exc:
            self.close()
            return {"present": False, "error": str(exc)}

    async def selftest(self) -> Dict[str, Any]:
        async with self._lock:
            return await asyncio.to_thread(self._selftest_sync)

    def _selftest_sync(self) -> Dict[str, Any]:
        if self._serial is None:
            self._serial = self._open()
        ser = self._serial
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
            self.close()
            return {"ok": False, "error": str(exc)}

    async def set_threshold(self, value: int) -> Dict[str, Any]:
        if not isinstance(value, int) or value < THRESHOLD_MIN or value > THRESHOLD_MAX:
            raise ValueError(f"threshold out of range: {value!r}")
        return await self._send_cfg(f"MIC_THRESHOLD={value}")

    async def set_armed(self, armed: bool) -> Dict[str, Any]:
        return await self._send_cfg(f"ARMED={1 if armed else 0}")

    async def set_min_inter_shot(self, ms: int) -> Dict[str, Any]:
        if not isinstance(ms, int):
            raise ValueError(f"min_inter_shot must be int: {ms!r}")
        clamped = max(MIN_INTER_SHOT_FLOOR_MS, min(ms, MIN_INTER_SHOT_CEIL_MS))
        return await self._send_cfg(f"MIN_INTER_SHOT_MS={clamped}")

    # ------------------------------------------------------------------
    # RMS streaming
    # ------------------------------------------------------------------

    async def start_rms_stream(self, hz: int) -> AsyncIterator[Dict[str, int]]:
        """Yield `{value, timestamp}` dicts at roughly `hz` samples/sec.

        The iterator holds the serial handle (and the shared lock) for its
        full lifetime; callers must `aclose()` it (or iterate to completion)
        so the lock is released. The firmware caps hz at STREAM_RMS_MAX_HZ
        regardless of what we ask for, so clamp here too.
        """
        clamped = max(1, min(int(hz), STREAM_RMS_MAX_HZ))
        return self._rms_iter(clamped)

    async def _rms_iter(self, hz: int) -> AsyncIterator[Dict[str, int]]:
        await self._lock.acquire()
        try:
            if self._serial is None:
                self._serial = await asyncio.to_thread(self._open)
            ser = self._serial
            try:
                await asyncio.to_thread(ser.reset_input_buffer)
            except Exception:
                pass
            self._stream_active = True
            await asyncio.to_thread(self._write_line, ser, f"CFG STREAM_RMS={hz}")
            buf = b""
            try:
                while self._stream_active:
                    chunk = await asyncio.to_thread(ser.read, READ_CHUNK)
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
                try:
                    await asyncio.to_thread(self._write_line, ser, "CFG STREAM_RMS=0")
                except Exception:
                    logger.debug("RMS stop write failed", exc_info=True)
                self._stream_active = False
        finally:
            self._lock.release()

    async def stop_rms_stream(self) -> None:
        """Signal the active iterator to drain and release the lock."""
        self._stream_active = False

    async def _send_cfg(self, suffix: str) -> Dict[str, Any]:
        async with self._lock:
            return await asyncio.to_thread(self._send_cfg_sync, suffix)

    def _send_cfg_sync(self, suffix: str) -> Dict[str, Any]:
        if self._serial is None:
            self._serial = self._open()
        ser = self._serial
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
            self.close()
            return {"ok": False, "error": str(exc)}


    # ------------------------------------------------------------------
    # firmware flash via picotool
    # ------------------------------------------------------------------

    async def flash(
        self,
        uf2_path: str,
        on_progress: Optional[Callable[[str], None]] = None,
    ) -> Dict[str, Any]:
        if shutil.which("picotool") is None:
            raise RuntimeError(
                "picotool not installed; rerun packaging/build.sh dev"
            )

        async with self._lock:
            # Close the local handle so picotool can claim the USB device.
            self.close()
            try:
                await self._picotool(["reboot", "-f", "-u"], on_progress)
            except Exception:
                logger.info("picotool reboot returned non-zero (often fine)")
            await asyncio.sleep(2)
            ok = await self._picotool(
                ["load", "-f", "-x", uf2_path], on_progress
            )
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


def _coerce_scalar(value: str) -> Any:
    """Best-effort: int → float → leave-as-string."""
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
    """Parse `EVENT RMS value=<int> timestamp=<us>` into a dict."""
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

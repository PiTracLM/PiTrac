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
STREAM_RMS_MAX_HZ = 100


class PicoManager:
    def __init__(self, config_manager: Any, lock: asyncio.Lock):
        self._config_manager = config_manager
        self._lock = lock
        self._serial = None  # type: ignore[assignment]
        self._stream_active = False

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

    async def start_rms_stream(self, hz: int) -> AsyncIterator[Dict[str, int]]:
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
            sent_bootsel = False
            try:
                if self._serial is None:
                    self._serial = await asyncio.to_thread(self._open)
                await asyncio.to_thread(self._write_line, self._serial, "BOOTSEL")
                sent_bootsel = True
                if on_progress is not None:
                    on_progress("sent BOOTSEL over CDC, waiting for re-enumeration")
            except Exception as exc:
                if on_progress is not None:
                    on_progress(f"BOOTSEL via CDC failed ({exc}); will try picotool reboot")

            self.close()
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
        directory = Path(fw_dir).expanduser().resolve()
        if not directory.is_dir():
            raise RuntimeError(f"firmware dir not found at {directory}")

        chosen = target
        if not chosen:
            chosen = await self._detect_target(on_progress)
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

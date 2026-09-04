from __future__ import annotations

import asyncio
import json
import logging
import math
from typing import Coroutine, Dict, List, Optional, Union

from sim_interface import (
    SimInterface,
    STATUS_CONNECTED,
    STATUS_CONNECTING,
    STATUS_ERROR,
    STATUS_OFF,
)

logger = logging.getLogger(__name__)

_RECONNECT_BACKOFF_SEC = [1, 2, 5, 10]
_MAX_UNPARSED_CHARS = 64 * 1024


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def round1(value: float) -> float:
    # std::round in GsResults::FormatDoubleAsString rounds halves away from zero
    return math.copysign(math.floor(abs(value) * 10 + 0.5), value) / 10


class TcpSim(SimInterface):
    """A sim that is a TCP client of its simulator, with reconnect.

    Subclasses frame messages in _encode, send their greeting in _on_connected,
    and set READS to have each JSON object the simulator sends, with its raw text, passed to _on_message.
    One that must authenticate first sets CONNECTED_ON_OPEN False and reports connected itself.
    """

    CONNECT_TIMEOUT_SEC = 5
    READS = False
    CONNECTED_ON_OPEN = True

    def __init__(self, host: str, port: int) -> None:
        super().__init__()
        self.host = host
        self.port = port
        self._writer: Optional[asyncio.StreamWriter] = None
        self._reader: Optional[asyncio.StreamReader] = None
        self._tasks: List[asyncio.Task] = []
        self._reconnect_task: Optional[asyncio.Task] = None
        self._want_connected = False
        self._conn_lock = asyncio.Lock()

    def info(self) -> Dict[str, str]:
        data = super().info()
        data["target"] = f"{self.host}:{self.port}" if self.host else ""
        return data

    async def connect(self) -> None:
        self._want_connected = True
        await self._open()

    async def _open(self) -> None:
        async with self._conn_lock:
            if self._writer is not None:
                return
            if not self.host:
                await self._set_status(STATUS_ERROR, "no host configured")
                return
            await self._set_status(STATUS_CONNECTING, f"{self.host}:{self.port}")
            try:
                self._reader, self._writer = await asyncio.wait_for(
                    asyncio.open_connection(self.host, self.port), self.CONNECT_TIMEOUT_SEC
                )
                if self._want_connected:
                    await self._on_connected()
            except asyncio.CancelledError:
                self._teardown_socket()
                await self._set_status(STATUS_OFF, "")
                raise
            except Exception as e:
                if not self._want_connected:
                    return
                logger.warning(f"{self.display_name} connect failed: {e!r}")
                await self._set_status(STATUS_ERROR, str(e) or type(e).__name__)
                self._schedule_reconnect()
                return
            # A disconnect during any await above already tore down and set the status
            if not self._want_connected or self._writer is None:
                self._teardown_socket()
                return
            if self.CONNECTED_ON_OPEN:
                await self._set_status(STATUS_CONNECTED, f"{self.host}:{self.port}")
            if self.READS and self._reader is not None:
                self._spawn(self._read_loop())

    async def _on_connected(self) -> None:
        pass

    def _encode(self, obj: Dict[str, object]) -> bytes:
        return json.dumps(obj).encode("utf-8")

    async def _send_obj(self, obj: Union[Dict[str, object], bytes]) -> None:
        if self._writer is None:
            raise ConnectionError("not connected")
        self._writer.write(obj if isinstance(obj, bytes) else self._encode(obj))
        await self._writer.drain()

    async def _send_or_reconnect(self, obj: Union[Dict[str, object], bytes]) -> None:
        try:
            await self._send_obj(obj)
        except Exception as e:
            logger.warning(f"{self.display_name} send failed: {e}")
            await self._set_status(STATUS_ERROR, str(e))
            self._schedule_reconnect()
            raise

    async def _on_message(self, obj: object, raw: str = "") -> None:
        pass

    async def _read_loop(self) -> None:
        decoder = json.JSONDecoder()
        reader = self._reader
        buf = ""
        try:
            while True:
                chunk = await reader.read(4096)
                if not chunk:
                    break
                buf += chunk.decode("utf-8", errors="replace")
                while buf := buf.lstrip():
                    try:
                        obj, end = decoder.raw_decode(buf)
                    except json.JSONDecodeError:
                        if buf[0] != "{" or len(buf) > _MAX_UNPARSED_CHARS:
                            logger.warning(f"{self.display_name} sent unparseable data, dropped: {buf[:200]!r}")
                            buf = ""
                        break
                    raw, buf = buf[:end], buf[end:]
                    try:
                        await self._on_message(obj, raw)
                    except Exception as e:
                        logger.warning(f"{self.display_name} message handling failed: {e!r}")
                    if self._reader is not reader:
                        return
        except (ConnectionError, OSError) as e:
            logger.warning(f"{self.display_name} read failed: {e}")
        await self._set_status(STATUS_ERROR, "connection closed")
        self._schedule_reconnect()

    def _spawn(self, coro: Coroutine) -> asyncio.Task:
        task = asyncio.create_task(coro)
        self._tasks.append(task)
        return task

    def _schedule_reconnect(self) -> None:
        self._teardown_socket()
        if not self._want_connected:
            return
        if self._reconnect_task and not self._reconnect_task.done():
            return
        self._reconnect_task = asyncio.create_task(self._reconnect_loop())

    async def _reconnect_loop(self) -> None:
        attempt = 0
        while self._want_connected and self._writer is None:
            delay = _RECONNECT_BACKOFF_SEC[min(attempt, len(_RECONNECT_BACKOFF_SEC) - 1)]
            await asyncio.sleep(delay)
            if not self._want_connected:
                return
            await self._open()
            attempt += 1

    def _teardown_socket(self) -> None:
        for task in self._tasks:
            task.cancel()
        self._tasks = []
        if self._writer is not None:
            try:
                self._writer.close()
            except Exception:
                pass
        self._writer = None
        self._reader = None

    async def disconnect(self) -> None:
        self._want_connected = False
        if self._reconnect_task and not self._reconnect_task.done():
            self._reconnect_task.cancel()
        writer = self._writer
        self._teardown_socket()
        if writer is not None:
            try:
                await writer.wait_closed()
            except Exception:
                pass
        await self._set_status(STATUS_OFF, "")

from __future__ import annotations

import asyncio
import json
import logging
from typing import Coroutine, Dict, List, Optional

from sim_interface import (
    SimInterface,
    STATUS_CONNECTED,
    STATUS_CONNECTING,
    STATUS_ERROR,
    STATUS_OFF,
)

logger = logging.getLogger(__name__)

_RECONNECT_BACKOFF_SEC = [1, 2, 5, 10]


class TcpSim(SimInterface):
    """A sim that is a TCP client of its simulator, with reconnect.

    Subclasses frame messages in _encode, send their greeting in _on_connected,
    and set READS to have each JSON object the simulator sends passed to _on_message.
    """

    CONNECT_TIMEOUT_SEC = 5
    READS = False

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
                await self._on_connected()
            except asyncio.CancelledError:
                self._teardown_socket()
                await self._set_status(STATUS_OFF, "")
                raise
            except Exception as e:
                logger.warning(f"{self.display_name} connect failed: {e!r}")
                await self._set_status(STATUS_ERROR, str(e) or type(e).__name__)
                self._schedule_reconnect()
                return
            await self._set_status(STATUS_CONNECTED, f"{self.host}:{self.port}")
            if self.READS:
                self._spawn(self._read_loop())

    async def _on_connected(self) -> None:
        pass

    def _encode(self, obj: Dict[str, object]) -> bytes:
        return json.dumps(obj).encode("utf-8")

    async def _send_obj(self, obj: Dict[str, object]) -> None:
        if self._writer is None:
            raise ConnectionError("not connected")
        self._writer.write(self._encode(obj))
        await self._writer.drain()

    async def _send_or_reconnect(self, obj: Dict[str, object]) -> None:
        try:
            await self._send_obj(obj)
        except Exception as e:
            logger.warning(f"{self.display_name} send failed: {e}")
            await self._set_status(STATUS_ERROR, str(e))
            self._schedule_reconnect()
            raise

    async def _on_message(self, obj: object) -> None:
        pass

    async def _read_loop(self) -> None:
        decoder = json.JSONDecoder()
        buf = ""
        try:
            while True:
                chunk = await self._reader.read(4096)
                if not chunk:
                    break
                buf += chunk.decode("utf-8", errors="replace")
                while buf := buf.lstrip():
                    try:
                        obj, end = decoder.raw_decode(buf)
                    except json.JSONDecodeError:
                        if buf[0] != "{":
                            logger.warning(f"{self.display_name} sent non-JSON, dropped: {buf[:200]!r}")
                            buf = ""
                        break
                    buf = buf[end:]
                    await self._on_message(obj)
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

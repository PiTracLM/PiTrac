from __future__ import annotations

import asyncio
import json
import logging
import math
from typing import Dict, Optional

from models import ShotData
from sim_interface import (
    SimInterface,
    STATUS_CONNECTED,
    STATUS_CONNECTING,
    STATUS_ERROR,
    STATUS_OFF,
)

logger = logging.getLogger(__name__)

_RECONNECT_BACKOFF_SEC = [1, 2, 5, 10]


def _clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def build_shot_payload(shot: ShotData) -> Dict[str, object]:
    back = float(shot.back_spin)
    side = float(shot.side_spin)
    spin_speed = round(math.hypot(back, side))
    spin_axis = math.degrees(math.atan2(side, back)) if (back or side) else 0.0
    return {
        "type": "shot",
        "unit": "imperial",
        "shot": {
            "ballSpeed": round(float(shot.speed), 1),
            "verticalLaunchAngle": round(_clamp(float(shot.launch_angle), 0, 45), 1),
            "horizontalLaunchAngle": round(_clamp(float(shot.side_angle), -45, 45), 1),
            "spinSpeed": spin_speed,
            "spinAxis": round(_clamp(spin_axis, -45, 45), 1),
        },
    }


class OGSSim(SimInterface):
    name = "ogs"
    display_name = "OpenGolfSim"

    def __init__(self, host: str, port: int = 3111, keepalive_sec: int = 5) -> None:
        super().__init__()
        self.host = host
        self.port = port
        self.keepalive_sec = max(1, int(keepalive_sec))
        self._writer: Optional[asyncio.StreamWriter] = None
        self._reader: Optional[asyncio.StreamReader] = None
        self._keepalive_task: Optional[asyncio.Task] = None
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
                self._reader, self._writer = await asyncio.open_connection(self.host, self.port)
            except Exception as e:
                logger.warning(f"OGS connect failed: {e}")
                await self._set_status(STATUS_ERROR, str(e))
                self._schedule_reconnect()
                return
            await self._send_obj({"type": "device", "status": "ready"})
            await self._set_status(STATUS_CONNECTED, f"{self.host}:{self.port}")
            self._keepalive_task = asyncio.create_task(self._keepalive_loop())

    async def _send_obj(self, obj: Dict[str, object]) -> None:
        if self._writer is None:
            raise ConnectionError("not connected")
        self._writer.write((json.dumps(obj) + "\n").encode("utf-8"))
        await self._writer.drain()

    async def send_shot(self, shot: ShotData) -> None:
        if self._writer is None:
            raise ConnectionError("OGS not connected")
        try:
            await self._send_obj(build_shot_payload(shot))
        except Exception as e:
            logger.warning(f"OGS send_shot failed: {e}")
            await self._set_status(STATUS_ERROR, str(e))
            self._schedule_reconnect()
            raise

    async def _keepalive_loop(self) -> None:
        try:
            while self._want_connected and self._writer is not None:
                await asyncio.sleep(self.keepalive_sec)
                try:
                    await self._send_obj({"type": "device", "status": "ready"})
                except Exception as e:
                    logger.warning(f"OGS keepalive failed: {e}")
                    await self._set_status(STATUS_ERROR, str(e))
                    self._schedule_reconnect()
                    return
        except asyncio.CancelledError:
            pass

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
        if self._keepalive_task and not self._keepalive_task.done():
            self._keepalive_task.cancel()
        self._keepalive_task = None
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

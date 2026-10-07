from __future__ import annotations

import asyncio
import json
import logging
import math
from typing import Callable, Dict, Optional, Union

from models import ShotData
from sims.tcp_sim import HOST_FIELD, TcpSim, clamp, port_field

logger = logging.getLogger(__name__)

# OGS parses each socket read as one JSON message, so two messages landing in one read are both dropped
_MIN_SEND_GAP_SEC = 0.1


def build_device_status(ready: bool) -> Dict[str, object]:
    return {"type": "device", "status": "ready" if ready else "busy"}


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
            "verticalLaunchAngle": round(clamp(float(shot.launch_angle), 0, 45), 1),
            "horizontalLaunchAngle": round(clamp(float(shot.side_angle), -45, 45), 1),
            "spinSpeed": spin_speed,
            "spinAxis": round(clamp(spin_axis, -45, 45), 1),
        },
    }


class OGSSim(TcpSim):
    type = "ogs"
    display_name = "OpenGolfSim"
    READS = True
    FIELDS = [
        HOST_FIELD,
        port_field(3111),
        {"key": "keepalive_sec", "label": "Keepalive (seconds)", "type": "integer",
         "default": 5, "min": 1, "max": 60, "advanced": True},
    ]

    def __init__(
        self,
        host: str,
        port: int = 3111,
        keepalive_sec: int = 5,
        ball_state: Callable[[], bool] = lambda: False,
    ) -> None:
        super().__init__(host, port)
        self.keepalive_sec = max(1, int(keepalive_sec))
        self._ball_state = ball_state
        self._keepalive_task: Optional[asyncio.Task] = None
        self._send_lock = asyncio.Lock()
        self._last_send = 0.0

    def _teardown_socket(self) -> None:
        super()._teardown_socket()
        self._keepalive_task = None

    def _encode(self, obj: Dict[str, object]) -> bytes:
        return (json.dumps(obj) + "\n").encode("utf-8")

    async def _send_obj(self, obj: Union[Dict[str, object], bytes]) -> None:
        async with self._send_lock:
            loop = asyncio.get_running_loop()
            wait = self._last_send + _MIN_SEND_GAP_SEC - loop.time()
            if wait > 0:
                await asyncio.sleep(wait)
            await super()._send_obj(obj)
            self._last_send = loop.time()

    async def _on_connected(self) -> None:
        await self._send_obj(build_device_status(self._ball_state()))
        self._keepalive_task = self._spawn(self._keepalive_loop())

    async def on_ball_state(self, ball_detected: bool) -> None:
        if self._writer is None:
            return
        try:
            await self._send_or_reconnect(build_device_status(ball_detected))
        except Exception:
            pass

    async def send_shot(self, shot: ShotData) -> None:
        if self._writer is None:
            raise ConnectionError("OGS not connected")
        await self._send_or_reconnect(build_shot_payload(shot))

    async def _on_message(self, obj: object, raw: str = "") -> None:
        if not isinstance(obj, dict):
            return
        if obj.get("type") == "player":
            # Club IDs are logged, not acted on, until we know what OGS sends for the putter
            data = obj.get("data") or {}
            logger.info(f"OGS player update: club {data.get('club')!r}")
        elif obj.get("status") == 400:
            logger.warning(f"OGS rejected a message: {obj.get('error')!r}")

    async def _keepalive_loop(self) -> None:
        while self._want_connected and self._writer is not None:
            await asyncio.sleep(self.keepalive_sec)
            try:
                await self._send_or_reconnect(build_device_status(self._ball_state()))
            except Exception:
                return

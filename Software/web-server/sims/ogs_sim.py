from __future__ import annotations

import asyncio
import json
import math
from typing import Dict, Optional

from models import ShotData
from sims.tcp_sim import TcpSim

_READY = {"type": "device", "status": "ready"}


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


class OGSSim(TcpSim):
    name = "ogs"
    display_name = "OpenGolfSim"

    def __init__(self, host: str, port: int = 3111, keepalive_sec: int = 5) -> None:
        super().__init__(host, port)
        self.keepalive_sec = max(1, int(keepalive_sec))
        self._keepalive_task: Optional[asyncio.Task] = None

    def _teardown_socket(self) -> None:
        super()._teardown_socket()
        self._keepalive_task = None

    def _encode(self, obj: Dict[str, object]) -> bytes:
        return (json.dumps(obj) + "\n").encode("utf-8")

    async def _on_connected(self) -> None:
        await self._send_obj(_READY)
        self._keepalive_task = self._spawn(self._keepalive_loop())

    async def send_shot(self, shot: ShotData) -> None:
        if self._writer is None:
            raise ConnectionError("OGS not connected")
        await self._send_or_reconnect(build_shot_payload(shot))

    async def _keepalive_loop(self) -> None:
        while self._want_connected and self._writer is not None:
            await asyncio.sleep(self.keepalive_sec)
            try:
                await self._send_or_reconnect(_READY)
            except Exception:
                return

from __future__ import annotations

import itertools
import json
import logging
import math
from typing import Callable, Dict, Optional

from models import ShotData
from sims.tcp_sim import TcpSim, round1

logger = logging.getLogger(__name__)

_MAX_SPEED_MPH = 200.0
# Process-wide like GsSimInterface::shot_counter_, so numbers survive reconnects and sim rebuilds
_shot_numbers = itertools.count(1)
_ZERO_CLUB_DATA = {
    key: 0.0
    for key in (
        "Speed",
        "AngleOfAttack",
        "FaceToTarget",
        "Lie",
        "Loft",
        "Path",
        "SpeedAtImpact",
        "VerticalFaceImpact",
        "HorizontalFaceImpact",
        "ClosureRate",
    )
}


def _spin_axis(back: float, side: float) -> float:
    # Same formula as GsResults::GetSpinAxis, so a negative back spin keeps the C++ sign
    if abs(side) <= 0.0001:
        return 0.0
    if back == 0:
        return math.copysign(90.0, side)
    return math.degrees(math.atan(side / back + 0.00001))


def _message(shot_number: int, ball: Dict[str, float], heartbeat: bool, ball_detected: bool) -> Dict[str, object]:
    return {
        "DeviceID": "PiTrac LM 0.1",
        "Units": "Yards",
        "ShotNumber": shot_number,
        "APIversion": "1",
        "BallData": {key: round1(value) for key, value in ball.items()},
        "ClubData": _ZERO_CLUB_DATA,
        "ShotDataOptions": {
            "ContainsBallData": not heartbeat,
            "ContainsClubData": False,
            "LaunchMonitorIsReady": True,
            "LaunchMonitorBallDetected": ball_detected,
            "IsHeartBeat": heartbeat,
        },
    }


def build_shot_payload(shot: ShotData, shot_number: int) -> Dict[str, object]:
    back = float(shot.back_spin)
    side = float(shot.side_spin)
    ball = {
        "Speed": min(float(shot.speed), _MAX_SPEED_MPH),
        "SpinAxis": _spin_axis(back, side),
        "TotalSpin": math.hypot(back, side),
        "BackSpin": back,
        "SideSpin": side,
        "HLA": float(shot.side_angle),
        "VLA": float(shot.launch_angle),
    }
    return _message(shot_number, ball, heartbeat=False, ball_detected=True)


def build_heartbeat(ball_detected: bool) -> Dict[str, object]:
    ball = dict.fromkeys(("Speed", "SpinAxis", "TotalSpin", "BackSpin", "SideSpin", "HLA", "VLA"), 0.0)
    return _message(0, ball, heartbeat=True, ball_detected=ball_detected)


class GSProSim(TcpSim):
    name = "gspro"
    display_name = "GSPro"
    READS = True

    def __init__(self, host: str, port: int = 921, on_club: Optional[Callable[[str], None]] = None) -> None:
        super().__init__(host, port)
        self._on_club = on_club

    def _encode(self, obj: Dict[str, object]) -> bytes:
        # The layout boost's write_json gave the C++ sender
        return (json.dumps(obj, indent=4) + "\n").encode("utf-8")

    async def _on_connected(self) -> None:
        await self._send_obj(build_heartbeat(False))

    async def on_ball_state(self, ball_detected: bool) -> None:
        if self._writer is None:
            return
        try:
            await self._send_or_reconnect(build_heartbeat(ball_detected))
        except Exception:
            pass

    async def send_shot(self, shot: ShotData) -> None:
        if self._writer is None:
            raise ConnectionError("GSPro not connected")
        await self._send_or_reconnect(build_shot_payload(shot, next(_shot_numbers)))

    async def _on_message(self, obj: object, raw: str = "") -> None:
        if not isinstance(obj, dict):
            logger.warning(f"GSPro sent an unexpected message: {obj!r}")
            return
        message = obj.get("Message", "")
        try:
            code = int(obj.get("Code", 0))
        except (TypeError, ValueError):
            logger.warning(f"GSPro sent a message with no usable Code: {obj!r}")
            return
        if code == 201:
            player = obj.get("Player") or {}
            # GsGSProResponse treats any club but PT as the driver
            club = "putter" if player.get("Club") == "PT" else "driver"
            logger.info(f"GSPro player info: club {player.get('Club')!r}, handed {player.get('Handed')!r}")
            if self._on_club is not None:
                self._on_club(club)
        elif code == 200:
            logger.info(f"GSPro: {message}")
        else:
            logger.warning(f"GSPro returned {code}: {message}")
            await self._set_status(self.status, f"{self.host}:{self.port} GSPro {code}: {message}")

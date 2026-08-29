from __future__ import annotations

import asyncio
import logging
from typing import Callable, Dict, Optional

from models import ShotData
from sim_interface import STATUS_CONNECTED, STATUS_CONNECTING, STATUS_ERROR
from sims import e6_auth
from sims.tcp_sim import TcpSim, clamp, round1

logger = logging.getLogger(__name__)

_CLUBS = {"Putter": "putter", "Driver": "driver"}
_ZERO_CLUB_DATA = dict.fromkeys(("ClubHeadSpeed", "ClubAngleFace", "ClubAnglePath", "ClubHeadSpeedMPH"), 0.0)


def build_ball_data(shot: ShotData) -> Dict[str, object]:
    # The ranges E6 enforces
    ball = {
        "BackSpin": clamp(float(shot.back_spin), -999, 19999),
        "BallSpeed": clamp(float(shot.speed), 0.09, 249.9),
        "LaunchAngle": float(shot.launch_angle),
        "LaunchDirection": float(shot.side_angle),
        "SideSpin": clamp(float(shot.side_spin), -5999, 5999),
    }
    return {"Type": "SetBallData", "BallData": {key: round1(value) for key, value in ball.items()}}


class E6Sim(TcpSim):
    name = "e6"
    display_name = "E6 Connect"
    READS = True
    CONNECTED_ON_OPEN = False
    AUTH_TIMEOUT_SEC = 10

    def __init__(
        self,
        host: str,
        port: int = 2483,
        inter_message_delay_ms: int = 50,
        on_club: Optional[Callable[[str], None]] = None,
        auth: Callable[[str], str] = e6_auth.challenge_response,
    ) -> None:
        super().__init__(host, port)
        self.inter_message_delay_ms = max(0, int(inter_message_delay_ms))
        self.armed = False
        self._on_club = on_club
        self._auth = auth

    def _teardown_socket(self) -> None:
        super()._teardown_socket()
        self.armed = False

    async def _on_connected(self) -> None:
        await self._send_obj({"Type": "Handshake"})
        self._spawn(self._auth_deadline())

    async def _auth_deadline(self) -> None:
        await asyncio.sleep(self.AUTH_TIMEOUT_SEC)
        if self.status == STATUS_CONNECTING:
            await self._set_status(STATUS_ERROR, "E6 did not authenticate")
            self._schedule_reconnect()

    async def disconnect(self) -> None:
        if self._writer is not None:
            try:
                await self._send_obj({"Type": "Disconnect"})
            except Exception as e:
                logger.warning(f"E6 disconnect message failed: {e}")
        await super().disconnect()

    async def _set_armed(self, armed: bool) -> None:
        self.armed = armed
        await self._set_status(self.status, f"{self.host}:{self.port}" + (" armed" if armed else ""))

    async def _give_up(self, detail: str) -> None:
        # Reconnecting cannot fix authentication, so stay down until the user connects again
        logger.warning(f"E6: {detail}")
        self._want_connected = False
        await self._set_status(STATUS_ERROR, detail)
        self._teardown_socket()

    async def send_shot(self, shot: ShotData) -> None:
        if not self.armed:
            logger.warning("E6 shot not sent: E6 is not armed")
            return
        delay = self.inter_message_delay_ms / 1000
        await self._send_or_reconnect(build_ball_data(shot))
        await asyncio.sleep(delay)
        await self._send_or_reconnect({"Type": "SetClubData", "ClubData": _ZERO_CLUB_DATA})
        await asyncio.sleep(delay)
        await self._send_or_reconnect({"Type": "SendShot"})
        await self._set_armed(False)

    async def _on_message(self, obj: object, raw: str = "") -> None:
        if not isinstance(obj, dict):
            logger.warning(f"E6 sent an unexpected message: {obj!r}")
            return
        msg_type = obj.get("Type")
        if e6_auth.is_challenge(raw):
            # The exact text both ways: the object's reply is not always valid JSON, and the C++ sent it as is
            try:
                reply = self._auth(raw)
                if not reply:
                    raise e6_auth.E6AuthError("no reply to the challenge")
            except e6_auth.E6AuthError as e:
                await self._give_up(f"E6 authentication failed: {e}")
                return
            await self._send_or_reconnect(reply.encode())
        elif msg_type == "Authentication":
            if obj.get("Success") in ("true", True):
                logger.info("E6 authenticated")
                await self._set_status(STATUS_CONNECTED, f"{self.host}:{self.port}")
            else:
                await self._give_up(f"E6 authentication failed: {obj.get('Message') or obj}")
        elif msg_type == "SimCommand":
            await self._on_command(obj.get("SubType"), obj.get("Details"))
        elif msg_type == "ACK":
            logger.debug(f"E6 ACK: {obj.get('SubType')}")
        elif msg_type in ("Warning", "ShotError"):
            logger.warning(f"E6 {msg_type}: {obj}")
        else:
            logger.info(f"E6 sent {obj}")

    async def _on_command(self, sub_type: object, details: object) -> None:
        if sub_type == "Ping":
            await self._send_or_reconnect({"Type": "Pong"})
        elif sub_type == "Arm":
            await self._set_armed(True)
        elif sub_type == "Disarm":
            await self._set_armed(False)
        elif sub_type == "PlayerDataModified":
            club_type = details.get("ClubType") if isinstance(details, dict) else None
            club = _CLUBS.get(club_type)
            logger.info(f"E6 player data: club {club_type!r}" + ("" if club else ", ignored"))
            if club and self._on_club is not None:
                self._on_club(club)
        elif sub_type == "ShotComplete":
            logger.info(f"E6 shot complete: {details}")
        else:
            logger.info(f"E6 sim command {sub_type!r}: {details}")

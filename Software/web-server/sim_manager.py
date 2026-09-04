from __future__ import annotations

import asyncio
import functools
import logging
from typing import Awaitable, Callable, Dict, List, Optional, Set

from models import ShotData
from sim_interface import SimInterface
from sims.e6_sim import E6Sim
from sims.gspro_sim import GSProSim
from sims.ogs_sim import OGSSim

logger = logging.getLogger(__name__)

# The C++ sent a ball-detected heartbeat when it found the ball, just before the stabilization status
_BALL_DETECTED_BY_STATUS = {
    "Waiting For Ball": False,
    "Waiting For Placement To Stabilize": True,
    "Ball Placed": True,
}

BroadcastFn = Callable[[Dict[str, object]], Awaitable[None]]


class SimManager:
    RELOAD_DEBOUNCE_SEC = 0.5

    def __init__(self, config_manager, broadcast: Optional[BroadcastFn] = None) -> None:
        self.config_manager = config_manager
        self._broadcast = broadcast
        self._sims: Dict[str, SimInterface] = {}
        self._build_errors: Dict[str, Dict[str, str]] = {}
        self.loop: Optional[asyncio.AbstractEventLoop] = None
        self._reload_task: Optional[asyncio.Task] = None
        self._connect_tasks: Set[asyncio.Task] = set()
        self._reload_lock = asyncio.Lock()
        self._ball_detected: Optional[bool] = None
        self._club: Optional[str] = None
        self._club_sim: Optional[str] = None
        config_manager.register_callback("simulators.", self._on_config_change)

    def build_sims(self) -> None:
        get = self.config_manager.get_config
        factories = {
            OGSSim: lambda: OGSSim(
                host=get("simulators.ogs.host") or "",
                port=int(get("simulators.ogs.port") or 3111),
                keepalive_sec=int(get("simulators.ogs.keepalive_sec") or 5),
            ),
            GSProSim: lambda: GSProSim(
                host=get("simulators.gspro.host") or "",
                port=int(get("simulators.gspro.port") or 921),
                on_club=functools.partial(self.set_club, "gspro"),
                ball_state=lambda: bool(self._ball_detected),
            ),
            E6Sim: lambda: E6Sim(
                host=get("simulators.e6.host") or "",
                port=int(get("simulators.e6.port") or 2483),
                inter_message_delay_ms=int(get("simulators.e6.inter_message_delay_ms") or 0),
                on_club=functools.partial(self.set_club, "e6"),
            ),
        }
        self._sims = {}
        self._build_errors = {}
        for cls, factory in factories.items():
            if not get(f"simulators.{cls.name}.enabled"):
                continue
            try:
                sim = factory()
            except (TypeError, ValueError, OverflowError) as e:
                logger.error(f"{cls.display_name} settings are invalid, sim not started: {e}")
                self._build_errors[cls.name] = {
                    "name": cls.name,
                    "display_name": cls.display_name,
                    "status": "error",
                    "detail": f"Invalid settings: {e}",
                }
                continue
            sim.set_status_callback(functools.partial(self._on_sim_status, sim))
            self._sims[sim.name] = sim

    async def start(self) -> None:
        self.build_sims()
        for sim in self._sims.values():
            if self.config_manager.get_config(f"simulators.{sim.name}.auto_connect"):
                task = asyncio.create_task(sim.connect())
                self._connect_tasks.add(task)
                task.add_done_callback(self._connect_tasks.discard)
        await self._broadcast_status()

    async def stop(self) -> None:
        self.loop = None
        if self._reload_task is not None:
            self._reload_task.cancel()
            self._reload_task = None
        await self._disconnect_all()

    def _on_config_change(self, key: str, value: object) -> None:
        # Runs on the config writer's thread; startup builds the sims, so ignore until the loop is set.
        if self.loop is not None:
            self.loop.call_soon_threadsafe(self._schedule_reload)

    def _schedule_reload(self) -> None:
        if self._reload_task is not None:
            self._reload_task.cancel()
        self._reload_task = asyncio.create_task(self._reload_after_debounce())

    async def _reload_after_debounce(self) -> None:
        await asyncio.sleep(self.RELOAD_DEBOUNCE_SEC)
        async with self._reload_lock:
            await self._disconnect_all()
            await self.start()

    async def _disconnect_all(self) -> None:
        for task in self._connect_tasks:
            task.cancel()
        for sim in self._sims.values():
            try:
                await sim.disconnect()
            except Exception as e:
                logger.warning(f"sim {sim.name} disconnect failed: {e}")

    async def on_shot(self, shot: ShotData) -> None:
        for name, sim in self._sims.items():
            if sim.status != "connected":
                continue
            try:
                await sim.send_shot(shot)
            except Exception as e:
                logger.warning(f"sim {name} send_shot failed: {e}")

    async def on_status(self, result_type: str) -> None:
        ball_detected = _BALL_DETECTED_BY_STATUS.get(result_type)
        if ball_detected is None or ball_detected == self._ball_detected:
            return
        self._ball_detected = ball_detected
        for name, sim in self._sims.items():
            if sim.status != "connected":
                continue
            try:
                await sim.on_ball_state(ball_detected)
            except Exception as e:
                logger.warning(f"sim {name} on_ball_state failed: {e}")

    @property
    def armed(self) -> bool:
        return all(
            sim.armed for sim in self._sims.values() if sim.status == "connected" and hasattr(sim, "armed")
        )

    @property
    def club(self) -> Optional[str]:
        return self._club

    def set_club(self, sim_name: str, club: str) -> None:
        self._club = club
        self._club_sim = sim_name

    async def connect(self, name: str) -> None:
        await (await self._current_sim(name)).connect()

    async def disconnect(self, name: str) -> None:
        await (await self._current_sim(name)).disconnect()

    async def _current_sim(self, name: str) -> SimInterface:
        # Waits out a reload in progress, so the request reaches the rebuilt sim and not one being torn down
        async with self._reload_lock:
            sim = self._sims.get(name)
        if sim is None:
            raise KeyError(name)
        return sim

    def status(self) -> List[Dict[str, str]]:
        return [sim.info() for sim in self._sims.values()] + list(self._build_errors.values())

    async def _on_sim_status(self, sim: SimInterface) -> None:
        if sim.name == self._club_sim and sim.status != "connected":
            self._club = None
            self._club_sim = None
        await self._broadcast_status()

    async def _broadcast_status(self) -> None:
        if self._broadcast is None:
            return
        try:
            await self._broadcast({"type": "sim_status", "sims": self.status()})
        except Exception as e:
            logger.warning(f"sim status broadcast failed: {e}")

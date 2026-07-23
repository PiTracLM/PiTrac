from __future__ import annotations

import asyncio
import logging
from typing import Awaitable, Callable, Dict, List, Optional

from models import ShotData
from sim_interface import SimInterface
from sims.ogs_sim import OGSSim

logger = logging.getLogger(__name__)

BroadcastFn = Callable[[Dict[str, object]], Awaitable[None]]


class SimManager:
    RELOAD_DEBOUNCE_SEC = 0.5

    def __init__(self, config_manager, broadcast: Optional[BroadcastFn] = None) -> None:
        self.config_manager = config_manager
        self._broadcast = broadcast
        self._sims: Dict[str, SimInterface] = {}
        self.loop: Optional[asyncio.AbstractEventLoop] = None
        self._reload_task: Optional[asyncio.Task] = None
        config_manager.register_callback("simulators.", self._on_config_change)

    def build_sims(self) -> None:
        self._sims = {}
        if self.config_manager.get_config("simulators.ogs.enabled"):
            sim = OGSSim(
                host=self.config_manager.get_config("simulators.ogs.host") or "",
                port=int(self.config_manager.get_config("simulators.ogs.port") or 3111),
                keepalive_sec=int(self.config_manager.get_config("simulators.ogs.keepalive_sec") or 5),
            )
            sim.set_status_callback(self._broadcast_status)
            self._sims[sim.name] = sim

    async def start(self) -> None:
        self.build_sims()
        for sim in self._sims.values():
            if self.config_manager.get_config(f"simulators.{sim.name}.auto_connect"):
                await sim.connect()
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
        await self._disconnect_all()
        await self.start()

    async def _disconnect_all(self) -> None:
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

    async def connect(self, name: str) -> None:
        sim = self._sims.get(name)
        if sim is None:
            raise KeyError(name)
        await sim.connect()

    async def disconnect(self, name: str) -> None:
        sim = self._sims.get(name)
        if sim is None:
            raise KeyError(name)
        await sim.disconnect()

    def status(self) -> List[Dict[str, str]]:
        return [sim.info() for sim in self._sims.values()]

    async def _broadcast_status(self) -> None:
        if self._broadcast is None:
            return
        try:
            await self._broadcast({"type": "sim_status", "sims": self.status()})
        except Exception as e:
            logger.warning(f"sim status broadcast failed: {e}")

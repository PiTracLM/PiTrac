import asyncio

import pytest

from models import ShotData
from sim_manager import SimManager


class _StubConfig:
    def __init__(self, values):
        self._v = values

    def get_config(self, key):
        return self._v.get(key)


class _StubSim:
    def __init__(self):
        self.name = "stub"
        self.display_name = "Stub"
        self.shots = []
        self.connected = False
        self._cb = None

    def set_status_callback(self, cb):
        self._cb = cb

    async def connect(self):
        self.connected = True

    async def disconnect(self):
        self.connected = False

    async def send_shot(self, shot):
        self.shots.append(shot)

    def info(self):
        return {"name": self.name, "status": "connected" if self.connected else "off"}


@pytest.mark.asyncio
async def test_disabled_ogs_creates_no_sims():
    cfg = _StubConfig({"simulators.ogs.enabled": False})
    mgr = SimManager(cfg, broadcast=None)
    mgr.build_sims()
    assert mgr.status() == []


@pytest.mark.asyncio
async def test_enabled_ogs_is_built():
    cfg = _StubConfig({
        "simulators.ogs.enabled": True, "simulators.ogs.auto_connect": False,
        "simulators.ogs.host": "1.2.3.4", "simulators.ogs.port": 3111,
        "simulators.ogs.keepalive_sec": 5,
    })
    mgr = SimManager(cfg, broadcast=None)
    mgr.build_sims()
    names = [s["name"] for s in mgr.status()]
    assert names == ["ogs"]


@pytest.mark.asyncio
async def test_on_shot_fans_out_and_isolates_failures():
    mgr = SimManager(_StubConfig({}), broadcast=None)
    good = _StubSim()

    class _Boom(_StubSim):
        async def send_shot(self, shot):
            raise RuntimeError("dead sim")

    bad = _Boom()
    mgr._sims = {"good": good, "boom": bad}
    shot = ShotData(speed=100)
    await mgr.on_shot(shot)  # must not raise
    assert good.shots == [shot]


@pytest.mark.asyncio
async def test_connect_disconnect_by_name():
    mgr = SimManager(_StubConfig({}), broadcast=None)
    sim = _StubSim()
    mgr._sims = {"stub": sim}
    await mgr.connect("stub")
    assert sim.connected is True
    await mgr.disconnect("stub")
    assert sim.connected is False

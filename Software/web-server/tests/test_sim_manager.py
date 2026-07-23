import asyncio

import pytest

from models import ShotData
from sim_manager import SimManager


class _StubConfig:
    def __init__(self, values):
        self._v = values

    def get_config(self, key):
        return self._v.get(key)

    def register_callback(self, pattern, callback):
        self.callbacks = getattr(self, "callbacks", []) + [(pattern, callback)]


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

    @property
    def status(self):
        return "connected" if self.connected else "off"

    async def send_shot(self, shot):
        self.shots.append(shot)

    def info(self):
        return {"name": self.name, "status": self.status}


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
    good.connected = True

    class _Boom(_StubSim):
        async def send_shot(self, shot):
            raise RuntimeError("dead sim")

    boom = _Boom()
    boom.connected = True

    skipped = _StubSim()  # left disconnected — should receive nothing

    mgr._sims = {"good": good, "boom": boom, "skipped": skipped}
    shot = ShotData(speed=100)
    await mgr.on_shot(shot)  # must not raise even though boom raises
    assert good.shots == [shot]
    assert skipped.shots == []


@pytest.mark.asyncio
async def test_connect_disconnect_by_name():
    mgr = SimManager(_StubConfig({}), broadcast=None)
    sim = _StubSim()
    mgr._sims = {"stub": sim}
    await mgr.connect("stub")
    assert sim.connected is True
    await mgr.disconnect("stub")
    assert sim.connected is False


def _ogs_config(enabled):
    return _StubConfig({
        "simulators.ogs.enabled": enabled, "simulators.ogs.auto_connect": False,
        "simulators.ogs.host": "1.2.3.4", "simulators.ogs.port": 3111,
        "simulators.ogs.keepalive_sec": 5,
    })


@pytest.mark.asyncio
async def test_config_change_rebuilds_sims(monkeypatch):
    monkeypatch.setattr(SimManager, "RELOAD_DEBOUNCE_SEC", 0.01)
    cfg = _ogs_config(False)
    mgr = SimManager(cfg, broadcast=None)
    mgr.loop = asyncio.get_running_loop()
    mgr.build_sims()
    assert mgr.status() == []
    assert cfg.callbacks[0][0] == "simulators."
    cfg._v["simulators.ogs.enabled"] = True
    mgr._on_config_change("simulators.ogs.enabled", True)
    await asyncio.sleep(0.1)
    assert [s["name"] for s in mgr.status()] == ["ogs"]


@pytest.mark.asyncio
async def test_burst_of_changes_reloads_once(monkeypatch):
    monkeypatch.setattr(SimManager, "RELOAD_DEBOUNCE_SEC", 0.05)
    mgr = SimManager(_ogs_config(True), broadcast=None)
    mgr.loop = asyncio.get_running_loop()
    builds = []
    mgr.build_sims = lambda: builds.append(1)
    for key in ("host", "port", "keepalive_sec"):
        mgr._on_config_change(f"simulators.ogs.{key}", 1)
        await asyncio.sleep(0.01)
    await asyncio.sleep(0.2)
    assert len(builds) == 1


@pytest.mark.asyncio
async def test_stop_cancels_pending_reload(monkeypatch):
    monkeypatch.setattr(SimManager, "RELOAD_DEBOUNCE_SEC", 0.05)
    mgr = SimManager(_ogs_config(True), broadcast=None)
    mgr.loop = asyncio.get_running_loop()
    builds = []
    mgr.build_sims = lambda: builds.append(1)
    mgr._on_config_change("simulators.ogs.host", "x")
    await asyncio.sleep(0)
    await mgr.stop()
    await asyncio.sleep(0.15)
    assert builds == []


@pytest.mark.asyncio
async def test_change_after_stop_does_not_reload(monkeypatch):
    monkeypatch.setattr(SimManager, "RELOAD_DEBOUNCE_SEC", 0.01)
    mgr = SimManager(_ogs_config(True), broadcast=None)
    mgr.loop = asyncio.get_running_loop()
    builds = []
    mgr.build_sims = lambda: builds.append(1)
    await mgr.stop()
    mgr._on_config_change("simulators.ogs.host", "x")
    await asyncio.sleep(0.05)
    assert builds == []

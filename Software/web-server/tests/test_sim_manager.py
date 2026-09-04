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
        self.ball_states = []
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

    async def on_ball_state(self, ball_detected):
        self.ball_states.append(ball_detected)

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


def _gspro_values(enabled):
    return {
        "simulators.gspro.enabled": enabled, "simulators.gspro.auto_connect": False,
        "simulators.gspro.host": "1.2.3.5", "simulators.gspro.port": 921,
    }


@pytest.mark.asyncio
async def test_enabled_gspro_is_built():
    mgr = SimManager(_StubConfig(_gspro_values(True)), broadcast=None)
    mgr.build_sims()
    assert [s["name"] for s in mgr.status()] == ["gspro"]


@pytest.mark.asyncio
async def test_both_sims_are_built():
    cfg = _ogs_config(True)
    cfg._v.update(_gspro_values(True))
    mgr = SimManager(cfg, broadcast=None)
    mgr.build_sims()
    assert [s["name"] for s in mgr.status()] == ["ogs", "gspro"]


@pytest.mark.asyncio
async def test_start_survives_unconvertible_settings_and_reports_an_error():
    cfg = _ogs_config(True)
    cfg._v.update(_gspro_values(True))
    cfg._v["simulators.gspro.port"] = "abc"
    mgr = SimManager(cfg, broadcast=None)
    await mgr.start()
    by_name = {s["name"]: s for s in mgr.status()}
    assert by_name["ogs"]["status"] != "error"
    assert by_name["gspro"]["status"] == "error"
    assert "abc" in by_name["gspro"]["detail"]


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
async def test_ball_state_changes_reach_sims_once():
    mgr = SimManager(_StubConfig({}), broadcast=None)
    sim = _StubSim()
    sim.connected = True
    mgr._sims = {"stub": sim}
    for status in ("Waiting For Ball", "Waiting For Ball", "Ball Placed", "Ball Placed"):
        await mgr.on_status(status)
    assert sim.ball_states == [False, True]


@pytest.mark.asyncio
async def test_gspro_reads_the_ball_state_the_manager_last_saw():
    mgr = SimManager(_StubConfig(_gspro_values(True)), broadcast=None)
    mgr.build_sims()
    assert mgr._sims["gspro"]._ball_state() is False
    await mgr.on_status("Ball Placed")
    assert mgr._sims["gspro"]._ball_state() is True


@pytest.mark.asyncio
async def test_start_does_not_wait_for_auto_connect():
    class _Hangs(_StubSim):
        async def connect(self):
            await asyncio.Event().wait()

    mgr = SimManager(_StubConfig({"simulators.stub.auto_connect": True}), broadcast=None)
    sim = _Hangs()
    mgr.build_sims = lambda: setattr(mgr, "_sims", {"stub": sim})

    await asyncio.wait_for(mgr.start(), 0.5)
    (task,) = mgr._connect_tasks
    await mgr.stop()
    await asyncio.sleep(0)
    assert task.cancelled()


@pytest.mark.asyncio
async def test_on_status_isolates_failures():
    mgr = SimManager(_StubConfig({}), broadcast=None)

    class _Boom(_StubSim):
        async def on_ball_state(self, ball_detected):
            raise RuntimeError("dead sim")

    boom = _Boom()
    boom.connected = True
    good = _StubSim()
    good.connected = True
    mgr._sims = {"boom": boom, "good": good}
    await mgr.on_status("Ball Placed")
    assert good.ball_states == [True]


@pytest.mark.asyncio
async def test_armed_follows_sims_that_have_an_arm_state():
    cfg = _ogs_config(True)
    cfg._v.update(_gspro_values(True))
    mgr = SimManager(cfg, broadcast=None)
    assert mgr.armed is True
    mgr.build_sims()
    for sim in mgr._sims.values():
        sim._status = "connected"
    assert mgr.armed is True
    stub = _StubSim()
    stub.connected = True
    stub.armed = False
    mgr._sims["stub"] = stub
    assert mgr.armed is False
    stub.armed = True
    assert mgr.armed is True


@pytest.mark.asyncio
async def test_gspro_club_is_kept_until_it_disconnects():
    mgr = SimManager(_StubConfig(_gspro_values(True)), broadcast=None)
    mgr.build_sims()
    gspro = mgr._sims["gspro"]
    gspro._status = "connected"
    assert mgr.club is None
    await gspro._on_message({"Code": 201, "Player": {"Club": "PT"}})
    assert mgr.club == "putter"
    await mgr.disconnect("gspro")
    assert mgr.club is None


@pytest.mark.asyncio
async def test_e6_is_built_and_gates_armed():
    mgr = SimManager(_StubConfig({
        "simulators.e6.enabled": True, "simulators.e6.auto_connect": False,
        "simulators.e6.host": "1.2.3.6", "simulators.e6.port": 2483,
        "simulators.e6.inter_message_delay_ms": 50,
    }), broadcast=None)
    mgr.build_sims()
    assert [s["name"] for s in mgr.status()] == ["e6"]
    e6 = mgr._sims["e6"]
    e6._status = "connected"
    assert mgr.armed is False
    await e6._on_message({"Type": "SimCommand", "SubType": "Arm"})
    assert mgr.armed is True
    await e6._on_message({"Type": "SimCommand", "SubType": "PlayerDataModified", "Details": {"ClubType": "Putter"}})
    assert mgr.club == "putter"
    await e6._on_message({"Type": "SimCommand", "SubType": "Disarm"})
    assert mgr.armed is False


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
async def test_connect_during_reload_reaches_the_rebuilt_sim(monkeypatch):
    monkeypatch.setattr(SimManager, "RELOAD_DEBOUNCE_SEC", 0)
    release = asyncio.Event()

    class _SlowStop(_StubSim):
        async def disconnect(self):
            await release.wait()
            self.connected = False

    mgr = SimManager(_StubConfig({}), broadcast=None)
    mgr.loop = asyncio.get_running_loop()
    built = []

    def build():
        built.append(_SlowStop())
        mgr._sims = {"stub": built[-1]}

    mgr.build_sims = build
    mgr.build_sims()
    mgr._on_config_change("simulators.stub.host", "x")
    for _ in range(5):
        await asyncio.sleep(0)
    connecting = asyncio.create_task(mgr.connect("stub"))
    for _ in range(5):
        await asyncio.sleep(0)
    release.set()
    await asyncio.wait_for(asyncio.gather(mgr._reload_task, connecting), 1)

    old, new = built
    assert old.connected is False
    assert new.connected is True
    assert mgr._sims["stub"] is new


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

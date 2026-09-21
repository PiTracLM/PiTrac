import asyncio

import pytest

from db.database import Database
from db.repositories import KeyValueRepository, SimulatorRepository
from models import ShotData
from sim_manager import SimManager, SimSettingsError, import_legacy_settings


@pytest.fixture
def db(tmp_path):
    d = Database(tmp_path / "t.db")
    yield d
    d.close()


@pytest.fixture
def repo(db):
    return SimulatorRepository(db)


@pytest.fixture
def mgr(repo):
    return SimManager(repo, broadcast=None)


class _StubSim:
    def __init__(self):
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
        return {"status": self.status, "detail": ""}


def _gspro(mgr, name=None, on=True, host="1.2.3.5", port=921):
    return mgr.create({"type": "gspro", "name": name, "on": on, "settings": {"host": host, "port": port}})


class _FakeSimulator:
    """A local asyncio TCP server that records what each client sends."""

    def __init__(self):
        self.received = b""
        self.server = None

    async def __aenter__(self):
        self.server = await asyncio.start_server(self._handle, "127.0.0.1", 0)
        self.port = self.server.sockets[0].getsockname()[1]
        return self

    async def __aexit__(self, *exc):
        self.server.close()

    async def _handle(self, reader, writer):
        while chunk := await reader.read(4096):
            self.received += chunk


async def _until(predicate, timeout=2.0):
    deadline = asyncio.get_running_loop().time() + timeout
    while not predicate():
        if asyncio.get_running_loop().time() > deadline:
            raise AssertionError("timed out")
        await asyncio.sleep(0.01)


# -- Instances and building --


@pytest.mark.asyncio
async def test_only_instances_that_are_on_are_built(mgr):
    on = _gspro(mgr)
    off = _gspro(mgr, on=False)
    mgr.build_sims()
    assert list(mgr._sims) == [on["id"]]
    status = {s["id"]: s for s in mgr.status()}
    assert status[off["id"]]["status"] == "off"
    assert off["id"] not in mgr._sims


@pytest.mark.asyncio
async def test_status_payload_shape(mgr):
    created = _gspro(mgr, name="Garage")
    (status,) = mgr.status()
    assert status == {
        "id": created["id"],
        "type": "gspro",
        "name": "Garage",
        "display_type": "GSPro",
        "on": True,
        "status": "off",
        "target": "1.2.3.5:921",
        "detail": "",
        "settings": {"host": "1.2.3.5", "port": 921},
    }


@pytest.mark.asyncio
async def test_name_defaults_to_the_display_name_then_counts_up(mgr):
    assert _gspro(mgr)["name"] == "GSPro"
    assert _gspro(mgr)["name"] == "GSPro 2"
    assert _gspro(mgr, name="  ")["name"] == "GSPro 3"
    assert mgr.create({"type": "e6", "settings": {"host": "10.0.0.6"}})["name"] == "E6 Connect"


@pytest.mark.asyncio
async def test_fields_get_their_defaults(mgr):
    e6 = mgr.create({"type": "e6", "settings": {"host": "10.0.0.6"}})
    assert e6["settings"] == {"host": "10.0.0.6", "port": 2483, "inter_message_delay_ms": 50}
    assert e6["on"] is True
    ogs = mgr.create({"type": "ogs", "on": False, "settings": {"host": "ipad.local", "port": "3112"}})
    assert ogs["settings"] == {"host": "ipad.local", "port": 3112, "keepalive_sec": 5}


@pytest.mark.parametrize("settings,field,message", [
    ({"port": 921}, "host", "Host is required"),
    ({"host": "1.2.3.4:921"}, "host", "Enter an IP address or hostname without a port"),
    ({"host": "1.2.3.4", "port": 0}, "port", "Port must be between 1 and 65535"),
    ({"host": "1.2.3.4", "port": 70000}, "port", "Port must be between 1 and 65535"),
    ({"host": "1.2.3.4", "port": "abc"}, "port", "Port must be a whole number"),
])
def test_field_validation(mgr, settings, field, message):
    with pytest.raises(SimSettingsError) as e:
        mgr.create({"type": "gspro", "settings": settings})
    assert e.value.fields == {field: message}
    assert mgr.status() == []


def test_unknown_type_is_rejected(mgr):
    with pytest.raises(SimSettingsError) as e:
        mgr.create({"type": "trackman", "settings": {"host": "1.2.3.4"}})
    assert "type" in e.value.fields


def test_update_merges_settings_and_keeps_the_type(mgr):
    created = _gspro(mgr)
    updated = mgr.update(created["id"], {"settings": {"port": 922}, "on": "false"})
    assert updated["settings"] == {"host": "1.2.3.5", "port": 922}
    assert updated["on"] is False
    assert updated["name"] == "GSPro"
    with pytest.raises(SimSettingsError):
        mgr.update(created["id"], {"type": "e6"})
    with pytest.raises(KeyError):
        mgr.update("missing", {"on": True})


def test_delete(mgr):
    created = _gspro(mgr)
    mgr.delete(created["id"])
    assert mgr.status() == []
    with pytest.raises(KeyError):
        mgr.delete(created["id"])


@pytest.mark.asyncio
async def test_invalid_stored_settings_report_an_error(mgr, repo):
    good = _gspro(mgr)
    bad = repo.add("gspro", "Broken", True, {"host": "1.2.3.4", "port": "abc"})
    await mgr.start()
    by_id = {s["id"]: s for s in mgr.status()}
    assert by_id[good["id"]]["status"] != "error"
    assert by_id[bad["id"]]["status"] == "error"
    assert "Port must be a whole number" in by_id[bad["id"]]["detail"]
    await mgr.stop()


# -- Shots, ball state, club --


@pytest.mark.asyncio
async def test_every_shot_reaches_two_connected_instances(mgr):
    async with _FakeSimulator() as one, _FakeSimulator() as two:
        _gspro(mgr, host="127.0.0.1", port=one.port)
        _gspro(mgr, host="127.0.0.1", port=two.port)
        await mgr.start()
        await _until(lambda: [s["status"] for s in mgr.status()] == ["connected", "connected"])
        await mgr.on_shot(ShotData(speed=100))
        await _until(lambda: b'"ContainsBallData": true' in one.received and b'"ContainsBallData": true' in two.received)
        await mgr.stop()


@pytest.mark.asyncio
async def test_on_shot_fans_out_and_isolates_failures(mgr):
    good = _StubSim()
    good.connected = True

    class _Boom(_StubSim):
        async def send_shot(self, shot):
            raise RuntimeError("dead sim")

    boom = _Boom()
    boom.connected = True
    skipped = _StubSim()

    mgr._sims = {"good": good, "boom": boom, "skipped": skipped}
    shot = ShotData(speed=100)
    await mgr.on_shot(shot)
    assert good.shots == [shot]
    assert skipped.shots == []


@pytest.mark.asyncio
async def test_shot_reaches_the_rest_when_a_reload_drops_a_sim_mid_send(mgr):
    class _Dropped(_StubSim):
        async def send_shot(self, shot):
            mgr._sims.pop("dropped")
            await asyncio.sleep(0)
            self.shots.append(shot)

    first, dropped, last = _StubSim(), _Dropped(), _StubSim()
    for sim in (first, dropped, last):
        sim.connected = True
    mgr._sims = {"first": first, "dropped": dropped, "last": last}
    shot = ShotData(speed=100)
    await mgr.on_shot(shot)
    assert (first.shots, last.shots) == ([shot], [shot])

    class _DroppedOnBall(_StubSim):
        async def on_ball_state(self, ball_detected):
            mgr._sims.pop("dropped")
            await asyncio.sleep(0)

    first, last = _StubSim(), _StubSim()
    dropped = _DroppedOnBall()
    for sim in (first, dropped, last):
        sim.connected = True
    mgr._sims = {"first": first, "dropped": dropped, "last": last}
    await mgr.on_status("Ball Placed")
    assert (first.ball_states, last.ball_states) == ([True], [True])


@pytest.mark.asyncio
async def test_ball_state_changes_reach_sims_once(mgr):
    sim = _StubSim()
    sim.connected = True
    mgr._sims = {"stub": sim}
    for status in ("Waiting For Ball", "Waiting For Ball", "Ball Placed", "Ball Placed"):
        await mgr.on_status(status)
    assert sim.ball_states == [False, True]


@pytest.mark.asyncio
async def test_on_status_isolates_failures(mgr):
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
async def test_gspro_reads_the_ball_state_the_manager_last_saw(mgr):
    created = _gspro(mgr)
    mgr.build_sims()
    assert mgr._sims[created["id"]]._ball_state() is False
    await mgr.on_status("Ball Placed")
    assert mgr._sims[created["id"]]._ball_state() is True


@pytest.mark.asyncio
async def test_club_comes_from_the_instance_that_last_reported(mgr):
    first, second = _gspro(mgr), _gspro(mgr)
    mgr.build_sims()
    a, b = mgr._sims[first["id"]], mgr._sims[second["id"]]
    a._status = b._status = "connected"
    assert mgr.club is None
    await a._on_message({"Code": 201, "Player": {"Club": "PT"}})
    assert mgr.club == "putter"
    await b._on_message({"Code": 201, "Player": {"Club": "DR"}})
    assert mgr.club == "driver"
    await mgr.disconnect(first["id"])
    assert mgr.club == "driver"
    await mgr.disconnect(second["id"])
    assert mgr.club is None


@pytest.mark.asyncio
async def test_e6_is_built_and_gates_armed(mgr):
    created = mgr.create({"type": "e6", "settings": {"host": "1.2.3.6"}})
    _gspro(mgr)
    mgr.build_sims()
    for sim in mgr._sims.values():
        sim._status = "connected"
    e6 = mgr._sims[created["id"]]
    assert mgr.armed is False
    await e6._on_message({"Type": "SimCommand", "SubType": "Arm"})
    assert mgr.armed is True
    await e6._on_message({"Type": "SimCommand", "SubType": "PlayerDataModified", "Details": {"ClubType": "Putter"}})
    assert mgr.club == "putter"
    await e6._on_message({"Type": "SimCommand", "SubType": "Disarm"})
    assert mgr.armed is False


# -- Connect, reload --


@pytest.mark.asyncio
async def test_connect_disconnect_by_id(mgr):
    sim = _StubSim()
    mgr._sims = {"abc": sim}
    await mgr.connect("abc")
    assert sim.connected is True
    await mgr.disconnect("abc")
    assert sim.connected is False
    with pytest.raises(KeyError):
        await mgr.connect("missing")


@pytest.mark.asyncio
async def test_start_does_not_wait_for_connect(mgr):
    class _Hangs(_StubSim):
        async def connect(self):
            await asyncio.Event().wait()

    sim = _Hangs()
    mgr.build_sims = lambda: setattr(mgr, "_sims", {"stub": sim})
    await asyncio.wait_for(mgr.start(), 0.5)
    task = mgr._connect_tasks["stub"]
    await mgr.stop()
    await asyncio.sleep(0)
    assert task.cancelled()


@pytest.mark.asyncio
async def test_changes_rebuild_only_that_instance(mgr, monkeypatch):
    monkeypatch.setattr(SimManager, "RELOAD_DEBOUNCE_SEC", 0.01)
    keep = _gspro(mgr)
    change = _gspro(mgr)
    await mgr.start()
    kept_sim = mgr._sims[keep["id"]]

    mgr.update(change["id"], {"on": False})
    await asyncio.sleep(0.1)
    assert list(mgr._sims) == [keep["id"]]

    mgr.update(change["id"], {"on": True, "settings": {"port": 922}})
    await asyncio.sleep(0.1)
    assert mgr._sims[change["id"]].port == 922
    assert mgr._sims[keep["id"]] is kept_sim

    mgr.delete(change["id"])
    await asyncio.sleep(0.1)
    assert list(mgr._sims) == [keep["id"]]
    await mgr.stop()


@pytest.mark.asyncio
async def test_connect_during_reload_reaches_the_rebuilt_sim(mgr, monkeypatch):
    monkeypatch.setattr(SimManager, "RELOAD_DEBOUNCE_SEC", 0)
    release = asyncio.Event()

    class _SlowStop(_StubSim):
        async def disconnect(self):
            await release.wait()
            self.connected = False

    created = _gspro(mgr)
    built = []

    def build(inst):
        built.append(_SlowStop())
        mgr._sims[inst["id"]] = built[-1]

    mgr._build = build
    await mgr.start()
    mgr.update(created["id"], {"name": "Garage"})
    for _ in range(5):
        await asyncio.sleep(0)
    connecting = asyncio.create_task(mgr.connect(created["id"]))
    for _ in range(5):
        await asyncio.sleep(0)
    release.set()
    await asyncio.wait_for(asyncio.gather(mgr._reload_tasks[created["id"]], connecting), 1)

    old, new = built
    assert old.connected is False
    assert new.connected is True
    assert mgr._sims[created["id"]] is new


@pytest.mark.asyncio
async def test_burst_of_changes_reloads_once(mgr, monkeypatch):
    monkeypatch.setattr(SimManager, "RELOAD_DEBOUNCE_SEC", 0.05)
    created = _gspro(mgr)
    await mgr.start()
    builds = []
    mgr._build = builds.append
    for port in (922, 923, 924):
        mgr.update(created["id"], {"settings": {"port": port}})
        await asyncio.sleep(0.01)
    await asyncio.sleep(0.2)
    assert len(builds) == 1
    await mgr.stop()


@pytest.mark.asyncio
async def test_stop_cancels_pending_reload(mgr, monkeypatch):
    monkeypatch.setattr(SimManager, "RELOAD_DEBOUNCE_SEC", 0.05)
    created = _gspro(mgr)
    await mgr.start()
    builds = []
    mgr._build = builds.append
    mgr.update(created["id"], {"name": "x"})
    await asyncio.sleep(0)
    await mgr.stop()
    await asyncio.sleep(0.15)
    assert builds == []


@pytest.mark.asyncio
async def test_change_before_start_or_after_stop_does_not_reload(mgr, monkeypatch):
    monkeypatch.setattr(SimManager, "RELOAD_DEBOUNCE_SEC", 0.01)
    builds = []
    created = _gspro(mgr)
    await mgr.start()
    await mgr.stop()
    mgr._build = builds.append
    mgr.update(created["id"], {"name": "x"})
    await asyncio.sleep(0.05)
    assert builds == []


# -- Test connection --


@pytest.mark.asyncio
async def test_test_connection_reports_success_without_saving(mgr):
    async with _FakeSimulator() as fake:
        result = await mgr.test_connection({"type": "gspro", "settings": {"host": "127.0.0.1", "port": fake.port}})
    assert result == {"ok": True, "message": f"Connected to GSPro at 127.0.0.1:{fake.port}"}
    assert mgr.status() == []


@pytest.mark.asyncio
async def test_test_connection_reports_the_error_on_a_closed_port(mgr):
    async with _FakeSimulator() as fake:
        port = fake.port
    result = await mgr.test_connection({"type": "gspro", "settings": {"host": "127.0.0.1", "port": port}})
    assert result["ok"] is False
    assert result["message"].startswith(f"Could not connect to GSPro at 127.0.0.1:{port}: ")
    assert len(result["message"]) > len(f"Could not connect to GSPro at 127.0.0.1:{port}: ")


# -- Moving the old one-per-type settings --


def _legacy(db, rows):
    settings = KeyValueRepository(db, "settings")
    settings.replace_all(rows)
    return settings


def test_old_settings_become_instances_and_are_removed(db, repo):
    settings = _legacy(db, {
        "simulators.gspro.host": "10.0.0.5",
        "simulators.gspro.port": 9210,
        "simulators.gspro.enabled": True,
        "simulators.gspro.auto_connect": True,
        "simulators.e6.host": "10.0.0.6",
        "simulators.e6.inter_message_delay_ms": "75",
        "simulators.ogs.port": 3112,
        "cameras.slot1.type": "4",
    })

    assert import_legacy_settings(settings, repo) is True

    sims = {s["type"]: s for s in repo.list()}
    assert set(sims) == {"gspro", "e6"}
    assert (sims["gspro"]["name"], sims["gspro"]["on"]) == ("GSPro", True)
    assert sims["gspro"]["settings"] == {"host": "10.0.0.5", "port": 9210}
    assert (sims["e6"]["name"], sims["e6"]["on"]) == ("E6 Connect", False)
    assert sims["e6"]["settings"] == {"host": "10.0.0.6", "port": 2483, "inter_message_delay_ms": 75}
    assert settings.load() == {"cameras.slot1.type": "4"}


def test_moving_old_settings_twice_changes_nothing(db, repo):
    settings = _legacy(db, {"simulators.ogs.host": "ipad.local", "simulators.ogs.enabled": True})
    assert import_legacy_settings(settings, repo) is True
    before = repo.list()
    assert import_legacy_settings(settings, repo) is False
    assert repo.list() == before


@pytest.mark.parametrize("stored,on", [("false", False), ("0", False), ("true", True), ("1", True), ("maybe", False)])
def test_old_enabled_is_read_by_type_not_truthiness(db, repo, stored, on):
    settings = _legacy(db, {"simulators.gspro.host": "10.0.0.5", "simulators.gspro.enabled": stored})
    import_legacy_settings(settings, repo)
    (sim,) = repo.list()
    assert sim["on"] is on


def test_old_values_that_do_not_validate_fall_back_to_defaults(db, repo):
    settings = _legacy(db, {"simulators.gspro.enabled": True, "simulators.gspro.port": "abc"})
    import_legacy_settings(settings, repo)
    (sim,) = repo.list()
    assert sim["settings"] == {"host": "", "port": 921}
    assert settings.load() == {}


def test_old_keys_matching_an_existing_instance_make_no_duplicate(db, repo):
    repo.add("gspro", "Garage", True, {"host": "10.0.0.5", "port": 921})
    settings = _legacy(db, {"simulators.gspro.host": "10.0.0.5", "simulators.gspro.enabled": True})

    assert import_legacy_settings(settings, repo) is True

    assert [s["name"] for s in repo.list()] == ["Garage"]
    assert settings.load() == {}


def test_old_keys_with_another_host_still_make_an_instance(db, repo):
    repo.add("gspro", "Garage", True, {"host": "10.0.0.5", "port": 921})
    settings = _legacy(db, {"simulators.gspro.host": "10.0.0.6", "simulators.gspro.enabled": True})
    import_legacy_settings(settings, repo)
    assert [s["name"] for s in repo.list()] == ["Garage", "GSPro"]

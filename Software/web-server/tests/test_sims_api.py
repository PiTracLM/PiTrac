import time

import pytest

from models import ShotData


def test_ogs_config_defaults_resolve(server_instance):
    cm = server_instance.config_manager
    assert cm.get_config("simulators.ogs.enabled") is False
    assert cm.get_config("simulators.ogs.auto_connect") is False
    assert cm.get_config("simulators.ogs.host") == ""
    assert cm.get_config("simulators.ogs.port") == 3111
    assert cm.get_config("simulators.ogs.keepalive_sec") == 5


def test_get_sims_endpoint(client):
    r = client.get("/api/sims")
    assert r.status_code == 200
    assert isinstance(r.json().get("sims"), list)


def test_connect_unknown_sim_returns_404(client):
    r = client.post("/api/sims/nope/connect")
    assert r.status_code == 404


def test_connect_on_a_build_error_sim_returns_404_with_error(client, server_instance):
    server_instance.sim_manager._build_errors = {
        "gspro": {"name": "gspro", "display_name": "GSPro", "status": "error", "detail": "Invalid settings"},
    }
    assert [s["name"] for s in client.get("/api/sims").json()["sims"]] == ["gspro"]
    r = client.post("/api/sims/gspro/connect")
    assert r.status_code == 404
    assert r.json()["error"] == "Unknown sim: gspro"


def _install_on_shot_spy(server_instance):
    """Replace sim_manager.on_shot with an async spy that records its calls."""
    calls = []

    async def spy(shot):
        calls.append(shot)

    server_instance.sim_manager.on_shot = spy
    return calls


def _wait_for(predicate, timeout=1.0):
    """Poll until predicate() is true or timeout, so the fire-and-forget task can run."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.01)
    return predicate()


def test_real_shot_is_forwarded_to_sims(client, server_instance):
    # result_type 7 = "Hit" with no fake-hit message -> a real shot
    calls = _install_on_shot_spy(server_instance)

    r = client.post("/api/internal/shot-result", json={
        "result_type": 7,
        "speed_mps": 45.0,
        "launch_angle": 12,
        "side_angle": 1,
        "back_spin": 2500,
        "side_spin": 100,
    })
    assert r.status_code == 200

    assert _wait_for(lambda: len(calls) == 1)
    assert len(calls) == 1
    assert isinstance(calls[0], ShotData)


def test_club_change_is_not_forwarded_to_sims(client, server_instance):
    calls = _install_on_shot_spy(server_instance)

    r = client.post("/api/internal/shot-result", json={
        "result_type": 10,
        "message": "Club type was set to Putter",
    })
    assert r.status_code == 200

    # Give any erroneously-scheduled task a chance to run, then confirm none did.
    _wait_for(lambda: len(calls) > 0)
    assert calls == []


def test_sims_get_unrounded_values(client, server_instance):
    calls = _install_on_shot_spy(server_instance)
    client.post("/api/internal/shot-result", json={
        "result_type": 7, "speed_mps": 65.0, "launch_angle": 12.25, "side_angle": -2.15,
    })
    assert _wait_for(lambda: len(calls) == 1)
    assert calls[0].speed == pytest.approx(65.0 * 2.23694)
    assert (calls[0].launch_angle, calls[0].side_angle) == (12.25, -2.15)
    stored = server_instance.shot_store.get()
    assert (stored.speed, stored.launch_angle, stored.side_angle) == (145.4, 12.2, -2.1)


def test_status_reply_carries_armed_and_club(client, server_instance):
    status = {"result_type": 2, "message": "Waiting for ball to be teed up."}
    r = client.post("/api/internal/shot-result", json=status)
    assert r.json() == {"status": "ok", "armed": True}
    server_instance.sim_manager.set_club("gspro", "putter")
    r = client.post("/api/internal/shot-result", json=status)
    assert r.json() == {"status": "ok", "armed": True, "club": "putter"}


def test_sims_are_not_held_up_by_the_shot_insert(client, server_instance):
    calls = _install_on_shot_spy(server_instance)
    sims_called_during_insert = []

    def slow_persist(*args):
        sims_called_during_insert.append(_wait_for(lambda: len(calls) == 1))

    server_instance._persist_shot = slow_persist
    client.post("/api/internal/shot-result", json={"result_type": 7, "speed_mps": 45.0})
    assert sims_called_during_insert == [True]


def test_club_change_is_not_shown_on_the_dashboard(client, server_instance):
    broadcasts = []

    async def spy(message):
        broadcasts.append(message)

    server_instance.connection_manager.broadcast = spy
    before = server_instance.shot_store.get()
    server_instance.sim_manager.set_club("gspro", "putter")
    r = client.post("/api/internal/shot-result", json={
        "result_type": 10,
        "message": "Club type was set to Putter",
    })
    assert r.json() == {"status": "ok", "armed": True, "club": "putter"}
    assert broadcasts == []
    assert server_instance.shot_store.get() == before

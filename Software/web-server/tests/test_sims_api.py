import copy
import json
import time

from models import ShotData


def test_python_passed_via_excluded_from_golf_sim_config(server_instance, monkeypatch):
    cm = server_instance.config_manager
    sentinel = "203.0.113.77"

    meta = copy.deepcopy(cm.load_configurations_metadata())
    meta["settings"]["simulators.ogs.host"] = {
        "category": "Simulators", "subcategory": "basic",
        "displayName": "OGS Host", "description": "x",
        "type": "ip_address", "default": sentinel, "passedVia": "python",
    }
    monkeypatch.setattr(cm, "load_configurations_metadata", lambda: meta)

    merged = copy.deepcopy(cm.merged_config)
    merged.setdefault("simulators", {}).setdefault("ogs", {})["host"] = sentinel
    monkeypatch.setattr(cm, "merged_config", merged)

    out_path = cm.generate_golf_sim_config()
    flat = out_path.read_text()
    assert sentinel not in flat


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


def test_status_or_fake_hit_is_not_forwarded_to_sims(client, server_instance):
    # result_type 7 with a fake-hit message -> must NOT be forwarded
    calls = _install_on_shot_spy(server_instance)

    r = client.post("/api/internal/shot-result", json={
        "result_type": 7,
        "message": "Club type was set",
    })
    assert r.status_code == 200

    # Give any erroneously-scheduled task a chance to run, then confirm none did.
    _wait_for(lambda: len(calls) > 0)
    assert calls == []

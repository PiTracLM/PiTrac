import copy
import json


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

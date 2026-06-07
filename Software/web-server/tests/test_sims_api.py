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

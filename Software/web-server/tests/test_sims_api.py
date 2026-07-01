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

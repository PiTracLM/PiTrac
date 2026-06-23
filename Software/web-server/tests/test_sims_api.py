def test_ogs_config_defaults_resolve(server_instance):
    cm = server_instance.config_manager
    assert cm.get_config("simulators.ogs.enabled") is False
    assert cm.get_config("simulators.ogs.auto_connect") is False
    assert cm.get_config("simulators.ogs.host") == ""
    assert cm.get_config("simulators.ogs.port") == 3111
    assert cm.get_config("simulators.ogs.keepalive_sec") == 5

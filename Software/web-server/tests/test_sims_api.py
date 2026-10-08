import asyncio
import time

import httpx
import pytest

from models import ShotData


def test_sim_types(client):
    types = {t["type"]: t for t in client.get("/api/sims/types").json()}
    assert [t for t in types] == ["gspro", "e6", "ogs"]
    assert types["gspro"]["display_name"] == "GSPro"
    assert [f["key"] for f in types["e6"]["fields"]] == ["host", "port", "inter_message_delay_ms"]
    assert types["ogs"]["fields"][2]["advanced"] is True


def test_create_list_update_delete(client):
    first = client.post("/api/sims", json={"type": "gspro", "settings": {"host": "10.0.0.5"}})
    assert first.status_code == 200
    first = first.json()
    assert (first["name"], first["on"], first["status"], first["target"]) == ("GSPro", True, "off", "10.0.0.5:921")
    second = client.post("/api/sims", json={"type": "gspro", "on": False, "settings": {"host": "10.0.0.6"}}).json()
    assert second["name"] == "GSPro 2"
    assert [s["id"] for s in client.get("/api/sims").json()] == [first["id"], second["id"]]

    updated = client.put(f"/api/sims/{second['id']}", json={"settings": {"port": 922}})
    assert updated.status_code == 200
    assert updated.json()["settings"] == {"host": "10.0.0.6", "port": 922}

    assert client.delete(f"/api/sims/{first['id']}").status_code == 200
    assert [s["id"] for s in client.get("/api/sims").json()] == [second["id"]]
    assert client.delete(f"/api/sims/{first['id']}").status_code == 404
    assert client.put(f"/api/sims/{first['id']}", json={"on": True}).status_code == 404


def test_invalid_fields_are_a_400_with_the_field_errors(client):
    r = client.post("/api/sims", json={"type": "gspro", "settings": {"host": "1.2.3.4:921", "port": 0}})
    assert r.status_code == 400
    assert r.json()["fields"] == {
        "host": "Enter an IP address or hostname without a port",
        "port": "Port must be between 1 and 65535",
    }
    assert r.json()["error"]
    assert client.post("/api/sims", content=b"not json").status_code == 400
    assert client.get("/api/sims").json() == []


def test_connect_and_disconnect(client, server_instance):
    off = client.post("/api/sims", json={"type": "gspro", "on": False, "settings": {"host": "10.0.0.5"}}).json()
    assert client.post("/api/sims/nope/connect").status_code == 404
    assert client.post(f"/api/sims/{off['id']}/connect").status_code == 409

    calls = []

    class _Sim:
        status = "off"

        def info(self):
            return {"status": self.status, "detail": ""}

        async def connect(self):
            calls.append("connect")

        async def disconnect(self):
            calls.append("disconnect")

    server_instance.sim_manager._sims[off["id"]] = _Sim()
    r = client.post(f"/api/sims/{off['id']}/connect")
    assert r.status_code == 200
    assert r.json()[0]["id"] == off["id"]
    assert client.post(f"/api/sims/{off['id']}/disconnect").status_code == 200
    assert calls == ["connect", "disconnect"]


@pytest.mark.asyncio
async def test_test_route_succeeds_against_a_listening_simulator(server_instance):
    async def handle(reader, writer):
        await reader.read()

    server = await asyncio.start_server(handle, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    try:
        async with _client(server_instance) as client:
            r = await client.post("/api/sims/test", json={"type": "gspro", "settings": {"host": "127.0.0.1", "port": port}})
    finally:
        server.close()
    assert r.status_code == 200
    assert r.json() == {"ok": True, "message": f"Connected to GSPro at 127.0.0.1:{port}"}
    assert server_instance.sim_manager.status() == []


@pytest.mark.asyncio
async def test_test_route_reports_the_error_on_a_closed_port(server_instance):
    server = await asyncio.start_server(lambda r, w: None, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    server.close()
    await server.wait_closed()
    async with _client(server_instance) as client:
        r = await client.post("/api/sims/test", json={"type": "gspro", "settings": {"host": "127.0.0.1", "port": port}})
    assert r.status_code == 200
    assert r.json()["ok"] is False
    assert r.json()["message"].startswith(f"Could not connect to GSPro at 127.0.0.1:{port}: ")


def test_test_route_validates_fields(client):
    r = client.post("/api/sims/test", json={"type": "gspro", "settings": {}})
    assert r.status_code == 400
    assert r.json()["fields"] == {"host": "Host is required"}


def _client(server_instance):
    return httpx.AsyncClient(transport=httpx.ASGITransport(app=server_instance.app), base_url="http://test")


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
    server_instance.sim_manager.set_club("abc", "putter")
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
    server_instance.sim_manager.set_club("abc", "putter")
    r = client.post("/api/internal/shot-result", json={
        "result_type": 10,
        "message": "Club type was set to Putter",
    })
    assert r.json() == {"status": "ok", "armed": True, "club": "putter"}
    assert broadcasts == []
    assert server_instance.shot_store.get() == before

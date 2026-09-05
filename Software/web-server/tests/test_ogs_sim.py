import asyncio
import json
from unittest.mock import MagicMock

import pytest

from models import ShotData
from sims.ogs_sim import OGSSim, build_shot_payload


def test_build_shot_payload_maps_and_clamps():
    shot = ShotData(speed=101.2, launch_angle=15.4, side_angle=-2.1,
                     back_spin=3000, side_spin=-300)
    p = build_shot_payload(shot)
    assert p["type"] == "shot"
    assert p["unit"] == "imperial"
    s = p["shot"]
    assert s["ballSpeed"] == 101.2
    assert s["verticalLaunchAngle"] == 15.4
    assert s["horizontalLaunchAngle"] == -2.1
    # total spin magnitude and a negative (left) axis for negative side spin
    assert s["spinSpeed"] == pytest.approx(3015, abs=1)
    assert s["spinAxis"] < 0


def test_build_shot_payload_clamps_out_of_range():
    shot = ShotData(speed=200, launch_angle=80, side_angle=-90,
                    back_spin=0, side_spin=9000)
    s = build_shot_payload(shot)["shot"]
    assert s["verticalLaunchAngle"] == 45
    assert s["horizontalLaunchAngle"] == -45
    assert -45 <= s["spinAxis"] <= 45


class _FakeOGS:
    """Minimal asyncio TCP server that records newline-delimited JSON it receives."""
    def __init__(self):
        self.messages = []
        self.connection_count = 0
        self._server = None
        self.port = None

    async def start(self):
        self._server = await asyncio.start_server(self._handle, "127.0.0.1", 0)
        self.port = self._server.sockets[0].getsockname()[1]

    async def _handle(self, reader, writer):
        self.connection_count += 1
        while True:
            line = await reader.readline()
            if not line:
                break
            self.messages.append(json.loads(line))
        writer.close()

    async def stop(self):
        self._server.close()
        await self._server.wait_closed()


@pytest.fixture(autouse=True)
def _enable_sockets_if_plugin_present():
    try:
        import pytest_socket
        pytest_socket.enable_socket()
    except ImportError:
        pass


@pytest.mark.asyncio
async def test_connect_sends_ready_then_shot():
    fake = _FakeOGS()
    await fake.start()
    sim = OGSSim(host="127.0.0.1", port=fake.port, keepalive_sec=999)
    await sim.connect()
    await sim.send_shot(ShotData(speed=99.0, launch_angle=12.0, side_angle=1.0,
                                 back_spin=2500, side_spin=200))
    await asyncio.sleep(0.05)
    await sim.disconnect()
    await fake.stop()

    assert sim.status == "off"
    types = [m["type"] for m in fake.messages]
    assert types[0] == "device" and fake.messages[0]["status"] == "ready"
    assert "shot" in types


@pytest.mark.asyncio
async def test_double_connect_does_not_leak():
    fake = _FakeOGS()
    await fake.start()
    sim = OGSSim(host="127.0.0.1", port=fake.port, keepalive_sec=999)
    await sim.connect()
    first_task = sim._keepalive_task
    await sim.connect()  # second connect must be a no-op
    await asyncio.sleep(0.05)
    assert sim._keepalive_task is first_task      # keepalive task not replaced
    assert fake.connection_count == 1             # only one socket opened
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_connect_failure_sets_error_status():
    # Nothing listening on this port.
    sim = OGSSim(host="127.0.0.1", port=1, keepalive_sec=999)
    await sim.connect()
    await asyncio.sleep(0.05)
    assert sim.status in ("error", "connecting")
    await sim.disconnect()


@pytest.mark.asyncio
async def test_connect_timeout_is_a_connect_failure(monkeypatch):
    async def never_connects(host, port):
        await asyncio.Event().wait()

    monkeypatch.setattr(asyncio, "open_connection", never_connects)
    sim = OGSSim(host="10.255.255.1", port=3111, keepalive_sec=999)
    sim.CONNECT_TIMEOUT_SEC = 0.05

    await asyncio.wait_for(sim.connect(), 1)

    assert sim.status == "error"
    assert sim._writer is None
    assert sim._reconnect_task is not None
    await sim.disconnect()


@pytest.mark.asyncio
async def test_cancel_during_first_send_closes_socket(monkeypatch):
    async def stalled_drain():
        await asyncio.Event().wait()

    writer = MagicMock()
    writer.drain = stalled_drain

    async def opens(host, port):
        return MagicMock(), writer

    monkeypatch.setattr(asyncio, "open_connection", opens)
    sim = OGSSim(host="127.0.0.1", port=3111, keepalive_sec=999)
    task = asyncio.create_task(sim.connect())
    while not writer.write.called:
        await asyncio.sleep(0)

    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task

    writer.close.assert_called_once()
    assert sim._writer is None
    assert sim.status == "off"


@pytest.mark.asyncio
async def test_disconnect_during_connect_closes_the_late_socket(monkeypatch):
    opened = asyncio.Event()
    writer = MagicMock()

    async def slow_open(host, port):
        await opened.wait()
        return MagicMock(), writer

    monkeypatch.setattr(asyncio, "open_connection", slow_open)
    sim = OGSSim(host="127.0.0.1", port=3111, keepalive_sec=999)
    task = asyncio.create_task(sim.connect())
    for _ in range(5):
        await asyncio.sleep(0)
    await sim.disconnect()
    opened.set()
    await task

    writer.close.assert_called_once()
    writer.write.assert_not_called()
    assert sim._writer is None
    assert sim.status == "off"


@pytest.mark.asyncio
async def test_failed_connect_after_disconnect_leaves_status_off(monkeypatch):
    refused = asyncio.Event()

    async def slow_refusal(host, port):
        await refused.wait()
        raise ConnectionRefusedError("refused")

    monkeypatch.setattr(asyncio, "open_connection", slow_refusal)
    sim = OGSSim(host="127.0.0.1", port=3111, keepalive_sec=999)
    task = asyncio.create_task(sim.connect())
    for _ in range(5):
        await asyncio.sleep(0)
    await sim.disconnect()
    refused.set()
    await task

    assert sim.status == "off"
    assert sim._reconnect_task is None


@pytest.mark.asyncio
async def test_disconnect_does_not_wait_forever_for_the_socket_to_close():
    async def never_closes():
        await asyncio.Event().wait()

    writer = MagicMock()
    writer.wait_closed = never_closes
    sim = OGSSim(host="127.0.0.1", port=3111, keepalive_sec=999)
    sim.DISCONNECT_TIMEOUT_SEC = 0.05
    sim._writer = writer

    await asyncio.wait_for(sim.disconnect(), 1)

    writer.transport.abort.assert_called_once()
    assert sim.status == "off"

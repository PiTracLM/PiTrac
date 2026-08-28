import asyncio
import json
from unittest.mock import MagicMock

import pytest

from models import ShotData
from sims.e6_auth import E6AuthUnavailable
from sims.e6_sim import E6Sim

_ARM = b'{"Type":"SimCommand","SubType":"Arm"}'
_DISARM = b'{"Type":"SimCommand","SubType":"Disarm"}'
_PING = b'{"Type":"SimCommand","SubType":"Ping"}'
_CHALLENGE = b'{"Type":"Handshake",  "Challenge":"abc123","E6Version":"2, 0, 0, 0"}'


class _FakeE6:
    """Asyncio TCP server playing E6 Connect: challenge on Handshake, authenticates any reply, ACKs everything."""

    def __init__(self, auth_reply=b'{"Type":"Authentication","Success":"true"}', challenge=_CHALLENGE):
        self.messages = []
        self.raw = b""
        self.auth_reply = auth_reply
        self.challenge = challenge
        self.writers = []
        self._server = None
        self.port = None

    async def start(self):
        self._server = await asyncio.start_server(self._handle, "127.0.0.1", 0)
        self.port = self._server.sockets[0].getsockname()[1]

    async def _handle(self, reader, writer):
        self.writers.append(writer)
        decoder = json.JSONDecoder()
        buf = ""
        while True:
            chunk = await reader.read(4096)
            if not chunk:
                break
            self.raw += chunk
            buf += chunk.decode()
            while buf := buf.lstrip():
                try:
                    obj, end = decoder.raw_decode(buf)
                except json.JSONDecodeError:
                    buf = ""
                    break
                buf = buf[end:]
                self.messages.append(obj)
                writer.write(json.dumps({"Type": "ACK", "SubType": obj["Type"], "Details": "Success."}).encode())
                if obj["Type"] == "Handshake" and self.challenge:
                    writer.write(self.challenge)
                elif obj["Type"] == "Challenge":
                    writer.write(self.auth_reply)
        writer.close()

    def send(self, data):
        self.writers[-1].write(data)

    def types(self):
        return [m["Type"] for m in self.messages]

    async def stop(self):
        self._server.close()
        await self._server.wait_closed()


def _fake_auth(seen, reply='{"Type":"Challenge","Developer":"dev","Hash":"hash"}'):
    def answer(text):
        seen.append(text)
        return reply

    return answer


async def _until(predicate, timeout=1.0):
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while not predicate():
        assert loop.time() < deadline, "timed out waiting"
        await asyncio.sleep(0.005)


@pytest.fixture(autouse=True)
def _enable_sockets_if_plugin_present():
    try:
        import pytest_socket

        pytest_socket.enable_socket()
    except ImportError:
        pass


async def _connected(fake, **kwargs):
    kwargs.setdefault("auth", _fake_auth([]))
    sim = E6Sim(host="127.0.0.1", port=fake.port, **kwargs)
    await sim.connect()
    await _until(lambda: sim.status != "connecting")
    return sim


async def _armed(fake, **kwargs):
    sim = await _connected(fake, **kwargs)
    fake.send(_ARM)
    await _until(lambda: sim.armed)
    return sim


@pytest.mark.asyncio
async def test_handshake_is_answered_by_the_auth_function_then_connects():
    fake = _FakeE6()
    await fake.start()
    seen = []
    sim = E6Sim(host="127.0.0.1", port=fake.port, auth=_fake_auth(seen))
    await sim.connect()
    assert sim.status == "connecting"
    await _until(lambda: sim.status == "connected")

    assert seen == [_CHALLENGE.decode()]
    assert fake.messages[:2] == [{"Type": "Handshake"}, {"Type": "Challenge", "Developer": "dev", "Hash": "hash"}]
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_auth_reply_is_written_verbatim():
    # The shim unquotes numeric-looking strings, which is not valid JSON but is what the C++ sent
    reply = '{"Type":"Challenge","Developer":0123,"Hash":"abc"}'
    fake = _FakeE6()
    await fake.start()
    sim = E6Sim(host="127.0.0.1", port=fake.port, auth=_fake_auth([], reply))
    await sim.connect()
    await _until(lambda: fake.raw.endswith(reply.encode()))

    assert fake.raw == b'{"Type": "Handshake"}' + reply.encode()
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_failed_authentication_sets_error_and_does_not_arm():
    fake = _FakeE6(auth_reply=b'{"Type":"Authentication","Success":"false","Message":"bad developer"}' + _ARM)
    await fake.start()
    sim = await _connected(fake)

    assert sim.status == "error"
    assert "bad developer" in sim.info()["detail"]
    assert sim.armed is False
    assert sim._reconnect_task is None
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_missing_auth_library_is_an_error_without_reconnecting():
    def unavailable(text):
        raise E6AuthUnavailable("e6 auth library missing: /usr/lib/pitrac/libpitrac_e6.so")

    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake, auth=unavailable)

    assert sim.status == "error"
    assert "e6 auth library missing" in sim.info()["detail"]
    assert sim._reconnect_task is None and sim._writer is None
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_empty_auth_reply_is_an_authentication_failure():
    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake, auth=_fake_auth([], ""))

    assert sim.status == "error"
    assert "authentication failed" in sim.info()["detail"]
    assert sim._reconnect_task is None
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_no_authentication_in_time_is_an_error_that_reconnects(monkeypatch):
    monkeypatch.setattr(E6Sim, "AUTH_TIMEOUT_SEC", 0.05)
    fake = _FakeE6(challenge=None)
    await fake.start()
    sim = await _connected(fake)

    assert sim.status == "error"
    assert sim.info()["detail"] == "E6 did not authenticate"
    assert sim._reconnect_task is not None and not sim._reconnect_task.done()
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_arm_and_disarm_set_armed_and_the_detail():
    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake)
    assert sim.armed is False

    fake.send(_ARM)
    await _until(lambda: sim.armed)
    assert sim.info()["detail"].endswith("armed")

    fake.send(_DISARM)
    await _until(lambda: not sim.armed)
    assert not sim.info()["detail"].endswith("armed")
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_ping_gets_pong():
    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake)
    fake.send(_PING)

    await _until(lambda: fake.types()[-1] == "Pong")
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_send_shot_while_disarmed_sends_nothing():
    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake)
    sent = len(fake.messages)
    await sim.send_shot(ShotData(speed=100.0, back_spin=3000))
    fake.send(_PING)
    await _until(lambda: fake.types()[-1] == "Pong")

    assert fake.types()[sent:] == ["Pong"]
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_armed_shot_sends_three_messages_with_the_delay_then_disarms(monkeypatch):
    fake = _FakeE6()
    await fake.start()
    sim = await _armed(fake, inter_message_delay_ms=1)

    sleeps = []
    real_sleep = asyncio.sleep

    async def recording_sleep(delay, *args):
        sleeps.append(delay)
        await real_sleep(delay, *args)

    monkeypatch.setattr(asyncio, "sleep", recording_sleep)
    await sim.send_shot(ShotData(speed=101.24, launch_angle=15.4, side_angle=-2.1, back_spin=3000, side_spin=-300))
    monkeypatch.undo()
    await _until(lambda: fake.types()[-1] == "SendShot")

    assert sleeps == [0.001, 0.001]
    assert fake.types()[-3:] == ["SetBallData", "SetClubData", "SendShot"]
    assert fake.messages[-3]["BallData"] == {
        "BackSpin": 3000.0,
        "BallSpeed": 101.2,
        "LaunchAngle": 15.4,
        "LaunchDirection": -2.1,
        "SideSpin": -300.0,
    }
    assert fake.messages[-2]["ClubData"] == {
        "ClubHeadSpeed": 0.0,
        "ClubAngleFace": 0.0,
        "ClubAnglePath": 0.0,
        "ClubHeadSpeedMPH": 0.0,
    }
    assert sim.armed is False
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_ball_data_is_clamped_to_the_e6_ranges():
    fake = _FakeE6()
    await fake.start()
    sim = await _armed(fake, inter_message_delay_ms=0)
    await sim.send_shot(ShotData(speed=300.0, back_spin=30000, side_spin=-7000))
    await _until(lambda: "SetBallData" in fake.types())

    ball = next(m for m in fake.messages if m["Type"] == "SetBallData")["BallData"]
    assert (ball["BackSpin"], ball["BallSpeed"], ball["SideSpin"]) == (19999.0, 249.9, -5999.0)
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_messages_sharing_a_segment_or_split_across_reads_are_all_handled():
    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake)
    big = json.dumps({"Type": "SimCommand", "SubType": "ShotComplete", "Details": {"pad": "x" * 5000}}).encode()
    fake.send(big + _ARM + _PING)
    await _until(lambda: fake.types()[-1] == "Pong")

    assert sim.armed is True
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_write_failure_raises_and_sets_error():
    async def broken_drain():
        raise ConnectionResetError("gone")

    sim = E6Sim(host="127.0.0.1", port=2483, auth=_fake_auth([]))
    sim._writer = MagicMock()
    sim._writer.drain = broken_drain
    sim.armed = True
    with pytest.raises(ConnectionResetError):
        await sim.send_shot(ShotData(speed=100.0))

    assert sim.status == "error"
    assert sim.armed is False
    await sim.disconnect()


@pytest.mark.asyncio
async def test_player_club_type_reaches_the_callback():
    fake = _FakeE6()
    await fake.start()
    clubs = []
    sim = await _connected(fake, on_club=clubs.append)
    for club_type in ("Putter", "Iron7", "Driver"):
        fake.send(json.dumps({"Type": "SimCommand", "SubType": "PlayerDataModified", "Details": {"ClubType": club_type}}).encode())
    fake.send(_PING)
    await _until(lambda: fake.types()[-1] == "Pong")

    assert clubs == ["putter", "driver"]
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_disconnect_sends_disconnect_first():
    fake = _FakeE6()
    await fake.start()
    sim = await _armed(fake)
    await sim.disconnect()
    await _until(lambda: fake.types()[-1] == "Disconnect")

    assert sim.status == "off"
    assert sim.armed is False
    await fake.stop()

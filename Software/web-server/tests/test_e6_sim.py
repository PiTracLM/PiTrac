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


class _FakeE6:
    """Asyncio TCP server playing E6 Connect: challenge on Handshake, authenticates any reply, ACKs everything."""

    def __init__(self, auth_reply=b'{"Type":"Authentication","Success":"true"}'):
        self.messages = []
        self.auth_reply = auth_reply
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
            buf += chunk.decode()
            while buf := buf.lstrip():
                obj, end = decoder.raw_decode(buf)
                buf = buf[end:]
                self.messages.append(obj)
                writer.write(json.dumps({"Type": "ACK", "SubType": obj["Type"], "Details": "Success."}).encode())
                if obj["Type"] == "Handshake":
                    writer.write(b'{"Type":"Handshake","Challenge":"abc123","E6Version":"2, 0, 0, 0"}')
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


def _fake_auth(seen):
    def answer(text):
        seen.append(json.loads(text))
        return json.dumps({"Type": "Challenge", "Developer": "dev", "Hash": "hash-" + json.loads(text)["Challenge"]})

    return answer


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
    await asyncio.sleep(0.05)
    return sim


@pytest.mark.asyncio
async def test_handshake_is_answered_by_the_auth_function_then_connects():
    fake = _FakeE6()
    await fake.start()
    seen = []
    sim = E6Sim(host="127.0.0.1", port=fake.port, auth=_fake_auth(seen))
    await sim.connect()
    assert sim.status == "connecting"
    await asyncio.sleep(0.05)

    assert sim.status == "connected"
    assert seen[0]["Challenge"] == "abc123"
    assert fake.messages[:2] == [{"Type": "Handshake"}, {"Type": "Challenge", "Developer": "dev", "Hash": "hash-abc123"}]
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
async def test_arm_and_disarm_set_armed_and_the_detail():
    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake)
    assert sim.armed is False

    fake.send(_ARM)
    await asyncio.sleep(0.05)
    assert sim.armed is True
    assert sim.info()["detail"].endswith("armed")

    fake.send(_DISARM)
    await asyncio.sleep(0.05)
    assert sim.armed is False
    assert not sim.info()["detail"].endswith("armed")
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_ping_gets_pong():
    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake)
    fake.send(_PING)
    await asyncio.sleep(0.05)

    assert fake.types()[-1] == "Pong"
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_send_shot_while_disarmed_sends_nothing():
    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake)
    sent = len(fake.messages)
    await sim.send_shot(ShotData(speed=100.0, back_spin=3000))
    await asyncio.sleep(0.05)

    assert len(fake.messages) == sent
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_armed_shot_sends_three_messages_with_the_delay_then_disarms(monkeypatch):
    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake, inter_message_delay_ms=1)
    fake.send(_ARM)
    await asyncio.sleep(0.05)

    sleeps = []
    real_sleep = asyncio.sleep

    async def recording_sleep(delay, *args):
        sleeps.append(delay)
        await real_sleep(delay, *args)

    monkeypatch.setattr(asyncio, "sleep", recording_sleep)
    await sim.send_shot(ShotData(speed=101.24, launch_angle=15.4, side_angle=-2.1, back_spin=3000, side_spin=-300))
    monkeypatch.undo()
    await asyncio.sleep(0.05)

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
    sim = await _connected(fake, inter_message_delay_ms=0)
    fake.send(_ARM)
    await asyncio.sleep(0.05)
    await sim.send_shot(ShotData(speed=300.0, back_spin=30000, side_spin=-7000))
    await asyncio.sleep(0.05)

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
    await asyncio.sleep(0.05)

    assert sim.armed is True
    assert fake.types()[-1] == "Pong"
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
    fake.send(b'{"Type":"SimCommand","SubType":"PlayerDataModified","Details":{"ClubType":"Putter"}}')
    await asyncio.sleep(0.05)
    fake.send(b'{"Type":"SimCommand","SubType":"PlayerDataModified","Details":{"ClubType":"Driver"}}')
    await asyncio.sleep(0.05)

    assert clubs == ["putter", "driver"]
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_disconnect_sends_disconnect_first():
    fake = _FakeE6()
    await fake.start()
    sim = await _connected(fake)
    fake.send(_ARM)
    await asyncio.sleep(0.05)
    await sim.disconnect()
    await asyncio.sleep(0.05)

    assert fake.types()[-1] == "Disconnect"
    assert sim.status == "off"
    assert sim.armed is False
    await fake.stop()

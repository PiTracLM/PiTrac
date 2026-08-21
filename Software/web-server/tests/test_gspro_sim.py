import asyncio
import json

import pytest

from models import ShotData
from sims.gspro_sim import GSProSim, build_heartbeat, build_shot_payload

_ZERO_CLUB = {
    "Speed": 0.0,
    "AngleOfAttack": 0.0,
    "FaceToTarget": 0.0,
    "Lie": 0.0,
    "Loft": 0.0,
    "Path": 0.0,
    "SpeedAtImpact": 0.0,
    "VerticalFaceImpact": 0.0,
    "HorizontalFaceImpact": 0.0,
    "ClosureRate": 0.0,
}


def test_build_shot_payload_matches_the_cpp_object():
    shot = ShotData(speed=101.24, launch_angle=15.4, side_angle=-2.1, back_spin=3000, side_spin=-300)
    assert build_shot_payload(shot, 7) == {
        "DeviceID": "PiTrac LM 0.1",
        "Units": "Yards",
        "ShotNumber": 7,
        "APIversion": "1",
        "BallData": {
            "Speed": 101.2,
            "SpinAxis": -5.7,
            "TotalSpin": 3015.0,
            "BackSpin": 3000.0,
            "SideSpin": -300.0,
            "HLA": -2.1,
            "VLA": 15.4,
        },
        "ClubData": _ZERO_CLUB,
        "ShotDataOptions": {
            "ContainsBallData": True,
            "ContainsClubData": False,
            "LaunchMonitorIsReady": True,
            "LaunchMonitorBallDetected": True,
            "IsHeartBeat": False,
        },
    }
    assert build_shot_payload(ShotData(speed=250.0), 1)["BallData"]["Speed"] == 200.0
    assert build_shot_payload(ShotData(speed=101.25), 1)["BallData"]["Speed"] == 101.3


def test_build_heartbeat_flips_the_flags():
    hb = build_heartbeat(True)
    assert hb["ShotNumber"] == 0
    assert hb["BallData"]["Speed"] == 0.0
    assert hb["ShotDataOptions"] == {
        "ContainsBallData": False,
        "ContainsClubData": False,
        "LaunchMonitorIsReady": True,
        "LaunchMonitorBallDetected": True,
        "IsHeartBeat": True,
    }
    assert build_heartbeat(False)["ShotDataOptions"]["LaunchMonitorBallDetected"] is False


class _FakeGSPro:
    """Asyncio TCP server that records the raw concatenated JSON objects it receives."""

    def __init__(self, on_connect=b'{"Code":201,"Message":"Player Info","Player":{"Handed":"RH","Club":"DR"}}'):
        self.messages = []
        self.on_connect = on_connect
        self.writers = []
        self._server = None
        self.port = None

    async def start(self):
        self._server = await asyncio.start_server(self._handle, "127.0.0.1", 0)
        self.port = self._server.sockets[0].getsockname()[1]

    async def _handle(self, reader, writer):
        self.writers.append(writer)
        writer.write(self.on_connect)
        decoder = json.JSONDecoder()
        buf = ""
        while True:
            chunk = await reader.read(4096)
            if not chunk:
                break
            buf += chunk.decode()
            while buf:
                obj, end = decoder.raw_decode(buf)
                buf = buf[end:].lstrip()
                self.messages.append(obj)
                if not obj["ShotDataOptions"]["IsHeartBeat"]:
                    writer.write(b'{"Code":200,"Message":"Shot received successfully"}')
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
async def test_connect_sends_heartbeat_then_numbered_shots():
    fake = _FakeGSPro()
    await fake.start()
    sim = GSProSim(host="127.0.0.1", port=fake.port)
    await sim.connect()
    await sim.send_shot(ShotData(speed=99.0, back_spin=2500))
    await sim.send_shot(ShotData(speed=98.0, back_spin=2400))
    await asyncio.sleep(0.05)
    await sim.disconnect()
    await fake.stop()

    assert [m["ShotDataOptions"]["IsHeartBeat"] for m in fake.messages] == [True, False, False]
    assert [m["ShotNumber"] for m in fake.messages[1:]] == [1, 2]


@pytest.mark.asyncio
async def test_on_ball_state_sends_a_heartbeat():
    fake = _FakeGSPro()
    await fake.start()
    sim = GSProSim(host="127.0.0.1", port=fake.port)
    await sim.connect()
    await sim.on_ball_state(True)
    await asyncio.sleep(0.05)
    await sim.disconnect()
    await fake.stop()

    assert fake.messages[-1]["ShotDataOptions"]["IsHeartBeat"] is True
    assert fake.messages[-1]["ShotDataOptions"]["LaunchMonitorBallDetected"] is True


@pytest.mark.asyncio
async def test_two_responses_in_one_segment_both_parse():
    fake = _FakeGSPro(on_connect=b'{"Code":201,"Player":{"Club":"PT"}}{"Code":501,"Message":"bad shot"}')
    await fake.start()
    clubs = []
    sim = GSProSim(host="127.0.0.1", port=fake.port, on_club=clubs.append)
    await sim.connect()
    await asyncio.sleep(0.05)

    assert clubs == ["putter"]
    assert "501" in sim.info()["detail"]
    await sim.disconnect()
    await fake.stop()


@pytest.mark.asyncio
async def test_player_info_club_reaches_the_callback():
    fake = _FakeGSPro(on_connect=b'{"Code":201,"Player":{"Handed":"RH","Club":"PT"}}')
    await fake.start()
    clubs = []
    sim = GSProSim(host="127.0.0.1", port=fake.port, on_club=clubs.append)
    await sim.connect()
    await asyncio.sleep(0.05)
    fake.writers[0].write(b'{"Code":201,"Player":{"Handed":"RH","Club":"DR"}}')
    await asyncio.sleep(0.05)
    await sim.disconnect()
    await fake.stop()

    assert clubs == ["putter", "driver"]


@pytest.mark.asyncio
async def test_connect_failure_sets_error_and_schedules_reconnect():
    sim = GSProSim(host="127.0.0.1", port=1)
    await sim.connect()

    assert sim.status == "error"
    assert sim._reconnect_task is not None and not sim._reconnect_task.done()
    await sim.disconnect()

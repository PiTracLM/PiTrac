import ctypes

import pytest

from sims import e6_auth


class _FakeLib:
    def __init__(self, reply: bytes, result: int):
        self.received = None

        def e6_process(text, buf, cap):
            self.received = text
            ctypes.memmove(buf, reply + b"\0", len(reply) + 1)
            return result

        self.e6_process = e6_process


@pytest.fixture(autouse=True)
def _unloaded(monkeypatch):
    monkeypatch.setattr(e6_auth, "_lib", None)


def _inject(monkeypatch, fake):
    monkeypatch.setattr(e6_auth.ctypes, "CDLL", lambda path: fake)


def test_missing_library_raises_unavailable(monkeypatch, tmp_path):
    monkeypatch.setattr(e6_auth, "LIB_PATH", str(tmp_path / "libpitrac_e6.so"))
    with pytest.raises(e6_auth.E6AuthUnavailable):
        e6_auth.challenge_response('{"Type":"Challenge","Challenge":"abc"}')


def test_returns_the_reply_from_the_buffer(monkeypatch):
    fake = _FakeLib(b'{"Type":"Pong"}', 15)
    _inject(monkeypatch, fake)
    assert e6_auth.challenge_response('{"Type":"SimCommand","SubType":"Ping"}') == '{"Type":"Pong"}'
    assert fake.received == b'{"Type":"SimCommand","SubType":"Ping"}'


def test_negative_return_raises(monkeypatch):
    _inject(monkeypatch, _FakeLib(b"", -1))
    with pytest.raises(e6_auth.E6AuthError):
        e6_auth.challenge_response('{"Type":"Challenge"}')

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


_CHALLENGE = '{"Type":"Challenge","Challenge":"abc"}'


def test_missing_library_raises_unavailable(monkeypatch, tmp_path):
    monkeypatch.setattr(e6_auth, "LIB_PATH", str(tmp_path / "libpitrac_e6.so"))
    with pytest.raises(e6_auth.E6AuthUnavailable):
        e6_auth.challenge_response(_CHALLENGE)


def test_library_without_entry_point_raises_unavailable(monkeypatch):
    _inject(monkeypatch, object())
    with pytest.raises(e6_auth.E6AuthUnavailable):
        e6_auth.challenge_response(_CHALLENGE)


def test_returns_the_reply_from_the_buffer(monkeypatch):
    fake = _FakeLib(b'{"Type":"Challenge","Hash":"h"}', 31)
    _inject(monkeypatch, fake)
    assert e6_auth.challenge_response(_CHALLENGE) == '{"Type":"Challenge","Hash":"h"}'
    assert fake.received == _CHALLENGE.encode()


@pytest.mark.parametrize(
    "text",
    [
        '{"Type":"SimCommand","SubType":"Arm"}',
        '{"Type":"SimCommand","SubType":"Ping"}',
        '{"Type":"Handshake"}',
        "not json",
    ],
)
def test_non_challenge_never_reaches_the_library(monkeypatch, text):
    fake = _FakeLib(b"", 0)
    _inject(monkeypatch, fake)
    with pytest.raises(e6_auth.E6AuthError):
        e6_auth.challenge_response(text)
    assert fake.received is None


def test_negative_return_raises(monkeypatch):
    _inject(monkeypatch, _FakeLib(b"", -1))
    with pytest.raises(e6_auth.E6AuthError):
        e6_auth.challenge_response(_CHALLENGE)


def test_non_utf8_reply_raises(monkeypatch):
    _inject(monkeypatch, _FakeLib(b"\xff\xfe", 2))
    with pytest.raises(e6_auth.E6AuthError):
        e6_auth.challenge_response(_CHALLENGE)

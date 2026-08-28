from __future__ import annotations

import ctypes
import json
from typing import Optional

LIB_PATH = "/usr/lib/pitrac/libpitrac_e6.so"
_BUFFER_SIZE = 4096
_CHALLENGE_TYPES = ("Handshake", "Challenge")

_lib: Optional[ctypes.CDLL] = None


class E6AuthError(RuntimeError):
    pass


class E6AuthUnavailable(E6AuthError):
    pass


def _load() -> ctypes.CDLL:
    global _lib
    if _lib is None:
        try:
            lib = ctypes.CDLL(LIB_PATH)
            process = lib.e6_process
        except (OSError, AttributeError) as e:
            raise E6AuthUnavailable(f"e6 auth library missing: {LIB_PATH}") from e
        process.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
        process.restype = ctypes.c_int
        _lib = lib
    return _lib


def is_challenge(text: str) -> bool:
    try:
        message = json.loads(text)
    except ValueError:
        return False
    return isinstance(message, dict) and message.get("Type") in _CHALLENGE_TYPES and "Challenge" in message


def challenge_response(text: str) -> str:
    """Answer an E6 authentication challenge (a Handshake or Challenge message carrying a Challenge field)."""
    # Other messages reach code in the closed-source object that crashes this process
    if not is_challenge(text):
        raise E6AuthError("not an e6 authentication challenge")
    buf = ctypes.create_string_buffer(_BUFFER_SIZE)
    length = _load().e6_process(text.encode(), buf, _BUFFER_SIZE)
    if length < 0:
        raise E6AuthError(f"e6_process returned {length}")
    try:
        return buf.value.decode()
    except UnicodeDecodeError as e:
        raise E6AuthError("e6_process reply is not utf-8") from e

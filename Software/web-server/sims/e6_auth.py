from __future__ import annotations

import ctypes
from typing import Optional

LIB_PATH = "/usr/lib/pitrac/libpitrac_e6.so"
_BUFFER_SIZE = 4096

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
        except OSError as e:
            raise E6AuthUnavailable(f"e6 auth library missing: {LIB_PATH}") from e
        lib.e6_process.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
        lib.e6_process.restype = ctypes.c_int
        _lib = lib
    return _lib


def challenge_response(text: str) -> str:
    """Pass one raw E6 message to the closed-source responder and return its reply ("" for none)."""
    buf = ctypes.create_string_buffer(_BUFFER_SIZE)
    length = _load().e6_process(text.encode(), buf, _BUFFER_SIZE)
    if length < 0:
        raise E6AuthError(f"e6_process returned {length}")
    return buf.value.decode()

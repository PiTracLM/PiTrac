"""Pin pico_manager's range-check constants to the firmware's config.h bounds.

The web validator mirrors the firmware's limits by hand (it can't include a C
header), so they can silently drift -- a bumped bound in config.h would leave the
web side rejecting valid values or waving through ones the firmware rejects. This
test fails the moment they disagree. Skipped when the firmware tree isn't present
(e.g. a web-only deployment)."""

import re
from pathlib import Path

import pytest

import pico_manager as pm

_CONFIG_H = Path(__file__).resolve().parents[3] / "pico" / "include" / "config.h"

# firmware #define -> the pico_manager constant that must equal it
_BOUNDS = {
    "STROBE_MAX_PULSES": pm.PULSE_INTERVALS_MAX,
    "STROBE_MAX_INTERVAL_MS": pm.INTERVAL_MS_MAX,
    "STROBE_MAX_PULSE_WIDTH_US": pm.PULSE_WIDTH_US_MAX,
    "STROBE_MIN_INTER_SHOT_MS_FLOOR": pm.MIN_INTER_SHOT_FLOOR_MS,
    "STREAM_RMS_MAX_HZ": pm.STREAM_RMS_MAX_HZ,
}


def _firmware_defines() -> dict:
    text = _CONFIG_H.read_text()
    # Capture the leading numeric literal, ignoring any f/u/l suffix.
    return {
        m.group(1): float(m.group(2))
        for m in re.finditer(r"^#define\s+(\w+)\s+([0-9][0-9.]*)", text, re.M)
    }


@pytest.mark.skipif(not _CONFIG_H.exists(), reason="firmware config.h not present")
def test_python_pico_bounds_match_firmware_config_h():
    defs = _firmware_defines()
    for name, py_value in _BOUNDS.items():
        assert name in defs, f"{name} missing from config.h"
        assert defs[name] == float(py_value), (
            f"{name}={defs[name]} in config.h but pico_manager has {py_value}"
        )

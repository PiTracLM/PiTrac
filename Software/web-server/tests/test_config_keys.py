"""Every config key pitrac_lm reads must be defined in configurations.json, and the C++
initializers it falls back on must match the defaults there."""

import json
import re
from pathlib import Path

HERE = Path(__file__).parent
CPP_DIR = HERE / "../../LMSourceCode/ImageProcessing"
SCHEMA = HERE.parent / "configurations.json"

# Keys camera_hardware.cpp builds at runtime, {n} = camera 1 and 2, with the source text that builds each
DYNAMIC_KEYS = {
    "gs_config.cameras.kExpectedBallRadiusPixelsAt40cmCamera{n}": [
        '"gs_config.cameras." + ball_radius_pixels_at_40cm_name',
        '"kExpectedBallRadiusPixelsAt40cmCamera" + std::string(camera_number == GsCameraNumber::kGsCamera1 ? "1" : "2")',
    ],
    "gs_config.cameras.kCamera{n}CalibrationMatrix": [
        '"gs_config.cameras." + calibration_element_name',
        '"kCamera" + std::to_string((int)camera_number_) + "CalibrationMatrix"',
    ],
    "gs_config.cameras.kCamera{n}DistortionVector": [
        '"gs_config.cameras." + distortion_element_name',
        '"kCamera" + std::to_string((int)camera_number_) + "DistortionVector"',
    ],
    "cameras.kSlot{n}CustomLensFocalLength": [
        '"cameras.kSlot" + std::to_string(camera_number_) + "CustomLensFocalLength"',
    ],
    "gs_config.cameras.kCamera{n}FocalLength": [
        '"gs_config.cameras.kCamera" + std::to_string(camera_number_) + "FocalLength"',
    ],
    "gs_config.cameras.kCamera{n}Angles": [
        '"gs_config.cameras.kCamera" + std::to_string(camera_number_) + "Angles"',
    ],
}

# The calls above plus GetConfigString forwarding its tag_name
NON_LITERAL_SET_CONSTANT_CALLS = 6

MEMBER_DEF = re.compile(r"^[ \t]*(?:static\s+)?[\w:<>]+(?:[ \t]+[\w:<>]+)?[ \t]+(\w+)::(\w+)\s*=\s*([^;]+);", re.M)
PLAIN_DEF = re.compile(r"^[ \t]*(?:static\s+)?[\w:<>]+(?:[ \t]+[\w:<>]+)?[ \t]+(\w+)\s*=", re.M)
SET_CONSTANT = re.compile(r'SetConstant\(\s*"([^"]+)"\s*,\s*([\w:]+)\s*\)')

# Comparison-mode override read into the same member as gs_config.strobing.number_bits_for_fast_on_pulse_
SECOND_KEY_FOR_MEMBER = {"gs_config.testing.kExternallyStrobedEnvNumber_bits_for_fast_on_pulse_"}


def test_every_key_the_cpp_reads_is_in_the_schema():
    source = "\n".join(p.read_text(errors="replace") for p in sorted(CPP_DIR.glob("*.cpp")))

    literal_keys = set(re.findall(r'(?:SetConstant|GetConfigString|apply_enum)\(\s*"([^"]+)"\s*[,)]', source))
    assert len(literal_keys) > 200

    non_literal_calls = re.findall(r'SetConstant\((?!\s*const\b)(?!\s*"[^"]+"\s*,)', source)
    assert len(non_literal_calls) == NON_LITERAL_SET_CONSTANT_CALLS, "update DYNAMIC_KEYS for the new runtime-built key"

    stale = [f for fragments in DYNAMIC_KEYS.values() for f in fragments if f not in source]
    assert stale == []
    dynamic_keys = {template.format(n=n) for template in DYNAMIC_KEYS for n in (1, 2)}

    settings = json.loads(SCHEMA.read_text())["settings"]
    missing = sorted((literal_keys | dynamic_keys) - settings.keys())
    assert missing == []


def cpp_literal(text):
    """The value of a bool, number or string literal; None for anything else."""
    text = text.strip()
    if text in ("true", "false"):
        return text == "true"
    if m := re.fullmatch(r'"((?:[^"\\]|\\.)*)"', text):
        return re.sub(r"\\(.)", r"\1", m.group(1))
    if re.fullmatch(r"[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?[fFlLuU]*", text):
        return float(text.rstrip("fFlLuU"))
    return None


def same_value(cpp, default):
    if isinstance(cpp, bool):
        return cpp == (str(default).lower() in ("1", "true"))
    if isinstance(cpp, float):
        return cpp == float(default)
    return cpp == str(default)


def test_cpp_member_initializers_match_the_schema_defaults():
    members, plain, sites = {}, set(), []
    for path in sorted(CPP_DIR.glob("*.cpp")):
        text = re.sub(r"#ifdef _WIN32\n.*?#else\n", "", path.read_text(errors="replace"), flags=re.S)
        for cls, name, value in MEMBER_DEF.findall(text):
            members.setdefault(name, []).append((path.name, cls, value))
        plain |= {(path.name, name) for name in PLAIN_DEF.findall(text)}
        sites += [(path.name, key, target) for key, target in SET_CONSTANT.findall(text)]

    settings = json.loads(SCHEMA.read_text())["settings"]
    checked, drift = 0, []
    for file, key, target in sites:
        *scope, name = target.split("::")
        if scope:
            defs = [d for d in members.get(name, []) if d[1] == scope[-1]]
        else:
            defs = [] if (file, name) in plain else [d for d in members.get(name, []) if d[0] == file]
        default = settings.get(key, {}).get("default")
        value = cpp_literal(defs[0][2]) if len(defs) == 1 else None
        if value is None or default is None or isinstance(default, (list, dict)) or key in SECOND_KEY_FOR_MEMBER:
            continue
        checked += 1
        if not same_value(value, default):
            drift.append(f"{key}: {defs[0][1]}::{name} = {defs[0][2].strip()}, schema default {default!r}")

    assert checked > 150
    assert drift == []

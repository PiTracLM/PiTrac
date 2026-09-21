"""Every config key pitrac_lm reads must be defined in configurations.json, and the C++
initializers it falls back on must match the defaults there."""

import json
import re
from itertools import accumulate
from pathlib import Path

HERE = Path(__file__).parent
CPP_DIR = HERE / "../../LMSourceCode/ImageProcessing"
SCHEMA = HERE.parent / "configurations.json"

# Every whole-key literal in the C++; a key prefix left for runtime concatenation shows up as a key the schema lacks
KEY_LITERAL = re.compile(r'(?<!#include )"((?:gs_config|cameras)\.[\w.]*)"')
CALL_LITERAL = re.compile(r'(?:SetConstant|GetConfigString|apply_enum)\(\s*"([^"]+)"\s*[,)]')

DEFINITION = re.compile(r"^[ \t]*(?:static\s+)?([\w:<>]+(?:[ \t]+[\w:<>]+)?)[ \t]+(?:(\w+)::)?(\w+)\s*=\s*([^;]+);", re.M)
INTEGER_TYPES = {"int", "long", "unsigned int", "uint"}
SET_CONSTANT = re.compile(r'SetConstant\(\s*"([^"]+)"\s*,\s*([\w:]+)\s*\)')

# Keys whose C++ fallback stays off the schema default on purpose
NOT_SYNCED = {
    # comparison-mode override read into the same member as gs_config.strobing.number_bits_for_fast_on_pulse_
    "gs_config.testing.kExternallyStrobedEnvNumber_bits_for_fast_on_pulse_",
    # rig_type 0 is no rig, so a missing key fails auto-calibration instead of assuming one
    "gs_config.calibration.kCalibrationRigType",
    # kBaseTestDir is a placeholder global, and a Windows path in testProjection
    "gs_config.logging.kLinuxBaseImageLoggingDir",
    "gs_config.logging.kPCBaseImageLoggingDir",
}


def cpp_keys():
    source = "\n".join(p.read_text(errors="replace") for p in sorted(CPP_DIR.glob("*.cpp")))
    return set(CALL_LITERAL.findall(source)) | set(KEY_LITERAL.findall(source))


def test_every_key_the_cpp_reads_is_in_the_schema():
    keys = cpp_keys()
    assert len(keys) > 200

    settings = json.loads(SCHEMA.read_text())["settings"]
    missing = sorted(keys - settings.keys())
    assert missing == []


def test_every_schema_key_has_a_reader():
    web_source = "\n".join(
        p.read_text(errors="replace")
        for p in sorted(HERE.parent.rglob("*"))
        if p.suffix in (".py", ".js", ".html") and not {"tests", "node_modules", "htmlcov"} & set(p.relative_to(HERE.parent).parts)
    )
    read_by_cpp = cpp_keys()

    settings = json.loads(SCHEMA.read_text())["settings"]
    unread = sorted(
        key
        for key in settings
        if key not in read_by_cpp
        and key not in web_source
    )
    assert unread == []


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


def set_constant_sites():
    """(file, key, target, C++ type, initializer text) for each literal-key SetConstant whose target definition is found."""
    sources = {
        p.name: re.sub(r"#ifdef _WIN32\n.*?#else\n", "", p.read_text(errors="replace"), flags=re.S)
        for p in sorted(CPP_DIR.glob("*.cpp"))
    }
    definitions = {file: list(DEFINITION.finditer(text)) for file, text in sources.items()}
    members = [d.groups() for defs in definitions.values() for d in defs if d[2]]

    for file, text in sources.items():
        depth = [0, *accumulate((c == "{") - (c == "}") for c in text)]
        for site in SET_CONSTANT.finditer(text):
            key, target = site.groups()
            *scope, name = target.split("::")
            if scope:
                found = [(typ, value) for typ, cls, member, value in members if (cls, member) == (scope[-1], name)]
            else:
                # the nearest definition above the call whose scope has not closed by then
                found = [
                    (d[1], d[4])
                    for d in definitions[file]
                    if d[3] == name
                    and d.start() < site.start()
                    and min(depth[d.start() : site.start()]) >= depth[d.start()]
                ][-1:]
            if len(found) == 1:
                yield file, key, target, *found[0]


def test_cpp_initializers_match_the_schema_defaults():
    settings = json.loads(SCHEMA.read_text())["settings"]
    checked, drift = 0, []
    for file, key, target, _, initializer in set_constant_sites():
        default = settings.get(key, {}).get("default")
        value = cpp_literal(initializer)
        if value is None or default is None or isinstance(default, (list, dict)) or key in NOT_SYNCED:
            continue
        checked += 1
        if not same_value(value, default):
            drift.append(f"{key}: {target} = {initializer.strip()} in {file}, schema default {default!r}")

    assert checked > 150
    assert drift == []


def integer_typed(setting):
    if setting.get("type") == "select":
        return all(re.fullmatch(r"-?\d+", option) for option in setting.get("options", {}))
    return setting.get("type") == "integer"


def test_integer_cpp_targets_have_integer_schema_types():
    """The server truncates integer-typed values for boost's integer parse, which rejects "35.5"."""
    settings = json.loads(SCHEMA.read_text())["settings"]
    integer_sites = [(key, typ) for _, key, _, typ, _ in set_constant_sites() if typ in INTEGER_TYPES]
    assert len(integer_sites) > 60
    untyped = sorted({key for key, _ in integer_sites if key in settings and not integer_typed(settings[key])})
    assert untyped == []

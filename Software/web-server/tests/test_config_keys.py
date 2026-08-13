"""Every config key pitrac_lm reads must be defined in configurations.json."""

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

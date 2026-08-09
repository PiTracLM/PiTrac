"""Every config key pitrac_lm reads must be defined in configurations.json."""

import json
import re
from pathlib import Path

HERE = Path(__file__).parent
CPP_DIR = HERE / "../../LMSourceCode/ImageProcessing"
SCHEMA = HERE.parent / "configurations.json"

# Tags the C++ builds at runtime (gs_config.cpp GetConfigString/apply_enum, camera_hardware.cpp)
DYNAMIC_KEYS = {
    "cameras.slot1.type",
    "cameras.slot1.lens",
    "cameras.slot1.orientation",
    "cameras.slot2.type",
    "cameras.slot2.lens",
    "cameras.slot2.orientation",
    "logging.level",
    "gs_config.player.kGolferOrientation",
    "gs_config.logging.kArtifactSaveLevel",
    "gs_config.cameras.kCamera1FocalLength",
    "gs_config.cameras.kCamera2FocalLength",
    "gs_config.cameras.kCamera1Angles",
    "gs_config.cameras.kCamera2Angles",
    "gs_config.cameras.kCamera1CalibrationMatrix",
    "gs_config.cameras.kCamera2CalibrationMatrix",
    "gs_config.cameras.kCamera1DistortionVector",
    "gs_config.cameras.kCamera2DistortionVector",
    "cameras.kSlot1CustomLensFocalLength",
    "cameras.kSlot2CustomLensFocalLength",
}


def test_every_key_the_cpp_reads_is_in_the_schema():
    literal_keys = {
        key
        for path in CPP_DIR.glob("*.cpp")
        for key in re.findall(r'SetConstant\(\s*"([^"]+)"\s*,', path.read_text(errors="replace"))
    }
    assert len(literal_keys) > 200

    settings = json.loads(SCHEMA.read_text())["settings"]
    missing = sorted((literal_keys | DYNAMIC_KEYS) - settings.keys())
    assert missing == []

"""Test suite for ConfigManager using pytest"""

import json
import os
import pytest
from pathlib import Path
from unittest.mock import patch

from config_manager import ConfigurationManager as ConfigManager
from db.database import Database
from db.repositories import KeyValueRepository


@pytest.fixture
def db(tmp_path):
    d = Database(tmp_path / "t.db")
    yield d
    d.close()


@pytest.fixture
def config_manager(db, tmp_path):
    manager = ConfigManager(db)
    manager.user_settings_path = tmp_path / "user_settings.json"
    manager.calibration_data_path = tmp_path / "calibration_data.json"
    return manager


class TestConfigManager:
    """Test the configuration manager functionality"""

    @pytest.fixture
    def setup_config_files(self, config_manager, db):
        KeyValueRepository(db, "settings").replace_all({"gs_config.cameras.kCamera1Gain": "2.0"})
        config_manager.reload()
        return config_manager

    def test_config_loading_and_merging(self, setup_config_files):
        """Test loading and merging of system and user configs"""
        config_manager = setup_config_files

        camera1_gain = config_manager.get_config("gs_config.cameras.kCamera1Gain")
        assert camera1_gain is not None

        user_settings = config_manager.get_user_settings()
        assert isinstance(user_settings, dict)

    def test_set_config(self, setup_config_files):
        """Test setting configuration values"""
        config_manager = setup_config_files

        result = config_manager.set_config("gs_config.cameras.kCamera2Gain", "8.0")
        if isinstance(result, tuple):
            success = result[0]
        else:
            success = result
        assert success

        camera2_gain = config_manager.get_config("gs_config.cameras.kCamera2Gain")
        assert camera2_gain is not None

    def test_config_validation_gain(self, config_manager):
        """Test configuration validation for camera gain"""
        is_valid, error = config_manager.validate_config("gs_config.cameras.kCamera1Gain", "0.0")
        assert not is_valid
        assert "minimum" in error.lower() or "at least" in error.lower()

        is_valid, error = config_manager.validate_config("gs_config.cameras.kCamera1Gain", "20.0")
        assert not is_valid
        assert "maximum" in error.lower() or "at most" in error.lower()

        is_valid, _ = config_manager.validate_config("gs_config.cameras.kCamera1Gain", "8.0")
        assert is_valid

    def test_config_validation_port(self, config_manager):
        """Test configuration validation for network ports"""
        is_valid, _ = config_manager.validate_config("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectPort", "0")
        assert not is_valid

        is_valid, _ = config_manager.validate_config(
            "gs_config.golf_simulator_interfaces.GSPro.kGSProConnectPort", "70000"
        )
        assert not is_valid

        is_valid, _ = config_manager.validate_config(
            "gs_config.golf_simulator_interfaces.GSPro.kGSProConnectPort", "8080"
        )
        assert is_valid

    def test_reset_all(self, setup_config_files):
        """Test resetting all user settings"""
        config_manager = setup_config_files

        result = config_manager.reset_all()
        if isinstance(result, tuple):
            success = result[0]
        else:
            success = result
        assert success is not None

    def test_get_categories(self, config_manager):
        """Test getting configuration categories"""
        categories = config_manager.get_categories()
        assert isinstance(categories, dict)
        assert "Cameras" in categories or len(categories) == 0

    def test_get_diff(self, setup_config_files):
        """Test getting differences between system and user configs"""
        config_manager = setup_config_files

        diff = config_manager.get_diff()
        assert isinstance(diff, dict)
        assert "gs_config.cameras.kCamera1Gain" in diff
        diff_entry = diff["gs_config.cameras.kCamera1Gain"]
        assert "user" in diff_entry and "default" in diff_entry

    def test_get_user_settings(self, setup_config_files):
        """Test getting user settings"""
        config_manager = setup_config_files

        user_settings = config_manager.get_user_settings()
        assert isinstance(user_settings, dict)

    def test_set_and_get_config(self, config_manager):
        """Test setting and getting configuration values"""
        result = config_manager.set_config("gs_config.cameras.kCamera1Gain", "7.0")
        assert result is not None

        gain_value = config_manager.get_config("gs_config.cameras.kCamera1Gain")
        assert gain_value is not None

    def test_invalid_json_file_is_left_alone(self, config_manager, db):
        settings = KeyValueRepository(db, "settings")
        settings.replace_all({"gs_config.cameras.kCamera1Gain": "3.0"})
        config_manager.user_settings_path.write_text("{ invalid json }")

        config_manager.reload()

        assert config_manager.user_settings_path.exists()
        assert settings.load() == {"gs_config.cameras.kCamera1Gain": "3.0"}
        assert config_manager.user_settings == {"gs_config": {"cameras": {"kCamera1Gain": "3.0"}}}

    def test_missing_file_handling(self, config_manager):
        """Test handling of missing config files"""
        config_manager.user_settings_path = Path("/nonexistent/path/user_settings.json")
        config_manager.calibration_data_path = Path("/nonexistent/path/calibration.json")
        config_manager.reload()

        assert config_manager.user_settings == {}
        assert config_manager.calibration_data == {}
        assert isinstance(config_manager.merged_config, dict)

    def test_nested_config_access(self, config_manager):
        """Test accessing deeply nested configuration values"""
        config_manager.user_settings = {
            "gs_config": {
                "golf_simulator_interfaces": {
                    "GSPro": {"kGSProConnectAddress": "192.168.1.100", "kGSProConnectPort": 921}
                }
            }
        }
        config_manager._rebuild_merged_config()

        port = config_manager.get_config("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectPort")
        assert port == 921

        address = config_manager.get_config("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress")
        assert address == "192.168.1.100"

    def test_set_survives_reload(self, setup_config_files):
        config_manager = setup_config_files

        config_manager.set_config("gs_config.cameras.kCamera1Gain", "3.5")
        config_manager.reload()

        assert config_manager.get_config("gs_config.cameras.kCamera1Gain") == "3.5"

    def test_reset_all_clears_settings_and_keeps_calibration(self, config_manager, db):
        config_manager.set_config("gs_config.cameras.kCamera1Gain", "3.5")
        config_manager.set_config("gs_config.cameras.kCamera1FocalLength", 6.1)

        config_manager.reset_all()

        assert KeyValueRepository(db, "settings").load() == {}
        assert KeyValueRepository(db, "calibration").load() == {"gs_config.cameras.kCamera1FocalLength": 6.1}

    def test_callback_fires_after_set_config(self, config_manager):
        seen = []
        config_manager.register_callback("gs_config.cameras", lambda k, v: seen.append((k, v)))

        config_manager.set_config("gs_config.cameras.kCamera1Gain", "3.5")

        assert seen == [("gs_config.cameras.kCamera1Gain", "3.5")]

    def test_imports_json_files_once(self, config_manager, db, tmp_path):
        config_manager.user_settings_path.write_text(json.dumps({
            "gs_config": {"golf_simulator_interfaces": {"GSPro": {"kGSProConnectAddress": "10.0.0.5"}}},
            "cameras.slot1.type": "4",
        }))
        config_manager.calibration_data_path.write_text(json.dumps({
            "gs_config": {"cameras": {"kCamera1FocalLength": 5.9, "kCamera1Angles": [2.14, -26.42]}},
        }))

        expected_settings = {
            "gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress": "10.0.0.5",
            "cameras.slot1.type": "4",
        }
        expected_calibration = {
            "gs_config.cameras.kCamera1FocalLength": 5.9,
            "gs_config.cameras.kCamera1Angles": [2.14, -26.42],
        }
        settings = KeyValueRepository(db, "settings")
        calibration = KeyValueRepository(db, "calibration")

        config_manager.reload()

        assert settings.load() == expected_settings
        assert calibration.load() == expected_calibration
        assert config_manager.get_config("cameras.slot1.type") == "4"
        assert not config_manager.user_settings_path.exists()
        assert not config_manager.calibration_data_path.exists()
        assert (tmp_path / "user_settings.json.imported").exists()
        assert (tmp_path / "calibration_data.json.imported").exists()

        config_manager.reload()

        assert settings.load() == expected_settings
        assert calibration.load() == expected_calibration

    def test_later_user_settings_file_merges_over_rows(self, config_manager, db, tmp_path):
        settings = KeyValueRepository(db, "settings")
        settings.replace_all({"cameras.slot1.type": "4", "gs_config.cameras.kCamera1Gain": "3.0"})
        (tmp_path / "user_settings.json.imported").write_text("{}")
        config_manager.user_settings_path.write_text(json.dumps({"cameras.slot1.type": "5"}))

        config_manager.reload()

        assert settings.load() == {"cameras.slot1.type": "5", "gs_config.cameras.kCamera1Gain": "3.0"}
        assert not config_manager.user_settings_path.exists()

    @patch.dict(os.environ, {"HOME": "/test/home"})
    def test_default_paths(self):
        """Test default configuration file paths"""
        manager = ConfigManager()

        assert "/test/home/.pitrac" in str(manager.user_settings_path)

    def test_get_metadata(self, config_manager):
        """Test getting configuration metadata"""
        metadata = config_manager.load_configurations_metadata()
        assert isinstance(metadata, dict)

        if metadata:
            assert "cameraDefinitions" in metadata or "settings" in metadata

    def test_reload_configurations_metadata(self, config_manager):
        """Test reloading configuration metadata"""
        metadata = config_manager.load_configurations_metadata()
        assert isinstance(metadata, dict)

    def test_setting_validation_with_metadata(self, config_manager):
        """Test validation using metadata definitions"""
        config_manager.metadata = {
            "settings": {"gs_config.cameras.kCamera1Gain": {"min": 1.0, "max": 16.0, "type": "float"}}
        }

        is_valid, _ = config_manager.validate_config("gs_config.cameras.kCamera1Gain", "8.0")
        assert is_valid

        is_valid, _ = config_manager.validate_config("gs_config.cameras.kCamera1Gain", "20.0")
        assert not is_valid


class TestArrayHandling:
    """Test suite for array type configuration handling"""

    def test_validate_array_with_list(self, config_manager):
        """Test validation of array type with a proper list"""
        # Mock metadata to include array type setting
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.calibration.kTestArray": {
                        "type": "array",
                        "default": [0.0, 0.1, 0.2]
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        is_valid, error = config_manager.validate_config(
            "gs_config.calibration.kTestArray",
            [0.0, 0.109, 0.472]
        )
        assert is_valid, f"Array validation failed: {error}"
        assert error == ""

        config_manager.load_configurations_metadata = original_load

    def test_validate_array_with_json_string(self, config_manager):
        """Test validation of array type with JSON string representation"""
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.calibration.kTestArray": {
                        "type": "array",
                        "default": [0.0, 0.1, 0.2]
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        # JSON string should be valid for array type
        is_valid, error = config_manager.validate_config(
            "gs_config.calibration.kTestArray",
            '[0.0, 0.109, 0.472]'
        )
        assert is_valid, f"JSON string array validation failed: {error}"

        config_manager.load_configurations_metadata = original_load

    def test_validate_array_rejects_invalid_json(self, config_manager):
        """Test that invalid JSON strings are rejected for array types"""
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.calibration.kTestArray": {
                        "type": "array",
                        "default": [0.0, 0.1, 0.2]
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        is_valid, error = config_manager.validate_config(
            "gs_config.calibration.kTestArray",
            '[0.0, 0.109, invalid]'
        )
        assert not is_valid
        assert "JSON" in error or "array" in error.lower()

        config_manager.load_configurations_metadata = original_load

    def test_validate_array_rejects_non_array_json(self, config_manager):
        """Test that valid JSON that's not an array is rejected"""
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.calibration.kTestArray": {
                        "type": "array",
                        "default": [0.0, 0.1, 0.2]
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        # JSON object should be rejected for array type
        is_valid, error = config_manager.validate_config(
            "gs_config.calibration.kTestArray",
            '{"not": "array"}'
        )
        assert not is_valid
        assert "array" in error.lower()


    def test_validate_array_rejects_plain_string(self, config_manager):
        """Test that plain strings are rejected for array types"""
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.calibration.kTestArray": {
                        "type": "array",
                        "default": [0.0, 0.1, 0.2]
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        is_valid, error = config_manager.validate_config(
            "gs_config.calibration.kTestArray",
            'not an array'
        )
        assert not is_valid
        assert "JSON" in error or "array" in error.lower()

        config_manager.load_configurations_metadata = original_load

    def test_set_config_converts_json_string_to_array(self, config_manager):
        """Test that set_config auto-converts JSON strings to arrays"""
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.calibration.kTestArray": {
                        "type": "array",
                        "default": [0.0, 0.1, 0.2],
                        "category": "calibration"
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        # Set using JSON string (simulating frontend sending string)
        success, message, _ = config_manager.set_config(
            "gs_config.calibration.kTestArray",
            '[0.0, 0.109, 0.472]'
        )
        assert success, f"set_config failed: {message}"

        # Verify it's stored as a list, not a string
        value = config_manager.get_config("gs_config.calibration.kTestArray")
        assert isinstance(value, list), f"Expected list, got {type(value)}: {value}"
        assert value == [0.0, 0.109, 0.472]

        config_manager.load_configurations_metadata = original_load

    def test_set_config_preserves_array_type(self, config_manager):
        """Test that set_config preserves arrays when passed directly"""
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.calibration.kTestArray": {
                        "type": "array",
                        "default": [0.0, 0.1, 0.2],
                        "category": "calibration"
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        test_array = [0.0, 0.109, 0.472]
        success, message, _ = config_manager.set_config(
            "gs_config.calibration.kTestArray",
            test_array
        )
        assert success, f"set_config failed: {message}"

        value = config_manager.get_config("gs_config.calibration.kTestArray")
        assert isinstance(value, list)
        assert value == test_array

        config_manager.load_configurations_metadata = original_load

    def test_set_config_rejects_invalid_json_for_array(self, config_manager):
        """Test that set_config rejects invalid JSON for array types"""
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.calibration.kTestArray": {
                        "type": "array",
                        "default": [0.0, 0.1, 0.2],
                        "category": "calibration"
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        success, message, _ = config_manager.set_config(
            "gs_config.calibration.kTestArray",
            '[invalid json'
        )
        assert not success
        assert "JSON" in message or "Invalid" in message

        config_manager.load_configurations_metadata = original_load

    def test_array_with_nested_arrays(self, config_manager):
        """Test handling of nested arrays (like calibration matrices)"""
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.cameras.kCamera1CalibrationMatrix": {
                        "type": "array",
                        "default": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
                        "category": "calibration"
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        nested_array = [[1833.5, 0.0, 697.2], [0.0, 1832.2, 513.0], [0.0, 0.0, 1.0]]

        is_valid, error = config_manager.validate_config(
            "gs_config.cameras.kCamera1CalibrationMatrix",
            nested_array
        )
        assert is_valid, f"Nested array validation failed: {error}"

        success, message, _ = config_manager.set_config(
            "gs_config.cameras.kCamera1CalibrationMatrix",
            nested_array
        )
        assert success, f"set_config failed: {message}"

        value = config_manager.get_config("gs_config.cameras.kCamera1CalibrationMatrix")
        assert isinstance(value, list)
        assert len(value) == 3
        assert isinstance(value[0], list)

        config_manager.load_configurations_metadata = original_load

    def test_array_with_irregular_whitespace(self, config_manager):
        """Test handling of arrays with irregular whitespace in JSON string"""
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.calibration.kTestArray": {
                        "type": "array",
                        "default": [0.0, 0.1, 0.2],
                        "category": "calibration"
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        # Test various whitespace patterns that users might input
        whitespace_cases = [
            '[  0.00,  0.109,  0.472]',      # Extra leading spaces
            '[ 0.00, 0.109, 0.472 ]',        # Spaces around brackets
            '[0.00,0.109,0.472]',            # No spaces
            '[\n  0.00,\n  0.109,\n  0.472\n]',  # Newlines
            '[  0.00 , 0.109 , 0.472  ]',    # Spaces around commas
        ]

        for json_string in whitespace_cases:
            is_valid, error = config_manager.validate_config(
                "gs_config.calibration.kTestArray",
                json_string
            )
            assert is_valid, f"Whitespace case failed validation: '{json_string}' - {error}"

            success, message, _ = config_manager.set_config(
                "gs_config.calibration.kTestArray",
                json_string
            )
            assert success, f"Whitespace case failed set_config: '{json_string}' - {message}"

            value = config_manager.get_config("gs_config.calibration.kTestArray")
            assert isinstance(value, list), f"Expected list for '{json_string}', got {type(value)}"
            assert len(value) == 3, f"Expected 3 elements for '{json_string}', got {len(value)}"

        config_manager.load_configurations_metadata = original_load

    def test_array_with_string_elements(self, config_manager):
        """Test handling of arrays with string elements (as in configurations.json)"""
        original_load = config_manager.load_configurations_metadata

        def mock_metadata():
            return {
                "settings": {
                    "gs_config.cameras.kCamera1DistortionVector": {
                        "type": "array",
                        "default": ["-0.508", "0.340", "-0.002", "0.002", "-0.134"],
                        "category": "calibration"
                    }
                }
            }

        config_manager.load_configurations_metadata = mock_metadata

        # Arrays with string elements should be valid
        string_array = ["-0.5", "0.34", "-0.002", "0.002", "-0.13"]

        is_valid, error = config_manager.validate_config(
            "gs_config.cameras.kCamera1DistortionVector",
            string_array
        )
        assert is_valid, f"String array validation failed: {error}"

        success, message, _ = config_manager.set_config(
            "gs_config.cameras.kCamera1DistortionVector",
            string_array
        )
        assert success, f"set_config failed: {message}"

        config_manager.load_configurations_metadata = original_load


class TestSetCalibrationBatch:
    """Atomic multi-key calibration writes used by distortion calibration save."""

    def test_empty_updates_returns_success(self, config_manager):
        ok, msg = config_manager.set_calibration_batch({})
        assert ok
        assert "no updates" in msg.lower()

    def test_writes_both_keys_atomically(self, config_manager):
        matrix = [[1833.0, 0.0, 728.0], [0.0, 1833.0, 544.0], [0.0, 0.0, 1.0]]
        dist = [-0.51, 0.34, -0.002, 0.002, -0.13]
        ok, _ = config_manager.set_calibration_batch({
            "gs_config.cameras.kCamera1CalibrationMatrix": matrix,
            "gs_config.cameras.kCamera1DistortionVector": dist,
        })
        assert ok
        cams = config_manager.calibration_data["gs_config"]["cameras"]
        assert cams["kCamera1CalibrationMatrix"] == matrix
        assert cams["kCamera1DistortionVector"] == dist

    def test_persists_to_calibration_table(self, config_manager, db):
        config_manager.set_calibration_batch({
            "gs_config.cameras.kCamera1CalibrationMatrix": [[1.0]],
        })
        saved = KeyValueRepository(db, "calibration").load()
        assert saved["gs_config.cameras.kCamera1CalibrationMatrix"] == [[1.0]]

    def test_rejects_non_calibration_key(self, config_manager):
        ok, msg = config_manager.set_calibration_batch({
            "gs_config.cameras.kCamera1Gain": 2.5,
        })
        assert not ok
        assert "not a calibration field" in msg

    def test_rejects_mixed_when_any_non_calibration(self, config_manager):
        ok, _ = config_manager.set_calibration_batch({
            "gs_config.cameras.kCamera1CalibrationMatrix": [[1.0]],
            "gs_config.cameras.kCamera1Gain": 2.5,
        })
        assert not ok
        # Reject before writing — calibration_data must remain untouched.
        assert config_manager.calibration_data == {}

    def test_callbacks_fire_for_each_key(self, config_manager):
        seen = []
        config_manager.register_callback(
            "gs_config.cameras.kCamera1CalibrationMatrix",
            lambda k, v: seen.append((k, v)),
        )
        config_manager.set_calibration_batch({
            "gs_config.cameras.kCamera1CalibrationMatrix": [[1.0]],
            "gs_config.cameras.kCamera1DistortionVector": [0.0] * 5,
        })
        assert any(k == "gs_config.cameras.kCamera1CalibrationMatrix" for k, _ in seen)


class TestBuildGeneratedConfig:
    """Tests for the extracted config dict builder."""

    def test_build_generated_config_returns_merged_dict(self, config_manager):
        cfg = config_manager.build_generated_config()
        assert isinstance(cfg, dict)
        assert "gs_config" in cfg

    def test_build_generated_config_merges_transient_overrides(self, config_manager):
        config_manager.transient_overrides = {"gs_config": {"testing": {"kBaseTestImageDir": "/tmp/x/"}}}
        cfg = config_manager.build_generated_config()
        assert cfg["gs_config"]["testing"]["kBaseTestImageDir"] == "/tmp/x/"
        assert "kGolferOrientation" in cfg["gs_config"]["player"]

        config_manager.transient_overrides = {}
        cfg = config_manager.build_generated_config()
        assert cfg["gs_config"].get("testing", {}).get("kBaseTestImageDir") != "/tmp/x/"

    def test_build_generated_config_includes_former_cli_and_env(self, config_manager):
        cfg = config_manager.build_generated_config()
        # former CLI setting — gs_config.player.kGolferOrientation
        assert "kGolferOrientation" in cfg.get("gs_config", {}).get("player", {})
        # former environment setting — cameras.slot1.type
        assert "type" in cfg.get("cameras", {}).get("slot1", {})

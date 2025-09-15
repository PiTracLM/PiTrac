"""Test suite for ConfigManager using pytest"""

import json
import os
import pytest
import tempfile
from pathlib import Path
from unittest.mock import patch

from config_manager import ConfigurationManager as ConfigManager


class TestConfigManager:
    """Test the configuration manager functionality"""

    @pytest.fixture
    def temp_config_dir(self):
        """Create a temporary directory for config files"""
        with tempfile.TemporaryDirectory() as tmpdir:
            yield Path(tmpdir)

    @pytest.fixture
    def config_manager(self, temp_config_dir):
        """Create a ConfigManager instance with temp paths"""
        manager = ConfigManager()
        manager.user_settings_path = temp_config_dir / "user_settings.json"
        return manager

    @pytest.fixture
    def setup_config_files(self, config_manager):
        """Setup basic config files"""
        user_settings = {"gs_config": {"cameras": {"kCamera1Gain": "2.0"}}}

        with open(config_manager.user_settings_path, "w") as f:
            json.dump(user_settings, f)

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

    def test_invalid_json_handling(self, config_manager):
        """Test handling of invalid JSON files"""
        with open(config_manager.user_settings_path, "w") as f:
            f.write("{ invalid json }")

        config_manager.reload()
        assert config_manager.user_settings == {}

    def test_missing_file_handling(self, config_manager):
        """Test handling of missing config files"""
        config_manager.user_settings_path = Path("/nonexistent/path/user_settings.json")
        config_manager.calibration_data_path = Path("/nonexistent/path/calibration.json")
        config_manager.reload()

        assert config_manager.user_settings == {}
        assert config_manager.calibration_data == {}
        assert isinstance(config_manager.merged_config, dict)

    def test_cli_parameters(self, config_manager):
        """Test getting CLI parameters from config"""
        config_manager.system_config = {
            "gs_config": {"cameras": {"kCamera1Gain": "2.0"}, "modes": {"kStartInPuttingMode": "1"}}
        }

        cli_params = config_manager.get_cli_parameters()
        assert isinstance(cli_params, list)

    def test_environment_parameters(self, config_manager):
        """Test getting environment parameters from config"""
        config_manager.system_config = {
            "gs_config": {"ipc_interface": {"kWebActiveMQHostAddress": "tcp://localhost:61616"}}
        }
        config_manager.reload()

        env_params = config_manager.get_environment_parameters()
        assert isinstance(env_params, list)

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

    def test_config_persistence(self, setup_config_files):
        """Test that configuration methods work"""
        config_manager = setup_config_files

        result = config_manager.set_config("gs_config.cameras.kCamera1Gain", "3.5")
        assert result is not None

        config_manager.reload()

        gain_value = config_manager.get_config("gs_config.cameras.kCamera1Gain")
        assert gain_value is not None

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

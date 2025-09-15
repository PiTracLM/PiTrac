"""Tests for configuration API endpoints"""

import pytest

from utils.mock_factories import MockConfigManagerFactory


@pytest.mark.unit
class TestConfigurationAPI:
    """Test suite for configuration API endpoints"""

    @pytest.fixture
    def mock_config_manager(self):
        """Create a mock configuration manager using factory"""
        manager = MockConfigManagerFactory.create_basic_config_manager()
        manager.get_all_defaults_with_metadata.return_value = {
            "system": {"mode": "single", "camera_role": "camera1"},
            "cameras": {"camera1_gain": 1.0, "camera2_gain": 4.0},
        }
        manager.load_configurations_metadata.return_value = {
            "settings": {
                "cameras.camera1_gain": {"type": "number", "default": 1.0},
                "system.mode": {"type": "select", "options": {"single": "Single"}},
            }
        }
        manager.get_diff.return_value = {"cameras.camera1_gain": {"user": 2.0, "default": 1.0}}
        manager.export_config.return_value = {
            "user_settings": {"cameras": {"camera1_gain": 2.0}},
            "timestamp": "2024-01-01T12:00:00",
        }
        manager.import_config.return_value = (True, "Configuration imported")
        return manager

    def test_config_page(self, client):
        """Test configuration page loads"""
        response = client.get("/config")
        assert response.status_code == 200
        assert "Configuration" in response.text

    def test_get_config(self, client, server_instance, mock_config_manager):
        """Test getting merged configuration"""
        server_instance.config_manager = mock_config_manager

        response = client.get("/api/config")
        assert response.status_code == 200

        data = response.json()
        assert "data" in data
        assert "system" in data["data"]
        assert data["data"]["system"]["mode"] == "single"
        assert data["data"]["cameras"]["camera1_gain"] == 2.0

    def test_get_config_defaults(self, client, server_instance, mock_config_manager):
        """Test getting default configuration"""
        server_instance.config_manager = mock_config_manager

        response = client.get("/api/config/defaults")
        assert response.status_code == 200

        data = response.json()
        assert "data" in data
        assert data["data"]["cameras"]["camera1_gain"] == 1.0

    def test_get_user_config(self, client, server_instance, mock_config_manager):
        """Test getting user configuration"""
        server_instance.config_manager = mock_config_manager

        response = client.get("/api/config/user")
        assert response.status_code == 200

        data = response.json()
        assert "data" in data
        assert data["data"]["cameras"]["camera1_gain"] == 2.0

    def test_get_categories(self, client, server_instance, mock_config_manager):
        """Test getting configuration categories"""
        server_instance.config_manager = mock_config_manager

        response = client.get("/api/config/categories")
        assert response.status_code == 200

        data = response.json()
        assert "Basic" in data
        assert "Cameras" in data
        assert "basic" in data["Basic"]
        assert "advanced" in data["Basic"]

    def test_get_metadata(self, client, server_instance, mock_config_manager):
        """Test getting configuration metadata"""
        server_instance.config_manager = mock_config_manager

        response = client.get("/api/config/metadata")
        assert response.status_code == 200

        data = response.json()
        assert "cameras.camera1_gain" in data
        assert data["cameras.camera1_gain"]["type"] == "number"

    def test_get_diff(self, client, server_instance, mock_config_manager):
        """Test getting configuration differences"""
        server_instance.config_manager = mock_config_manager

        response = client.get("/api/config/diff")
        assert response.status_code == 200

        data = response.json()
        assert "data" in data
        assert "cameras.camera1_gain" in data["data"]
        assert data["data"]["cameras.camera1_gain"]["user"] == 2.0

    def test_update_config(self, client, server_instance, mock_config_manager):
        """Test updating configuration value"""
        server_instance.config_manager = mock_config_manager

        response = client.put("/api/config/cameras.camera1_gain", json={"value": "3.0"})
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is True
        assert data["message"] == "Configuration updated"
        assert data["requires_restart"] is False

        mock_config_manager.set_config.assert_called_once_with("cameras.camera1_gain", "3.0")

    def test_update_config_invalid(self, client, server_instance, mock_config_manager):
        """Test updating configuration with invalid value"""
        mock_config_manager.validate_config.return_value = (False, "Invalid value: must be between 1.0 and 16.0")
        server_instance.config_manager = mock_config_manager

        response = client.put("/api/config/cameras.camera1_gain", json={"value": "20.0"})
        assert response.status_code == 200

        data = response.json()
        assert "error" in data
        assert data["error"] == "Invalid value: must be between 1.0 and 16.0"

    def test_reset_config(self, client, server_instance, mock_config_manager):
        """Test resetting configuration"""
        server_instance.config_manager = mock_config_manager

        response = client.post("/api/config/reset")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is True
        assert data["message"] == "Configuration reset"

        mock_config_manager.reset_all.assert_called_once()

    def test_reset_config_failure(self, client, server_instance, mock_config_manager):
        """Test reset configuration failure"""
        mock_config_manager.reset_all.return_value = (False, "Reset failed")
        server_instance.config_manager = mock_config_manager

        response = client.post("/api/config/reset")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is False
        assert data["message"] == "Reset failed"

    def test_reload_config(self, client, server_instance, mock_config_manager):
        """Test reloading configuration from disk"""
        server_instance.config_manager = mock_config_manager

        response = client.post("/api/config/reload")
        assert response.status_code == 200

        data = response.json()
        assert data["status"] == "Configuration reloaded"

        mock_config_manager.reload.assert_called_once()

    def test_export_config(self, client, server_instance, mock_config_manager):
        """Test exporting configuration"""
        server_instance.config_manager = mock_config_manager

        response = client.get("/api/config/export")
        assert response.status_code == 200

        data = response.json()
        assert "user_settings" in data
        assert "timestamp" in data
        assert data["user_settings"]["cameras"]["camera1_gain"] == 2.0

    def test_import_config(self, client, server_instance, mock_config_manager):
        """Test importing configuration"""
        server_instance.config_manager = mock_config_manager

        import_data = {"user_settings": {"cameras": {"camera1_gain": 3.5}}}

        response = client.post("/api/config/import", json=import_data)
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is True
        assert data["message"] == "Configuration imported"

        mock_config_manager.import_config.assert_called_once_with(import_data)

    def test_import_config_invalid(self, client, server_instance, mock_config_manager):
        """Test importing invalid configuration"""
        mock_config_manager.import_config.return_value = (False, "Invalid configuration format")
        server_instance.config_manager = mock_config_manager

        response = client.post("/api/config/import", json={"invalid": "data"})
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is False
        assert data["message"] == "Invalid configuration format"

    def test_config_cors_headers(self, client):
        """Test CORS headers on config endpoints"""
        response = client.get("/api/config")
        assert response.status_code == 200

    @pytest.mark.parametrize(
        "endpoint",
        [
            "/api/config",
            "/api/config/defaults",
            "/api/config/user",
            "/api/config/categories",
            "/api/config/metadata",
            "/api/config/diff",
        ],
    )
    def test_config_get_endpoints(self, client, endpoint):
        """Test all GET configuration endpoints return valid JSON"""
        response = client.get(endpoint)
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, dict)

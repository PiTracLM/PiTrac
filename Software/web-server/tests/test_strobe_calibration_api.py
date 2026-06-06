"""Tests for strobe calibration API endpoints"""

import pytest
from unittest.mock import Mock, AsyncMock, patch


@pytest.mark.unit
class TestStrobeCalibrationAPI:
    """Test suite for strobe calibration API endpoints"""

    @pytest.fixture
    def mock_strobe_manager(self):
        """Create a mock strobe calibration manager"""
        manager = Mock()
        manager.get_status = Mock(
            return_value={"state": "idle", "progress": 0, "message": ""}
        )
        manager.get_saved_settings = AsyncMock(
            return_value={"dac_setting": None}
        )
        manager.start_calibration = AsyncMock(
            return_value={"status": "success", "dac_setting": 150, "led_current": 9.85}
        )
        manager.cancel = Mock()
        manager.read_diagnostics = AsyncMock(
            return_value={
                "ldo_voltage": 7.42,
                "adc_ch0_raw": 1234,
                "adc_ch1_raw": 2048,
                "led_current": 9.5,
            }
        )
        manager.set_dac_manual = AsyncMock(
            return_value={"status": "success", "dac_value": 128, "ldo_voltage": 6.8}
        )
        manager.get_dac_start = AsyncMock(
            return_value={"dac_start": 200, "ldo_voltage": 5.1}
        )
        return manager

    def test_get_status(self, client, server_instance, mock_strobe_manager):
        """Status endpoint returns expected shape"""
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.get("/api/strobe-calibration/status")
        assert response.status_code == 200

        data = response.json()
        assert "state" in data
        assert "progress" in data
        assert "message" in data
        assert data["state"] == "idle"

    def test_get_settings(self, client, server_instance, mock_strobe_manager):
        """Settings endpoint returns saved DAC value"""
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.get("/api/strobe-calibration/settings")
        assert response.status_code == 200

        data = response.json()
        assert "dac_setting" in data

    def test_start_calibration(self, client, server_instance, mock_strobe_manager):
        """Start endpoint accepts parameters and returns immediately"""
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.post(
            "/api/strobe-calibration/start",
            json={"led_type": "v3", "target_current": 10.0, "overwrite": True},
        )
        assert response.status_code == 200

        data = response.json()
        assert data["status"] == "started"

    def test_start_calibration_defaults(self, client, server_instance, mock_strobe_manager):
        """Start endpoint works with empty body (all defaults)"""
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.post("/api/strobe-calibration/start", json={})
        assert response.status_code == 200
        assert response.json()["status"] == "started"

    def test_cancel(self, client, server_instance, mock_strobe_manager):
        """Cancel endpoint calls manager.cancel and returns ok"""
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.post("/api/strobe-calibration/cancel")
        assert response.status_code == 200

        data = response.json()
        assert data["status"] == "ok"
        mock_strobe_manager.cancel.assert_called_once()

    def test_get_diagnostics(self, client, server_instance, mock_strobe_manager):
        """Diagnostics endpoint returns hardware readings"""
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.get("/api/strobe-calibration/diagnostics")
        assert response.status_code == 200

        data = response.json()
        assert "ldo_voltage" in data
        assert "led_current" in data

    def test_set_dac_valid(self, client, server_instance, mock_strobe_manager):
        """Set-dac endpoint accepts a valid integer"""
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.post(
            "/api/strobe-calibration/set-dac", json={"value": 128}
        )
        assert response.status_code == 200

        data = response.json()
        assert data["status"] == "success"
        mock_strobe_manager.set_dac_manual.assert_called_once_with(128)

    def test_set_dac_missing_value(self, client, server_instance, mock_strobe_manager):
        """Set-dac rejects request with no value"""
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.post("/api/strobe-calibration/set-dac", json={})
        assert response.status_code == 200

        data = response.json()
        assert data["status"] == "error"
        assert "value" in data["message"].lower()

    def test_set_dac_non_integer(self, client, server_instance, mock_strobe_manager):
        """Set-dac rejects a non-integer value"""
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.post(
            "/api/strobe-calibration/set-dac", json={"value": "abc"}
        )
        assert response.status_code == 200

        data = response.json()
        assert data["status"] == "error"

    def test_get_dac_start(self, client, server_instance, mock_strobe_manager):
        """Dac-start endpoint returns boundary value"""
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.get("/api/strobe-calibration/dac-start")
        assert response.status_code == 200

        data = response.json()
        assert "dac_start" in data
        assert "ldo_voltage" in data

    def test_status_during_calibration(self, client, server_instance, mock_strobe_manager):
        """Status reflects in-progress calibration"""
        mock_strobe_manager.get_status.return_value = {
            "state": "calibrating",
            "progress": 45,
            "message": "Sweeping DAC",
        }
        server_instance.strobe_calibration_manager = mock_strobe_manager

        response = client.get("/api/strobe-calibration/status")
        assert response.status_code == 200

        data = response.json()
        assert data["state"] == "calibrating"
        assert data["progress"] == 45

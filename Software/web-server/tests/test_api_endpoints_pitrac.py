"""Tests for PiTrac process management API endpoints"""

import pytest
from unittest.mock import MagicMock, AsyncMock


@pytest.mark.unit
class TestPiTracAPIEndpoints:
    """Test PiTrac process management endpoints"""

    def test_pitrac_start(self, client, server_instance):
        """Test starting PiTrac process"""
        server_instance.pitrac_manager.start = AsyncMock(
            return_value={
                "status": "started",
                "message": "PiTrac started successfully",
                "pid": 12345,
                "camera1_pid": 12345,
            }
        )

        response = client.post("/api/pitrac/start")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "started"
        assert "started" in data["message"].lower()

        server_instance.pitrac_manager.start.assert_called_once()

    def test_pitrac_start_failure(self, client, server_instance):
        """Test starting PiTrac process failure"""
        server_instance.pitrac_manager.start = AsyncMock(
            return_value={"status": "error", "message": "Failed to start PiTrac: Binary not found"}
        )

        response = client.post("/api/pitrac/start")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "error"
        assert "failed" in data["message"].lower()

    def test_pitrac_start_already_running(self, client, server_instance):
        """Test starting PiTrac when already running"""
        server_instance.pitrac_manager.start = AsyncMock(
            return_value={"status": "already_running", "message": "PiTrac is already running", "pid": 12345}
        )

        response = client.post("/api/pitrac/start")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "already_running"
        assert "already running" in data["message"].lower()

    def test_pitrac_stop(self, client, server_instance):
        """Test stopping PiTrac process"""
        server_instance.pitrac_manager.stop = AsyncMock(
            return_value={"status": "stopped", "message": "PiTrac stopped successfully"}
        )

        response = client.post("/api/pitrac/stop")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "stopped"
        assert "stopped" in data["message"].lower()

    def test_pitrac_stop_not_running(self, client, server_instance):
        """Test stopping PiTrac when not running"""
        server_instance.pitrac_manager.stop = AsyncMock(
            return_value={"status": "not_running", "message": "PiTrac is not running"}
        )

        response = client.post("/api/pitrac/stop")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "not_running"
        assert "not running" in data["message"].lower()

    def test_pitrac_restart(self, client, server_instance):
        """Test restarting PiTrac process"""
        server_instance.pitrac_manager.restart = AsyncMock(
            return_value={
                "status": "restarted",
                "message": "PiTrac restarted successfully",
                "pid": 12346,
                "camera1_pid": 12346,
            }
        )

        response = client.post("/api/pitrac/restart")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] in ["restarted", "started"]
        assert "restart" in data["message"].lower() or "start" in data["message"].lower()

    def test_pitrac_status(self, client, server_instance):
        """Test getting PiTrac process status"""
        server_instance.pitrac_manager.get_status = MagicMock(
            return_value={
                "is_running": True,
                "camera1_pid": 12345,
                "camera2_pid": None,
                "mode": "single",
                "config_file": "/etc/pitrac/golf_sim_config.json",
            }
        )

        response = client.get("/api/pitrac/status")
        assert response.status_code == 200
        data = response.json()

        assert data["is_running"] is True
        assert data["camera1_pid"] == 12345
        assert data["mode"] == "single"

    def test_pitrac_status_not_running(self, client, server_instance):
        """Test getting PiTrac process status when not running"""
        server_instance.pitrac_manager.get_status = MagicMock(
            return_value={
                "is_running": False,
                "camera1_pid": None,
                "camera2_pid": None,
                "mode": "single",
                "config_file": "/etc/pitrac/golf_sim_config.json",
            }
        )

        response = client.get("/api/pitrac/status")
        assert response.status_code == 200
        data = response.json()

        assert data["is_running"] is False
        assert data["camera1_pid"] is None

    def test_pitrac_status_dual_camera(self, client, server_instance):
        """Test getting PiTrac process status with dual cameras"""
        server_instance.pitrac_manager.get_status = MagicMock(
            return_value={
                "is_running": True,
                "camera1_pid": 12345,
                "camera2_pid": 12346,
                "mode": "dual",
                "config_file": "/etc/pitrac/golf_sim_config.json",
            }
        )

        response = client.get("/api/pitrac/status")
        assert response.status_code == 200
        data = response.json()

        assert data["is_running"] is True
        assert data["camera1_pid"] == 12345
        assert data["camera2_pid"] == 12346
        assert data["mode"] == "dual"

    def test_pitrac_process_workflow(self, client, server_instance):
        """Test complete PiTrac process management workflow"""
        server_instance.pitrac_manager.get_status = MagicMock(
            return_value={"is_running": False, "camera1_pid": None, "camera2_pid": None}
        )

        response = client.get("/api/pitrac/status")
        assert response.status_code == 200
        assert response.json()["is_running"] is False

        server_instance.pitrac_manager.start = AsyncMock(
            return_value={"status": "started", "message": "PiTrac started successfully", "pid": 12345}
        )

        response = client.post("/api/pitrac/start")
        assert response.status_code == 200
        assert response.json()["status"] == "started"

        server_instance.pitrac_manager.stop = AsyncMock(
            return_value={"status": "stopped", "message": "PiTrac stopped successfully"}
        )

        response = client.post("/api/pitrac/stop")
        assert response.status_code == 200
        assert response.json()["status"] == "stopped"

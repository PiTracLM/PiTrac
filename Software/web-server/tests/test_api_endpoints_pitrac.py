"""Tests for PiTrac process management API endpoints"""

import pytest
from unittest.mock import AsyncMock, MagicMock, patch


@pytest.fixture(autouse=True)
def mock_strobe_safety(server_instance):
    """All pitrac process tests assume strobe is safe (V1/V2 or calibrated V3)"""
    server_instance.strobe_calibration_manager.is_strobe_safe = MagicMock(
        return_value={"safe": True, "board_version": None}
    )


class TestPiTracAPI:
    """Test PiTrac process management endpoints"""

    @pytest.mark.asyncio
    async def test_start_pitrac(self, client, server_instance):
        """Test starting PiTrac process"""
        server_instance.pitrac_manager.start = AsyncMock(
            return_value={"status": "started", "message": "PiTrac started successfully", "pid": 12345}
        )

        response = client.post("/api/pitrac/start")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] in ["started", "already_running"]

    @pytest.mark.asyncio
    async def test_stop_pitrac(self, client, server_instance):
        """Test stopping PiTrac process"""
        server_instance.pitrac_manager.stop = AsyncMock(
            return_value={"status": "stopped", "message": "PiTrac stopped successfully"}
        )

        response = client.post("/api/pitrac/stop")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] in ["stopped", "not_running"]

    @pytest.mark.asyncio
    async def test_restart_pitrac(self, client, server_instance):
        """Test restarting PiTrac process"""
        server_instance.pitrac_manager.restart = AsyncMock(
            return_value={"status": "started", "message": "PiTrac restarted successfully", "pid": 12345}
        )

        response = client.post("/api/pitrac/restart")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] in ["restarted", "started"]

    def test_pitrac_status(self, client, server_instance):
        """Test getting PiTrac process status"""
        server_instance.pitrac_manager.get_status = MagicMock(
            return_value={
                "is_running": True,
                "pid": 12345,
                "log_file": "/home/test/.pitrac/logs/pitrac.log",
                "generated_config_path": "/home/test/.pitrac/config/generated_golf_sim_config.json",
                "binary": "/usr/lib/pitrac/pitrac_lm",
            }
        )

        response = client.get("/api/pitrac/status")
        assert response.status_code == 200
        data = response.json()
        assert data["is_running"] is True
        assert data["pid"] == 12345

    def test_pitrac_status_not_running(self, client, server_instance):
        """Test getting PiTrac process status when not running"""
        server_instance.pitrac_manager.get_status = MagicMock(
            return_value={
                "is_running": False,
                "pid": None,
                "log_file": "/home/test/.pitrac/logs/pitrac.log",
                "generated_config_path": "/home/test/.pitrac/config/generated_golf_sim_config.json",
                "binary": "/usr/lib/pitrac/pitrac_lm",
            }
        )

        response = client.get("/api/pitrac/status")
        assert response.status_code == 200
        data = response.json()
        assert data["is_running"] is False
        assert data["pid"] is None

    def test_pitrac_process_workflow(self, client, server_instance):
        """Test complete PiTrac process management workflow"""
        server_instance.pitrac_manager.get_status = MagicMock(
            return_value={"is_running": False, "pid": None}
        )

        response = client.get("/api/pitrac/status")
        assert response.status_code == 200
        assert response.json()["is_running"] is False

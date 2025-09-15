"""Tests for calibration API endpoints"""

import pytest
from unittest.mock import Mock, AsyncMock


@pytest.mark.unit
class TestCalibrationAPI:
    """Test suite for calibration API endpoints"""

    @pytest.fixture
    def mock_calibration_manager(self):
        """Create a mock calibration manager"""
        manager = Mock()
        manager.is_calibrating = False
        manager.current_camera = None
        manager.calibration_type = None
        manager.calibration_data = {"camera1": {"status": "not_calibrated"}, "camera2": {"status": "not_calibrated"}}

        manager.check_ball_location = AsyncMock(
            return_value={
                "success": True,
                "message": "Ball detected at correct location",
                "ball_present": True,
                "location": {"x": 100, "y": 200},
            }
        )

        manager.run_auto_calibration = AsyncMock(
            return_value={"success": True, "message": "Auto calibration started", "camera": "camera1"}
        )

        manager.run_manual_calibration = AsyncMock(
            return_value={
                "success": True,
                "message": "Manual calibration started",
                "camera": "camera1",
                "instructions": "Adjust camera until ball is centered",
            }
        )

        manager.stop_calibration = AsyncMock(return_value={"success": True, "message": "Calibration stopped"})

        manager.capture_still_image = AsyncMock(
            return_value={"success": True, "message": "Image captured", "image_path": "/tmp/calibration_camera1.jpg"}
        )

        manager.stop_calibration = AsyncMock(return_value={"success": True, "message": "Calibration stopped"})

        manager.get_status = Mock(
            return_value={"is_calibrating": False, "current_camera": None, "calibration_type": None, "progress": 0}
        )

        manager.get_calibration_data = Mock(
            return_value={
                "camera1": {"calibrated": False, "last_calibration": None, "settings": {}},
                "camera2": {"calibrated": False, "last_calibration": None, "settings": {}},
            }
        )

        return manager

    def test_calibration_page(self, client):
        """Test calibration page loads"""
        response = client.get("/calibration")
        assert response.status_code == 200
        assert "Calibration" in response.text

    def test_get_calibration_status(self, client, server_instance, mock_calibration_manager):
        """Test getting calibration status"""
        server_instance.calibration_manager = mock_calibration_manager

        response = client.get("/api/calibration/status")
        assert response.status_code == 200

        data = response.json()
        assert data["is_calibrating"] is False
        assert data["current_camera"] is None
        assert data["calibration_type"] is None
        assert data["progress"] == 0

    def test_get_calibration_data(self, client, server_instance, mock_calibration_manager):
        """Test getting calibration data"""
        server_instance.calibration_manager = mock_calibration_manager

        response = client.get("/api/calibration/data")
        assert response.status_code == 200

        data = response.json()
        assert "camera1" in data
        assert "camera2" in data
        assert data["camera1"]["calibrated"] is False
        assert data["camera2"]["calibrated"] is False

    @pytest.mark.asyncio
    async def test_check_ball_location(self, client, server_instance, mock_calibration_manager):
        """Test checking ball location"""
        server_instance.calibration_manager = mock_calibration_manager

        response = client.post("/api/calibration/ball-location/camera1")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is True
        assert data["ball_present"] is True
        assert "location" in data

        mock_calibration_manager.check_ball_location.assert_called_once_with("camera1")

    @pytest.mark.asyncio
    async def test_check_ball_location_not_found(self, client, server_instance, mock_calibration_manager):
        """Test checking ball location when not found"""
        mock_calibration_manager.check_ball_location.return_value = {
            "success": False,
            "message": "Ball not detected",
            "ball_present": False,
        }
        server_instance.calibration_manager = mock_calibration_manager

        response = client.post("/api/calibration/ball-location/camera2")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is False
        assert data["ball_present"] is False
        assert "Ball not detected" in data["message"]

    @pytest.mark.asyncio
    async def test_start_auto_calibration(self, client, server_instance, mock_calibration_manager):
        """Test starting auto calibration"""
        server_instance.calibration_manager = mock_calibration_manager

        response = client.post("/api/calibration/auto/camera1")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is True
        assert data["camera"] == "camera1"
        assert "started" in data["message"]

        mock_calibration_manager.run_auto_calibration.assert_called_once_with("camera1")

    @pytest.mark.asyncio
    async def test_start_auto_calibration_busy(self, client, server_instance, mock_calibration_manager):
        """Test starting auto calibration when already calibrating"""
        mock_calibration_manager.is_calibrating = True
        mock_calibration_manager.run_auto_calibration.return_value = {
            "success": False,
            "message": "Calibration already in progress",
        }
        server_instance.calibration_manager = mock_calibration_manager

        response = client.post("/api/calibration/auto/camera1")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is False
        assert "already in progress" in data["message"]

    @pytest.mark.asyncio
    async def test_start_manual_calibration(self, client, server_instance, mock_calibration_manager):
        """Test starting manual calibration"""
        server_instance.calibration_manager = mock_calibration_manager

        response = client.post("/api/calibration/manual/camera2")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is True
        assert data["camera"] == "camera1"  # From mock return value
        assert "instructions" in data

        mock_calibration_manager.run_manual_calibration.assert_called_once_with("camera2")

    @pytest.mark.asyncio
    async def test_capture_still_image(self, client, server_instance, mock_calibration_manager):
        """Test capturing still image during calibration"""
        server_instance.calibration_manager = mock_calibration_manager

        response = client.post("/api/calibration/capture/camera1")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is True
        assert "image_path" in data
        assert "captured" in data["message"]

        mock_calibration_manager.capture_still_image.assert_called_once_with("camera1")

    @pytest.mark.asyncio
    async def test_capture_still_image_failure(self, client, server_instance, mock_calibration_manager):
        """Test capturing still image failure"""
        mock_calibration_manager.capture_still_image.return_value = {
            "success": False,
            "message": "Camera not available",
        }
        server_instance.calibration_manager = mock_calibration_manager

        response = client.post("/api/calibration/capture/camera1")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is False
        assert "Camera not available" in data["message"]

    @pytest.mark.asyncio
    async def test_stop_calibration(self, client, server_instance, mock_calibration_manager):
        """Test stopping calibration"""
        mock_calibration_manager.is_calibrating = True
        server_instance.calibration_manager = mock_calibration_manager

        response = client.post("/api/calibration/stop")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is True
        assert "stopped" in data["message"]

        mock_calibration_manager.stop_calibration.assert_called_once()

    @pytest.mark.asyncio
    async def test_stop_calibration_not_running(self, client, server_instance, mock_calibration_manager):
        """Test stopping calibration when not running"""
        mock_calibration_manager.is_calibrating = False
        mock_calibration_manager.stop_calibration.return_value = {
            "success": False,
            "message": "No calibration in progress",
        }
        server_instance.calibration_manager = mock_calibration_manager

        response = client.post("/api/calibration/stop")
        assert response.status_code == 200

        data = response.json()
        assert data["success"] is False
        assert "No calibration" in data["message"]

    def test_calibration_status_during_auto(self, client, server_instance, mock_calibration_manager):
        """Test calibration status during auto calibration"""
        mock_calibration_manager.is_calibrating = True
        mock_calibration_manager.current_camera = "camera1"
        mock_calibration_manager.calibration_type = "auto"
        mock_calibration_manager.get_status.return_value = {
            "is_calibrating": True,
            "current_camera": "camera1",
            "calibration_type": "auto",
            "progress": 45,
        }
        server_instance.calibration_manager = mock_calibration_manager

        response = client.get("/api/calibration/status")
        assert response.status_code == 200

        data = response.json()
        assert data["is_calibrating"] is True
        assert data["current_camera"] == "camera1"
        assert data["calibration_type"] == "auto"
        assert data["progress"] == 45

    def test_calibration_data_after_calibration(self, client, server_instance, mock_calibration_manager):
        """Test calibration data after successful calibration"""
        mock_calibration_manager.get_calibration_data.return_value = {
            "camera1": {
                "calibrated": True,
                "last_calibration": "2024-01-01T12:00:00",
                "settings": {"exposure": 1000, "gain": 2.0, "offset_x": 10, "offset_y": 20},
            },
            "camera2": {"calibrated": False, "last_calibration": None, "settings": {}},
        }
        server_instance.calibration_manager = mock_calibration_manager

        response = client.get("/api/calibration/data")
        assert response.status_code == 200

        data = response.json()
        assert data["camera1"]["calibrated"] is True
        assert data["camera1"]["last_calibration"] == "2024-01-01T12:00:00"
        assert data["camera1"]["settings"]["exposure"] == 1000
        assert data["camera2"]["calibrated"] is False

    @pytest.mark.parametrize("camera", ["camera1", "camera2"])
    def test_calibration_endpoints_with_different_cameras(
        self, client, server_instance, mock_calibration_manager, camera
    ):
        """Test calibration endpoints work with different camera names"""
        server_instance.calibration_manager = mock_calibration_manager

        response = client.post(f"/api/calibration/ball-location/{camera}")
        assert response.status_code == 200

        response = client.post(f"/api/calibration/auto/{camera}")
        assert response.status_code == 200

        response = client.post(f"/api/calibration/manual/{camera}")
        assert response.status_code == 200

        response = client.post(f"/api/calibration/capture/{camera}")
        assert response.status_code == 200

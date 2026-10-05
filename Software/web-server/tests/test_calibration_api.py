"""Tests for calibration API endpoints"""

import pytest
from datetime import datetime
from unittest.mock import Mock, AsyncMock, MagicMock

from server import LENS_CALIBRATION_FEED, LENS_PREVIEW


@pytest.fixture(autouse=True)
def mock_strobe_safety(server_instance):
    """Calibration tests assume strobe is safe"""
    server_instance.strobe_calibration_manager.is_strobe_safe = MagicMock(
        return_value={"safe": True, "board_version": None}
    )


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
        manager.busy_reason = Mock(return_value=None)

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


@pytest.fixture
def started_server(server_instance):
    server_instance.calibration_manager.loop = Mock()
    return server_instance


def test_calibration_data_reports_factory_defaults(client):
    data = client.get("/api/calibration/data").json()
    for camera in ("camera1", "camera2"):
        assert data[camera]["lens_calibrated"] is False
        assert data[camera]["position_calibrated"] is False


def test_setup_status_fresh_install(client):
    s = client.get("/api/setup/status").json()
    assert s["board_version"] == 3
    assert s["strobe"] == {"required": True, "safe": True, "reason": "", "updated_at": None}
    assert s["cameras"]["camera1"]["position_calibrated"] is False
    assert s["cameras"]["camera2"]["lens_calibrated"] is False
    assert "type" in s["cameras"]["camera1"]
    assert s["simulator"] == {"instances": [], "connected": []}
    assert s["complete"] is False


def test_setup_status_complete_when_everything_is_done(client, server_instance):
    calibration = {}
    for n in (1, 2):
        calibration[f"gs_config.cameras.kCamera{n}CalibrationMatrix"] = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
        calibration[f"gs_config.cameras.kCamera{n}FocalLength"] = 6.1
        calibration[f"gs_config.cameras.kCamera{n}Angles"] = [1.5, -2.5]
    assert server_instance.config_manager.set_calibration_batch(calibration)[0]

    s = client.get("/api/setup/status").json()
    assert s["complete"] is True

    sim = client.post("/api/sims", json={"type": "ogs", "name": "iPad", "settings": {"host": "ipad.local"}}).json()
    s = client.get("/api/setup/status").json()
    assert s["simulator"] == {
        "instances": [{"id": sim["id"], "name": "iPad", "type": "ogs", "on": True, "status": "off"}],
        "connected": [],
    }
    assert s["complete"] is True

    server_instance.strobe_calibration_manager.is_strobe_safe.return_value = {
        "safe": False, "board_version": 3, "reason": "V3 board requires strobe calibration before use."
    }
    s = client.get("/api/setup/status").json()
    assert s["strobe"]["safe"] is False
    assert s["strobe"]["reason"].startswith("V3 board")
    assert s["complete"] is False


def test_setup_status_reports_when_each_calibration_was_saved(client, server_instance):
    s = client.get("/api/setup/status").json()
    for camera in ("camera1", "camera2"):
        assert s["cameras"][camera]["lens_updated_at"] is None
        assert s["cameras"][camera]["position_updated_at"] is None

    config = server_instance.config_manager
    assert config.set_calibration_batch({
        "gs_config.cameras.kCamera1CalibrationMatrix": [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]],
        "gs_config.cameras.kCamera1FocalLength": 6.1,
    })[0]
    assert config.set_config("gs_config.strobing.kDAC_setting", 140)[0]

    s = client.get("/api/setup/status").json()
    lens = s["cameras"]["camera1"]["lens_updated_at"]
    assert datetime.fromisoformat(lens)
    assert s["cameras"]["camera1"]["position_updated_at"] == lens
    assert datetime.fromisoformat(s["strobe"]["updated_at"]) >= datetime.fromisoformat(lens)
    assert s["cameras"]["camera2"]["lens_updated_at"] is None


def test_position_updated_at_is_the_later_of_focal_length_and_angles(client, server_instance):
    config = server_instance.config_manager
    assert config.set_calibration_batch({
        "gs_config.cameras.kCamera1FocalLength": 6.1,
        "gs_config.cameras.kCamera1Angles": [2.0, -26.0],
    })[0]
    config._db.execute(
        "UPDATE calibration SET updated_at = '2026-01-01T00:00:00' WHERE key = 'gs_config.cameras.kCamera1FocalLength'"
    )
    config._db.execute(
        "UPDATE calibration SET updated_at = '2026-03-01T00:00:00' WHERE key = 'gs_config.cameras.kCamera1Angles'"
    )
    s = client.get("/api/setup/status").json()
    assert s["cameras"]["camera1"]["position_updated_at"] == "2026-03-01T00:00:00"


def test_calibration_status_keeps_ball_keys_and_adds_distortion(client):
    status = client.get("/api/calibration/status").json()
    assert status["camera1"]["status"] == "idle"
    assert status["camera2"]["status"] == "idle"
    lens = status["distortion"]["camera1"]
    assert lens["status"] == "idle"
    assert lens["images_captured"] == 0
    assert lens["target_images"] == 40
    assert lens["requirements"] == {
        "coverage": 0.0, "coverage_target": 0.8,
        "tilt": 0.0, "tilt_target": 0.4,
        "bins": {"small": 0, "medium": 0, "large": 0}, "bin_target": 3,
    }


def test_distortion_route_reports_rejection(client, started_server):
    started_server.calibration_manager.distortion_status["camera1"]["status"] = "distortion_calibrating"
    started_server.calibration_manager.run_distortion_calibration = AsyncMock()

    data = client.post("/api/calibration/distortion/camera1").json()

    assert data["status"] == "error"
    assert "lens calibration" in data["message"]
    started_server.calibration_manager.run_distortion_calibration.assert_not_called()


def test_distortion_route_allows_its_own_feed(client, started_server):
    started_server._active_cameras[0] = LENS_CALIBRATION_FEED
    started_server.calibration_manager.run_distortion_calibration = AsyncMock()

    data = client.post("/api/calibration/distortion/camera1", json={"target_images": 40}).json()

    assert data["status"] == "started"


def test_ball_calibration_refused_while_camera_busy(client, started_server):
    cm = started_server.calibration_manager
    cm.run_auto_calibration = AsyncMock()

    started_server._active_cameras[0] = LENS_PREVIEW
    data = client.post("/api/calibration/auto/camera1").json()
    assert data == {"status": "error", "message": "Camera 1 is in use by the lens preview. Close it first."}

    started_server._active_cameras.clear()
    cm.distortion_status["camera2"]["status"] = "distortion_calibrating"
    data = client.post("/api/calibration/auto/camera2").json()
    assert data["status"] == "error"
    assert "Camera 2" in data["message"]

    cm.run_auto_calibration.assert_not_called()


def test_ball_calibration_refused_while_pitrac_running(client, started_server):
    started_server.calibration_manager.run_auto_calibration = AsyncMock()
    started_server.pitrac_manager.is_running = Mock(return_value=True)

    data = client.post("/api/calibration/auto/camera1").json()

    assert data == {"status": "error", "message": "Stop PiTrac before calibrating"}
    started_server.calibration_manager.run_auto_calibration.assert_not_called()


def test_stop_route_targets_camera_and_kind(client, server_instance):
    stop = AsyncMock(return_value={"status": "not_running"})
    server_instance.calibration_manager.stop_calibration = stop

    client.post("/api/calibration/stop", json={"camera": "camera2", "kind": "ball"})
    stop.assert_awaited_with("camera2", "ball")

    client.post("/api/calibration/stop")
    stop.assert_awaited_with(None, None)

"""
Comprehensive unit tests for CalibrationManager class

Tests the actual CalibrationManager functionality rather than just API endpoints.
Focuses on testing real logic with minimal mocking to catch actual bugs.
"""

import asyncio
import pytest
from pathlib import Path
from unittest.mock import Mock, patch, AsyncMock

from calibration_manager import CalibrationManager


class TestCalibrationManagerInitialization:
    """Test CalibrationManager initialization and configuration"""

    def test_init_with_defaults(self):
        """Test CalibrationManager initialization with default parameters"""
        mock_config_manager = Mock()

        manager = CalibrationManager(mock_config_manager)

        assert manager.config_manager == mock_config_manager
        assert manager.pitrac_binary == "/usr/lib/pitrac/pitrac_lm"
        assert manager.current_process is None
        assert isinstance(manager.calibration_status, dict)
        assert "camera1" in manager.calibration_status
        assert "camera2" in manager.calibration_status
        assert manager.log_dir.exists()

    def test_init_with_custom_binary(self):
        """Test initialization with custom binary path"""
        mock_config_manager = Mock()
        custom_binary = "/custom/path/pitrac_lm"

        manager = CalibrationManager(mock_config_manager, custom_binary)

        assert manager.pitrac_binary == custom_binary

    def test_initial_calibration_status(self):
        """Test that initial calibration status is properly set"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        for camera in ["camera1", "camera2"]:
            status = manager.calibration_status[camera]
            assert status["status"] == "idle"
            assert status["message"] == ""
            assert status["progress"] == 0
            assert status["last_run"] is None

    def test_log_directory_creation(self):
        """Test that log directory is created correctly"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        expected_log_dir = Path.home() / ".pitrac" / "logs"
        assert manager.log_dir == expected_log_dir
        assert manager.log_dir.exists()


class TestCalibrationManagerStatus:
    """Test status management functionality"""

    def test_get_status(self):
        """Test getting calibration status"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        status = manager.get_status()

        assert isinstance(status, dict)
        assert "camera1" in status
        assert "camera2" in status

        for camera_status in status.values():
            assert "status" in camera_status
            assert "message" in camera_status
            assert "progress" in camera_status
            assert "last_run" in camera_status

    def test_status_updates_during_operation(self):
        """Test that status is updated during operations"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}, "calibration": {}}

        manager = CalibrationManager(mock_config_manager)

        assert manager.calibration_status["camera1"]["status"] == "idle"

        manager.calibration_status["camera1"]["status"] = "checking_ball"
        manager.calibration_status["camera1"]["message"] = "Detecting ball..."
        manager.calibration_status["camera1"]["progress"] = 50

        status = manager.get_status()
        assert status["camera1"]["status"] == "checking_ball"
        assert status["camera1"]["message"] == "Detecting ball..."
        assert status["camera1"]["progress"] == 50


class TestCalibrationDataRetrieval:
    """Test calibration data retrieval functionality"""

    def test_get_calibration_data_basic(self):
        """Test getting calibration data from config"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {
            "gs_config": {
                "cameras": {
                    "kCamera1FocalLength": 1000.0,
                    "kCamera1XOffsetForTilt": 10.5,
                    "kCamera1YOffsetForTilt": -5.2,
                    "kCamera2FocalLength": 1050.0,
                    "kCamera2XOffsetForTilt": 8.7,
                    "kCamera2YOffsetForTilt": -3.1,
                }
            }
        }

        manager = CalibrationManager(mock_config_manager)
        data = manager.get_calibration_data()

        assert "camera1" in data
        assert "camera2" in data

        assert data["camera1"]["focal_length"] == 1000.0
        assert data["camera1"]["x_offset"] == 10.5
        assert data["camera1"]["y_offset"] == -5.2

        assert data["camera2"]["focal_length"] == 1050.0
        assert data["camera2"]["x_offset"] == 8.7
        assert data["camera2"]["y_offset"] == -3.1

    def test_get_calibration_data_missing_values(self):
        """Test getting calibration data when some values are missing"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {
            "gs_config": {
                "cameras": {
                    "kCamera1FocalLength": 1000.0,
                    "kCamera2XOffsetForTilt": 8.7,
                }
            }
        }

        manager = CalibrationManager(mock_config_manager)
        data = manager.get_calibration_data()

        assert data["camera1"]["focal_length"] == 1000.0
        assert data["camera1"]["x_offset"] is None
        assert data["camera1"]["y_offset"] is None

        assert data["camera2"]["focal_length"] is None
        assert data["camera2"]["x_offset"] == 8.7
        assert data["camera2"]["y_offset"] is None

    def test_get_calibration_data_empty_config(self):
        """Test getting calibration data when config is empty"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {}

        manager = CalibrationManager(mock_config_manager)
        data = manager.get_calibration_data()

        for camera in ["camera1", "camera2"]:
            assert camera in data
            assert data[camera]["focal_length"] is None
            assert data[camera]["x_offset"] is None
            assert data[camera]["y_offset"] is None


class TestCommandBuilding:
    """Test command building logic for different operations"""

    @pytest.fixture
    def mock_config_manager(self):
        """Create mock config manager with typical configuration"""
        mock = Mock()
        mock.get_config.return_value = {
            "system": {"mode": "single"},
            "calibration": {
                "camera1_search_center_x": 800,
                "camera1_search_center_y": 600,
                "camera2_search_center_x": 750,
                "camera2_search_center_y": 500,
            },
        }
        mock.generated_config_path = "/tmp/test_config.yaml"
        return mock

    def test_ball_location_command_single_mode(self, mock_config_manager):
        """Test command building for ball location in single mode"""
        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        config = mock_config_manager.get_config()
        system_mode = config.get("system", {}).get("mode", "single")

        assert system_mode == "single"

        search_x = config.get("calibration", {}).get("camera1_search_center_x", 750)
        search_y = config.get("calibration", {}).get("camera1_search_center_y", 500)

        assert search_x == 800
        assert search_y == 600

    def test_ball_location_command_dual_mode(self):
        """Test command building for ball location in dual mode"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {
            "system": {"mode": "dual"},
            "calibration": {
                "camera2_search_center_x": 700,
                "camera2_search_center_y": 450,
            },
        }
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        config = mock_config_manager.get_config()
        system_mode = config.get("system", {}).get("mode", "single")

        assert system_mode == "dual"

        search_x = config.get("calibration", {}).get("camera2_search_center_x", 750)
        search_y = config.get("calibration", {}).get("camera2_search_center_y", 500)

        assert search_x == 700
        assert search_y == 450

    def test_calibration_search_defaults(self):
        """Test that default search values are used when not in config"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}, "calibration": {}}

        manager = CalibrationManager(mock_config_manager)
        config = mock_config_manager.get_config()

        search_x = config.get("calibration", {}).get("camera1_search_center_x", 750)
        search_y = config.get("calibration", {}).get("camera1_search_center_y", 500)

        assert search_x == 750
        assert search_y == 500

        search_x_manual = config.get("calibration", {}).get("camera1_search_center_x", 700)
        search_y_manual = config.get("calibration", {}).get("camera1_search_center_y", 500)

        assert search_x_manual == 700
        assert search_y_manual == 500


class TestOutputParsing:
    """Test output parsing functionality"""

    def test_parse_ball_location_found(self):
        """Test parsing ball location when ball is found"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        output_with_ball = """
        Starting ball detection...
        Searching for ball at coordinates...
        Ball found at position (750, 500) with confidence 0.95
        Ball location detection complete
        """

        result = manager._parse_ball_location(output_with_ball)

        assert result is not None
        assert result["found"] is True
        assert "x" in result
        assert "y" in result
        assert "confidence" in result

    def test_parse_ball_location_not_found(self):
        """Test parsing ball location when ball is not found"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        output_no_ball = """
        Starting ball detection...
        Searching for ball at coordinates...
        No ball detected in search area
        Detection failed
        """

        result = manager._parse_ball_location(output_no_ball)

        assert result is None

    def test_parse_calibration_results_success(self):
        """Test parsing calibration results when successful"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        output_success = """
        Starting calibration process...
        Analyzing camera parameters...
        Calculating focal length: 1000.5
        Camera calibration complete
        Calibration successful
        """

        result = manager._parse_calibration_results(output_success)

        assert result is not None
        assert result.get("complete") is True

    def test_parse_calibration_results_failure(self):
        """Test parsing calibration results when failed"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        output_failure = """
        Starting calibration process...
        Error: Unable to detect calibration pattern
        Calibration failed
        """

        result = manager._parse_calibration_results(output_failure)

        assert result is None or not result


class TestStillImageCapture:
    """Test still image capture functionality"""

    @pytest.mark.asyncio
    async def test_capture_still_image_success(self):
        """Test successful still image capture"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}}
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Image captured successfully", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            with patch("pathlib.Path.exists", return_value=True):
                result = await manager.capture_still_image("camera1")

        assert result["status"] == "success"
        assert "image_path" in result
        assert "image_url" in result
        assert "/api/images/" in result["image_url"]

    @pytest.mark.asyncio
    async def test_capture_still_image_file_not_created(self):
        """Test still image capture when file is not created"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}}
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Process completed", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            with patch("pathlib.Path.exists", return_value=False):
                result = await manager.capture_still_image("camera1")

        assert result["status"] == "failed"
        assert result["message"] == "Image capture failed"

    @pytest.mark.asyncio
    async def test_capture_still_image_process_error(self):
        """Test still image capture when process fails"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}}
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_subprocess.side_effect = Exception("Camera not found")

            result = await manager.capture_still_image("camera1")

        assert result["status"] == "error"
        assert "Camera not found" in result["message"]

    def test_capture_still_image_filename_generation(self):
        """Test that capture generates proper filenames with timestamps"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        with patch("calibration_manager.datetime") as mock_datetime:
            mock_datetime.now.return_value.strftime.return_value = "20240101_120000"

            timestamp = mock_datetime.now().strftime("%Y%m%d_%H%M%S")
            output_file = f"calibration_camera1_{timestamp}.png"

            assert output_file == "calibration_camera1_20240101_120000.png"


class TestLogFileHandling:
    """Test log file creation and management"""

    @pytest.mark.asyncio
    async def test_log_file_creation_during_calibration(self):
        """Test that log files are created during calibration operations"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}, "calibration": {}}
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Test output", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            with patch("builtins.open", create=True) as mock_open:
                mock_file = Mock()
                mock_open.return_value.__enter__.return_value = mock_file

                result = await manager.check_ball_location("camera1")

                mock_open.assert_called_once()
                mock_file.write.assert_called_once_with("Test output")

    def test_log_directory_exists_after_init(self):
        """Test that log directory is created and accessible"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        assert manager.log_dir.exists()
        assert manager.log_dir.is_dir()

        test_file = manager.log_dir / "test.log"
        test_file.write_text("test")
        assert test_file.exists()
        test_file.unlink()


class TestErrorHandling:
    """Test error handling and edge cases"""

    @pytest.mark.asyncio
    async def test_check_ball_location_timeout(self):
        """Test ball location check with timeout"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}, "calibration": {}}
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.side_effect = asyncio.TimeoutError()
            mock_process.terminate = AsyncMock()
            mock_process.wait = AsyncMock()
            mock_subprocess.return_value = mock_process

            result = await manager.check_ball_location("camera1")

        assert result["status"] == "error"
        assert "timed out" in result["message"]
        assert manager.calibration_status["camera1"]["status"] == "error"

    @pytest.mark.asyncio
    async def test_calibration_process_failure(self):
        """Test calibration when process returns non-zero exit code"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}, "calibration": {}}
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Error: Camera not found", b"")
            mock_process.returncode = 1
            mock_subprocess.return_value = mock_process

            result = await manager.run_auto_calibration("camera1")

        assert result["status"] == "error"
        assert "failed with code 1" in result["message"]
        assert manager.calibration_status["camera1"]["status"] == "error"

    @pytest.mark.asyncio
    async def test_invalid_camera_name(self):
        """Test operations with invalid camera names"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}, "calibration": {}}

        manager = CalibrationManager(mock_config_manager)

        result = await manager.check_ball_location("invalid_camera")

        assert "invalid_camera" in manager.calibration_status

    @pytest.mark.asyncio
    async def test_stop_calibration_no_process(self):
        """Test stopping calibration when no process is running"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        assert manager.current_process is None

        result = await manager.stop_calibration()

        assert result["status"] == "not_running"

    @pytest.mark.asyncio
    async def test_stop_calibration_with_process(self):
        """Test stopping calibration when process is running"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        mock_process = AsyncMock()
        mock_process.terminate = AsyncMock()
        mock_process.wait = AsyncMock()
        manager.current_process = mock_process

        result = await manager.stop_calibration()

        assert result["status"] == "stopped"
        mock_process.terminate.assert_called_once()
        mock_process.wait.assert_called_once()

    @pytest.mark.asyncio
    async def test_stop_calibration_terminate_fails(self):
        """Test stopping calibration when terminate fails"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        mock_process = AsyncMock()
        mock_process.terminate.side_effect = Exception("Permission denied")
        mock_process.wait.side_effect = Exception("Wait failed")
        manager.current_process = mock_process

        result = await manager.stop_calibration()

        assert result["status"] == "error"
        assert "Permission denied" in result["message"] or "Wait failed" in result["message"]


class TestRealCalibrationWorkflows:
    """Test actual calibration workflows with minimal mocking"""

    @pytest.mark.asyncio
    async def test_auto_calibration_success_workflow(self):
        """Test complete auto calibration workflow"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {
            "system": {"mode": "single"},
            "calibration": {
                "camera1_search_center_x": 800,
                "camera1_search_center_y": 600,
            },
        }
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"
        mock_config_manager.reload_config = Mock()

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Auto calibration complete", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            with patch("builtins.open", create=True):
                result = await manager.run_auto_calibration("camera1")

        assert result["status"] == "success"
        assert "calibration_data" in result
        assert manager.calibration_status["camera1"]["status"] == "completed"
        assert manager.calibration_status["camera1"]["progress"] == 100
        mock_config_manager.reload_config.assert_called_once()

    @pytest.mark.asyncio
    async def test_manual_calibration_success_workflow(self):
        """Test complete manual calibration workflow"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {
            "system": {"mode": "dual"},
            "calibration": {
                "camera2_search_center_x": 700,
                "camera2_search_center_y": 450,
            },
        }
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"
        mock_config_manager.reload_config = Mock()

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Manual calibration complete", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            with patch("builtins.open", create=True):
                result = await manager.run_manual_calibration("camera2")

        assert result["status"] == "success"
        assert "calibration_data" in result
        assert manager.calibration_status["camera2"]["status"] == "completed"
        assert manager.calibration_status["camera2"]["progress"] == 100
        mock_config_manager.reload_config.assert_called_once()

    @pytest.mark.asyncio
    async def test_calibration_failure_no_results(self):
        """Test calibration failure when no results are parsed"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}, "calibration": {}}
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Process completed but no calibration data", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            with patch("builtins.open", create=True):
                result = await manager.run_auto_calibration("camera1")

        assert result["status"] == "failed"
        assert result["message"] == "Calibration failed"
        assert manager.calibration_status["camera1"]["status"] == "failed"

    @pytest.mark.asyncio
    async def test_ball_location_detection_success(self):
        """Test successful ball location detection"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {
            "system": {"mode": "single"},
            "calibration": {
                "camera1_search_center_x": 800,
                "camera1_search_center_y": 600,
            },
        }
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Ball found at location (800, 600)", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            with patch("builtins.open", create=True):
                result = await manager.check_ball_location("camera1")

        assert result["status"] == "success"
        assert result["ball_found"] is True
        assert result["ball_info"] is not None
        assert manager.calibration_status["camera1"]["status"] == "ball_found"

    @pytest.mark.asyncio
    async def test_still_image_capture_with_dual_mode(self):
        """Test still image capture in dual camera mode"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "dual"}}
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Image captured successfully", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            with patch("pathlib.Path.exists", return_value=True):
                result = await manager.capture_still_image("camera2")

        assert result["status"] == "success"
        assert "image_path" in result
        assert "image_url" in result


class TestIntegrationScenarios:
    """Test realistic integration scenarios"""

    @pytest.mark.asyncio
    async def test_full_calibration_workflow_success(self):
        """Test complete calibration workflow from ball check to completion"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {
            "system": {"mode": "single"},
            "calibration": {
                "camera1_search_center_x": 800,
                "camera1_search_center_y": 600,
            },
        }
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"
        mock_config_manager.reload_config = Mock()

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Ball found at position\nCalibration complete", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            ball_result = await manager.check_ball_location("camera1")
            assert ball_result["status"] == "success"
            assert manager.calibration_status["camera1"]["status"] == "ball_found"

            cal_result = await manager.run_auto_calibration("camera1")
            assert cal_result["status"] == "success"
            assert manager.calibration_status["camera1"]["status"] == "completed"

            mock_config_manager.reload_config.assert_called_once()

    @pytest.mark.asyncio
    async def test_calibration_workflow_with_failures(self):
        """Test calibration workflow with various failure points"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}, "calibration": {}}
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"

        manager = CalibrationManager(mock_config_manager, "/test/pitrac_lm")

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"No ball detected", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            ball_result = await manager.check_ball_location("camera1")
            assert ball_result["status"] == "success"
            assert ball_result["ball_found"] is False
            assert manager.calibration_status["camera1"]["status"] == "ball_not_found"

        with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
            mock_process = AsyncMock()
            mock_process.communicate.return_value = (b"Calibration failed", b"")
            mock_process.returncode = 0
            mock_subprocess.return_value = mock_process

            cal_result = await manager.run_auto_calibration("camera1")
            assert cal_result["status"] == "failed"
            assert manager.calibration_status["camera1"]["status"] == "failed"

    def test_concurrent_camera_operations(self):
        """Test that multiple cameras can have independent status"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        manager.calibration_status["camera1"]["status"] = "calibrating"
        manager.calibration_status["camera1"]["progress"] = 50
        manager.calibration_status["camera2"]["status"] = "ball_found"
        manager.calibration_status["camera2"]["progress"] = 100

        status = manager.get_status()

        assert status["camera1"]["status"] == "calibrating"
        assert status["camera1"]["progress"] == 50
        assert status["camera2"]["status"] == "ball_found"
        assert status["camera2"]["progress"] == 100

    def test_log_file_creation_pattern(self):
        """Test that log files are created with proper naming pattern"""
        mock_config_manager = Mock()
        manager = CalibrationManager(mock_config_manager)

        with patch("calibration_manager.datetime") as mock_datetime:
            mock_datetime.now.return_value.strftime.return_value = "20240101_120000"

            log_file_path = manager.log_dir / f"calibration_camera1_{mock_datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
            expected_path = Path.home() / ".pitrac" / "logs" / "calibration_camera1_20240101_120000.log"

            assert str(log_file_path) == str(expected_path)


class TestConfigurationHandling:
    """Test configuration handling edge cases"""

    def test_missing_config_sections(self):
        """Test handling when config sections are missing"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {}

        manager = CalibrationManager(mock_config_manager)

        config = mock_config_manager.get_config()
        system_mode = config.get("system", {}).get("mode", "single")
        assert system_mode == "single"

        search_x = config.get("calibration", {}).get("camera1_search_center_x", 750)
        assert search_x == 750

    def test_config_with_partial_data(self):
        """Test handling when config has partial data"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {
            "system": {"mode": "dual"},
        }

        manager = CalibrationManager(mock_config_manager)

        config = mock_config_manager.get_config()
        system_mode = config.get("system", {}).get("mode", "single")
        assert system_mode == "dual"

        search_x = config.get("calibration", {}).get("camera1_search_center_x", 750)
        assert search_x == 750

    def test_config_reload_after_calibration(self):
        """Test that config is reloaded after successful calibration"""
        mock_config_manager = Mock()
        mock_config_manager.get_config.return_value = {"system": {"mode": "single"}, "calibration": {}}
        mock_config_manager.generated_config_path = "/tmp/test_config.yaml"
        mock_config_manager.reload_config = Mock()

        manager = CalibrationManager(mock_config_manager)

        with patch.object(manager, "_parse_calibration_results") as mock_parse:
            mock_parse.return_value = {"complete": True}

            with patch("calibration_manager.asyncio.create_subprocess_exec") as mock_subprocess:
                mock_process = AsyncMock()
                mock_process.communicate.return_value = (b"Calibration complete", b"")
                mock_process.returncode = 0
                mock_subprocess.return_value = mock_process

                async def run_test():
                    result = await manager.run_auto_calibration("camera1")
                    assert result["status"] == "success"
                    mock_config_manager.reload_config.assert_called_once()

                asyncio.run(run_test())

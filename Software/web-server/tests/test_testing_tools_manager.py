import pytest
import asyncio
import os
import time
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, Mock, patch, mock_open
from testing_tools_manager import TestingToolsManager


@pytest.fixture
def mock_config_manager():
    """Create a mock config manager"""
    manager = MagicMock()
    manager.get_config.return_value = {
        "system": {"mode": "single"},
        "gs_config": {
            "ipc_interface": {"kWebServerShareDirectory": "~/LM_Shares/Images/"}
        },
    }
    manager.generate_golf_sim_config.return_value = "/tmp/test_config.json"
    manager.get_environment_parameters.return_value = [
        {"key": "camera1.slot1_camera_type", "envVariable": "PITRAC_SLOT1_CAMERA_TYPE"}
    ]
    return manager


@pytest.fixture
def testing_manager(mock_config_manager, tmp_path):
    """Create TestingToolsManager instance for testing"""
    with patch("testing_tools_manager.Path.home", return_value=tmp_path):
        manager = TestingToolsManager(mock_config_manager)
        return manager


@pytest.mark.unit
class TestTestingToolsManagerInit:
    """Test TestingToolsManager initialization"""

    def test_initialization(self, testing_manager):
        """Test that manager initializes correctly"""
        assert testing_manager.pitrac_binary == "/usr/lib/pitrac/pitrac_lm"
        assert isinstance(testing_manager.running_processes, dict)
        assert isinstance(testing_manager.completed_results, dict)
        assert len(testing_manager.running_processes) == 0
        assert len(testing_manager.completed_results) == 0

    def test_test_images_directory_created(self, testing_manager, tmp_path):
        """Test that test images directory is created"""
        test_images_dir = tmp_path / "LM_Shares/TestImages"
        assert test_images_dir.exists()

    def test_tools_defined(self, testing_manager):
        """Test that all tools are properly defined"""
        assert "test_uploaded_image" in testing_manager.tools
        assert "pulse_test" in testing_manager.tools
        assert "camera1_still" in testing_manager.tools
        assert "camera2_still" in testing_manager.tools
        assert "test_images" in testing_manager.tools
        assert "automated_testing" in testing_manager.tools

    def test_tool_structure(self, testing_manager):
        """Test that each tool has required fields"""
        for tool_id, tool_info in testing_manager.tools.items():
            assert "name" in tool_info
            assert "description" in tool_info
            assert "category" in tool_info
            assert "args" in tool_info
            assert "requires_sudo" in tool_info
            assert "timeout" in tool_info


@pytest.mark.unit
class TestGetAvailableTools:
    """Test get_available_tools method"""

    def test_get_available_tools_structure(self, testing_manager):
        """Test that available tools are organized by category"""
        tools = testing_manager.get_available_tools()
        assert isinstance(tools, dict)
        assert "testing" in tools
        assert "hardware" in tools
        assert "camera" in tools
        assert "calibration" in tools

    def test_tool_format(self, testing_manager):
        """Test that each tool has correct format"""
        tools = testing_manager.get_available_tools()
        for category, tool_list in tools.items():
            assert isinstance(tool_list, list)
            for tool in tool_list:
                assert "id" in tool
                assert "name" in tool
                assert "description" in tool
                assert "requires_sudo" in tool


@pytest.mark.unit
class TestRunTool:
    """Test run_tool method"""

    @pytest.mark.asyncio
    async def test_run_unknown_tool(self, testing_manager):
        """Test running an unknown tool returns error"""
        result = await testing_manager.run_tool("unknown_tool")
        assert result["status"] == "error"
        assert "Unknown tool" in result["message"]

    @pytest.mark.asyncio
    async def test_run_tool_already_running(self, testing_manager):
        """Test running a tool that's already running"""
        mock_process = MagicMock()
        testing_manager.running_processes["pulse_test"] = mock_process

        result = await testing_manager.run_tool("pulse_test")
        assert result["status"] == "error"
        assert "already running" in result["message"]

    @pytest.mark.asyncio
    async def test_run_tool_success(self, testing_manager, mock_config_manager):
        """Test successfully running a tool"""
        mock_process = AsyncMock()
        mock_process.returncode = 0
        mock_process.communicate.return_value = (b"Test output", b"")

        with patch("asyncio.create_subprocess_exec", return_value=mock_process):
            result = await testing_manager.run_tool("pulse_test")

        assert result["status"] == "success"
        assert "output" in result
        assert result["return_code"] == 0
        assert "timestamp" in result

    @pytest.mark.asyncio
    async def test_run_tool_failure(self, testing_manager, mock_config_manager):
        """Test running a tool that fails"""
        mock_process = AsyncMock()
        mock_process.returncode = 1
        mock_process.communicate.return_value = (b"", b"Error occurred")

        with patch("asyncio.create_subprocess_exec", return_value=mock_process):
            result = await testing_manager.run_tool("camera1_still")

        assert result["status"] == "failed"
        assert result["return_code"] == 1
        assert "error" in result

    @pytest.mark.asyncio
    async def test_run_tool_timeout(self, testing_manager, mock_config_manager):
        """Test tool timeout handling"""
        mock_process = AsyncMock()
        mock_process.communicate.side_effect = asyncio.TimeoutError()
        mock_process.terminate = AsyncMock()
        mock_process.wait = AsyncMock()

        with patch("asyncio.create_subprocess_exec", return_value=mock_process):
            result = await testing_manager.run_tool("camera1_still")

        assert result["status"] == "timeout"
        assert "timed out" in result["message"]
        mock_process.terminate.assert_called_once()

    @pytest.mark.asyncio
    async def test_run_continuous_test_timeout(self, testing_manager, mock_config_manager):
        """Test continuous test timeout behavior"""
        mock_process = AsyncMock()
        mock_process.communicate.side_effect = asyncio.TimeoutError()
        mock_process.terminate = AsyncMock()
        mock_process.wait = AsyncMock()

        with patch("asyncio.create_subprocess_exec", return_value=mock_process):
            with patch.object(testing_manager, "_find_and_read_test_log", return_value="Test log content"):
                result = await testing_manager.run_tool("pulse_test")

        assert result["status"] == "success"
        assert "Test log content" in result["output"]

    @pytest.mark.asyncio
    async def test_run_tool_with_sudo(self, testing_manager, mock_config_manager):
        """Test running a tool that requires sudo"""
        mock_process = AsyncMock()
        mock_process.returncode = 0
        mock_process.communicate.return_value = (b"Output", b"")

        with patch("asyncio.create_subprocess_exec", return_value=mock_process) as mock_exec:
            await testing_manager.run_tool("test_images")

            # Verify sudo was added to command
            args, kwargs = mock_exec.call_args
            assert args[0] == "sudo"
            assert "-E" in args

    @pytest.mark.asyncio
    async def test_run_tool_exception(self, testing_manager, mock_config_manager):
        """Test exception handling during tool run"""
        mock_config_manager.generate_golf_sim_config.side_effect = Exception("Config error")

        result = await testing_manager.run_tool("camera1_still")
        assert result["status"] == "error"
        assert "Config error" in result["message"]

    @pytest.mark.asyncio
    async def test_run_tool_with_test_image(self, testing_manager, mock_config_manager, tmp_path):
        """Test running tool with uploaded test image"""
        # Create test image
        test_image = testing_manager.test_images_dir / "test_flight.jpg"
        test_image.touch()

        # Mock config file operations
        config_content = '{"gs_config": {}}'

        mock_process = AsyncMock()
        mock_process.returncode = 0
        mock_process.communicate.return_value = (b"Test completed", b"")

        with patch("asyncio.create_subprocess_exec", return_value=mock_process):
            with patch("builtins.open", mock_open(read_data=config_content)):
                result = await testing_manager.run_tool("test_uploaded_image")

        assert result["status"] == "success"

    @pytest.mark.asyncio
    async def test_run_tool_no_test_image(self, testing_manager):
        """Test running test_uploaded_image with no images"""
        result = await testing_manager.run_tool("test_uploaded_image")
        assert result["status"] == "error"
        assert "No test images found" in result["message"]

    @pytest.mark.asyncio
    async def test_run_still_capture_basic(self, testing_manager):
        """Test still image capture runs successfully"""
        mock_process = AsyncMock()
        mock_process.returncode = 0
        mock_process.communicate.return_value = (b"Image captured", b"")

        with patch("asyncio.create_subprocess_exec", return_value=mock_process):
            result = await testing_manager.run_tool("camera1_still")

        assert result["status"] == "success"
        assert result["return_code"] == 0
        # Image path will only be added if the file actually exists on disk
        # which we're not testing here to avoid filesystem dependencies


@pytest.mark.unit
class TestStopTool:
    """Test stop_tool method"""

    @pytest.mark.asyncio
    async def test_stop_tool_not_running(self, testing_manager):
        """Test stopping a tool that is not running"""
        result = await testing_manager.stop_tool("pulse_test")
        assert result["status"] == "error"
        assert "not running" in result["message"]

    @pytest.mark.asyncio
    async def test_stop_tool_success(self, testing_manager):
        """Test successfully stopping a running tool"""
        mock_process = AsyncMock()
        mock_process.terminate = Mock()
        mock_process.wait = AsyncMock()
        testing_manager.running_processes["pulse_test"] = mock_process

        result = await testing_manager.stop_tool("pulse_test")

        assert result["status"] == "success"
        assert "stopped" in result["message"]
        mock_process.terminate.assert_called_once()
        assert "pulse_test" not in testing_manager.running_processes

    @pytest.mark.asyncio
    async def test_stop_tool_kill_on_timeout(self, testing_manager):
        """Test killing tool if terminate times out"""
        mock_process = AsyncMock()
        mock_process.terminate = Mock()
        mock_process.wait = AsyncMock(side_effect=asyncio.TimeoutError())
        mock_process.kill = Mock()

        # After kill, wait should succeed
        async def wait_side_effect():
            if mock_process.kill.called:
                return
            raise asyncio.TimeoutError()

        mock_process.wait = AsyncMock(side_effect=wait_side_effect)
        testing_manager.running_processes["pulse_test"] = mock_process

        result = await testing_manager.stop_tool("pulse_test")

        assert result["status"] == "success"
        mock_process.terminate.assert_called_once()
        mock_process.kill.assert_called_once()

    @pytest.mark.asyncio
    async def test_stop_tool_exception(self, testing_manager):
        """Test exception handling when stopping tool"""
        mock_process = AsyncMock()
        # Make terminate raise an exception
        mock_process.terminate = Mock(side_effect=Exception("Terminate failed"))
        testing_manager.running_processes["pulse_test"] = mock_process

        result = await testing_manager.stop_tool("pulse_test")

        assert result["status"] == "error"
        assert "Terminate failed" in result["message"]


@pytest.mark.unit
class TestFindAndReadTestLog:
    """Test _find_and_read_test_log method"""

    @pytest.mark.asyncio
    async def test_find_log_no_directory(self, testing_manager, tmp_path):
        """Test when log directory doesn't exist"""
        with patch("testing_tools_manager.Path.home", return_value=tmp_path):
            result = await testing_manager._find_and_read_test_log(time.time())
        assert result is None

    @pytest.mark.asyncio
    async def test_find_log_no_files(self, testing_manager, tmp_path):
        """Test when no log files exist"""
        log_dir = tmp_path / ".pitrac" / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)

        with patch("testing_tools_manager.Path.home", return_value=tmp_path):
            result = await testing_manager._find_and_read_test_log(time.time())
        assert result is None

    @pytest.mark.asyncio
    async def test_find_log_file_found(self, testing_manager, tmp_path):
        """Test finding and reading log file"""
        log_dir = tmp_path / ".pitrac" / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        log_file = log_dir / "test_12345.log"
        log_file.write_text("Test log content\nLine 2\nLine 3")

        start_time = time.time() - 10  # 10 seconds ago

        with patch("testing_tools_manager.Path.home", return_value=tmp_path):
            result = await testing_manager._find_and_read_test_log(start_time)

        assert result is not None
        assert "Test log content" in result
        assert "Line 2" in result

    @pytest.mark.asyncio
    async def test_find_log_file_truncated(self, testing_manager, tmp_path):
        """Test that large log files are truncated"""
        log_dir = tmp_path / ".pitrac" / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        log_file = log_dir / "test_12345.log"

        # Create log with > 1000 lines
        log_content = "\n".join([f"Line {i}" for i in range(1500)])
        log_file.write_text(log_content)

        start_time = time.time() - 10

        with patch("testing_tools_manager.Path.home", return_value=tmp_path):
            result = await testing_manager._find_and_read_test_log(start_time)

        assert result is not None
        assert "... (truncated) ..." in result
        # Should contain the last 1000 lines
        assert "Line 1499" in result
        assert "Line 500" in result

    @pytest.mark.asyncio
    async def test_find_log_multiple_files(self, testing_manager, tmp_path):
        """Test finding latest log file among multiple"""
        log_dir = tmp_path / ".pitrac" / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)

        # Create older log
        old_log = log_dir / "test_old.log"
        old_log.write_text("Old log")
        os.utime(old_log, (time.time() - 100, time.time() - 100))

        # Create newer log
        new_log = log_dir / "test_new.log"
        new_log.write_text("New log content")

        start_time = time.time() - 200

        with patch("testing_tools_manager.Path.home", return_value=tmp_path):
            result = await testing_manager._find_and_read_test_log(start_time)

        assert result is not None
        assert "New log content" in result
        assert "Old log" not in result

    @pytest.mark.asyncio
    async def test_find_log_exception(self, testing_manager, tmp_path):
        """Test exception handling in log reading"""
        log_dir = tmp_path / ".pitrac" / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)

        with patch("testing_tools_manager.Path.home", return_value=tmp_path):
            with patch("glob.glob", side_effect=Exception("Glob failed")):
                result = await testing_manager._find_and_read_test_log(time.time())

        assert result is None


@pytest.mark.unit
class TestExtractTimingSummary:
    """Test _extract_timing_summary method"""

    def test_extract_timing_no_data(self, testing_manager):
        """Test with no timing data"""
        log_lines = ["Regular log line", "Another line"]
        result = testing_manager._extract_timing_summary(log_lines)
        assert result is None

    def test_extract_timing_grayscale(self, testing_manager):
        """Test extracting grayscale timing"""
        log_lines = ["Grayscale conversion completed in 150us"]
        result = testing_manager._extract_timing_summary(log_lines)
        assert result is not None
        assert "Grayscale Conversion" in result
        assert "150" in result

    def test_extract_timing_onnx_preload(self, testing_manager):
        """Test extracting ONNX preload timing"""
        log_lines = ["ONNX Runtime detector preloaded successfully in 450ms"]
        result = testing_manager._extract_timing_summary(log_lines)
        assert result is not None
        assert "ONNX Runtime Preload" in result
        assert "450ms" in result

    def test_extract_timing_onnx_warmup(self, testing_manager):
        """Test extracting ONNX warmup timing"""
        # Need preload timing as well for output to be generated
        log_lines = [
            "ONNX Runtime detector preloaded successfully in 450ms",
            "Warmup complete. Final inference time: 23.5 ms",
        ]
        result = testing_manager._extract_timing_summary(log_lines)
        assert result is not None
        assert "Warmup Inference" in result or "Final Warmup Inference" in result
        assert "23.5" in result or "23.50" in result

    def test_extract_timing_onnx_detection(self, testing_manager):
        """Test extracting ONNX detection timing"""
        log_lines = [
            "ONNX Runtime detected 1 balls in 25ms",
            "ONNX Runtime detected 2 balls in 30ms",
        ]
        result = testing_manager._extract_timing_summary(log_lines)
        assert result is not None
        assert "Ball Detection (ONNX Runtime)" in result
        assert "Average" in result

    def test_extract_timing_opencv_fallback(self, testing_manager):
        """Test extracting OpenCV fallback timing"""
        log_lines = ["OpenCV DNN completed processing in 45 ms"]
        result = testing_manager._extract_timing_summary(log_lines)
        assert result is not None
        assert "OpenCV DNN Fallback" in result
        assert "45" in result

    def test_extract_timing_getball(self, testing_manager):
        """Test extracting GetBall timing"""
        log_lines = ["GetBall (ball detection) completed in 12ms"]
        result = testing_manager._extract_timing_summary(log_lines)
        assert result is not None
        assert "GetBall" in result
        assert "12" in result

    def test_extract_timing_spin_detection(self, testing_manager):
        """Test extracting spin detection timing"""
        log_lines = ["Spin detection completed in 35ms"]
        result = testing_manager._extract_timing_summary(log_lines)
        assert result is not None
        assert "Spin Analysis" in result
        assert "35" in result

    def test_extract_timing_complete_pipeline(self, testing_manager):
        """Test extracting complete pipeline timing"""
        log_lines = [
            "ONNX Runtime detector preloaded successfully in 450ms",
            "Warmup complete. Final inference time: 23.5 ms",
            "Grayscale conversion completed in 150us",
            "ONNX Runtime detected 1 balls in 25ms",
            "ONNX Runtime detected 2 balls in 30ms",
            "Spin detection completed in 35ms",
            "Spin detection completed in 40ms",
        ]
        result = testing_manager._extract_timing_summary(log_lines)
        assert result is not None
        assert "Initialization" in result
        assert "Image Preprocessing" in result
        assert "Ball Detection (ONNX Runtime)" in result
        assert "Spin Analysis" in result
        assert "Total Per-Shot Time" in result

    def test_extract_timing_multiple_detections_range(self, testing_manager):
        """Test that range is shown for multiple detections"""
        log_lines = [
            "ONNX Runtime detected 1 balls in 20ms",
            "ONNX Runtime detected 1 balls in 25ms",
            "ONNX Runtime detected 1 balls in 30ms",
        ]
        result = testing_manager._extract_timing_summary(log_lines)
        assert result is not None
        assert "Range" in result
        assert "20ms" in result
        assert "30ms" in result


@pytest.mark.unit
class TestGetRunningTools:
    """Test get_running_tools method"""

    def test_get_running_tools_empty(self, testing_manager):
        """Test with no running tools"""
        result = testing_manager.get_running_tools()
        assert isinstance(result, list)
        assert len(result) == 0

    def test_get_running_tools_with_processes(self, testing_manager):
        """Test with running tools"""
        testing_manager.running_processes = {
            "pulse_test": MagicMock(),
            "camera1_still": MagicMock(),
        }
        result = testing_manager.get_running_tools()
        assert len(result) == 2
        assert "pulse_test" in result
        assert "camera1_still" in result

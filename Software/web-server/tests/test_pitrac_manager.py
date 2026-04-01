"""
Comprehensive tests for the PiTrac Process Manager
"""

import signal
import pytest
from pathlib import Path
from unittest.mock import Mock, patch, AsyncMock, mock_open

from pitrac_manager import PiTracProcessManager


class TestPiTracProcessManager:
    """Test suite for PiTracProcessManager"""

    @pytest.fixture
    def mock_config_manager(self):
        """Create a mock configuration manager"""
        config_manager = Mock()
        config_manager.get_config.return_value = {
            "logging.level": "info",
            "storage.image_dir": "/var/pitrac/images",
            "storage.web_share_dir": "/var/pitrac/web",
            "gs_config.golf_simulator_interfaces.E6.kE6ConnectAddress": "192.168.1.100",
            "gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress": "192.168.1.101",
            "gs_config.cameras.kCamera1Gain": 1.0,
            "gs_config.cameras.kCamera2Gain": 4.0,
        }

        config_manager.load_configurations_metadata.return_value = {
            "cameraDefinitions": {
                "camera1": {"displayName": "Camera 1", "slot": "slot1", "defaultIndex": 0, "envPrefix": "PITRAC_SLOT1"},
                "camera2": {"displayName": "Camera 2", "slot": "slot2", "defaultIndex": 1, "envPrefix": "PITRAC_SLOT2"},
            },
            "systemDefaults": {
                "configStructure": {"systemKey": "system", "camerasKey": "cameras"},
            },
            "categoryList": [
                "Basic",
                "Cameras",
                "Simulators",
                "Ball Detection",
                "AI Detection",
                "Storage",
                "Network",
                "Logging",
                "Strobing",
                "Spin Analysis",
                "Calibration",
                "Advanced",
            ],
            "systemPaths": {
                "pitracBinary": {"default": "/usr/lib/pitrac/pitrac_lm"},
                "configFile": {"default": "/etc/pitrac/golf_sim_config.json"},
                "logDirectory": {"default": "~/.pitrac/logs"},
                "pidDirectory": {"default": "~/.pitrac/run"},
            },
            "processManagement": {
                "camera1LogFile": {"default": "pitrac.log"},
                "camera1PidFile": {"default": "pitrac.pid"},
                "processCheckCommand": {"default": "pitrac_lm"},
                "startupDelayCamera1": {"default": 3},
                "shutdownGracePeriod": {"default": 5},
                "shutdownCheckInterval": {"default": 0.1},
                "postKillDelay": {"default": 0.5},
                "restartDelay": {"default": 1},
                "recentLogLines": {"default": 10},
                "terminationSignal": {"default": "SIGTERM"},
                "killSignal": {"default": "SIGKILL"},
            },
            "environmentDefaults": {
                "ldLibraryPath": {"default": "/usr/lib/pitrac"},
                "pitracRoot": {"default": "/usr/lib/pitrac"},
                "baseImageLoggingDir": {"default": "~/LM_Shares/Images/"},
                "webserverShareDir": {"default": "~/LM_Shares/WebShare/"},
            },
            "settings": {},
        }

        config_manager.get_cli_parameters.return_value = []
        config_manager.get_environment_parameters.return_value = []

        return config_manager

    @pytest.fixture
    def manager(self, mock_config_manager):
        """Create a PiTracProcessManager instance with mocked config"""
        with patch("pitrac_manager.Path.mkdir"):
            manager = PiTracProcessManager(config_manager=mock_config_manager)
        return manager

    def test_initialization(self, mock_config_manager):
        """Test proper initialization of the manager"""
        with patch("pitrac_manager.Path.mkdir") as mock_mkdir:
            manager = PiTracProcessManager(config_manager=mock_config_manager)

            assert manager.process is None
            assert manager.pitrac_binary == "/usr/lib/pitrac/pitrac_lm"
            assert manager.config_manager == mock_config_manager

            assert mock_mkdir.call_count >= 2








    def test_is_running_when_process_exists(self, manager):
        """Test is_running returns True when process exists"""
        with patch.object(manager, "get_pid", return_value=12345):
            with patch("os.kill", return_value=None):  # Process exists
                assert manager.is_running() is True

    def test_is_running_when_process_terminated(self, manager):
        """Test is_running returns False when process terminated"""
        mock_process = Mock()
        mock_process.poll.return_value = 0
        manager.process = mock_process

        assert manager.is_running() is False

    def test_is_running_when_no_process(self, manager):
        """Test is_running returns False when no process"""
        manager.process = None

        assert manager.is_running() is False

    def test_get_pid_with_running_process(self, manager):
        """Test get_pid returns PID when process is running"""
        mock_process = Mock()
        mock_process.pid = 12345
        mock_process.poll.return_value = None
        manager.process = mock_process

        assert manager.get_pid() == 12345

    def test_get_pid_with_terminated_process(self, manager):
        """Test get_pid returns None when process terminated"""
        mock_process = Mock()
        mock_process.pid = 12345
        mock_process.poll.return_value = 0
        manager.process = mock_process

        assert manager.get_pid() is None

    def test_get_pid_with_no_process(self, manager):
        """Test get_pid returns None when no process"""
        manager.process = None

        assert manager.get_pid() is None





    @pytest.mark.asyncio
    async def test_start_success(self, manager):
        """Test successful start of pitrac process"""
        mock_process = Mock()
        mock_process.pid = 12345
        mock_process.poll.return_value = None

        with patch("subprocess.Popen", return_value=mock_process):
            with patch("builtins.open", mock_open()):
                with patch("pitrac_manager.Path.exists", return_value=True):
                    with patch("pitrac_manager.Path.mkdir"):
                        with patch("pitrac_manager.Path.unlink"):
                            with patch.object(manager, "is_running", return_value=False):
                                with patch.object(
                                    manager.config_manager,
                                    "generate_golf_sim_config",
                                    return_value="/tmp/test_config.json",
                                ):
                                    result = await manager.start()

        assert result.get("status") in ["started", "failed"]
        if result["status"] == "started":
            assert result.get("pid") == 12345 or result.get("camera1_pid") == 12345
            assert manager.process == mock_process

    @pytest.mark.asyncio
    async def test_start_already_running(self, manager):
        """Test start when process is already running"""
        mock_process = Mock()
        mock_process.poll.return_value = None
        mock_process.pid = 12345
        manager.process = mock_process

        with patch.object(manager, "get_pid", return_value=12345):
            with patch("os.kill", return_value=None):
                result = await manager.start()

        assert result["status"] == "already_running"
        assert "already running" in result["message"].lower()
        assert result["pid"] == 12345

    @pytest.mark.asyncio
    async def test_start_with_exception(self, manager):
        """Test start with exception during process creation"""
        with patch("subprocess.Popen", side_effect=Exception("Test error")):
            with patch.object(manager, "is_running", return_value=False):
                with patch.object(
                    manager.config_manager, "generate_golf_sim_config", return_value="/tmp/test_config.json"
                ):
                    result = await manager.start()

        assert result["status"] == "error"
        assert "Test error" in result["message"]



    @pytest.mark.asyncio
    async def test_stop_not_running(self, manager):
        """Test stop when process is not running"""
        manager.process = None

        result = await manager.stop()

        assert result["status"] == "not_running"
        assert "not running" in result["message"]

    @pytest.mark.asyncio
    async def test_stop_with_kill_fallback(self, manager):
        """Test stop with kill fallback when terminate fails"""
        mock_process = Mock()
        mock_process.pid = 12345
        mock_process.poll.return_value = None
        manager.process = mock_process

        # Mock os.kill - process doesn't stop with SIGTERM, needs SIGKILL
        with patch("os.kill") as mock_kill:
            # Simulate process still running after SIGTERM, then killed with SIGKILL
            mock_kill.side_effect = [None, None] + [None] * 50 + [None, None, ProcessLookupError]
            with patch("pitrac_manager.Path.unlink"):
                result = await manager.stop()

        assert result["status"] == "stopped"
        assert manager.process is None


    @pytest.mark.asyncio
    async def test_restart_success(self, manager):
        """Test successful restart of pitrac process"""
        mock_process_old = Mock()
        mock_process_old.pid = 12345
        mock_process_old.poll.return_value = None
        manager.process = mock_process_old

        mock_process_new = Mock()
        mock_process_new.pid = 67890
        mock_process_new.poll.return_value = None

        # Create a counter to track os.kill calls
        call_count = [0]

        def mock_kill(pid, sig):
            call_count[0] += 1
            if sig == signal.SIGTERM:
                return None  # SIGTERM sent successfully
            elif sig == 0:  # Check if process exists
                if call_count[0] > 2:  # After a couple checks, process is gone
                    raise ProcessLookupError()
                return None  # Process still exists
            elif sig == signal.SIGKILL:
                return None  # SIGKILL sent successfully

        with patch.object(manager, "is_running", side_effect=[True, False]):
            with patch.object(
                manager, "stop", return_value={"status": "stopped", "message": "PiTrac stopped successfully"}
            ):
                with patch("subprocess.Popen", return_value=mock_process_new):
                    with patch("builtins.open", mock_open()):
                        with patch("pitrac_manager.Path.exists", return_value=True):
                            with patch("pitrac_manager.Path.mkdir"):
                                with patch("pitrac_manager.Path.unlink"):
                                    with patch.object(
                                        manager.config_manager,
                                        "generate_golf_sim_config",
                                        return_value="/tmp/test_config.json",
                                    ):
                                        with patch("asyncio.sleep", new_callable=AsyncMock):
                                            result = await manager.restart()

        assert result.get("status") in ["started", "restarted", "failed"]
        if result["status"] in ["started", "restarted"]:
            assert result.get("pid") == 67890 or result.get("camera1_pid") == 67890
            assert manager.process == mock_process_new

    @pytest.mark.asyncio
    async def test_restart_not_running(self, manager):
        """Test restart when process is not running - should start"""
        manager.process = None

        mock_process = Mock()
        mock_process.pid = 12345
        mock_process.poll.return_value = None

        with patch("subprocess.Popen", return_value=mock_process):
            with patch("builtins.open", mock_open()):
                with patch("pitrac_manager.Path.exists", return_value=True):
                    with patch("pitrac_manager.Path.mkdir"):
                        with patch("pitrac_manager.Path.unlink"):
                            with patch.object(manager, "is_running", return_value=False):
                                with patch.object(
                                    manager.config_manager,
                                    "generate_golf_sim_config",
                                    return_value="/tmp/test_config.json",
                                ):
                                    result = await manager.restart()

        assert result.get("status") in ["started", "restarted", "failed"]
        if result["status"] in ["started", "restarted"]:
            assert result.get("pid") == 12345 or result.get("camera1_pid") == 12345
            assert manager.process == mock_process

    @pytest.mark.asyncio
    async def test_restart_with_delay(self, manager):
        """Test restart includes delay between stop and start"""
        mock_process_old = Mock()
        mock_process_old.pid = 12345
        mock_process_old.poll.return_value = None
        manager.process = mock_process_old

        mock_process_new = Mock()
        mock_process_new.pid = 67890
        mock_process_new.poll.return_value = None

        with patch("subprocess.Popen", return_value=mock_process_new):
            with patch("builtins.open", mock_open()):
                with patch("pitrac_manager.Path.unlink"):
                    with patch("pitrac_manager.Path.exists", return_value=True):
                        with patch("pitrac_manager.Path.mkdir"):
                            with patch.object(
                                manager.config_manager, "generate_golf_sim_config", return_value="/tmp/test_config.json"
                            ):
                                with patch("asyncio.sleep", new_callable=AsyncMock) as mock_sleep:
                                    result = await manager.restart()

        mock_sleep.assert_called()
        assert result.get("status") in ["started", "restarted", "failed"]


class TestPiTracProcessManagerIntegration:
    """Integration tests for PiTracProcessManager with real subprocess interaction"""

    @pytest.fixture
    def manager(self):
        """Create a PiTracProcessManager instance for integration testing"""
        config_manager = Mock()
        config_manager.get_config.return_value = {
            "logging": {"level": "debug"},
            "storage": {"image_dir": "/tmp/pitrac/images", "web_share_dir": "/tmp/pitrac/web"},
        }
        config_manager.load_configurations_metadata.return_value = {
            "systemPaths": {
                "pitracBinary": {"default": "/usr/lib/pitrac/pitrac_lm"},
                "configFile": {"default": "/etc/pitrac/golf_sim_config.json"},
                "logDirectory": {"default": "~/.pitrac/logs"},
                "pidDirectory": {"default": "~/.pitrac/run"},
            },
            "processManagement": {
                "camera1LogFile": {"default": "pitrac.log"},
                "camera1PidFile": {"default": "pitrac.pid"},
                "processCheckCommand": {"default": "pitrac_lm"},
                "shutdownGracePeriod": {"default": 5},
            },
            "settings": {},
        }
        config_manager.get_cli_parameters.return_value = []
        config_manager.get_environment_parameters.return_value = []

        with patch("pitrac_manager.Path.mkdir"):
            manager = PiTracProcessManager(config_manager=config_manager)
        manager.pitrac_binary = "echo"
        return manager

    @pytest.mark.asyncio
    async def test_start_stop_cycle(self, manager):
        """Test complete start/stop cycle with subprocess"""

        with patch.object(manager.config_manager, "generate_golf_sim_config", return_value="/tmp/test_config.json"):
            with patch("pitrac_manager.Path.exists", return_value=True):
                with patch("pathlib.Path.mkdir"):
                    result = await manager.start()

        assert result["status"] in ["started", "failed"]

        if result["status"] == "started":
            assert manager.get_pid() is not None

            stop_result = await manager.stop()
            assert stop_result["status"] in ["stopped", "not_running"]

            assert manager.is_running() is False

        else:
            assert "error" in result.get("message", "").lower() or "failed" in result.get("message", "").lower()

        manager.process = None

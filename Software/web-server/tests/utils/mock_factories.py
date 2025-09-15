"""
Mock factories for creating consistent test mocks across test modules.

This module provides factory functions to create commonly used mock objects
with realistic default values and behaviors.
"""

from unittest.mock import Mock, AsyncMock


class MockConfigManagerFactory:
    """Factory for creating ConfigManager mock instances."""

    @staticmethod
    def create_basic_config_manager() -> Mock:
        """Create a basic configuration manager mock with common methods."""
        manager = Mock()

        manager.get_config.return_value = {
            "system": {"mode": "single", "camera_role": "camera1"},
            "cameras": {"camera1_gain": 2.0, "camera2_gain": 4.0},
            "simulators": {"gspro_host": "192.168.1.100", "gspro_port": 921},
            "logging": {"level": "info"},
        }

        manager.get_default.return_value = 1.0
        manager.get_user_settings.return_value = {"cameras": {"camera1_gain": 2.0}}
        manager.get_categories.return_value = {
            "Basic": {"basic": ["system.mode"], "advanced": []},
            "Cameras": {"basic": ["cameras.camera1_gain"], "advanced": ["cameras.camera2_gain"]},
            "Simulators": {"basic": ["simulators.gspro_host"], "advanced": ["simulators.gspro_port"]},
        }

        manager.set_config.return_value = (True, "Configuration updated", False)
        manager.reset_all.return_value = (True, "Configuration reset")
        manager.validate_config.return_value = (True, "")
        manager.reload.return_value = None

        return manager

    @staticmethod
    def create_pitrac_config_manager() -> Mock:
        """Create a configuration manager mock with PiTrac-specific metadata."""
        manager = Mock()

        manager.get_config.return_value = {
            "system.mode": "single",
            "system.camera_role": "camera1",
            "logging.level": "info",
            "gs_config.ipc_interface.kWebActiveMQHostAddress": "tcp://localhost:61616",
            "storage.image_dir": "/var/pitrac/images",
            "storage.web_share_dir": "/var/pitrac/web",
            "gs_config.golf_simulator_interfaces.E6.kE6ConnectAddress": "192.168.1.100",
            "gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress": "192.168.1.101",
            "gs_config.cameras.kCamera1Gain": 1.0,
            "gs_config.cameras.kCamera2Gain": 4.0,
        }

        manager.load_configurations_metadata.return_value = {
            "cameraDefinitions": {
                "camera1": {"displayName": "Camera 1", "slot": "slot1", "defaultIndex": 0, "envPrefix": "PITRAC_SLOT1"},
                "camera2": {"displayName": "Camera 2", "slot": "slot2", "defaultIndex": 1, "envPrefix": "PITRAC_SLOT2"},
            },
            "systemDefaults": {
                "mode": "single",
                "cameraRole": "camera1",
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
                "camera2LogFile": {"default": "pitrac_camera2.log"},
                "camera1PidFile": {"default": "pitrac.pid"},
                "camera2PidFile": {"default": "pitrac_camera2.pid"},
                "processCheckCommand": {"default": "pitrac_lm"},
                "startupDelayCamera2": {"default": 2},
                "startupWaitCamera2Ready": {"default": 1},
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
                "msgBrokerFullAddress": {"default": "tcp://localhost:61616"},
            },
            "settings": {},
        }

        manager.get_cli_parameters.return_value = []
        manager.get_environment_parameters.return_value = []

        return manager


class MockProcessManagerFactory:
    """Factory for creating PiTrac process manager mocks."""

    @staticmethod
    def create_process_manager() -> Mock:
        """Create a basic process manager mock."""
        manager = Mock()

        manager.is_running.return_value = False
        manager.get_status.return_value = {
            "camera1": {"running": False, "pid": None},
            "camera2": {"running": False, "pid": None},
        }
        manager.get_logs.return_value = {
            "camera1": ["Sample log line 1", "Sample log line 2"],
            "camera2": ["Camera 2 log line 1"],
        }

        manager.start = AsyncMock(return_value=True)
        manager.stop = AsyncMock(return_value=True)
        manager.restart = AsyncMock(return_value=True)

        return manager


class MockActiveMQFactory:
    """Factory for creating ActiveMQ connection mocks."""

    @staticmethod
    def create_connection() -> Mock:
        """Create a basic ActiveMQ connection mock."""
        connection = Mock()

        connection.is_connected.return_value = True
        connection.connect.return_value = None
        connection.disconnect.return_value = None
        connection.send.return_value = None

        return connection


class MockWebSocketFactory:
    """Factory for creating WebSocket mocks."""

    @staticmethod
    def create_websocket() -> AsyncMock:
        """Create a basic WebSocket mock."""
        ws = AsyncMock()

        ws.accept = AsyncMock()
        ws.send_json = AsyncMock()
        ws.send_text = AsyncMock()
        ws.receive_json = AsyncMock(return_value={"service": "test"})
        ws.receive_text = AsyncMock(return_value="test message")
        ws.close = AsyncMock()

        return ws

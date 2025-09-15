import pytest
import sys
from pathlib import Path
from unittest.mock import patch


@pytest.mark.unit
class TestSmoke:
    """Basic smoke tests to verify the system is working"""

    def test_imports(self):
        """Test that all required modules can be imported"""

    def test_main_module_imports(self):
        """Test that main module can be imported"""
        parent = Path(__file__).parent.parent
        if str(parent) not in sys.path:
            sys.path.insert(0, str(parent))

        import main
        import server

        assert hasattr(main, "app")
        assert hasattr(server, "PiTracServer")
        assert hasattr(server, "app")

    def test_new_module_imports(self):
        """Test that new modular structure imports work"""
        parent = Path(__file__).parent.parent
        if str(parent) not in sys.path:
            sys.path.insert(0, str(parent))

        import models
        import managers
        import parsers
        import listeners
        import constants
        import config_manager
        import pitrac_manager
        import camera_detector
        import calibration_manager
        import testing_tools_manager

        assert hasattr(models, "ShotData")
        assert hasattr(models, "ResultType")
        assert hasattr(managers, "ConnectionManager")
        assert hasattr(managers, "ShotDataStore")
        assert hasattr(parsers, "ShotDataParser")
        assert hasattr(listeners, "ActiveMQListener")
        assert hasattr(constants, "MPS_TO_MPH")
        assert hasattr(config_manager, "ConfigurationManager")
        assert hasattr(pitrac_manager, "PiTracProcessManager")
        assert hasattr(camera_detector, "CameraDetector")
        assert hasattr(calibration_manager, "CalibrationManager")
        assert hasattr(testing_tools_manager, "TestingToolsManager")

    def test_fastapi_app_exists(self, app):
        """Test that FastAPI app is properly configured"""
        assert app is not None
        assert app.title == "PiTrac Dashboard"

        routes = [route.path for route in app.routes]
        # Core routes
        assert "/" in routes
        assert "/health" in routes
        assert "/ws" in routes

        # Shot data routes
        assert "/api/shot" in routes
        assert "/api/reset" in routes
        assert "/api/history" in routes
        assert "/api/stats" in routes

        # Config routes
        assert "/config" in routes
        assert "/api/config" in routes
        assert "/api/config/defaults" in routes
        assert "/api/config/user" in routes
        assert "/api/config/categories" in routes
        assert "/api/config/metadata" in routes
        assert "/api/config/diff" in routes
        assert "/api/config/{key:path}" in routes
        assert "/api/config/reset" in routes
        assert "/api/config/reload" in routes
        assert "/api/config/export" in routes
        assert "/api/config/import" in routes

        # PiTrac process management routes
        assert "/api/pitrac/start" in routes
        assert "/api/pitrac/stop" in routes
        assert "/api/pitrac/restart" in routes
        assert "/api/pitrac/status" in routes

        # Calibration routes
        assert "/calibration" in routes
        assert "/api/calibration/status" in routes
        assert "/api/calibration/data" in routes
        assert "/api/calibration/ball-location/{camera}" in routes
        assert "/api/calibration/auto/{camera}" in routes
        assert "/api/calibration/manual/{camera}" in routes
        assert "/api/calibration/capture/{camera}" in routes
        assert "/api/calibration/stop" in routes

        # Testing tools routes
        assert "/testing" in routes
        assert "/api/testing/tools" in routes
        assert "/api/testing/run/{tool_id}" in routes
        assert "/api/testing/stop/{tool_id}" in routes
        assert "/api/testing/status" in routes

        # Camera routes
        assert "/api/cameras/detect" in routes
        assert "/api/cameras/types" in routes

        # Logs routes
        assert "/logs" in routes
        assert "/ws/logs" in routes
        assert "/api/logs/services" in routes

    def test_templates_exist(self):
        """Test that template files exist"""
        template_dir = Path(__file__).parent.parent / "templates"
        assert template_dir.exists()

        dashboard = template_dir / "dashboard.html"
        assert dashboard.exists()

    def test_static_files_exist(self):
        """Test that static files exist"""
        static_dir = Path(__file__).parent.parent / "static"
        assert static_dir.exists()

        css_dir = static_dir / "css"
        assert css_dir.exists()

        js_dir = static_dir / "js"
        assert js_dir.exists()

        favicon = static_dir / "favicon.ico"
        assert favicon.exists()

    def test_initial_shot_state(self, server_instance):
        """Test initial shot state is correct"""
        shot = server_instance.shot_store.get()
        shot_dict = shot.to_dict()

        assert shot_dict is not None
        assert shot_dict["speed"] == 0.0
        assert shot_dict["carry"] == 0.0
        assert shot_dict["launch_angle"] == 0.0
        assert shot_dict["side_angle"] == 0.0
        assert shot_dict["back_spin"] == 0
        assert shot_dict["side_spin"] == 0
        assert shot_dict["result_type"] == "Waiting for ball..."

    def test_environment_detection(self):
        """Test that testing environment is detected"""
        import os

        assert os.environ.get("TESTING") == "true"

    def test_shot_data_model(self):
        """Test ShotData model functionality"""
        from models import ShotData

        shot = ShotData(speed=150.0, carry=250.0)
        assert shot.speed == 150.0
        assert shot.carry == 250.0

        shot_dict = shot.to_dict()
        assert isinstance(shot_dict, dict)
        assert shot_dict["speed"] == 150.0
        assert shot_dict["carry"] == 250.0

    def test_connection_manager(self, connection_manager):
        """Test ConnectionManager basic functionality"""
        assert connection_manager.connection_count == 0
        assert len(connection_manager.connections) == 0

    def test_shot_store(self, shot_store):
        """Test ShotDataStore basic functionality"""
        from models import ShotData

        initial = shot_store.get()
        assert initial.speed == 0.0

        new_shot = ShotData(speed=100.0)
        shot_store.update(new_shot)

        stored = shot_store.get()
        assert stored.speed == 100.0

        reset_shot = shot_store.reset()
        assert reset_shot.speed == 0.0

    def test_parser(self, parser):
        """Test ShotDataParser basic functionality"""
        from models import ShotData

        data = {"speed": 150.0, "carry": 250.0}
        current = ShotData()
        parsed = parser.parse_dict_format(data, current)

        assert parsed.speed == 150.0
        assert parsed.carry == 250.0
        assert parsed.timestamp is not None

    @patch("server.stomp.Connection")
    def test_activemq_connection_optional(self, mock_stomp, server_instance):
        """Test that app works without ActiveMQ connection"""
        mock_stomp.side_effect = Exception("Connection failed")

        assert server_instance is not None
        assert server_instance.app is not None
        assert server_instance.shot_store is not None
        assert server_instance.connection_manager is not None

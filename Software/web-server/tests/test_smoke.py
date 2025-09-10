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

        assert hasattr(models, "ShotData")
        assert hasattr(models, "ResultType")
        assert hasattr(managers, "ConnectionManager")
        assert hasattr(managers, "ShotDataStore")
        assert hasattr(parsers, "ShotDataParser")
        assert hasattr(listeners, "ActiveMQListener")
        assert hasattr(constants, "MPS_TO_MPH")

    def test_fastapi_app_exists(self, app):
        """Test that FastAPI app is properly configured"""
        assert app is not None
        assert app.title == "PiTrac Dashboard"

        routes = [route.path for route in app.routes]
        assert "/" in routes
        assert "/api/shot" in routes
        assert "/api/reset" in routes
        assert "/api/history" in routes
        assert "/api/stats" in routes
        assert "/health" in routes
        assert "/ws" in routes

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

        css_file = static_dir / "dashboard.css"
        js_file = static_dir / "dashboard.js"

        assert css_file.exists()
        assert js_file.exists()

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

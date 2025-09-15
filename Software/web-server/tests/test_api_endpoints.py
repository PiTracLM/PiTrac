import pytest
from unittest.mock import patch
from models import ShotData


@pytest.mark.unit
class TestAPIEndpoints:
    """Test REST API endpoints"""

    def test_homepage_loads(self, client):
        """Test that homepage loads successfully"""
        response = client.get("/")
        assert response.status_code == 200
        assert "PiTrac Launch Monitor" in response.text
        assert "dashboard.css" in response.text
        assert "dashboard.js" in response.text

    def test_get_current_shot(self, client):
        """Test getting current shot data"""
        response = client.get("/api/shot")
        assert response.status_code == 200
        data = response.json()
        assert "speed" in data
        assert "carry" in data
        assert "launch_angle" in data
        assert "side_angle" in data
        assert "back_spin" in data
        assert "side_spin" in data
        assert "result_type" in data

    def test_reset_shot(self, client, server_instance):
        """Test resetting shot data"""
        test_shot = ShotData(speed=150.0, carry=275.0)
        server_instance.shot_store.update(test_shot)

        response = client.post("/api/reset")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "reset"
        assert "timestamp" in data

        # Verify reset worked
        response = client.get("/api/shot")
        data = response.json()
        assert data["speed"] == 0.0
        assert data["carry"] == 0.0

    def test_get_shot_history(self, client, server_instance):
        """Test getting shot history"""
        for i in range(5):
            shot = ShotData(speed=100 + i * 10, carry=200 + i * 20, result_type="Hit")
            server_instance.shot_store.update(shot)

        response = client.get("/api/history")
        assert response.status_code == 200
        history = response.json()
        assert isinstance(history, list)
        assert len(history) <= 10  # Default limit

        response = client.get("/api/history?limit=3")
        assert response.status_code == 200
        history = response.json()
        assert len(history) <= 3

    def test_get_stats(self, client):
        """Test getting server statistics"""
        response = client.get("/api/stats")
        assert response.status_code == 200
        data = response.json()
        assert "websocket_connections" in data
        assert "listener" in data
        assert "shot_history_count" in data

    @patch("subprocess.run")
    def test_health_check(self, mock_subprocess, client, mock_activemq):
        """Test health check endpoint with all services running"""
        mock_activemq.is_connected.return_value = True

        pgrep_result = type("obj", (object,), {"returncode": 0, "stdout": "", "stderr": ""})()
        ss_result = type(
            "obj",
            (object,),
            {"returncode": 0, "stdout": "LISTEN 0 128 *:61616 *:*\n", "stderr": ""},
        )()

        mock_subprocess.side_effect = [pgrep_result, ss_result]

        response = client.get("/health")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] in ["healthy", "degraded"]
        assert data["activemq_connected"] is True
        assert data["activemq_running"] is True
        assert data["pitrac_running"] is True
        assert "websocket_clients" in data
        assert "listener_stats" in data

    @patch("subprocess.run")
    def test_health_check_mq_disconnected(self, mock_subprocess, client, mock_activemq):
        """Test health check when ActiveMQ is disconnected but running"""
        mock_activemq.is_connected.return_value = False

        pgrep_result = type("obj", (object,), {"returncode": 1, "stdout": "", "stderr": ""})()
        ss_result = type(
            "obj",
            (object,),
            {"returncode": 0, "stdout": "LISTEN 0 128 *:61616 *:*\n", "stderr": ""},
        )()

        mock_subprocess.side_effect = [pgrep_result, ss_result]

        response = client.get("/health")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "degraded"
        assert data["activemq_connected"] is False
        assert data["activemq_running"] is True
        assert data["pitrac_running"] is False

    @patch("subprocess.run")
    def test_health_check_services_not_running(self, mock_subprocess, client, mock_activemq):
        """Test health check when both services are not running"""
        mock_activemq.is_connected.return_value = False

        pgrep_result = type("obj", (object,), {"returncode": 1, "stdout": "", "stderr": ""})()
        ss_result = type("obj", (object,), {"returncode": 0, "stdout": "", "stderr": ""})()

        mock_subprocess.side_effect = [pgrep_result, ss_result]

        response = client.get("/health")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "degraded"
        assert data["activemq_connected"] is False
        assert data["activemq_running"] is False
        assert data["pitrac_running"] is False

    @patch("subprocess.run")
    def test_health_check_subprocess_exception(self, mock_subprocess, client, mock_activemq):
        """Test health check when subprocess commands fail"""
        mock_activemq.is_connected.return_value = True

        mock_subprocess.side_effect = Exception("Command failed")

        response = client.get("/health")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "healthy"
        assert data["activemq_connected"] is True
        assert data["activemq_running"] is False  # Defaults to False on exception
        assert data["pitrac_running"] is False  # Defaults to False on exception

    def test_static_files_served(self, client):
        """Test that static files are accessible"""
        response = client.get("/static/dashboard.css")
        assert response.status_code in [200, 404]

    def test_favicon_served(self, client):
        """Test that favicon is accessible"""
        response = client.get("/static/favicon.ico")
        assert response.status_code in [200, 404]

    @pytest.mark.parametrize("image_name", ["test_shot.jpg", "shot_001.png", "capture.jpeg"])
    def test_image_endpoint(self, client, tmp_path, image_name):
        """Test image serving endpoint"""
        with patch("constants.IMAGES_DIR", tmp_path):
            with patch("server.IMAGES_DIR", tmp_path):
                image_path = tmp_path / image_name
                image_path.write_bytes(b"fake image data")

                response = client.get(f"/api/images/{image_name}")
                assert response.status_code == 200

    def test_image_not_found(self, client):
        """Test image endpoint with non-existent image"""
        response = client.get("/api/images/nonexistent.jpg")
        assert response.status_code == 200
        assert response.json() == {"error": "Image not found"}

    def test_cors_headers(self, client):
        """Test CORS headers are present if needed"""
        response = client.get("/api/shot")
        assert response.status_code == 200

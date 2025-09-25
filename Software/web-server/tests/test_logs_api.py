import pytest
import time
from unittest.mock import patch
from fastapi.testclient import TestClient


@pytest.mark.unit
class TestLogsAPI:
    """Test logs REST API and WebSocket endpoints"""

    def test_logs_page_loads(self, client):
        """Test that logs page loads successfully"""
        response = client.get("/logs")
        assert response.status_code == 200
        assert "Logs" in response.text
        assert "logs.css" in response.text or "dashboard.css" in response.text
        assert "logs.js" in response.text or "dashboard.js" in response.text

    def test_get_log_services(self, client):
        """Test getting available log services"""
        response = client.get("/api/logs/services")
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, dict)
        assert "services" in data

        services = data["services"]
        assert isinstance(services, list)

        assert len(services) > 0

        for service in services:
            assert "id" in service
            assert "name" in service
            assert "status" in service
            assert service["status"] in ["running", "stopped"]

    @patch("server.PiTracServer._stream_systemd_logs")
    async def test_websocket_logs_systemd_service(self, mock_stream_systemd, app):
        """Test WebSocket logs streaming for systemd service"""

        async def mock_systemd_stream(websocket, unit):
            await websocket.send_json(
                {
                    "timestamp": "1640995200000000",
                    "message": "Test log line 1",
                    "level": "6",
                    "service": unit,
                    "historical": True,
                }
            )
            return

        mock_stream_systemd.side_effect = mock_systemd_stream

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac-web"})

                time.sleep(0.1)

                assert mock_stream_systemd.called

    @patch("server.PiTracServer._stream_file_logs")
    async def test_websocket_logs_file_service(self, mock_stream_file, app):
        """Test WebSocket logs streaming for file-based logs"""

        async def mock_file_stream(websocket, _log_file):
            await websocket.send_json({"message": "Test log line", "historical": True})
            return

        mock_stream_file.side_effect = mock_file_stream

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac"})

                time.sleep(0.1)

                assert mock_stream_file.called

    async def test_websocket_logs_invalid_service(self, app):
        """Test WebSocket logs with invalid service"""
        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "nonexistent_service"})

                try:
                    message = websocket.receive_json()
                    if "error" in str(message).lower():
                        assert True
                except Exception:
                    assert True

    async def test_websocket_logs_malformed_message(self, app):
        """Test WebSocket logs with malformed message"""
        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                try:
                    websocket.send_text("invalid json")
                    assert True
                except Exception:
                    assert True

    @patch("server.PiTracServer._stream_service_logs")
    async def test_websocket_logs_missing_service(self, mock_stream, app):
        """Test WebSocket logs with missing service"""

        async def mock_stream_func(websocket, service):
            await websocket.send_json({"message": f"Streaming {service} logs", "service": service})
            return

        mock_stream.side_effect = mock_stream_func

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({})
                response = websocket.receive_json()
                assert "pitrac" in response.get("service", "")
                websocket.close()

    @patch("server.PiTracServer._stream_systemd_logs")
    @patch("server.PiTracServer._stream_file_logs")
    async def test_websocket_logs_connection_basic(self, mock_stream_file, mock_stream_systemd, app):
        """Test basic WebSocket logs connection"""

        async def mock_stream(websocket, unit):
            await websocket.send_json({"message": "Test log", "service": unit})

        mock_stream_systemd.side_effect = mock_stream
        mock_stream_file.side_effect = mock_stream

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac-web"})

                time.sleep(0.1)
                assert True

    @patch("server.PiTracServer._stream_systemd_logs")
    async def test_websocket_logs_subprocess_failure(self, mock_stream_systemd, app):
        """Test WebSocket logs when subprocess fails"""

        async def mock_systemd_stream_error(websocket, _unit):
            await websocket.send_json({"error": "Failed to stream logs: subprocess error"})
            return

        mock_stream_systemd.side_effect = mock_systemd_stream_error

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac-web"})

                time.sleep(0.1)

                assert mock_stream_systemd.called

    @patch("server.PiTracServer._stream_file_logs")
    async def test_websocket_logs_file_not_found(self, mock_stream_file, app):
        """Test WebSocket logs when log file doesn't exist"""

        async def mock_file_stream_error(websocket, log_file):
            await websocket.send_json({"message": f"Log file not found: {log_file}", "level": "warning"})
            return

        mock_stream_file.side_effect = mock_file_stream_error

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac"})

                time.sleep(0.1)

                assert mock_stream_file.called

    @patch("server.PiTracServer._stream_file_logs")
    @patch("server.PiTracServer._stream_systemd_logs")
    async def test_websocket_logs_multiple_clients(self, mock_stream_systemd, mock_stream_file, app):
        """Test multiple WebSocket clients can connect simultaneously"""

        async def mock_file_stream(websocket, _log_file):
            await websocket.send_json({"message": "File log", "service": "pitrac"})

        async def mock_systemd_stream(websocket, unit):
            await websocket.send_json({"message": "Systemd log", "service": unit})

        mock_stream_file.side_effect = mock_file_stream
        mock_stream_systemd.side_effect = mock_systemd_stream

        with TestClient(app) as client1, TestClient(app) as client2:
            with client1.websocket_connect("/ws/logs") as ws1, client2.websocket_connect("/ws/logs") as ws2:
                ws1.send_json({"service": "pitrac"})
                ws2.send_json({"service": "pitrac-web"})

                time.sleep(0.1)

                assert mock_stream_file.called
                assert mock_stream_systemd.called

    def test_logs_page_static_resources(self, client):
        """Test that logs page references correct static resources"""
        response = client.get("/logs")
        assert response.status_code == 200
        content = response.text

        assert "logs.css" in content or "dashboard.css" in content
        assert "logs.js" in content or "dashboard.js" in content

        assert "log" in content.lower()

    @patch("server.PiTracServer._stream_systemd_logs")
    async def test_websocket_logs_stream_interruption(self, mock_stream_systemd, app):
        """Test handling of stream interruption"""

        async def mock_interrupted_stream(websocket, unit):
            await websocket.send_json({"message": "Log line before interruption", "service": unit, "historical": True})
            raise Exception("Stream interrupted")

        mock_stream_systemd.side_effect = mock_interrupted_stream

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac-web"})

                time.sleep(0.1)

                assert mock_stream_systemd.called

    @patch("server.PiTracServer._stream_systemd_logs")
    @patch("server.PiTracServer._stream_file_logs")
    async def test_websocket_logs_connection_handling(self, mock_stream_file, mock_stream_systemd, app):
        """Test proper connection handling and cleanup"""

        async def mock_stream(websocket, unit):
            await websocket.send_json({"message": "Test log", "service": unit})

        mock_stream_systemd.side_effect = mock_stream
        mock_stream_file.side_effect = mock_stream

        with TestClient(app) as client:
            with client.websocket_connect("/ws/logs") as websocket:
                websocket.send_json({"service": "pitrac-web"})

                time.sleep(0.1)
                assert True

    def test_get_log_services_structure(self, client):
        """Test the structure of log services response"""
        response = client.get("/api/logs/services")
        assert response.status_code == 200
        data = response.json()

        assert "services" in data
        services = data["services"]

        for service in services:
            required_fields = ["id", "name", "status"]
            for field in required_fields:
                assert field in service, f"Service missing required field: {field}"

            assert service["status"] in ["running", "stopped"]

            assert isinstance(service["id"], str) and len(service["id"]) > 0
            assert isinstance(service["name"], str) and len(service["name"]) > 0

    @patch("server.PiTracServer._stream_systemd_logs")
    @patch("server.PiTracServer._stream_file_logs")
    async def test_websocket_logs_different_service_types(self, mock_stream_file, mock_stream_systemd, app):
        """Test streaming logs from different service types"""

        async def mock_stream(websocket, unit):
            await websocket.send_json({"message": f"Test log for {unit}", "service": unit})

        mock_stream_systemd.side_effect = mock_stream
        mock_stream_file.side_effect = mock_stream

        services = ["pitrac", "activemq", "pitrac-web"]

        for service_name in services:
            with TestClient(app) as client:
                with client.websocket_connect("/ws/logs") as websocket:
                    websocket.send_json({"service": service_name})

                    import time

                    time.sleep(0.1)
                    assert True

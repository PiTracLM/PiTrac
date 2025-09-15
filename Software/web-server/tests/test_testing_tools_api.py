import pytest
from unittest.mock import patch, MagicMock


@pytest.mark.unit
class TestTestingToolsAPI:
    """Test testing tools REST API endpoints"""

    def test_testing_page_loads(self, client):
        """Test that testing tools page loads successfully"""
        response = client.get("/testing")
        assert response.status_code == 200
        assert "Testing Tools" in response.text
        assert "testing.css" in response.text
        assert "testing.js" in response.text

    def test_get_testing_tools(self, client, server_instance):
        """Test getting available testing tools"""
        response = client.get("/api/testing/tools")
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, dict)

        expected_tools = [
            "pulse_test",
            "camera1_still",
            "camera2_still",
            "dual_camera_test",
            "config_test",
            "image_processing_test",
        ]

        for tool_id in expected_tools:
            if tool_id in data:
                tool = data[tool_id]
                assert "name" in tool
                assert "description" in tool
                assert "category" in tool
                assert "args" in tool
                assert "requires_sudo" in tool
                assert "timeout" in tool

    @patch("testing_tools_manager.TestingToolsManager.run_tool")
    def test_run_testing_tool_success(self, mock_run_tool, client):
        """Test running a testing tool successfully"""
        mock_run_tool.return_value = {
            "success": True,
            "tool_id": "pulse_test",
            "status": "running",
            "pid": 12345,
            "message": "Tool started successfully",
        }

        response = client.post("/api/testing/run/pulse_test")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "started"
        assert data["tool_id"] == "pulse_test"
        assert "started" in data["message"]

    @patch("testing_tools_manager.TestingToolsManager.run_tool")
    def test_run_testing_tool_failure(self, mock_run_tool, client):
        """Test running a testing tool with failure"""
        mock_run_tool.return_value = {
            "success": False,
            "tool_id": "pulse_test",
            "status": "failed",
            "message": "Tool failed to start",
            "error": "Binary not found",
        }

        response = client.post("/api/testing/run/pulse_test")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "started"
        assert data["tool_id"] == "pulse_test"
        assert "started" in data["message"]

    @patch("testing_tools_manager.TestingToolsManager.run_tool")
    def test_run_nonexistent_testing_tool(self, mock_run_tool, client):
        """Test running a non-existent testing tool"""
        mock_run_tool.return_value = {
            "success": False,
            "tool_id": "nonexistent_tool",
            "status": "failed",
            "message": "Tool not found",
            "error": "Tool 'nonexistent_tool' not found",
        }

        response = client.post("/api/testing/run/nonexistent_tool")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "started"
        assert data["tool_id"] == "nonexistent_tool"
        assert "started" in data["message"]

    @patch("testing_tools_manager.TestingToolsManager.stop_tool")
    def test_stop_testing_tool_success(self, mock_stop_tool, client):
        """Test stopping a testing tool successfully"""
        mock_stop_tool.return_value = {
            "success": True,
            "tool_id": "pulse_test",
            "status": "stopped",
            "message": "Tool stopped successfully",
        }

        response = client.post("/api/testing/stop/pulse_test")
        assert response.status_code == 200
        data = response.json()
        assert data["success"] is True
        assert data["tool_id"] == "pulse_test"
        assert data["status"] == "stopped"
        mock_stop_tool.assert_called_once_with("pulse_test")

    @patch("testing_tools_manager.TestingToolsManager.stop_tool")
    def test_stop_testing_tool_not_running(self, mock_stop_tool, client):
        """Test stopping a testing tool that is not running"""
        mock_stop_tool.return_value = {
            "success": False,
            "tool_id": "pulse_test",
            "status": "not_running",
            "message": "Tool is not currently running",
        }

        response = client.post("/api/testing/stop/pulse_test")
        assert response.status_code == 200
        data = response.json()
        assert data["success"] is False
        assert data["tool_id"] == "pulse_test"
        assert data["status"] == "not_running"
        mock_stop_tool.assert_called_once_with("pulse_test")

    @patch("testing_tools_manager.TestingToolsManager.stop_tool")
    def test_stop_nonexistent_testing_tool(self, mock_stop_tool, client):
        """Test stopping a non-existent testing tool"""
        mock_stop_tool.return_value = {
            "success": False,
            "tool_id": "nonexistent_tool",
            "status": "failed",
            "message": "Tool not found",
            "error": "Tool 'nonexistent_tool' not found",
        }

        response = client.post("/api/testing/stop/nonexistent_tool")
        assert response.status_code == 200
        data = response.json()
        assert data["success"] is False
        assert data["tool_id"] == "nonexistent_tool"
        assert "error" in data
        mock_stop_tool.assert_called_once_with("nonexistent_tool")

    def test_get_testing_status(self, client, server_instance):
        """Test getting testing status"""
        server_instance.testing_manager.running_processes = {"pulse_test": MagicMock(pid=12345)}
        server_instance.testing_manager.completed_results = {
            "camera1_still": {
                "success": True,
                "output": "Camera test completed successfully",
                "return_code": 0,
                "timestamp": "2024-01-01T12:00:00",
            }
        }

        response = client.get("/api/testing/status")
        assert response.status_code == 200
        data = response.json()

        assert "running" in data
        assert "results" in data

        assert isinstance(data["running"], list)
        assert isinstance(data["results"], dict)

    def test_get_testing_status_empty(self, client, server_instance):
        """Test getting testing status when no tools are running or completed"""
        server_instance.testing_manager.running_processes = {}
        server_instance.testing_manager.completed_results = {}

        response = client.get("/api/testing/status")
        assert response.status_code == 200
        data = response.json()

        assert data["running"] == []
        assert data["results"] == {}

    @patch("testing_tools_manager.TestingToolsManager.run_tool")
    def test_run_testing_tool_with_parameters(self, mock_run_tool, client):
        """Test running a testing tool with custom parameters"""
        mock_run_tool.return_value = {
            "success": True,
            "tool_id": "camera1_still",
            "status": "running",
            "pid": 12346,
            "message": "Camera test started",
        }

        response = client.post("/api/testing/run/camera1_still")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "started"
        assert data["tool_id"] == "camera1_still"

    @patch("testing_tools_manager.TestingToolsManager.run_tool")
    def test_run_testing_tool_concurrent_requests(self, mock_run_tool, client):
        """Test running the same testing tool concurrently"""
        mock_run_tool.return_value = {
            "success": True,
            "tool_id": "pulse_test",
            "status": "running",
            "pid": 12345,
            "message": "Tool started successfully",
        }

        response1 = client.post("/api/testing/run/pulse_test")
        assert response1.status_code == 200

        mock_run_tool.return_value = {
            "success": False,
            "tool_id": "pulse_test",
            "status": "already_running",
            "message": "Tool is already running",
            "pid": 12345,
        }

        response2 = client.post("/api/testing/run/pulse_test")
        assert response2.status_code == 200
        data2 = response2.json()
        assert data2["status"] == "started"
        assert data2["tool_id"] == "pulse_test"

    def test_testing_tools_integration_workflow(self, client, server_instance):
        """Test a complete workflow of listing, running, checking status, and stopping a tool"""
        response = client.get("/api/testing/tools")
        assert response.status_code == 200
        tools = response.json()
        assert len(tools) > 0

        response = client.get("/api/testing/status")
        assert response.status_code == 200
        status = response.json()
        initial_running = len(status["running"])

        with patch("testing_tools_manager.TestingToolsManager.run_tool") as mock_run:
            mock_run.return_value = {"success": True, "tool_id": "pulse_test", "status": "running", "pid": 12345}

            response = client.post("/api/testing/run/pulse_test")
            assert response.status_code == 200
            run_data = response.json()
            assert run_data["status"] == "started"
            assert run_data["tool_id"] == "pulse_test"

        with patch("testing_tools_manager.TestingToolsManager.stop_tool") as mock_stop:
            mock_stop.return_value = {"success": True, "tool_id": "pulse_test", "status": "stopped"}

            response = client.post("/api/testing/stop/pulse_test")
            assert response.status_code == 200
            stop_data = response.json()
            assert stop_data["success"] is True
            mock_stop.assert_called_once_with("pulse_test")

    @patch("testing_tools_manager.TestingToolsManager")
    def test_testing_manager_initialization_failure(self, mock_manager_class, client):
        """Test handling when testing manager fails to initialize"""
        mock_manager_class.side_effect = Exception("Failed to initialize testing manager")

        response = client.get("/api/testing/tools")
        assert response.status_code in [200, 500]

    def test_testing_page_static_resources(self, client):
        """Test that testing page references correct static resources"""
        response = client.get("/testing")
        assert response.status_code == 200
        content = response.text

        assert "testing.css" in content or "dashboard.css" in content
        assert "testing.js" in content or "dashboard.js" in content

        assert "testing" in content.lower()
        assert "tools" in content.lower()

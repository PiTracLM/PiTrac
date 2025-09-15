import os
import sys
from pathlib import Path
from typing import AsyncGenerator
from unittest.mock import patch

import pytest
from fastapi.testclient import TestClient
from httpx import AsyncClient

sys.path.insert(0, str(Path(__file__).parent.parent))

os.environ["TESTING"] = "true"

from models import ShotData
from managers import ConnectionManager, ShotDataStore
from parsers import ShotDataParser
from server import PiTracServer

from utils.mock_factories import (
    MockActiveMQFactory,
    MockWebSocketFactory,
)
from utils.test_helpers import ShotDataHelper


@pytest.fixture
def mock_activemq():
    """Mock ActiveMQ connection using factory"""
    with patch("server.stomp.Connection") as mock_conn:
        mock_instance = MockActiveMQFactory.create_connection()
        mock_conn.return_value = mock_instance
        yield mock_instance


@pytest.fixture
def server_instance(mock_activemq):
    """Create PiTracServer instance with mocked dependencies"""
    server = PiTracServer()
    server.mq_conn = mock_activemq
    server.shutdown_flag = False
    server.reconnect_task = None
    return server


@pytest.fixture
def app(server_instance):
    return server_instance.app


@pytest.fixture
def client(app):
    """Create test client for synchronous tests"""
    return TestClient(app)


@pytest.fixture
async def async_client(app) -> AsyncGenerator:
    """Create async test client for async tests"""
    async with AsyncClient(app=app, base_url="http://test") as ac:
        yield ac


@pytest.fixture
def sample_shot_data():
    """Sample shot data for testing using helper"""
    return ShotDataHelper.generate_realistic_shot(
        speed_range=(145, 146),  # Fixed range for consistent test data
        carry_range=(265, 266),
        launch_range=(12, 13),
        side_range=(-3, -2),
        backspin_range=(2850, 2850),
        sidespin_range=(-320, -320),
    )


@pytest.fixture
def shot_data_instance():
    """Create a ShotData instance for testing"""
    return ShotData(
        speed=145.5,
        carry=265.3,
        launch_angle=12.4,
        side_angle=-2.1,
        back_spin=2850,
        side_spin=-320,
        result_type="Hit",
        message="Great shot!",
        timestamp="2024-01-01T12:00:00",
        images=["shot_001.jpg", "shot_002.jpg"],
    )


@pytest.fixture
def mock_home_dir(tmp_path):
    """Mock home directory for testing with simplified structure"""
    from utils.test_helpers import ConfigTestHelper

    home = ConfigTestHelper.create_temp_config_dir()

    config = {
        "network": {
            "broker_address": "tcp://localhost:61616",
            "username": "test_user",
            "password": "test_pass",
        }
    }

    config_file = home / ".pitrac" / "config" / "pitrac.yaml"
    import yaml

    with open(config_file, "w") as f:
        yaml.dump(config, f)

    yield home


@pytest.fixture
def mock_websocket():
    """Mock WebSocket for testing using factory"""
    return MockWebSocketFactory.create_websocket()


@pytest.fixture
def websocket_test_client(app):
    """Create a simple WebSocket test client"""
    return TestClient(app)


@pytest.fixture
def connection_manager():
    """Create ConnectionManager instance for testing"""
    return ConnectionManager()


@pytest.fixture
def shot_store():
    """Create ShotDataStore instance for testing"""
    return ShotDataStore()


@pytest.fixture
def parser():
    """Create ShotDataParser instance for testing"""
    return ShotDataParser()


@pytest.fixture
def shot_simulator():
    """Shot simulator using helper class"""
    return ShotDataHelper

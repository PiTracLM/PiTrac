import asyncio
import json
import os
import sys
from pathlib import Path
from typing import AsyncGenerator, Generator
from unittest.mock import MagicMock, patch, AsyncMock

import pytest
from fastapi.testclient import TestClient
from httpx import AsyncClient

sys.path.insert(0, str(Path(__file__).parent.parent))

# Set test environment
os.environ["TESTING"] = "true"

from models import ShotData
from managers import ConnectionManager, ShotDataStore
from parsers import ShotDataParser
from server import PiTracServer


@pytest.fixture
def mock_activemq():
    """Mock ActiveMQ connection"""
    with patch('server.stomp.Connection') as mock_conn:
        mock_instance = MagicMock()
        mock_conn.return_value = mock_instance
        mock_instance.is_connected.return_value = True
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
    """Sample shot data for testing"""
    return {
        "speed": 145.5,
        "carry": 265.3,
        "launch_angle": 12.4,
        "side_angle": -2.1,
        "back_spin": 2850,
        "side_spin": -320,
        "result_type": 7,  # HIT
        "message": "Great shot!",
        "image_paths": ["shot_001.jpg", "shot_002.jpg"]
    }


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
        images=["shot_001.jpg", "shot_002.jpg"]
    )


@pytest.fixture
def mock_home_dir(tmp_path):
    """Mock home directory for testing"""
    home = tmp_path / "home"
    home.mkdir()
    
    (home / ".pitrac" / "config").mkdir(parents=True)
    (home / "LM_Shares" / "Images").mkdir(parents=True)
    
    # Create a test config file
    config = {
        "network": {
            "broker_address": "tcp://localhost:61616",
            "username": "test_user",
            "password": "test_pass"
        }
    }
    
    config_file = home / ".pitrac" / "config" / "pitrac.yaml"
    import yaml
    with open(config_file, 'w') as f:
        yaml.dump(config, f)
    
    with patch('constants.Path.home', return_value=home):
        with patch('constants.HOME_DIR', home):
            with patch('constants.PITRAC_DIR', home / ".pitrac"):
                with patch('constants.IMAGES_DIR', home / "LM_Shares" / "Images"):
                    with patch('constants.CONFIG_FILE', config_file):
                        yield home


@pytest.fixture
def mock_websocket():
    """Mock WebSocket for testing"""
    ws = AsyncMock()
    ws.accept = AsyncMock()
    ws.send_json = AsyncMock()
    ws.receive_text = AsyncMock(side_effect=asyncio.TimeoutError)
    ws.close = AsyncMock()
    return ws


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
    """Factory for simulating golf shots"""
    class ShotSimulator:
        @staticmethod
        def generate_shot(
            speed_range=(100, 180),
            carry_range=(150, 350),
            launch_range=(8, 20),
            side_range=(-5, 5),
            backspin_range=(1500, 4000),
            sidespin_range=(-1000, 1000)
        ):
            """Generate random realistic shot data"""
            import random
            
            return {
                "speed": round(random.uniform(*speed_range), 1),
                "carry": round(random.uniform(*carry_range), 1),
                "launch_angle": round(random.uniform(*launch_range), 1),
                "side_angle": round(random.uniform(*side_range), 1),
                "back_spin": random.randint(*backspin_range),
                "side_spin": random.randint(*sidespin_range),
                "result_type": 7,  # HIT
                "message": random.choice([
                    "Great shot!",
                    "Nice swing!",
                    "Perfect contact!",
                    "Solid strike!"
                ]),
                "image_paths": [f"shot_{random.randint(1000, 9999)}.jpg"]
            }
        
        @staticmethod
        def generate_sequence(count=10):
            """Generate a sequence of shots"""
            return [ShotSimulator.generate_shot() for _ in range(count)]
        
        @staticmethod
        def generate_array_format():
            """Generate shot data in array format (C++ MessagePack format)"""
            import random
            return [
                round(random.uniform(150, 350), 1),  # carry_meters
                round(random.uniform(44.7, 80.5), 1),  # speed_mpers (100-180 mph)
                round(random.uniform(8, 20), 1),  # launch_angle_deg
                round(random.uniform(-5, 5), 1),  # side_angle_deg
                random.randint(1500, 4000),  # back_spin_rpm
                random.randint(-1000, 1000),  # side_spin_rpm
                0.95,  # confidence
                1,  # club_type
                7,  # result_type (HIT)
                "Great shot!",  # message
                []  # log_messages
            ]
    
    return ShotSimulator()


@pytest.fixture(autouse=True)
def reset_singleton_state(server_instance):
    """Reset any singleton state between tests"""
    # Reset shot store
    server_instance.shot_store.reset()
    server_instance.shot_store.clear_history()
    
    # Clear websocket connections
    server_instance.connection_manager._connections.clear()
    
    yield
    
    # Cleanup after test
    server_instance.shot_store.reset()
    server_instance.shot_store.clear_history()
    server_instance.connection_manager._connections.clear()
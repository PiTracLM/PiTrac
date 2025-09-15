"""
Test helper functions and utilities for common test operations.

This module provides helper functions for common test patterns like
data generation, assertions, and test setup/teardown operations.
"""

import random
from typing import Dict, Any, List, Optional
from pathlib import Path
import tempfile
import json


class ShotDataHelper:
    """Helper class for generating and validating shot data."""

    @staticmethod
    def generate_realistic_shot(
        speed_range=(100, 180),
        carry_range=(150, 350),
        launch_range=(8, 20),
        side_range=(-5, 5),
        backspin_range=(1500, 4000),
        sidespin_range=(-1000, 1000),
    ) -> Dict[str, Any]:
        """Generate realistic shot data with random values within specified ranges."""
        return {
            "speed": round(random.uniform(*speed_range), 1),
            "carry": round(random.uniform(*carry_range), 1),
            "launch_angle": round(random.uniform(*launch_range), 1),
            "side_angle": round(random.uniform(*side_range), 1),
            "back_spin": random.randint(*backspin_range),
            "side_spin": random.randint(*sidespin_range),
            "result_type": 7,  # HIT
            "message": random.choice(["Great shot!", "Nice swing!", "Perfect contact!", "Solid strike!"]),
            "image_paths": [f"shot_{random.randint(1000, 9999)}.jpg"],
        }

    @staticmethod
    def generate_array_format_shot() -> List[Any]:
        """Generate shot data in C++ MessagePack array format."""
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
            [],  # log_messages
        ]

    @staticmethod
    def generate_shot_sequence(count: int = 10) -> List[Dict[str, Any]]:
        """Generate a sequence of realistic shots."""
        return [ShotDataHelper.generate_realistic_shot() for _ in range(count)]

    @staticmethod
    def validate_shot_data(shot_data: Dict[str, Any]) -> bool:
        """Validate that shot data contains required fields with reasonable values."""
        required_fields = ["speed", "carry", "launch_angle", "side_angle", "back_spin", "side_spin", "result_type"]

        if not all(field in shot_data for field in required_fields):
            return False

        if not (50 <= shot_data["speed"] <= 250):  # mph
            return False
        if not (50 <= shot_data["carry"] <= 400):  # yards
            return False
        if not (-10 <= shot_data["launch_angle"] <= 45):  # degrees
            return False
        if not (-45 <= shot_data["side_angle"] <= 45):  # degrees
            return False
        if not (0 <= shot_data["back_spin"] <= 10000):  # rpm
            return False
        if not (-5000 <= shot_data["side_spin"] <= 5000):  # rpm
            return False

        return True


class ConfigTestHelper:
    """Helper class for configuration-related test operations."""

    @staticmethod
    def create_temp_config_dir() -> Path:
        """Create a temporary directory with basic config structure."""
        temp_dir = Path(tempfile.mkdtemp())

        (temp_dir / ".pitrac" / "config").mkdir(parents=True)
        (temp_dir / "LM_Shares" / "Images").mkdir(parents=True)
        (temp_dir / "LM_Shares" / "WebShare").mkdir(parents=True)

        return temp_dir

    @staticmethod
    def create_config_metadata() -> Dict[str, Any]:
        """Create realistic configuration metadata for testing."""
        return {
            "settings": {
                "gs_config.cameras.kCamera1Gain": {
                    "type": "number",
                    "default": 1.0,
                    "min": 1.0,
                    "max": 16.0,
                    "category": "Cameras",
                    "subcategory": "basic",
                },
                "gs_config.cameras.kCamera2Gain": {
                    "type": "number",
                    "default": 4.0,
                    "min": 1.0,
                    "max": 16.0,
                    "category": "Cameras",
                    "subcategory": "basic",
                },
                "gs_config.golf_simulator_interfaces.GSPro.kGSProConnectPort": {
                    "type": "number",
                    "default": 921,
                    "min": 1,
                    "max": 65535,
                    "category": "Simulators",
                    "subcategory": "basic",
                },
            },
            "validationRules": {
                "gain": {"type": "range", "min": 1.0, "max": 16.0, "errorMessage": "Gain must be between 1.0 and 16.0"},
                "port": {"type": "range", "min": 1, "max": 65535, "errorMessage": "Port must be between 1 and 65535"},
            },
            "categoryList": ["Cameras", "Simulators"],
        }

    @staticmethod
    def create_user_settings(overrides: Optional[Dict] = None) -> Dict[str, Any]:
        """Create user settings with optional overrides."""
        default_settings = {"gs_config": {"cameras": {"kCamera1Gain": 2.0}}}

        if overrides:
            default_settings.update(overrides)

        return default_settings

    @staticmethod
    def write_config_file(path: Path, config: Dict[str, Any]) -> None:
        """Write configuration data to a JSON file."""
        with open(path, "w") as f:
            json.dump(config, f, indent=2)


class ProcessTestHelper:
    """Helper class for process management testing."""

    @staticmethod
    def create_mock_process_status(
        camera1_running: bool = False,
        camera2_running: bool = False,
        camera1_pid: Optional[int] = None,
        camera2_pid: Optional[int] = None,
    ) -> Dict[str, Any]:
        """Create mock process status data."""
        return {
            "camera1": {"running": camera1_running, "pid": camera1_pid},
            "camera2": {"running": camera2_running, "pid": camera2_pid},
        }

    @staticmethod
    def create_mock_log_data(
        camera1_logs: Optional[List[str]] = None, camera2_logs: Optional[List[str]] = None
    ) -> Dict[str, List[str]]:
        """Create mock log data."""
        return {
            "camera1": camera1_logs or ["Sample log line 1", "Sample log line 2"],
            "camera2": camera2_logs or ["Camera 2 log line 1"],
        }

    @staticmethod
    def generate_log_lines(count: int = 10) -> List[str]:
        """Generate realistic log lines for testing."""
        templates = [
            "[INFO] System initialized successfully",
            "[DEBUG] Camera frame received: {}x{}",
            "[WARN] Connection timeout, retrying...",
            "[ERROR] Failed to process frame",
            "[INFO] Shot detected: speed={:.1f} mph",
        ]

        return [
            (
                template.format(random.randint(640, 1920), random.randint(480, 1080), random.uniform(100, 180))
                if "{}" in template
                else template
            )
            for template in random.choices(templates, k=count)
        ]


class APITestHelper:
    """Helper class for API testing operations."""

    @staticmethod
    def validate_json_response(response_data: Dict[str, Any], required_fields: List[str]) -> bool:
        """Validate that a JSON response contains required fields."""
        return all(field in response_data for field in required_fields)

    @staticmethod
    def assert_successful_response(response_data: Dict[str, Any]) -> None:
        """Assert that an API response indicates success."""
        assert "success" in response_data, "Response should have 'success' field"
        assert response_data["success"] is True, f"Expected success=True, got {response_data.get('success')}"

    @staticmethod
    def assert_error_response(response_data: Dict[str, Any], expected_error: Optional[str] = None) -> None:
        """Assert that an API response indicates an error."""
        assert "success" in response_data, "Response should have 'success' field"
        assert response_data["success"] is False, f"Expected success=False, got {response_data.get('success')}"

        if expected_error:
            assert "message" in response_data, "Error response should have 'message' field"
            assert expected_error in response_data["message"], f"Expected error '{expected_error}' not found in message"

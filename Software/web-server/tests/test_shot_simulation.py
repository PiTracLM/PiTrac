import asyncio
import time
import pytest
from unittest.mock import patch, AsyncMock
from models import ShotData, ResultType


@pytest.mark.integration
class TestShotSimulation:
    """Test realistic shot simulation scenarios"""

    @pytest.mark.asyncio
    async def test_rapid_shot_sequence(self, server_instance, parser, shot_simulator):
        """Test handling rapid sequence of shots"""
        shots = shot_simulator.generate_sequence(10)

        for shot in shots:
            current = server_instance.shot_store.get()
            parsed_shot = parser.parse_dict_format(shot, current)
            server_instance.shot_store.update(parsed_shot)

            stored_shot = server_instance.shot_store.get()
            assert stored_shot.speed == shot["speed"]
            assert stored_shot.carry == shot["carry"]
            await asyncio.sleep(0.01)

    @pytest.mark.asyncio
    async def test_concurrent_shot_updates(
        self, server_instance, parser, shot_simulator
    ):
        """Test handling concurrent shot updates"""
        shots = shot_simulator.generate_sequence(5)

        async def update_shot(shot_data):
            current = server_instance.shot_store.get()
            parsed_shot = parser.parse_dict_format(shot_data, current)
            server_instance.shot_store.update(parsed_shot)

        tasks = [update_shot(shot) for shot in shots]
        await asyncio.gather(*tasks)

        stored_shot = server_instance.shot_store.get()
        assert stored_shot.speed > 0

    def test_shot_data_validation(self, shot_simulator):
        """Test shot data validation and bounds"""
        shot = shot_simulator.generate_shot()

        assert 100 <= shot["speed"] <= 180
        assert 150 <= shot["carry"] <= 350
        assert 8 <= shot["launch_angle"] <= 20
        assert -5 <= shot["side_angle"] <= 5
        assert 1500 <= shot["back_spin"] <= 4000
        assert -1000 <= shot["side_spin"] <= 1000

    @pytest.mark.asyncio
    async def test_shot_with_images(self, server_instance, parser, tmp_path):
        """Test shot processing with image handling"""
        image_files = []
        for i in range(3):
            img_path = tmp_path / f"shot_{i}.jpg"
            img_path.write_bytes(b"fake image data")
            image_files.append(f"shot_{i}.jpg")

        shot_data = {
            "speed": 155.0,
            "carry": 280.0,
            "launch_angle": 14.0,
            "side_angle": 0.0,
            "back_spin": 2800,
            "side_spin": 0,
            "result_type": 7,
            "message": "Perfect shot",
            "image_paths": image_files,
        }

        with patch("constants.IMAGES_DIR", tmp_path):
            current = server_instance.shot_store.get()
            parsed_shot = parser.parse_dict_format(shot_data, current)
            server_instance.shot_store.update(parsed_shot)

            stored_shot = server_instance.shot_store.get()
            assert stored_shot.images == image_files
            assert len(stored_shot.images) == 3

    @pytest.mark.slow
    @pytest.mark.asyncio
    async def test_sustained_shot_stream(self, server_instance, parser, shot_simulator):
        """Test sustained stream of shots over time"""
        mock_ws = AsyncMock()
        mock_ws.send_json = AsyncMock()
        server_instance.connection_manager._connections.add(mock_ws)

        start_time = time.time()
        shot_count = 0

        while time.time() - start_time < 2:
            shot = shot_simulator.generate_shot()
            current = server_instance.shot_store.get()
            parsed_shot = parser.parse_dict_format(shot, current)
            server_instance.shot_store.update(parsed_shot)
            await server_instance.connection_manager.broadcast(parsed_shot.to_dict())
            shot_count += 1
            await asyncio.sleep(0.1)

        assert shot_count > 0
        assert mock_ws.send_json.call_count == shot_count

    def test_shot_result_types(self):
        """Test different shot result types"""
        result_types = {
            ResultType.UNKNOWN: "Unknown",
            ResultType.INITIALIZING: "Initializing",
            ResultType.WAITING_FOR_BALL: "Waiting For Ball",
            ResultType.BALL_READY: "Ball Ready",
            ResultType.HIT: "Hit",
            ResultType.ERROR: "Error",
        }

        for enum_val, expected_text in result_types.items():
            formatted = enum_val.name.replace("_", " ").title()
            assert formatted == expected_text or formatted.replace(
                " ", ""
            ) == expected_text.replace(" ", "")

    @pytest.mark.asyncio
    async def test_shot_timestamp_generation(
        self, server_instance, parser, shot_simulator
    ):
        """Test that timestamps are properly generated"""
        from datetime import datetime

        shot = shot_simulator.generate_shot()

        before = datetime.now()
        current = server_instance.shot_store.get()
        parsed_shot = parser.parse_dict_format(shot, current)
        server_instance.shot_store.update(parsed_shot)
        after = datetime.now()

        stored_shot = server_instance.shot_store.get()
        assert stored_shot.timestamp is not None

        timestamp = datetime.fromisoformat(stored_shot.timestamp)
        assert before <= timestamp <= after

    @pytest.mark.asyncio
    async def test_shot_data_persistence(self, server_instance, parser, shot_simulator):
        """Test that shot data persists between requests"""
        shot = shot_simulator.generate_shot()
        current = server_instance.shot_store.get()
        parsed_shot = parser.parse_dict_format(shot, current)
        server_instance.shot_store.update(parsed_shot)

        stored_shot = server_instance.shot_store.get()
        stored_speed = stored_shot.speed
        stored_carry = stored_shot.carry

        await asyncio.sleep(0.1)

        current_shot = server_instance.shot_store.get()
        assert current_shot.speed == stored_speed
        assert current_shot.carry == stored_carry

    @pytest.mark.parametrize(
        "club_type,speed_range,carry_range",
        [
            ("driver", (140, 180), (250, 350)),
            ("iron", (100, 140), (150, 200)),
            ("wedge", (70, 100), (50, 120)),
        ],
    )
    def test_club_specific_shots(
        self, shot_simulator, club_type, speed_range, carry_range
    ):
        """Test generating club-specific shot data"""
        shot = shot_simulator.generate_shot(
            speed_range=speed_range, carry_range=carry_range
        )

        assert speed_range[0] <= shot["speed"] <= speed_range[1]
        assert carry_range[0] <= shot["carry"] <= carry_range[1]

    def test_parser_validation(self, parser):
        """Test parser validation of shot data"""
        valid_shot = ShotData(
            speed=150.0,
            carry=250.0,
            launch_angle=15.0,
            side_angle=2.0,
            back_spin=3000,
            side_spin=200,
        )
        assert parser.validate_shot_data(valid_shot) is True

        invalid_shot = ShotData(speed=500.0)
        assert parser.validate_shot_data(invalid_shot) is False

        invalid_shot = ShotData(launch_angle=100.0)
        assert parser.validate_shot_data(invalid_shot) is False

    @pytest.mark.asyncio
    async def test_shot_history(self, server_instance, shot_simulator):
        """Test shot history tracking"""
        for i in range(5):
            shot = ShotData(
                speed=100 + i * 10,
                carry=200 + i * 20,
                result_type="Hit",  # Only "Hit" shots are stored in history
            )
            server_instance.shot_store.update(shot)

        history = server_instance.shot_store.get_history(10)
        assert len(history) == 5

        for i, shot in enumerate(history):
            assert shot.speed == 100 + i * 10
            assert shot.carry == 200 + i * 20

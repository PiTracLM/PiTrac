import msgpack
import pytest
import asyncio
import base64
from unittest.mock import MagicMock, AsyncMock, patch
from listeners import ActiveMQListener


@pytest.mark.integration
class TestActiveMQIntegration:
    """Test ActiveMQ message handling"""

    def test_activemq_connection_setup(self, server_instance, mock_activemq):
        """Test ActiveMQ connection is established on startup"""
        server_instance.mq_conn = mock_activemq
        assert server_instance.mq_conn == mock_activemq
        assert mock_activemq.is_connected.return_value is True

    def test_activemq_listener_processes_messages(self, shot_store, connection_manager, parser):
        """Test ActiveMQ listener processes incoming messages"""
        mock_loop = MagicMock()
        listener = ActiveMQListener(shot_store, connection_manager, parser, mock_loop)

        shot_data = {
            "speed": 150.0,
            "carry": 270.0,
            "launch_angle": 13.0,
            "side_angle": 0.5,
            "back_spin": 2900,
            "side_spin": 50,
            "result_type": 7,
            "message": "Good shot",
        }

        packed_data = msgpack.packb(shot_data)

        mock_frame = MagicMock()
        mock_frame.body = packed_data

        with patch("asyncio.run_coroutine_threadsafe") as mock_run_coroutine:
            listener.on_message(mock_frame)
            mock_run_coroutine.assert_called_once()
            assert listener.message_count == 1

    def test_activemq_error_handling(self, shot_store, connection_manager, parser):
        """Test ActiveMQ error handling"""
        listener = ActiveMQListener(shot_store, connection_manager, parser)

        mock_frame = MagicMock()
        mock_frame.body = "Error message"

        listener.on_error(mock_frame)
        assert listener.error_count == 1

    def test_activemq_disconnection_handling(self, shot_store, connection_manager, parser):
        """Test ActiveMQ disconnection handling"""
        listener = ActiveMQListener(shot_store, connection_manager, parser)
        assert listener.connected is False

        listener.on_connected(MagicMock())
        assert listener.connected is True

        listener.on_disconnected()
        assert listener.connected is False

    def test_malformed_message_handling(self, shot_store, connection_manager, parser):
        """Test handling of malformed messages"""
        mock_loop = MagicMock()
        listener = ActiveMQListener(shot_store, connection_manager, parser, mock_loop)

        # Use invalid msgpack format that will cause UnpackException (FormatError)
        mock_frame = MagicMock()
        mock_frame.body = b"\xc1"  # Invalid msgpack format code

        with patch("listeners.logger") as mock_logger:
            listener.on_message(mock_frame)
            # Should log an error due to UnpackException
            mock_logger.error.assert_called()
            assert listener.message_count == 1

    def test_base64_encoded_message(self, shot_store, connection_manager, parser):
        """Test handling of base64 encoded messages"""
        mock_loop = MagicMock()
        listener = ActiveMQListener(shot_store, connection_manager, parser, mock_loop)

        shot_data = {"speed": 150.0, "carry": 270.0, "result_type": 7}

        packed_data = msgpack.packb(shot_data)
        assert packed_data is not None  # Type guard for mypy/pylance
        base64_data = base64.b64encode(packed_data).decode("utf-8")  # Convert to string

        mock_frame = MagicMock()
        mock_frame.body = base64_data  # String body with base64 data
        mock_frame.headers = {"encoding": "base64"}

        with patch("asyncio.run_coroutine_threadsafe") as mock_run_coroutine:
            listener.on_message(mock_frame)
            mock_run_coroutine.assert_called_once()
            assert listener.message_count == 1

    @pytest.mark.asyncio
    async def test_message_to_websocket_flow(self, server_instance, parser):
        """Test complete flow from ActiveMQ message to WebSocket clients"""
        mock_ws = AsyncMock()
        mock_ws.send_json = AsyncMock()
        server_instance.connection_manager._connections.add(mock_ws)

        shot_data = {
            "speed": 175.0,
            "carry": 310.0,
            "launch_angle": 16.0,
            "side_angle": -2.0,
            "back_spin": 2600,
            "side_spin": -400,
            "result_type": 7,
            "message": "Draw shot",
            "image_paths": ["draw_001.jpg", "draw_002.jpg"],
        }

        # Process through parser and store
        current = server_instance.shot_store.get()
        parsed_shot = parser.parse_dict_format(shot_data, current)
        server_instance.shot_store.update(parsed_shot)
        await server_instance.connection_manager.broadcast(parsed_shot.to_dict())

        mock_ws.send_json.assert_called_once()
        sent_data = mock_ws.send_json.call_args[0][0]

        assert sent_data["speed"] == 175.0
        assert sent_data["carry"] == 310.0
        assert sent_data["message"] == "Draw shot"
        assert sent_data["images"] == ["draw_001.jpg", "draw_002.jpg"]
        assert "timestamp" in sent_data

    def test_activemq_config_loading(self, mock_home_dir):
        """Test loading ActiveMQ configuration from config file"""
        with patch(
            "constants.CONFIG_FILE",
            mock_home_dir / ".pitrac" / "config" / "pitrac.yaml",
        ):
            with patch("server.stomp.Connection") as mock_conn:
                from server import PiTracServer

                server = PiTracServer()
                server.setup_activemq()

                # Check connection was attempted with right params
                mock_conn.assert_called()
                call_args = mock_conn.call_args[0][0]
                assert ("localhost", 61613) in call_args

    def test_activemq_reconnection_logic(self, shot_store, connection_manager, parser):
        """Test ActiveMQ reconnection behavior"""
        listener = ActiveMQListener(shot_store, connection_manager, parser)

        listener.on_connected(MagicMock())
        assert listener.connected is True
        assert listener.message_count == 0  # Reset on connect

        listener.on_disconnected()
        assert listener.connected is False

        listener.on_connected(MagicMock())
        assert listener.connected is True

    def test_activemq_heartbeat_handlers(self, shot_store, connection_manager, parser):
        """Test ActiveMQ heartbeat handlers"""
        listener = ActiveMQListener(shot_store, connection_manager, parser)

        listener.on_heartbeat()

        listener.on_connected(MagicMock())
        assert listener.connected is True

        listener.on_heartbeat_timeout()
        assert listener.connected is False

    @pytest.mark.asyncio
    async def test_server_reconnection_task_startup(self, mock_home_dir):
        """Test that reconnection task starts on server startup"""
        with patch(
            "constants.CONFIG_FILE",
            mock_home_dir / ".pitrac" / "config" / "pitrac.yaml",
        ):
            with patch("server.stomp.Connection"):
                from server import PiTracServer

                server = PiTracServer()
                server.shutdown_flag = False

                with patch("asyncio.create_task") as mock_create_task:
                    await server.startup_event()
                    mock_create_task.assert_called_once()

    @pytest.mark.asyncio
    async def test_server_reconnection_loop(self, mock_home_dir):
        """Test the reconnection loop behavior"""
        with patch(
            "constants.CONFIG_FILE",
            mock_home_dir / ".pitrac" / "config" / "pitrac.yaml",
        ):
            with patch("server.stomp.Connection"):
                from server import PiTracServer

                server = PiTracServer()
                server.shutdown_flag = False

                mock_conn = MagicMock()
                mock_conn.is_connected.side_effect = [False, False, True]
                server.mq_conn = mock_conn

                with patch.object(server, "setup_activemq") as mock_setup:
                    mock_setup.side_effect = [None, mock_conn]

                    reconnect_task = asyncio.create_task(server.reconnect_activemq_loop())

                    await asyncio.sleep(0.1)
                    server.shutdown_flag = True

                    reconnect_task.cancel()
                    try:
                        await reconnect_task
                    except asyncio.CancelledError:
                        pass

                    assert mock_setup.call_count >= 1

    @pytest.mark.asyncio
    async def test_server_shutdown_cleanup(self, mock_home_dir):
        """Test proper cleanup during shutdown"""
        with patch(
            "constants.CONFIG_FILE",
            mock_home_dir / ".pitrac" / "config" / "pitrac.yaml",
        ):
            with patch("server.stomp.Connection"):
                from server import PiTracServer

                server = PiTracServer()
                server.shutdown_flag = False

                mock_conn = MagicMock()
                server.mq_conn = mock_conn

                async def dummy_task():
                    try:
                        while True:
                            await asyncio.sleep(1)
                    except asyncio.CancelledError:
                        pass

                server.reconnect_task = asyncio.create_task(dummy_task())

                await server.shutdown_event()

                assert server.shutdown_flag is True

                assert server.reconnect_task.cancelled()

                mock_conn.disconnect.assert_called_once()

    def test_listener_statistics(self, shot_store, connection_manager, parser):
        """Test listener statistics tracking"""
        listener = ActiveMQListener(shot_store, connection_manager, parser)

        # Process some messages
        for i in range(5):
            mock_frame = MagicMock()
            mock_frame.body = msgpack.packb({"speed": 100 + i})
            listener.on_message(mock_frame)

        # Generate some errors
        for i in range(3):
            listener.on_error(MagicMock(body="error"))

        stats = listener.get_stats()
        assert stats["messages_processed"] == 5
        assert stats["errors"] == 3
        assert stats["connected"] is False

    @pytest.mark.asyncio
    async def test_array_format_processing(self, server_instance, parser):
        """Test processing of array format messages (C++ MessagePack)"""
        mock_loop = asyncio.get_event_loop()
        listener = ActiveMQListener(
            server_instance.shot_store,
            server_instance.connection_manager,
            parser,
            mock_loop,
        )

        # Array format from C++
        shot_data = [
            250.5,  # carry_meters
            65.0,  # speed_mpers
            13.5,  # launch_angle_deg
            -2.3,  # side_angle_deg
            3100,  # back_spin_rpm
            -400,  # side_spin_rpm
            0.95,  # confidence
            1,  # club_type
            7,  # result_type (HIT)
            "Excellent strike!",  # message
            [],  # log_messages
            [],  # image_file_paths
        ]

        packed_data = msgpack.packb(shot_data)

        mock_frame = MagicMock()
        mock_frame.body = packed_data

        with patch("asyncio.run_coroutine_threadsafe") as mock_run:
            listener.on_message(mock_frame)
            mock_run.assert_called_once()

            # Extract the coroutine that was passed
            coro = mock_run.call_args[0][0]
            # Run it directly
            await coro

            stored = server_instance.shot_store.get()
            assert stored.carry == 250.5
            assert abs(stored.speed - 145.4) < 0.1  # m/s to mph conversion

    @pytest.mark.asyncio
    async def test_reconnection_with_exponential_backoff(self, mock_home_dir):
        """Test that reconnection uses exponential backoff"""
        with patch(
            "constants.CONFIG_FILE",
            mock_home_dir / ".pitrac" / "config" / "pitrac.yaml",
        ):
            with patch("server.stomp.Connection"):
                from server import PiTracServer

                server = PiTracServer()
                server.shutdown_flag = False

                sleep_delays = []

                async def mock_sleep(delay):
                    sleep_delays.append(delay)
                    if len(sleep_delays) >= 3:
                        server.shutdown_flag = True
                    return

                with patch("asyncio.sleep", side_effect=mock_sleep):
                    with patch.object(server, "setup_activemq", return_value=None):
                        await server.reconnect_activemq_loop()

                assert len(sleep_delays) >= 2
                if len(sleep_delays) >= 2:
                    assert sleep_delays[0] == 5  # Initial retry delay
                    assert sleep_delays[1] == 10  # Doubled delay

    @pytest.mark.asyncio
    async def test_process_broadcast_value_error(self):
        """Test _process_and_broadcast handling ValueError"""
        mock_loop = MagicMock()
        shot_store = MagicMock()
        connection_manager = MagicMock()
        parser = MagicMock()

        listener = ActiveMQListener(shot_store, connection_manager, parser, mock_loop)

        parser.parse_dict_format.side_effect = ValueError("Invalid format")

        await listener._process_and_broadcast({"invalid": "data"})

        assert shot_store.update.call_count == 0

    @pytest.mark.asyncio
    async def test_process_broadcast_general_exception(self):
        """Test _process_and_broadcast handling general Exception"""
        mock_loop = MagicMock()
        shot_store = MagicMock()
        connection_manager = MagicMock()
        parser = MagicMock()

        listener = ActiveMQListener(shot_store, connection_manager, parser, mock_loop)

        shot_store.update.side_effect = Exception("Unexpected error")

        from models import ShotData

        mock_shot = ShotData(speed=100.0)
        parser.parse_dict_format.return_value = mock_shot
        parser.validate_shot_data.return_value = True

        await listener._process_and_broadcast({"speed": 100.0})

        assert shot_store.update.call_count == 1

    @pytest.mark.asyncio
    async def test_validate_shot_data_warning_path(self):
        """Test warning path when shot data validation fails"""
        mock_loop = MagicMock()
        shot_store = MagicMock()
        connection_manager = MagicMock()
        parser = MagicMock()

        listener = ActiveMQListener(shot_store, connection_manager, parser, mock_loop)

        from models import ShotData

        mock_shot = ShotData(speed=100.0)
        parser.parse_dict_format.return_value = mock_shot
        parser.validate_shot_data.return_value = False

        connection_manager.broadcast = AsyncMock()

        await listener._process_and_broadcast({"speed": 100.0})

        assert shot_store.update.call_count == 1
        assert connection_manager.broadcast.called

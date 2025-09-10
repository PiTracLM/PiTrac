import asyncio
import pytest
from unittest.mock import AsyncMock
from models import ShotData


@pytest.mark.websocket
class TestWebSocket:
    """Test WebSocket real-time updates"""

    def test_websocket_connection(self, client, server_instance):
        """Test WebSocket connection establishment"""
        server_instance.connection_manager._connections.clear()

        with client.websocket_connect("/ws") as websocket:
            data = websocket.receive_json()
            assert "speed" in data
            assert "carry" in data
            assert "timestamp" in data

            assert server_instance.connection_manager.connection_count == 1

        # Connection should be removed after disconnect
        assert server_instance.connection_manager.connection_count == 0

    def test_websocket_receives_updates(self, client, server_instance):
        """Test WebSocket receives shot updates"""
        server_instance.connection_manager._connections.clear()

        test_shot = ShotData(
            speed=155.5,
            carry=285.0,
            launch_angle=14.2,
            side_angle=1.5,
            back_spin=3200,
            side_spin=150,
            result_type="Hit",
            message="Perfect strike!",
            timestamp="2024-01-01T12:00:00",
        )
        server_instance.shot_store.update(test_shot)

        with client.websocket_connect("/ws") as websocket:
            data = websocket.receive_json()
            assert data["speed"] == 155.5
            assert data["carry"] == 285.0
            assert data["message"] == "Perfect strike!"

    def test_multiple_websocket_clients(self, client, server_instance):
        """Test multiple WebSocket clients can connect"""
        server_instance.connection_manager._connections.clear()

        with client.websocket_connect("/ws") as ws1:
            ws1.receive_json()
            assert server_instance.connection_manager.connection_count == 1

            with client.websocket_connect("/ws") as ws2:
                ws2.receive_json()
                assert server_instance.connection_manager.connection_count == 2

                with client.websocket_connect("/ws") as ws3:
                    ws3.receive_json()
                    assert server_instance.connection_manager.connection_count == 3

                # ws3 disconnected
                assert server_instance.connection_manager.connection_count == 2

            # ws2 disconnected
            assert server_instance.connection_manager.connection_count == 1

        # ws1 disconnected
        assert server_instance.connection_manager.connection_count == 0

    def test_websocket_disconnection_handling(self, client, server_instance):
        """Test WebSocket disconnection is handled properly"""
        server_instance.connection_manager._connections.clear()

        with client.websocket_connect("/ws") as websocket:
            websocket.receive_json()
            assert server_instance.connection_manager.connection_count == 1

        assert server_instance.connection_manager.connection_count == 0

    def test_websocket_reconnection(self, client, server_instance):
        """Test WebSocket reconnection scenario"""
        server_instance.connection_manager._connections.clear()

        shot1 = ShotData(speed=100.0)
        server_instance.shot_store.update(shot1)

        with client.websocket_connect("/ws") as ws1:
            data1 = ws1.receive_json()
            assert "speed" in data1
            assert data1["speed"] == 100.0

        shot2 = ShotData(speed=200.0)
        server_instance.shot_store.update(shot2)

        with client.websocket_connect("/ws") as ws2:
            data2 = ws2.receive_json()
            assert "speed" in data2
            assert data2["speed"] == 200.0


@pytest.mark.asyncio
@pytest.mark.websocket
class TestWebSocketAsync:
    """Async WebSocket tests for real-time shot updates"""

    async def test_websocket_broadcast(self, server_instance, shot_data_instance):
        """Test broadcasting to WebSocket clients"""
        mock_ws = AsyncMock()
        mock_ws.send_json = AsyncMock()

        server_instance.connection_manager._connections.add(mock_ws)

        await server_instance.connection_manager.broadcast(shot_data_instance.to_dict())

        mock_ws.send_json.assert_called_once()
        call_args = mock_ws.send_json.call_args[0][0]
        assert call_args["speed"] == 145.5
        assert call_args["carry"] == 265.3

    async def test_websocket_broadcast_to_multiple_clients(
        self, server_instance, shot_data_instance
    ):
        """Test that updates are broadcast to all connected clients"""
        mock_ws1 = AsyncMock()
        mock_ws1.send_json = AsyncMock()
        mock_ws2 = AsyncMock()
        mock_ws2.send_json = AsyncMock()
        mock_ws3 = AsyncMock()
        mock_ws3.send_json = AsyncMock()

        server_instance.connection_manager._connections.add(mock_ws1)
        server_instance.connection_manager._connections.add(mock_ws2)
        server_instance.connection_manager._connections.add(mock_ws3)

        await server_instance.connection_manager.broadcast(shot_data_instance.to_dict())

        # All clients should receive the update
        mock_ws1.send_json.assert_called_once()
        mock_ws2.send_json.assert_called_once()
        mock_ws3.send_json.assert_called_once()

        # Verify the data sent
        for mock_ws in [mock_ws1, mock_ws2, mock_ws3]:
            call_args = mock_ws.send_json.call_args[0][0]
            assert call_args["speed"] == 145.5
            assert call_args["carry"] == 265.3

    async def test_websocket_failed_client_removed(
        self, server_instance, shot_data_instance
    ):
        """Test that failed clients are removed from the list"""
        mock_ws_good = AsyncMock()
        mock_ws_good.send_json = AsyncMock()

        mock_ws_bad = AsyncMock()
        mock_ws_bad.send_json = AsyncMock(side_effect=Exception("Connection lost"))

        server_instance.connection_manager._connections.add(mock_ws_good)
        server_instance.connection_manager._connections.add(mock_ws_bad)

        assert server_instance.connection_manager.connection_count == 2

        await server_instance.connection_manager.broadcast(shot_data_instance.to_dict())

        # Good client should receive the update
        mock_ws_good.send_json.assert_called_once()

        # Bad client should be removed
        assert server_instance.connection_manager.connection_count == 1
        assert mock_ws_bad not in server_instance.connection_manager._connections
        assert mock_ws_good in server_instance.connection_manager._connections

    async def test_connection_manager_thread_safety(self, connection_manager):
        """Test ConnectionManager thread safety"""
        mock_ws1 = AsyncMock()
        mock_ws2 = AsyncMock()

        # Test concurrent operations
        await asyncio.gather(
            connection_manager.connect(mock_ws1), connection_manager.connect(mock_ws2)
        )

        assert connection_manager.connection_count == 2

        # Test concurrent disconnect
        connection_manager.disconnect(mock_ws1)
        connection_manager.disconnect(mock_ws2)

        assert connection_manager.connection_count == 0

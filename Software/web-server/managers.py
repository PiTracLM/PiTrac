import logging
from threading import Lock
from typing import Any, Dict, List, Set

from fastapi import WebSocket

from models import ShotData

logger = logging.getLogger(__name__)


class ConnectionManager:
    def __init__(self):
        self._connections: Set[WebSocket] = set()
        self._lock = Lock()

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        with self._lock:
            self._connections.add(websocket)
        logger.info(f"WebSocket connected. Total connections: {len(self._connections)}")

    def disconnect(self, websocket: WebSocket) -> None:
        with self._lock:
            self._connections.discard(websocket)
        logger.info(
            f"WebSocket disconnected. Total connections: {len(self._connections)}"
        )

    async def send_personal(self, data: Dict[str, Any], websocket: WebSocket) -> bool:
        try:
            await websocket.send_json(data)
            return True
        except Exception as e:
            logger.warning(f"Failed to send to websocket: {e}")
            self.disconnect(websocket)
            return False

    async def broadcast(self, data: Dict[str, Any]) -> None:
        with self._lock:
            connections = list(self._connections)

        disconnected = []
        for websocket in connections:
            try:
                await websocket.send_json(data)
            except Exception as e:
                logger.warning(f"Failed to send to websocket: {e}")
                disconnected.append(websocket)

        for websocket in disconnected:
            self.disconnect(websocket)

    @property
    def connection_count(self) -> int:
        """Get current number of connections"""
        with self._lock:
            return len(self._connections)

    @property
    def connections(self) -> List[WebSocket]:
        with self._lock:
            return list(self._connections)


class ShotDataStore:
    def __init__(self):
        self._current_shot = ShotData()
        self._lock = Lock()
        self._history: List[ShotData] = []
        self._max_history = 100

    def update(self, shot_data: ShotData) -> None:
        with self._lock:
            self._current_shot = shot_data
            self._add_to_history(shot_data)

    def get(self) -> ShotData:
        with self._lock:
            return self._current_shot

    def reset(self) -> ShotData:
        with self._lock:
            self._current_shot = ShotData()
            return self._current_shot

    def _add_to_history(self, shot_data: ShotData) -> None:
        if shot_data.result_type == "Hit":
            self._history.append(shot_data)
            if len(self._history) > self._max_history:
                self._history.pop(0)

    def get_history(self, limit: int = 10) -> List[ShotData]:
        with self._lock:
            return self._history[-limit:]

    def clear_history(self) -> None:
        with self._lock:
            self._history.clear()

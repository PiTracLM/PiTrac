from __future__ import annotations

import logging
from abc import ABC, abstractmethod
from typing import Awaitable, Callable, Dict, Optional

from models import ShotData

logger = logging.getLogger(__name__)

StatusCallback = Callable[[], Awaitable[None]]

STATUS_OFF = "off"
STATUS_CONNECTING = "connecting"
STATUS_CONNECTED = "connected"
STATUS_ERROR = "error"


class SimInterface(ABC):
    """Base for a single simulator output target.

    A sim holds its own connection and converts a ShotData into whatever its
    simulator expects. SimManager owns the collection and the status broadcast.
    """

    name = "sim"
    display_name = "Simulator"

    def __init__(self) -> None:
        self._status = STATUS_OFF
        self._detail = ""
        self._status_callback: Optional[StatusCallback] = None

    def set_status_callback(self, callback: StatusCallback) -> None:
        self._status_callback = callback

    @property
    def status(self) -> str:
        return self._status

    async def _set_status(self, status: str, detail: str = "") -> None:
        if status == self._status and detail == self._detail:
            return
        self._status = status
        self._detail = detail
        if self._status_callback is not None:
            try:
                await self._status_callback()
            except Exception as e:
                logger.warning(f"sim status callback failed: {e}")

    def info(self) -> Dict[str, str]:
        return {
            "name": self.name,
            "display_name": self.display_name,
            "status": self._status,
            "detail": self._detail,
        }

    @abstractmethod
    async def connect(self) -> None:
        ...

    @abstractmethod
    async def disconnect(self) -> None:
        ...

    @abstractmethod
    async def send_shot(self, shot: ShotData) -> None:
        ...

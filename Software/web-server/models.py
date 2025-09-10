from dataclasses import dataclass, field, asdict
from datetime import datetime
from enum import Enum
from typing import Any, Dict, List, Optional


class ResultType(Enum):
    UNKNOWN = 0
    INITIALIZING = 1
    WAITING_FOR_BALL = 2
    WAITING_FOR_SIMULATOR = 3
    PAUSING_FOR_STABILIZATION = 4
    MULTIPLE_BALLS = 5
    BALL_READY = 6
    HIT = 7
    ERROR = 8
    CALIBRATION = 9


@dataclass
class ShotData:
    speed: float = 0.0
    carry: float = 0.0
    launch_angle: float = 0.0
    side_angle: float = 0.0
    back_spin: int = 0
    side_spin: int = 0
    result_type: str = "Waiting for ball..."
    message: str = ""
    timestamp: Optional[str] = None
    images: List[str] = field(default_factory=list)
    
    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "ShotData":
        return cls(**data)
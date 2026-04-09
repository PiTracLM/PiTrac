import logging
from datetime import datetime
from typing import Any, Dict, List

from constants import MPS_TO_MPH
from models import ResultType, ShotData

logger = logging.getLogger(__name__)


class ShotDataParser:

    @staticmethod
    def _get_status_message_strings() -> List[str]:
        """Get all strings that represent status messages (not shot data)"""
        return [
            "Ball Placed",  # kBallPlacedAndReadyForHit - C++ actual string
            "Ball Ready",  # Legacy support for old enum conversion
            "Initializing",
            "Waiting For Ball",
            "Waiting For Simulator",
            "Waiting For Placement To Stabilize",  # C++ actual string
            "Pausing For Stabilization",  # Legacy support
            "Multiple Balls Present",  # C++ actual string
            "Multiple Balls",  # Legacy support
            "Error",
            "Calibration Results",  # C++ actual string
            "Calibration",  # Legacy support
            "Unknown",
            "Control Message",
        ]

    @staticmethod
    def _get_result_type_string(result_type: int) -> str:
        """Convert result type integer to the exact string that C++ FormatResultType sends"""
        cpp_string_mapping = {
            0: "Unknown",
            1: "Initializing",
            2: "Waiting For Ball",
            3: "Waiting For Simulator",
            4: "Waiting For Placement To Stabilize",
            5: "Multiple Balls Present",
            6: "Ball Placed",
            7: "Hit",
            8: "Error",
            9: "Calibration Results",
            10: "Control Message",
        }

        if result_type in cpp_string_mapping:
            return cpp_string_mapping[result_type]
        else:
            result_type_enum = ResultType(result_type)
            return result_type_enum.name.replace("_", " ").title()

    @staticmethod
    def parse_dict_format(data: Dict[str, Any], current: ShotData) -> ShotData:
        updates = {}

        if "speed" in data:
            updates["speed"] = round(float(data["speed"]), 1)
        if "carry" in data:
            updates["carry"] = round(float(data["carry"]), 1)
        if "launch_angle" in data:
            updates["launch_angle"] = round(float(data["launch_angle"]), 1)
        if "side_angle" in data:
            updates["side_angle"] = round(float(data["side_angle"]), 1)
        if "back_spin" in data:
            updates["back_spin"] = int(data["back_spin"])
        if "side_spin" in data:
            updates["side_spin"] = int(data["side_spin"])

        if "result_type" in data:
            try:
                result_type_val = data["result_type"]
                if isinstance(result_type_val, int):
                    updates["result_type"] = ShotDataParser._get_result_type_string(result_type_val)
                else:
                    updates["result_type"] = str(result_type_val)
            except ValueError:
                updates["result_type"] = f"Type {data['result_type']}"
                logger.warning(f"Unknown result type: {data['result_type']}")

        if "message" in data:
            updates["message"] = str(data["message"])

        updates["timestamp"] = datetime.now().isoformat()

        current_dict = current.to_dict()
        current_dict.update(updates)
        return ShotData(**current_dict)

    @staticmethod
    def validate_shot_data(shot_data: ShotData) -> bool:
        if shot_data.result_type in ShotDataParser._get_status_message_strings():
            logger.info(f"Validated status message: '{shot_data.result_type}' with message: '{shot_data.message}'")
            return True

        if not 0 <= shot_data.speed <= 250:
            logger.warning(f"Suspicious speed value: {shot_data.speed} mph")
            return False

        if not -90 <= shot_data.launch_angle <= 90:
            logger.warning(f"Suspicious launch angle: {shot_data.launch_angle}°")
            return False

        if not -10000 <= shot_data.back_spin <= 10000:
            logger.warning(f"Suspicious back spin: {shot_data.back_spin} rpm")
            return False

        if not -10000 <= shot_data.side_spin <= 10000:
            logger.warning(f"Suspicious side spin: {shot_data.side_spin} rpm")
            return False

        return True

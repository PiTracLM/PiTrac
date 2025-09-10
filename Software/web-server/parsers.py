import logging
from datetime import datetime
from typing import Any, Dict, List

from constants import EXPECTED_DATA_LENGTH, MPS_TO_MPH
from models import ResultType, ShotData

logger = logging.getLogger(__name__)


class ShotDataParser:
    
    @staticmethod
    def parse_array_format(data: List[Any]) -> ShotData:
        if len(data) < EXPECTED_DATA_LENGTH:
            raise ValueError(f"Expected at least {EXPECTED_DATA_LENGTH} elements, got {len(data)}")
        
        carry_meters = data[0]
        speed_mpers = data[1]
        launch_angle_deg = data[2]
        side_angle_deg = data[3]
        back_spin_rpm = data[4]
        side_spin_rpm = data[5]
        # confidence = data[6]  # Currently unused
        # club_type = data[7]   # Currently unused
        result_type = data[8]
        message = data[9]
        # log_messages = data[10] if len(data) > 10 else []  # Currently unused
        image_file_paths = data[11] if len(data) > 11 else []
        
        try:
            result_type_str = ResultType(result_type).name.replace("_", " ").title()
        except ValueError:
            result_type_str = f"Type {result_type}"
            logger.warning(f"Unknown result type: {result_type}")
        
        return ShotData(
            carry=carry_meters,
            speed=round(speed_mpers * MPS_TO_MPH, 1),
            launch_angle=round(launch_angle_deg, 1),
            side_angle=round(side_angle_deg, 1),
            back_spin=int(back_spin_rpm),
            side_spin=int(side_spin_rpm),
            result_type=result_type_str,
            message=message,
            timestamp=datetime.now().isoformat(),
            images=image_file_paths
        )
    
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
                    updates["result_type"] = ResultType(result_type_val).name.replace("_", " ").title()
                else:
                    updates["result_type"] = str(result_type_val)
            except ValueError:
                updates["result_type"] = f"Type {data['result_type']}"
                logger.warning(f"Unknown result type: {data['result_type']}")
                
        if "message" in data:
            updates["message"] = str(data["message"])
        if "image_paths" in data:
            updates["images"] = list(data["image_paths"])
        
        updates["timestamp"] = datetime.now().isoformat()
        
        current_dict = current.to_dict()
        current_dict.update(updates)
        return ShotData(**current_dict)
    
    @staticmethod
    def validate_shot_data(shot_data: ShotData) -> bool:

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
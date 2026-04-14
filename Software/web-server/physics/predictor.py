from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, Optional

import numpy as np

from .ml_inference import MlpEnsemble
from .roll import simulate_roll
from .trajectory import MPS_TO_MPH, M_TO_FT, M_TO_YD, simulate

_MODELS_DIR = Path(__file__).parent / "models"


def _downsample(arr: np.ndarray, n_target: int):
    n = len(arr)
    if n <= n_target:
        return arr, np.arange(n)
    idx = np.linspace(0, n - 1, n_target).astype(int)
    return arr[idx], idx


def predict_shot(
    ball_speed_mph: float,
    launch_angle_deg: float,
    spin_rate_rpm: float,
    spin_axis_deg: float,
    launch_direction_deg: float = 0.0,
    n_path_samples: int = 120,
) -> Dict[str, Any]:
    flight = simulate(
        ball_speed_mph=ball_speed_mph,
        launch_angle_deg=launch_angle_deg,
        spin_rate_rpm=spin_rate_rpm,
        spin_axis_deg=spin_axis_deg,
        launch_direction_deg=launch_direction_deg,
    )

    flight_path, sel = _downsample(flight.path_m, n_path_samples)
    flight_t = flight.t_s[sel]
    flight_pts_yd = flight_path * M_TO_YD

    landing_pos_m = flight.path_m[-1]
    landing_v_mps = flight.velocity_m[-1]
    landing_spin_rpm = float(np.linalg.norm(flight.spin_radps[-1])) * 60.0 / (2 * np.pi)

    roll_pts_m, roll_t = simulate_roll(landing_pos_m, landing_v_mps, landing_spin_rpm)
    roll_pts_yd = roll_pts_m * M_TO_YD
    roll_t_offset = float(flight_t[-1]) + roll_t

    if len(roll_pts_yd) > 0:
        path_yd = np.vstack([flight_pts_yd, roll_pts_yd])
        t_s = np.concatenate([flight_t, roll_t_offset])
        flight_last = len(flight_pts_yd) - 1
        final = roll_pts_yd[-1]
        roll_yd = float(np.linalg.norm(final[:2] - flight_pts_yd[-1, :2]))
    else:
        path_yd = flight_pts_yd
        t_s = flight_t
        flight_last = len(flight_pts_yd) - 1
        final = flight_pts_yd[-1]
        roll_yd = 0.0

    apex_idx = int(np.argmax(flight_pts_yd[:, 2]))
    apex = flight_pts_yd[apex_idx]

    carry_yd = float(flight_pts_yd[-1, 1])
    carry_side_yd = float(flight_pts_yd[-1, 0])
    total_yd = float(final[1])
    side_total_yd = float(final[0])

    vxy = float(np.hypot(landing_v_mps[0], landing_v_mps[1]))
    landing_angle_deg = float(np.degrees(np.arctan2(-landing_v_mps[2], max(vxy, 1e-9))))
    landing_speed_mph = float(np.linalg.norm(landing_v_mps)) * MPS_TO_MPH

    return {
        "path_yd": path_yd.tolist(),
        "t_s": t_s.tolist(),
        "apex_index": apex_idx,
        "flight_last_index": flight_last,
        "metrics": {
            "carry_yd": round(carry_yd, 1),
            "carry_side_yd": round(carry_side_yd, 1),
            "apex_ft": round(apex[2] * (M_TO_FT / M_TO_YD), 1),
            "landing_angle_deg": round(landing_angle_deg, 1),
            "landing_speed_mph": round(landing_speed_mph, 1),
            "flight_time_s": round(float(flight_t[-1]), 2),
            "roll_yd": round(roll_yd, 1),
            "total_yd": round(total_yd, 1),
            "side_total_yd": round(side_total_yd, 1),
        },
    }


class ShotPredictor:
    def __init__(self, models_dir: Optional[Path] = None):
        models_dir = models_dir or _MODELS_DIR
        self.flight_model: Optional[MlpEnsemble] = None
        self.roll_model: Optional[MlpEnsemble] = None
        flight_path = models_dir / "flight_ensemble.npz"
        roll_path = models_dir / "roll_ensemble.npz"
        if flight_path.exists():
            self.flight_model = MlpEnsemble(flight_path)
        if roll_path.exists():
            self.roll_model = MlpEnsemble(roll_path)

    def predict(
        self,
        ball_speed_mph: float,
        launch_angle_deg: float,
        back_spin_rpm: float,
        side_spin_rpm: float,
        launch_direction_deg: float = 0.0,
    ) -> Dict[str, Any]:
        spin_rate = float(np.hypot(back_spin_rpm, side_spin_rpm))
        spin_axis = float(np.degrees(np.arctan2(side_spin_rpm, back_spin_rpm)))

        result = predict_shot(
            ball_speed_mph=ball_speed_mph,
            launch_angle_deg=launch_angle_deg,
            spin_rate_rpm=spin_rate,
            spin_axis_deg=spin_axis,
            launch_direction_deg=launch_direction_deg,
        )

        if self.flight_model is not None:
            ml_flight = self.flight_model.predict({
                "ball_speed_mph": ball_speed_mph,
                "launch_angle_deg": launch_angle_deg,
                "spin_rate_rpm": spin_rate,
                "spin_axis_deg": spin_axis,
                "launch_direction_deg": launch_direction_deg,
            })
            for key in ("carry_len_yd", "carry_side_yd", "apex_height_ft",
                        "landing_angle_deg", "flight_time_s", "landing_speed_mph"):
                if key in ml_flight:
                    out_key = "carry_yd" if key == "carry_len_yd" else key
                    if out_key == "carry_yd":
                        result["metrics"]["carry_yd"] = round(ml_flight[key], 1)
                    elif out_key in ("carry_side_yd",):
                        result["metrics"]["carry_side_yd"] = round(ml_flight[key], 1)
                    elif out_key in ("apex_height_ft",):
                        result["metrics"]["apex_ft"] = round(ml_flight[key], 1)
                    else:
                        result["metrics"][out_key] = round(ml_flight[key], 1) if key != "flight_time_s" else round(ml_flight[key], 2)

            if self.roll_model is not None and "landing_spin_rpm" in ml_flight:
                ml_roll = self.roll_model.predict({
                    "carry_len_yd": ml_flight.get("carry_len_yd", result["metrics"]["carry_yd"]),
                    "landing_speed_mph": ml_flight.get("landing_speed_mph", result["metrics"]["landing_speed_mph"]),
                    "landing_angle_deg": ml_flight.get("landing_angle_deg", result["metrics"]["landing_angle_deg"]),
                    "landing_spin_rpm": ml_flight["landing_spin_rpm"],
                })
                result["metrics"]["roll_yd"] = round(ml_roll["roll_yd"], 1)
                result["metrics"]["total_yd"] = round(result["metrics"]["carry_yd"] + ml_roll["roll_yd"], 1)
                result["metrics"]["side_total_yd"] = round(
                    result["metrics"]["carry_side_yd"] + ml_roll.get("side_drift_yd", 0.0), 1,
                )

        return result

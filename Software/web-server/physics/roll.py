import numpy as np

from .trajectory import G, M_TO_YD


def simulate_roll(
    landing_pos_m: np.ndarray,
    landing_v_mps: np.ndarray,
    landing_spin_rpm: float,
    n_samples: int = 30,
    mu: float = 0.15,
):
    v_h_vec = np.array([landing_v_mps[0], landing_v_mps[1], 0.0])
    v_h = float(np.linalg.norm(v_h_vec))
    if v_h < 1e-3:
        return np.empty((0, 3)), np.empty(0)
    dir_h = v_h_vec / v_h

    v_z = abs(landing_v_mps[2])
    land_angle_deg = np.degrees(np.arctan2(v_z, v_h))

    cor = float(np.clip(0.70 - land_angle_deg / 120.0, 0.25, 0.70))
    spin_factor = float(np.clip(1.0 - landing_spin_rpm / 14000.0, 0.20, 1.0))
    v_roll = v_h * cor * spin_factor

    t_stop = v_roll / (mu * G)
    if t_stop <= 0:
        return np.empty((0, 3)), np.empty(0)

    ts = np.linspace(0.0, t_stop, n_samples)
    dist_m = v_roll * ts - 0.5 * mu * G * ts * ts

    start = landing_pos_m.copy()
    start[2] = 0.0
    pts = np.stack([start + dir_h * d for d in dist_m], axis=0)
    pts[:, 2] = 0.0
    return pts, ts

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .calibrated_params import AeroParams, CALIBRATED


MPH_TO_MPS = 0.44704
MPS_TO_MPH = 1.0 / MPH_TO_MPS
RPM_TO_RADPS = 2.0 * np.pi / 60.0
M_TO_YD = 1.0936133
M_TO_FT = 3.28084
DEG = np.pi / 180.0

BALL_MASS_KG = 0.04593
BALL_RADIUS_M = 0.021335
BALL_AREA_M2 = np.pi * BALL_RADIUS_M ** 2
BALL_DIAM_M = 2.0 * BALL_RADIUS_M
BALL_INERTIA = (2.0 / 5.0) * BALL_MASS_KG * BALL_RADIUS_M ** 2
RHO = 1.225
G = 9.81


@dataclass
class Flight:
    path_m: np.ndarray        # (N, 3)
    velocity_m: np.ndarray    # (N, 3)
    spin_radps: np.ndarray    # (N, 3)
    t_s: np.ndarray           # (N,)


def _coefficients(speed, omega_mag, p: AeroParams):
    S = BALL_RADIUS_M * omega_mag / max(speed, 1e-6)
    Cl = np.clip(p.p1 + p.p2 * S + p.p3 * S * S, 0.0, 0.5)
    Cd = np.clip(p.p4 + p.p5 * S + p.p6 * S * S, 0.0, 0.5)
    Cm = np.clip(p.p7 * S, 0.0, 0.02)
    return Cl, Cd, Cm


def _deriv(state, p):
    v = state[3:6]
    omega = state[6:9]
    speed = float(np.linalg.norm(v))
    omega_mag = float(np.linalg.norm(omega))
    if speed < 1e-9:
        return np.concatenate([v, np.array([0.0, 0.0, -G]), np.zeros(3)])

    Cl, Cd, Cm = _coefficients(speed, omega_mag, p)
    q = 0.5 * RHO * speed * speed
    qA = q * BALL_AREA_M2

    v_hat = v / speed
    F_drag = -Cd * qA * v_hat

    cross = np.cross(omega, v)
    cross_mag = float(np.linalg.norm(cross))
    if cross_mag > 1e-9 and omega_mag > 1e-9:
        F_lift = Cl * qA * cross / cross_mag
    else:
        F_lift = np.zeros(3)

    F_gravity = np.array([0.0, 0.0, -BALL_MASS_KG * G])
    dv = (F_drag + F_lift + F_gravity) / BALL_MASS_KG

    if omega_mag > 1e-9:
        torque = -Cm * q * BALL_DIAM_M * BALL_AREA_M2 * (omega / omega_mag)
        domega = torque / BALL_INERTIA
    else:
        domega = np.zeros(3)

    return np.concatenate([v, dv, domega])


def _rk4_step(state, dt, p):
    k1 = _deriv(state, p)
    k2 = _deriv(state + 0.5 * dt * k1, p)
    k3 = _deriv(state + 0.5 * dt * k2, p)
    k4 = _deriv(state + dt * k3, p)
    return state + (dt / 6.0) * (k1 + 2 * k2 + 2 * k3 + k4)


def simulate(
    ball_speed_mph: float,
    launch_angle_deg: float,
    spin_rate_rpm: float,
    spin_axis_deg: float = 0.0,
    launch_direction_deg: float = 0.0,
    aero: AeroParams = CALIBRATED,
    dt: float = 0.01,
    t_max: float = 12.0,
) -> Flight:
    v0 = ball_speed_mph * MPH_TO_MPS
    la = launch_angle_deg * DEG
    ld = launch_direction_deg * DEG

    v = np.array([
        v0 * np.cos(la) * np.sin(ld),
        v0 * np.cos(la) * np.cos(ld),
        v0 * np.sin(la),
    ])

    omega_mag = spin_rate_rpm * RPM_TO_RADPS
    psi = spin_axis_deg * DEG
    omega = omega_mag * np.array([np.cos(psi), 0.0, -np.sin(psi)])

    r = np.array([0.0, 0.0, 0.01])
    state = np.concatenate([r, v, omega])

    n_max = int(t_max / dt) + 1
    r_hist = np.empty((n_max, 3))
    v_hist = np.empty((n_max, 3))
    w_hist = np.empty((n_max, 3))
    t_hist = np.empty(n_max)
    r_hist[0] = state[0:3]; v_hist[0] = state[3:6]; w_hist[0] = state[6:9]; t_hist[0] = 0.0

    count = 1
    for i in range(1, n_max):
        prev = state
        state = _rk4_step(state, dt, aero)
        if state[2] <= 0.0 and prev[2] > 0.0:
            frac = prev[2] / (prev[2] - state[2])
            state = prev + frac * (state - prev)
            r_hist[count] = state[0:3]
            v_hist[count] = state[3:6]
            w_hist[count] = state[6:9]
            t_hist[count] = (i - 1 + frac) * dt
            count += 1
            break
        r_hist[count] = state[0:3]
        v_hist[count] = state[3:6]
        w_hist[count] = state[6:9]
        t_hist[count] = i * dt
        count += 1

    return Flight(
        path_m=r_hist[:count],
        velocity_m=v_hist[:count],
        spin_radps=w_hist[:count],
        t_s=t_hist[:count],
    )

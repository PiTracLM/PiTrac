"""Physics-based shot trajectory predictor for PiTrac.

Runs a Nathan-style aerodynamic ODE on the Pi 5 in pure numpy (no scipy, no
torch). Given launch monitor measurements it produces a full 3D trajectory
for the /sim visualization and derived metrics (carry, apex, roll, total).

Calibration was performed on 8873 real TrackMan shots; see the
Machine-Learning sibling repo for the calibration pipeline.
"""
from .predictor import ShotPredictor, predict_shot  # noqa: F401

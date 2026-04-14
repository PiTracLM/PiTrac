from __future__ import annotations

from pathlib import Path

import numpy as np


class MlpEnsemble:
    def __init__(self, path: Path):
        blob = np.load(path, allow_pickle=True)
        self.n_models = int(blob["n_models"][0])
        self.feature_names = [str(x) for x in blob["feature_names"]]
        self.target_names = [str(x) for x in blob["target_names"]]
        self.x_mean = blob["x_mean"].astype(np.float32)
        self.x_scale = blob["x_scale"].astype(np.float32)
        self.y_mean = blob["y_mean"].astype(np.float32)
        self.y_scale = blob["y_scale"].astype(np.float32)

        self.members = []
        for mi in range(self.n_models):
            n_linear = int(blob[f"m{mi}_n_linear"][0])
            layers = []
            for li in range(n_linear):
                W = blob[f"m{mi}_W{li}"].astype(np.float32)
                b = blob[f"m{mi}_b{li}"].astype(np.float32)
                layers.append((W, b))
            self.members.append(layers)

    def _forward_one(self, x: np.ndarray, layers) -> np.ndarray:
        h = x
        last = len(layers) - 1
        for i, (W, b) in enumerate(layers):
            h = h @ W.T + b
            if i < last:
                np.maximum(h, 0.0, out=h)
        return h

    def predict(self, features: dict) -> dict:
        x = np.array([float(features[n]) for n in self.feature_names], dtype=np.float32)
        x_norm = (x - self.x_mean) / self.x_scale
        acc = np.zeros(len(self.target_names), dtype=np.float32)
        for layers in self.members:
            acc += self._forward_one(x_norm, layers)
        acc /= float(self.n_models)
        y = acc * self.y_scale + self.y_mean
        return {n: float(v) for n, v in zip(self.target_names, y)}

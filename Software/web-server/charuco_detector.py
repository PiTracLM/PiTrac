"""ChArUco board detection for lens distortion calibration.

Requires OpenCV >= 4.7.0 (objdetect-based aruco API).
"""

import cv2
import logging
import numpy as np
from typing import Any, Dict, List, Optional, Tuple

logger = logging.getLogger(__name__)

MIN_OPENCV_VERSION = (4, 7, 0)


def _check_opencv_version() -> None:
    """Verify OpenCV version meets minimum requirement."""
    parts = cv2.__version__.split('.')[:3]
    version = tuple(int(p.split('-')[0]) for p in parts)
    if version < MIN_OPENCV_VERSION:
        raise RuntimeError(
            f"OpenCV >= {'.'.join(map(str, MIN_OPENCV_VERSION))} required, "
            f"got {cv2.__version__}"
        )


_check_opencv_version()


class CoverageTracker:
    """Tracks spatial coverage of calibration images across a grid."""

    def __init__(self, image_width: int, image_height: int,
                 grid_rows: int = 3, grid_cols: int = 3):
        self.grid_rows = grid_rows
        self.grid_cols = grid_cols
        self.image_width = image_width
        self.image_height = image_height
        self.cell_width = image_width / grid_cols
        self.cell_height = image_height / grid_rows
        self.coverage = [[0] * grid_cols for _ in range(grid_rows)]

    def update(self, corners: np.ndarray) -> None:
        """Update coverage grid based on the center of detected corners."""
        if corners is None or len(corners) == 0:
            return
        x_coords = corners[:, 0, 0]
        y_coords = corners[:, 0, 1]
        cx = (x_coords.min() + x_coords.max()) / 2
        cy = (y_coords.min() + y_coords.max()) / 2
        col = min(int(cx / self.cell_width), self.grid_cols - 1)
        row = min(int(cy / self.cell_height), self.grid_rows - 1)
        self.coverage[row][col] += 1

    def get_coverage_fraction(self) -> float:
        """Return fraction of grid cells with at least one image."""
        covered = sum(1 for row in self.coverage for cell in row if cell > 0)
        return covered / (self.grid_rows * self.grid_cols)

    def get_suggested_region(self) -> str:
        """Suggest which region needs more coverage."""
        min_count = float('inf')
        min_row, min_col = 0, 0
        for r in range(self.grid_rows):
            for c in range(self.grid_cols):
                if self.coverage[r][c] < min_count:
                    min_count = self.coverage[r][c]
                    min_row, min_col = r, c

        row_labels = ["top", "center", "bottom"]
        col_labels = ["left", "center", "right"]
        row_idx = min(min_row, 2) if self.grid_rows <= 3 else min(min_row * 2 // max(self.grid_rows - 1, 1), 2)
        col_idx = min(min_col, 2) if self.grid_cols <= 3 else min(min_col * 2 // max(self.grid_cols - 1, 1), 2)

        row_name = row_labels[row_idx]
        col_name = col_labels[col_idx]
        if row_name == col_name == "center":
            return "center"
        return f"{row_name}-{col_name}"

    def to_dict(self) -> Dict:
        return {
            "grid": self.coverage,
            "fraction": self.get_coverage_fraction(),
            "suggested_region": self.get_suggested_region(),
        }


class CompatibleCharucoDetector:
    """ChArUco detector using OpenCV >= 4.7 objdetect API."""

    def __init__(
        self,
        squares_x: int,
        squares_y: int,
        square_length: float,
        marker_length: float,
        dict_type: int = cv2.aruco.DICT_4X4_50,
        legacy_pattern: bool = False
    ):
        self.squares_x = squares_x
        self.squares_y = squares_y
        self.square_length = square_length
        self.marker_length = marker_length

        logger.info(f"OpenCV version: {cv2.__version__}")

        self.aruco_dict = cv2.aruco.getPredefinedDictionary(dict_type)
        self.parameters = cv2.aruco.DetectorParameters()

        self.board = cv2.aruco.CharucoBoard(
            (squares_x, squares_y),
            square_length,
            marker_length,
            self.aruco_dict
        )

        if legacy_pattern and hasattr(self.board, 'setLegacyPattern'):
            self.board.setLegacyPattern(True)
            logger.info("Using legacy ChArUco pattern")

        self.charuco_detector = cv2.aruco.CharucoDetector(self.board)

    def detect_charuco_corners(self, gray_image: np.ndarray):
        """Detect ChArUco corners, returning (charuco_corners, charuco_ids, marker_corners, marker_ids)."""
        return self.charuco_detector.detectBoard(gray_image)

    def compute_tilt_score(self, charuco_corners: Optional[np.ndarray]) -> float:
        """Estimate board tilt via observed vs expected aspect ratio.

        Returns:
            0.0 (flat-on) to 1.0 (significant tilt).
        """
        if charuco_corners is None or len(charuco_corners) < 6:
            return 0.0

        corners = charuco_corners[:, 0, :].astype(np.float32)
        rect = cv2.minAreaRect(corners)
        w, h = rect[1]
        if w == 0 or h == 0:
            return 0.0

        observed_ratio = min(w, h) / max(w, h)
        # Internal corner grid is (squares - 1) in each dimension
        expected_ratio = (min(self.squares_x - 1, self.squares_y - 1) /
                          max(self.squares_x - 1, self.squares_y - 1))

        ratio_diff = abs(observed_ratio - expected_ratio)
        return min(1.0, ratio_diff * 3)

    def assess_image_quality(
        self,
        gray_image: np.ndarray,
        charuco_corners: Optional[np.ndarray]
    ) -> Dict[str, Any]:
        """Assess image quality for calibration suitability."""
        metrics: Dict[str, Any] = {
            "is_good": False,
            "num_corners": 0,
            "blur_score": 0.0,
            "coverage": 0.0,
            "edge_margin": 0.0,
            "tilt_score": 0.0,
            "reasons": []
        }

        num_found = 0 if charuco_corners is None else len(charuco_corners)
        metrics["num_corners"] = num_found

        if num_found < 4:
            metrics["reasons"].append(
                f"Insufficient corners detected ({num_found} < 4)")
            return metrics

        # 1. Blur detection (Laplacian variance)
        laplacian = cv2.Laplacian(gray_image, cv2.CV_64F)
        metrics["blur_score"] = laplacian.var()

        blur_threshold = 100
        if metrics["blur_score"] < blur_threshold:
            metrics["reasons"].append(
                f"Image too blurry (score {metrics['blur_score']:.1f} < {blur_threshold})")

        # 2. Coverage check
        x_coords = charuco_corners[:, 0, 0]
        y_coords = charuco_corners[:, 0, 1]

        bbox_area = ((x_coords.max() - x_coords.min()) *
                     (y_coords.max() - y_coords.min()))
        image_area = gray_image.shape[0] * gray_image.shape[1]
        metrics["coverage"] = bbox_area / image_area

        min_coverage = 0.10
        if metrics["coverage"] < min_coverage:
            metrics["reasons"].append(
                f"Board too small (coverage {metrics['coverage']:.1%} < {min_coverage:.0%})")

        # 3. Edge margin check (board not cut off)
        margin_threshold = 20
        h, w = gray_image.shape[:2]

        metrics["edge_margin"] = min(
            x_coords.min(),
            y_coords.min(),
            w - x_coords.max(),
            h - y_coords.max()
        )

        if metrics["edge_margin"] < margin_threshold:
            metrics["reasons"].append(
                f"Board too close to edge (margin {metrics['edge_margin']:.0f}px < {margin_threshold}px)")

        # 4. Tilt assessment
        metrics["tilt_score"] = self.compute_tilt_score(charuco_corners)

        # Overall assessment
        metrics["is_good"] = (
            metrics["blur_score"] >= blur_threshold and
            metrics["coverage"] >= min_coverage and
            metrics["edge_margin"] >= margin_threshold
        )

        return metrics

    def calibrate_with_outlier_rejection(
        self,
        all_charuco_corners: List[np.ndarray],
        all_charuco_ids: List[np.ndarray],
        image_size: Tuple[int, int],
        fix_k3: bool = False,
        max_rejection_rounds: int = 3,
        improvement_threshold: float = 0.10
    ) -> Tuple[float, np.ndarray, np.ndarray, List[int]]:
        """Calibrate with iterative outlier rejection."""
        corners = list(all_charuco_corners)
        ids = list(all_charuco_ids)
        rejected_indices: List[int] = []
        original_indices = list(range(len(corners)))

        flags = 0
        if fix_k3:
            flags |= cv2.CALIB_FIX_K3

        def _calibrate(c_list, id_list):
            obj_pts_all = []
            img_pts_all = []
            for c, d in zip(c_list, id_list):
                obj_pts, img_pts = self.board.matchImagePoints(c, d)
                if len(obj_pts) == 0:
                    continue
                obj_pts_all.append(obj_pts)
                img_pts_all.append(img_pts)
            return cv2.calibrateCamera(
                obj_pts_all, img_pts_all, image_size, None, None, flags=flags)

        rms, camera_matrix, dist_coeffs, rvecs, tvecs = _calibrate(corners, ids)
        logger.info(f"Initial calibration: RMS={rms:.4f}, images={len(corners)}")

        for round_num in range(max_rejection_rounds):
            if len(corners) <= 5:
                logger.info("Too few images for further outlier rejection")
                break

            # Compute per-image reprojection error
            per_image_errors = []
            for i in range(len(corners)):
                obj_pts, img_pts = self.board.matchImagePoints(corners[i], ids[i])
                if len(obj_pts) == 0:
                    per_image_errors.append(float('inf'))
                    continue
                projected, _ = cv2.projectPoints(
                    obj_pts, rvecs[i], tvecs[i], camera_matrix, dist_coeffs)
                error = cv2.norm(
                    img_pts, projected, cv2.NORM_L2) / len(projected)
                per_image_errors.append(error)

            worst_idx = int(np.argmax(per_image_errors))
            worst_error = per_image_errors[worst_idx]
            median_error = float(np.median(per_image_errors))

            if worst_error < median_error * 2.0:
                logger.info(
                    f"Round {round_num + 1}: No significant outlier "
                    f"(worst={worst_error:.4f}, median={median_error:.4f})")
                break

            test_corners = [c for j, c in enumerate(corners) if j != worst_idx]
            test_ids = [d for j, d in enumerate(ids) if j != worst_idx]

            try:
                test_rms, test_matrix, test_dist, test_rvecs, test_tvecs = \
                    _calibrate(test_corners, test_ids)
            except Exception as e:
                logger.warning(f"Recalibration failed: {e}")
                break

            improvement = (rms - test_rms) / rms
            if improvement >= improvement_threshold:
                logger.info(
                    f"Round {round_num + 1}: Removing image {original_indices[worst_idx]} "
                    f"(error={worst_error:.4f}, RMS: {rms:.4f} -> {test_rms:.4f}, "
                    f"improvement={improvement:.1%})")
                rejected_indices.append(original_indices[worst_idx])
                corners = test_corners
                ids = test_ids
                original_indices = [
                    idx for j, idx in enumerate(original_indices) if j != worst_idx]
                rms = test_rms
                camera_matrix = test_matrix
                dist_coeffs = test_dist
                rvecs = test_rvecs
                tvecs = test_tvecs
            else:
                logger.info(
                    f"Round {round_num + 1}: Rejection would only improve "
                    f"{improvement:.1%} (threshold={improvement_threshold:.0%})")
                break

        logger.info(
            f"Final calibration: RMS={rms:.4f}, "
            f"images={len(corners)}, rejected={len(rejected_indices)}")
        return rms, camera_matrix, dist_coeffs, rejected_indices

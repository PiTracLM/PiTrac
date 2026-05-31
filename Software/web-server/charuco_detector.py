"""ChArUco board detection for lens distortion calibration.

Requires OpenCV >= 4.7.0 (objdetect-based aruco API).
"""

import cv2
import logging
import math
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


BLUR_THRESHOLD = 100        # Laplacian variance on 960-px-normalized image
MIN_COVERAGE = 0.08         # corner bbox area / image area; standard tee distance is ~0.12
EDGE_MARGIN_PX = 10         # below this, cornerSubPix's 11×11 window biases corners inward
CELL_TARGET_SAMPLES = 3


class CoverageTracker:
    """Tracks spatial coverage of calibration images across a grid."""

    def __init__(self, image_width: int, image_height: int,
                 grid_rows: int = 3, grid_cols: int = 3,
                 cell_target: int = CELL_TARGET_SAMPLES):
        self.grid_rows = grid_rows
        self.grid_cols = grid_cols
        self.image_width = image_width
        self.image_height = image_height
        self.cell_width = image_width / grid_cols
        self.cell_height = image_height / grid_rows
        self.cell_target = cell_target
        self.coverage = [[0] * grid_cols for _ in range(grid_rows)]
        self._frame_corners: List[np.ndarray] = []

    def update(self, corners: np.ndarray) -> None:
        if corners is None or len(corners) == 0:
            return
        self._frame_corners.append(corners)
        x_coords = corners[:, 0, 0]
        y_coords = corners[:, 0, 1]
        cx = (x_coords.min() + x_coords.max()) / 2
        cy = (y_coords.min() + y_coords.max()) / 2
        col = min(int(cx / self.cell_width), self.grid_cols - 1)
        row = min(int(cy / self.cell_height), self.grid_rows - 1)
        self.coverage[row][col] += 1

    def get_coverage_fraction(self) -> float:
        """Fraction of 3×3 cells with >= cell_target samples (UX coaching signal)."""
        covered = sum(
            1 for row in self.coverage for cell in row if cell >= self.cell_target
        )
        return covered / (self.grid_rows * self.grid_cols)

    def _pixel_cell_counts(self, grid_size: int = 10) -> np.ndarray:
        x_step = self.image_width / grid_size
        y_step = self.image_height / grid_size
        cells = np.zeros((grid_size, grid_size), dtype=np.int32)
        for frame_corners in self._frame_corners:
            for pt in frame_corners[:, 0, :]:
                c = min(int(pt[0] / x_step), grid_size - 1)
                r = min(int(pt[1] / y_step), grid_size - 1)
                cells[r][c] += 1
        return cells

    def get_pixel_coverage_fraction(self, grid_size: int = 10) -> float:
        """Fraction of fine-grid cells with >=1 detected corner across all frames."""
        cells = self._pixel_cell_counts(grid_size)
        return float(np.count_nonzero(cells)) / (grid_size * grid_size)

    def estimate_coverage_quality(
        self, active_indices: Optional[List[int]] = None,
        exclude_index: int = -1
    ) -> float:
        """Coverage uniformity over a 10x10 grid.

        Returns mean / (stddev + eps) of per-cell point counts.
        Higher values indicate more uniform spatial distribution.
        """
        grid_size = 10
        x_step = self.image_width / grid_size
        y_step = self.image_height / grid_size
        cells = np.zeros((grid_size, grid_size), dtype=np.float64)

        indices = active_indices if active_indices is not None else range(len(self._frame_corners))
        for i in indices:
            if i == exclude_index or i >= len(self._frame_corners):
                continue
            for pt in self._frame_corners[i][:, 0, :]:
                c = min(int(pt[0] / x_step), grid_size - 1)
                r = min(int(pt[1] / y_step), grid_size - 1)
                cells[r][c] += 1

        flat = cells.flatten()
        mean_val = flat.mean()
        std_val = flat.std()
        return mean_val / (std_val + 1e-7)

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
            "cell_target": self.cell_target,
            "pixel_coverage": self.get_pixel_coverage_fraction(),
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
        # Scale so that meaningful tilts (15-45 degrees) produce scores
        # in the 0.20-0.80 range
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

        h, w = gray_image.shape[:2]
        if h < 2 or w < 2:
            metrics["reasons"].append(f"Invalid image dimensions ({w}x{h})")
            return metrics

        num_found = 0 if charuco_corners is None else len(charuco_corners)
        metrics["num_corners"] = num_found

        if num_found < 4:
            metrics["reasons"].append(
                f"Insufficient corners detected ({num_found} < 4)")
            return metrics

        # Normalize to 960px before computing Laplacian so the score
        # doesn't swing wildly between preview and full-res capture.
        BLUR_REF_WIDTH = 960
        if gray_image.shape[1] != BLUR_REF_WIDTH:
            s = BLUR_REF_WIDTH / gray_image.shape[1]
            blur_img = cv2.resize(gray_image, (BLUR_REF_WIDTH, int(gray_image.shape[0] * s)))
        else:
            blur_img = gray_image
        laplacian = cv2.Laplacian(blur_img, cv2.CV_64F)
        metrics["blur_score"] = laplacian.var()

        blur_threshold = BLUR_THRESHOLD
        if metrics["blur_score"] < blur_threshold:
            metrics["reasons"].append(
                f"Image too blurry (score {metrics['blur_score']:.2f} < {blur_threshold})")

        # 2. Coverage check
        x_coords = charuco_corners[:, 0, 0]
        y_coords = charuco_corners[:, 0, 1]

        bbox_area = ((x_coords.max() - x_coords.min()) *
                     (y_coords.max() - y_coords.min()))
        image_area = gray_image.shape[0] * gray_image.shape[1]
        metrics["coverage"] = bbox_area / image_area

        min_coverage = MIN_COVERAGE
        if metrics["coverage"] < min_coverage:
            metrics["reasons"].append(
                f"Board too small (coverage {metrics['coverage']:.1%} < {min_coverage:.0%})")

        # 3. Edge margin check (board not cut off)
        margin_threshold = EDGE_MARGIN_PX
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

    def compute_image_params(
        self, charuco_corners: np.ndarray, image_size: Tuple[int, int]
    ) -> Optional[List[float]]:
        """4D diversity descriptor [X, Y, Size, Skew] normalized to [0, 1].

        X and Y are board center positions with an inset so large boards
        aren't penalized for limited position range. Size is the square
        root of the area fraction. Skew is the tilt score.
        """
        if charuco_corners is None or len(charuco_corners) < 4:
            return None

        w, h = image_size
        pts = charuco_corners[:, 0, :]
        x_min, x_max = float(pts[:, 0].min()), float(pts[:, 0].max())
        y_min, y_max = float(pts[:, 1].min()), float(pts[:, 1].max())
        area = (x_max - x_min) * (y_max - y_min)
        border = math.sqrt(area)

        cx = (x_min + x_max) / 2
        cy = (y_min + y_max) / 2
        p_x = max(0.0, min(1.0, (cx - border / 2) / max(w - border, 1)))
        p_y = max(0.0, min(1.0, (cy - border / 2) / max(h - border, 1)))
        p_size = math.sqrt(area / (w * h))
        p_skew = self.compute_tilt_score(charuco_corners)
        return [p_x, p_y, p_size, p_skew]

    @staticmethod
    def is_good_sample(
        params: List[float],
        existing_params: List[List[float]],
        min_distance: float = 0.2
    ) -> bool:
        """True if the sample is sufficiently different from all existing ones.

        Uses L1 distance in the 4D parameter space with a threshold of 0.2.
        """
        if not existing_params:
            return True
        for p in existing_params:
            if sum(abs(a - b) for a, b in zip(params, p)) <= min_distance:
                return False
        return True

    def calibrate_with_filtering(
        self,
        all_charuco_corners: List[np.ndarray],
        all_charuco_ids: List[np.ndarray],
        image_size: Tuple[int, int],
        coverage_tracker: Optional['CoverageTracker'] = None,
        fix_k3: bool = False,
    ) -> Tuple[float, np.ndarray, np.ndarray, List[int], Dict[str, Any]]:
        """Bicriterial filter (alpha=0.1: 90% coverage, 10% per-frame error). Returns (rms, K, dist, rejected, diagnostics)."""
        corners = list(all_charuco_corners)
        ids = list(all_charuco_ids)
        original_indices = list(range(len(corners)))
        rejected_indices: List[int] = []

        flags = 0
        if fix_k3:
            flags |= cv2.CALIB_FIX_K3

        def _run_calibration(c_list, id_list):
            obj_all, img_all = [], []
            for c, d in zip(c_list, id_list):
                obj_pts, img_pts = self.board.matchImagePoints(c, d)
                if len(obj_pts) == 0:
                    continue
                obj_all.append(obj_pts)
                img_all.append(img_pts)
            return cv2.calibrateCameraExtended(
                obj_all, img_all, image_size, None, None, flags=flags)

        (rms, camera_matrix, dist_coeffs, rvecs, tvecs,
         std_intrinsics, std_extrinsics, per_view_errors) = _run_calibration(corners, ids)
        logger.info(f"Initial calibration: RMS={rms:.4f}, images={len(corners)}")

        filter_alpha = 0.1
        max_rounds = len(corners) // 3

        def _intrinsic_uncertainty(std_intr):
            if std_intr is None:
                return float("inf")
            f = std_intr.flatten()
            if len(f) < 2:
                return float("inf")
            return max(float(f[0]), float(f[1]))

        # Min retained: below 15 frames, std_fx/fy diverges for a 5-coef Brown model.
        for round_num in range(max_rounds):
            if len(corners) <= 15:
                break

            per_image_errors = []
            for i in range(len(corners)):
                obj_pts, img_pts = self.board.matchImagePoints(corners[i], ids[i])
                if len(obj_pts) == 0:
                    per_image_errors.append(0.0)
                    continue
                ok, rvec, tvec = cv2.solvePnP(
                    obj_pts, img_pts, camera_matrix, dist_coeffs)
                if not ok:
                    per_image_errors.append(0.0)
                    continue
                projected, _ = cv2.projectPoints(
                    obj_pts, rvec, tvec, camera_matrix, dist_coeffs)
                per_image_errors.append(
                    cv2.norm(img_pts, projected, cv2.NORM_L2) / len(projected))

            max_err = max(per_image_errors) if per_image_errors else 1.0
            if max_err == 0:
                break
            norm_errors = [e / max_err for e in per_image_errors]

            if coverage_tracker:
                baseline_q = coverage_tracker.estimate_coverage_quality(original_indices)
            else:
                baseline_q = 0.0

            worst_score = -float('inf')
            worst_idx = -1
            for i in range(len(corners)):
                if coverage_tracker:
                    q_without = coverage_tracker.estimate_coverage_quality(
                        original_indices, exclude_index=original_indices[i])
                    cov_delta = q_without - baseline_q
                else:
                    cov_delta = 0.0
                score = filter_alpha * norm_errors[i] + (1 - filter_alpha) * cov_delta
                if score > worst_score:
                    worst_score = score
                    worst_idx = i

            if worst_idx < 0:
                break

            test_corners = [c for j, c in enumerate(corners) if j != worst_idx]
            test_ids = [d for j, d in enumerate(ids) if j != worst_idx]
            try:
                (test_rms, test_mat, test_dist, test_rv, test_tv,
                 test_std_int, test_std_ext, test_per_view) = \
                    _run_calibration(test_corners, test_ids)
            except Exception:
                break

            # Stop only if removal worsens BOTH parameter uncertainty AND RMS by >5%.
            # Protects edge frames whose removal lowers RMS but raises intrinsic stdDev.
            current_unc = _intrinsic_uncertainty(std_intrinsics)
            test_unc = _intrinsic_uncertainty(test_std_int)
            if test_unc >= current_unc and test_rms >= rms * 1.05:
                break

            logger.info(
                f"Filter round {round_num + 1}: removing frame {original_indices[worst_idx]} "
                f"(RMS {rms:.4f} -> {test_rms:.4f}, "
                f"std_max {current_unc:.2f} -> {test_unc:.2f})")

            rejected_indices.append(original_indices[worst_idx])
            corners = test_corners
            ids = test_ids
            original_indices = [
                idx for j, idx in enumerate(original_indices) if j != worst_idx]
            rms, camera_matrix, dist_coeffs = test_rms, test_mat, test_dist
            rvecs, tvecs = test_rv, test_tv
            std_intrinsics, std_extrinsics, per_view_errors = \
                test_std_int, test_std_ext, test_per_view

        # OpenCV stdDeviationsIntrinsics order: fx, fy, cx, cy, k1, k2, p1, p2, k3

        std_flat = std_intrinsics.flatten() if std_intrinsics is not None else np.zeros(4)
        per_view_flat = per_view_errors.flatten() if per_view_errors is not None else np.array([])
        diagnostics = {
            "std_fx": float(std_flat[0]) if len(std_flat) > 0 else 0.0,
            "std_fy": float(std_flat[1]) if len(std_flat) > 1 else 0.0,
            "std_cx": float(std_flat[2]) if len(std_flat) > 2 else 0.0,
            "std_cy": float(std_flat[3]) if len(std_flat) > 3 else 0.0,
            "per_view_errors": per_view_flat.tolist(),
            "per_view_median": float(np.median(per_view_flat)) if len(per_view_flat) else 0.0,
            "per_view_max": float(np.max(per_view_flat)) if len(per_view_flat) else 0.0,
            "retained_original_indices": list(original_indices),
        }

        logger.info(
            f"Final: RMS={rms:.4f}, images={len(corners)}, rejected={len(rejected_indices)}, "
            f"stdDev(fx,fy)=({diagnostics['std_fx']:.2f}, {diagnostics['std_fy']:.2f}), "
            f"per-view median={diagnostics['per_view_median']:.4f} max={diagnostics['per_view_max']:.4f}")
        return rms, camera_matrix, dist_coeffs, rejected_indices, diagnostics

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

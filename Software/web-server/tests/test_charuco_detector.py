import cv2
import numpy as np
import pytest
from unittest.mock import patch

from charuco_detector import (
    CoverageTracker,
    CompatibleCharucoDetector,
    _check_opencv_version,
    MIN_OPENCV_VERSION,
)


class TestCheckOpencvVersion:
    def test_current_version_passes(self):
        _check_opencv_version()

    def test_old_version_raises(self):
        with patch.object(cv2, '__version__', '4.6.0'):
            with pytest.raises(RuntimeError, match="OpenCV >= 4.7.0 required"):
                _check_opencv_version()

    def test_new_version_passes(self):
        with patch.object(cv2, '__version__', '5.0.0'):
            _check_opencv_version()

    def test_version_with_suffix(self):
        with patch.object(cv2, '__version__', '4.8.0-dev'):
            _check_opencv_version()


class TestCoverageTracker:
    def test_initial_state(self):
        tracker = CoverageTracker(1200, 900)
        assert tracker.get_coverage_fraction() == 0.0
        assert all(cell == 0 for row in tracker.coverage for cell in row)

    def test_update_center(self):
        tracker = CoverageTracker(900, 900, grid_rows=3, grid_cols=3, cell_target=1)
        corners = np.array([[[450, 450]]], dtype=np.float32)
        tracker.update(corners)
        assert tracker.coverage[1][1] == 1
        assert tracker.get_coverage_fraction() == pytest.approx(1 / 9)

    def test_update_top_left(self):
        tracker = CoverageTracker(900, 900)
        corners = np.array([[[50, 50]], [[100, 100]]], dtype=np.float32)
        tracker.update(corners)
        assert tracker.coverage[0][0] == 1

    def test_update_bottom_right(self):
        tracker = CoverageTracker(900, 900)
        corners = np.array([[[800, 800]], [[850, 850]]], dtype=np.float32)
        tracker.update(corners)
        assert tracker.coverage[2][2] == 1

    def test_update_none_corners(self):
        tracker = CoverageTracker(900, 900)
        tracker.update(None)
        assert tracker.get_coverage_fraction() == 0.0

    def test_update_empty_corners(self):
        tracker = CoverageTracker(900, 900)
        tracker.update(np.array([], dtype=np.float32).reshape(0, 1, 2))
        assert tracker.get_coverage_fraction() == 0.0

    def test_multiple_updates_same_cell(self):
        tracker = CoverageTracker(900, 900, cell_target=1)
        corners = np.array([[[50, 50]]], dtype=np.float32)
        tracker.update(corners)
        tracker.update(corners)
        assert tracker.coverage[0][0] == 2
        assert tracker.get_coverage_fraction() == pytest.approx(1 / 9)

    def test_cell_target_gates_coverage(self):
        """A cell must reach cell_target samples before it counts as covered."""
        tracker = CoverageTracker(900, 900, cell_target=3)
        corners = np.array([[[50, 50]]], dtype=np.float32)
        tracker.update(corners)
        tracker.update(corners)
        assert tracker.coverage[0][0] == 2
        assert tracker.get_coverage_fraction() == 0.0  # 2 < cell_target=3
        tracker.update(corners)
        assert tracker.coverage[0][0] == 3
        assert tracker.get_coverage_fraction() == pytest.approx(1 / 9)

    def test_full_coverage(self):
        tracker = CoverageTracker(900, 900, cell_target=1)
        for r in range(3):
            for c in range(3):
                cx = c * 300 + 150
                cy = r * 300 + 150
                corners = np.array([[[cx, cy]]], dtype=np.float32)
                tracker.update(corners)
        assert tracker.get_coverage_fraction() == 1.0

    def test_suggested_region_empty(self):
        tracker = CoverageTracker(900, 900)
        region = tracker.get_suggested_region()
        assert region == "top-left"

    def test_suggested_region_after_partial_coverage(self):
        tracker = CoverageTracker(900, 900)
        # Cover top-left
        tracker.update(np.array([[[50, 50]]], dtype=np.float32))
        region = tracker.get_suggested_region()
        assert region != "top-left"

    def test_suggested_region_center(self):
        tracker = CoverageTracker(900, 900)
        # Cover everything except center
        for r in range(3):
            for c in range(3):
                if r == 1 and c == 1:
                    continue
                cx = c * 300 + 150
                cy = r * 300 + 150
                tracker.update(np.array([[[cx, cy]]], dtype=np.float32))
        assert tracker.get_suggested_region() == "center"

    def test_to_dict(self):
        tracker = CoverageTracker(900, 900)
        tracker.update(np.array([[[50, 50]]], dtype=np.float32))
        d = tracker.to_dict()
        assert "grid" in d
        assert "fraction" in d
        assert "suggested_region" in d
        assert "cell_target" in d
        assert "pixel_coverage" in d
        assert len(d["grid"]) == 3
        assert len(d["grid"][0]) == 3

    def test_edge_clamp(self):
        tracker = CoverageTracker(900, 900)
        corners = np.array([[[899, 899]]], dtype=np.float32)
        tracker.update(corners)
        assert tracker.coverage[2][2] == 1

    def test_pixel_coverage_empty(self):
        tracker = CoverageTracker(1000, 1000)
        assert tracker.get_pixel_coverage_fraction() == 0.0

    def test_pixel_coverage_single_corner(self):
        tracker = CoverageTracker(1000, 1000)
        tracker.update(np.array([[[55, 55]]], dtype=np.float32))
        # 1 cell out of 100 in default 10x10 grid
        assert tracker.get_pixel_coverage_fraction() == pytest.approx(0.01)

    def test_pixel_coverage_spans_multiple_cells(self):
        tracker = CoverageTracker(1000, 1000)
        # Each corner lands in a different 10x10 cell at 100-px intervals
        corners = np.array([[[c * 100 + 50, c * 100 + 50]] for c in range(10)],
                           dtype=np.float32)
        tracker.update(corners)
        # Diagonal hits 10 distinct cells
        assert tracker.get_pixel_coverage_fraction() == pytest.approx(0.10)

    def test_pixel_coverage_custom_grid_size(self):
        tracker = CoverageTracker(1000, 1000)
        # Grid 2x2 has cells of 500x500. (100,100) is in (0,0); (700,100) is in (0,1).
        tracker.update(np.array([[[100, 100]], [[700, 100]]], dtype=np.float32))
        assert tracker.get_pixel_coverage_fraction(grid_size=2) == pytest.approx(0.50)

    def test_pixel_coverage_aggregates_across_frames(self):
        tracker = CoverageTracker(1000, 1000)
        tracker.update(np.array([[[55, 55]]], dtype=np.float32))
        tracker.update(np.array([[[955, 955]]], dtype=np.float32))
        # Two distinct corners across two frames -> 2/100 cells
        assert tracker.get_pixel_coverage_fraction() == pytest.approx(0.02)


class TestCompatibleCharucoDetector:
    @pytest.fixture
    def detector(self):
        return CompatibleCharucoDetector(
            squares_x=8, squares_y=11,
            square_length=0.023, marker_length=0.017
        )

    def test_init(self, detector):
        assert detector.squares_x == 8
        assert detector.squares_y == 11
        assert detector.board is not None
        assert detector.charuco_detector is not None

    def test_init_legacy_pattern(self):
        det = CompatibleCharucoDetector(
            squares_x=5, squares_y=7,
            square_length=0.03, marker_length=0.022,
            legacy_pattern=True
        )
        assert det.board is not None

    def test_detect_no_board(self, detector):
        gray = np.zeros((480, 640), dtype=np.uint8)
        result = detector.detect_charuco_corners(gray)
        assert len(result) == 4
        corners, ids, marker_corners, marker_ids = result
        # blank image should detect nothing
        assert corners is None or len(corners) == 0

    def test_detect_on_generated_board(self, detector):
        board_img = detector.board.generateImage((700, 1000))
        corners, ids, marker_corners, marker_ids = detector.detect_charuco_corners(board_img)
        assert corners is not None
        assert len(corners) > 10

    def test_compute_tilt_score_none(self, detector):
        assert detector.compute_tilt_score(None) == 0.0

    def test_compute_tilt_score_too_few(self, detector):
        corners = np.array([[[0, 0]], [[1, 1]]], dtype=np.float32)
        assert detector.compute_tilt_score(corners) == 0.0

    def test_compute_tilt_score_flat(self, detector):
        board_img = detector.board.generateImage((700, 1000))
        corners, ids, _, _ = detector.detect_charuco_corners(board_img)
        score = detector.compute_tilt_score(corners)
        assert score < 0.20  # flat board should have low tilt

    def test_compute_tilt_score_zero_rect(self, detector):
        corners = np.array([[[5, 5]] for _ in range(10)], dtype=np.float32)
        assert detector.compute_tilt_score(corners) == 0.0

    def test_assess_quality_no_corners(self, detector):
        gray = np.zeros((480, 640), dtype=np.uint8)
        quality = detector.assess_image_quality(gray, None)
        assert not quality["is_good"]
        assert quality["num_corners"] == 0
        assert len(quality["reasons"]) > 0

    def test_assess_quality_few_corners(self, detector):
        gray = np.zeros((480, 640), dtype=np.uint8)
        corners = np.array([[[100, 100]], [[200, 200]]], dtype=np.float32)
        quality = detector.assess_image_quality(gray, corners)
        assert not quality["is_good"]
        assert "Insufficient" in quality["reasons"][0]

    def test_assess_quality_good_board(self, detector):
        board_img = detector.board.generateImage((700, 1000))
        corners, ids, _, _ = detector.detect_charuco_corners(board_img)
        quality = detector.assess_image_quality(board_img, corners)
        assert quality["is_good"]
        assert quality["num_corners"] > 10
        assert quality["blur_score"] > 50
        assert len(quality["reasons"]) == 0

    def test_assess_quality_blurry(self, detector):
        board_img = detector.board.generateImage((700, 1000))
        blurred = cv2.GaussianBlur(board_img, (51, 51), 0)
        corners, ids, _, _ = detector.detect_charuco_corners(blurred)
        if corners is not None and len(corners) >= 4:
            quality = detector.assess_image_quality(blurred, corners)
            assert "blurry" in " ".join(quality["reasons"]).lower() or not quality["is_good"]

    def test_assess_quality_edge_margin(self, detector):
        gray = np.full((480, 640), 128, dtype=np.uint8)
        corners = np.array([
            [[5, 5]], [[100, 5]], [[100, 100]], [[5, 100]]
        ], dtype=np.float32)
        quality = detector.assess_image_quality(gray, corners)
        assert "edge" in " ".join(quality["reasons"]).lower()

    def test_assess_quality_small_board(self, detector):
        gray = np.full((480, 640), 128, dtype=np.uint8)
        corners = np.array([
            [[300, 230]], [[310, 230]], [[310, 240]], [[300, 240]]
        ], dtype=np.float32)
        quality = detector.assess_image_quality(gray, corners)
        assert "small" in " ".join(quality["reasons"]).lower() or "coverage" in " ".join(quality["reasons"]).lower()


class TestCalibrateWithOutlierRejection:
    @pytest.fixture
    def detector(self):
        return CompatibleCharucoDetector(
            squares_x=8, squares_y=11,
            square_length=0.023, marker_length=0.017
        )

    def _generate_views(self, detector, n=8):
        all_corners = []
        all_ids = []
        board_img = detector.board.generateImage((700, 1000))
        image_size = (board_img.shape[1], board_img.shape[0])
        for i in range(n):
            # slight variation via different crop offsets
            pad = 50 + i * 5
            padded = cv2.copyMakeBorder(board_img, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=255)
            resized = cv2.resize(padded, (image_size[0], image_size[1]))
            corners, ids, _, _ = detector.detect_charuco_corners(resized)
            if corners is not None and len(corners) >= 4:
                all_corners.append(corners)
                all_ids.append(ids)
        return all_corners, all_ids, image_size

    def test_basic_calibration(self, detector):
        corners, ids, image_size = self._generate_views(detector, n=8)
        assert len(corners) >= 5
        rms, matrix, dist, rejected = detector.calibrate_with_outlier_rejection(
            corners, ids, image_size)
        assert rms > 0
        assert matrix.shape == (3, 3)
        assert dist.shape[1] == 5 or dist.shape[1] == 14

    def test_calibration_with_fix_k3(self, detector):
        corners, ids, image_size = self._generate_views(detector, n=8)
        rms, matrix, dist, rejected = detector.calibrate_with_outlier_rejection(
            corners, ids, image_size, fix_k3=True)
        assert rms > 0

    def test_few_images_no_rejection(self, detector):
        corners, ids, image_size = self._generate_views(detector, n=5)
        corners = corners[:5]
        ids = ids[:5]
        rms, matrix, dist, rejected = detector.calibrate_with_outlier_rejection(
            corners, ids, image_size)
        assert len(rejected) == 0

    def test_calibrate_with_filtering_returns_diagnostics(self, detector):
        corners, ids, image_size = self._generate_views(detector, n=10)
        rms, matrix, dist, rejected, diagnostics = detector.calibrate_with_filtering(
            corners, ids, image_size)
        assert rms > 0
        assert matrix.shape == (3, 3)
        assert dist.shape[1] == 5
        assert isinstance(rejected, list)
        for k in ("std_fx", "std_fy", "std_cx", "std_cy",
                  "per_view_errors", "per_view_median", "per_view_max",
                  "retained_original_indices"):
            assert k in diagnostics, f"missing diagnostic key: {k}"
        assert isinstance(diagnostics["per_view_errors"], list)
        assert len(diagnostics["per_view_errors"]) == len(corners) - len(rejected)
        assert diagnostics["per_view_max"] >= diagnostics["per_view_median"]

    def test_calibrate_with_filtering_min_retained_floor(self, detector):
        # With very few frames, filtering should refuse to drop below the floor.
        corners, ids, image_size = self._generate_views(detector, n=8)
        _, _, _, rejected, _ = detector.calibrate_with_filtering(
            corners, ids, image_size)
        assert len(corners) - len(rejected) >= min(15, len(corners))

    def test_calibrate_with_filtering_runs_filter_loop(self, detector):
        # n>15 lets the filter loop actually iterate (otherwise capped by floor).
        from charuco_detector import CoverageTracker
        corners, ids, image_size = self._generate_views(detector, n=20)
        if len(corners) < 16:
            pytest.skip("synthetic detector did not yield enough usable views")
        coverage = CoverageTracker(image_size[0], image_size[1])
        for c in corners:
            coverage.update(c)
        rms, K, dist, rejected, diagnostics = detector.calibrate_with_filtering(
            corners, ids, image_size, coverage_tracker=coverage)
        retained = len(corners) - len(rejected)
        assert retained >= 15
        assert diagnostics["std_fx"] > 0  # extended path produced parameter uncertainty
        assert len(diagnostics["per_view_errors"]) == retained

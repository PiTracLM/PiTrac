"""Tests for camera_detector module"""

from pathlib import Path
from unittest.mock import Mock, patch, mock_open
import pytest
from camera_detector import CameraDetector


class TestCameraDetector:
    """Test suite for CameraDetector class"""

    @pytest.fixture
    def detector(self):
        """Create a CameraDetector instance"""
        with patch.object(CameraDetector, "_detect_pi_model", return_value="pi5"):
            with patch.object(CameraDetector, "_get_camera_command", return_value="rpicam-hello"):
                with patch.object(CameraDetector, "_get_device_tree_root", return_value=None):
                    return CameraDetector()

    def test_init(self):
        """Test CameraDetector initialization"""
        with patch.object(CameraDetector, "_detect_pi_model", return_value="pi4"):
            with patch.object(CameraDetector, "_get_camera_command", return_value="libcamera-hello"):
                with patch.object(
                    CameraDetector,
                    "_get_device_tree_root",
                    return_value=Path("/sys/firmware/devicetree/base"),
                ):
                    detector = CameraDetector()
                    assert detector.pi_model == "pi4"
                    assert detector.camera_cmd == "libcamera-hello"
                    assert detector.dt_root == Path("/sys/firmware/devicetree/base")

    def test_detect_pi_model_pi5(self):
        """Test Pi 5 model detection"""
        detector = CameraDetector()
        with patch(
            "builtins.open",
            mock_open(read_data=b"Raspberry Pi 5 Model B Rev 1.0\x00"),
        ):
            with patch("pathlib.Path.exists", return_value=True):
                model = detector._detect_pi_model()
                assert model == "pi5"

    def test_detect_pi_model_pi4(self):
        """Test Pi 4 model detection"""
        detector = CameraDetector()
        with patch(
            "builtins.open",
            mock_open(read_data=b"Raspberry Pi 4 Model B Rev 1.4\x00"),
        ):
            with patch("pathlib.Path.exists", return_value=True):
                model = detector._detect_pi_model()
                assert model == "pi4"

    def test_detect_pi_model_from_cpuinfo(self):
        """Test Pi model detection from /proc/cpuinfo"""
        detector = CameraDetector()
        cpuinfo = "Model           : Raspberry Pi 3 Model B Plus Rev 1.3\n"
        with patch("pathlib.Path.exists", return_value=False):
            with patch("builtins.open", mock_open(read_data=cpuinfo)):
                model = detector._detect_pi_model()
                assert model == "pi3"

    def test_get_camera_command_rpicam(self):
        """Test finding rpicam-hello command"""
        detector = CameraDetector()
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = Mock(returncode=0)
            cmd = detector._get_camera_command()
            assert cmd == "rpicam-hello"

    def test_get_camera_command_libcamera(self):
        """Test finding libcamera-hello command"""
        detector = CameraDetector()
        with patch("subprocess.run") as mock_run:
            mock_run.side_effect = [
                Mock(returncode=1),  # rpicam-hello not found
                Mock(returncode=0),  # libcamera-hello found
            ]
            cmd = detector._get_camera_command()
            assert cmd == "libcamera-hello"

    def test_get_camera_command_none(self):
        """Test when no camera command is found"""
        detector = CameraDetector()
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = Mock(returncode=1)
            with patch("pathlib.Path.exists", return_value=False):
                cmd = detector._get_camera_command()
                assert cmd is None

    def test_parse_camera_info_imx296(self):
        """Test parsing IMX296 camera info"""
        detector = CameraDetector()
        output = """Available cameras
-----------------
0 : imx296 [1456x1088] (/base/soc/i2c0mux/i2c@1/imx296@1a)
    Modes: 'SBGGR10_CSI2P' : 1456x1088 [60.05 fps - (0, 0)/1456x1088 crop]
"""
        cameras = detector._parse_camera_info(output)
        assert len(cameras) == 1
        assert cameras[0]["sensor"] == "imx296"
        assert cameras[0]["model"] == "Global Shutter Camera"
        assert cameras[0]["pitrac_type"] == 4
        assert cameras[0]["status"] == "SUPPORTED"

    def test_parse_camera_info_multiple(self):
        """Test parsing multiple cameras"""
        detector = CameraDetector()
        output = """Available cameras
-----------------
0 : imx219 [3280x2464] (/base/soc/i2c0mux/i2c@1/imx219@10)
    Modes: 'SRGGB10_CSI2P' : 640x480 [206.65 fps - (1000, 752)/1280x960 crop]
1 : imx296 [1456x1088] (/base/soc/i2c0mux/i2c@0/imx296@1a)
    Modes: 'SBGGR10_CSI2P' : 1456x1088 [60.05 fps - (0, 0)/1456x1088 crop]
"""
        cameras = detector._parse_camera_info(output)
        assert len(cameras) == 2
        assert cameras[0]["sensor"] == "imx219"
        assert cameras[1]["sensor"] == "imx296"

    def test_detect_no_cameras(self, detector):
        """Test detection when no cameras are found"""
        with patch.object(detector, "_run_camera_detection", return_value=None):
            result = detector.detect()
            assert result["success"] is False
            assert len(result["cameras"]) == 0
            assert "No camera detection tool available" in result["message"]

    def test_detect_with_cameras(self, detector):
        """Test successful camera detection"""
        output = """Available cameras
-----------------
0 : imx296 [1456x1088] (/base/soc/i2c0mux/i2c@1/imx296@1a)
    Modes: 'SBGGR10_CSI2P' : 1456x1088 [60.05 fps - (0, 0)/1456x1088 crop]
"""
        with patch.object(detector, "_run_camera_detection", return_value=output):
            with patch.object(detector, "_check_camera_tools", return_value=True):
                result = detector.detect()
                assert result["success"] is True
                assert len(result["cameras"]) == 1
                assert result["configuration"]["slot1"]["type"] == 4

    def test_detect_unsupported_camera(self, detector):
        """Test detection of unsupported camera"""
        output = """Available cameras
-----------------
0 : imx708 [4608x2592] (/base/soc/i2c0mux/i2c@1/imx708@1a)
    Modes: 'SRGGB10_CSI2P' : 2304x1296 [56.03 fps - (0, 0)/4608x2592 crop]
"""
        with patch.object(detector, "_run_camera_detection", return_value=output):
            with patch.object(detector, "_check_camera_tools", return_value=True):
                result = detector.detect()
                assert result["success"] is True
                assert len(result["cameras"]) == 1
                assert result["cameras"][0]["status"] == "UNSUPPORTED"
                assert "none are fully supported" in result["message"]

    def test_get_camera_types(self, detector):
        """Test getting camera types list"""
        types = detector.get_camera_types()
        assert len(types) > 0
        assert any(t["value"] == 4 for t in types)  # Pi Global Shutter
        assert any(t["value"] == 5 for t in types)  # InnoMaker

    def test_get_lens_types(self, detector):
        """Test getting lens types list"""
        types = detector.get_lens_types()
        assert len(types) > 0
        assert any(t["value"] == 1 for t in types)  # 6mm lens

    def test_get_diagnostic_info(self, detector):
        """Test getting diagnostic information"""
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = Mock(returncode=0, stdout="imx296\n")
            with patch("pathlib.Path.exists", return_value=True):
                with patch(
                    "builtins.open",
                    mock_open(read_data="camera_auto_detect=1\ndtoverlay=imx296"),
                ):
                    diag = detector.get_diagnostic_info()
                    assert "pi_model" in diag
                    assert "camera_tool" in diag
                    assert "tools_available" in diag
                    assert "config_files" in diag

    def test_detect_color_mode(self, detector):
        """Test color mode detection"""
        info_color = "Modes: 'SRGGB10_CSI2P' : 1456x1088"
        assert detector._detect_color_mode(info_color, "imx296") == "COLOR"

        info_mono = "Modes: 'Y10_CSI2P' : 1456x1088 MONO"
        assert detector._detect_color_mode(info_mono, "imx296") == "MONO"

    def test_detect_camera_port(self, detector):
        """Test camera port detection"""
        # Test with index-based fallback
        port = detector._detect_camera_port(0, None, "")
        assert port == "CAM0"

        port = detector._detect_camera_port(1, None, "")
        assert port == "CAM1"

        # Test with heuristic detection
        info = "/base/soc/i2c0mux/i2c@88000/imx296@1a"
        port = detector._heuristic_port_from_path(info)
        assert port == "CAM0"

        info = "/base/soc/i2c0mux/i2c@80000/imx296@1a"
        port = detector._heuristic_port_from_path(info)
        assert port == "CAM1"

    def test_run_camera_detection_success(self, detector):
        """Test successful camera detection command execution"""
        mock_result = Mock(
            returncode=0,
            stdout="Available cameras\n0 : imx296 [1456x1088]",
            stderr="",
        )
        with patch("subprocess.run", return_value=mock_result):
            output = detector._run_camera_detection()
            assert output is not None
            assert "imx296" in output

    def test_run_camera_detection_failure(self, detector):
        """Test failed camera detection"""
        mock_result = Mock(returncode=1, stdout="", stderr="ERROR: No cameras")
        with patch("subprocess.run", return_value=mock_result):
            output = detector._run_camera_detection()
            assert output is None

    def test_extract_camera_block(self, detector):
        """Test extracting camera block from output"""
        output = """0 : imx296 [1456x1088]
    Modes: 'SBGGR10_CSI2P' : 1456x1088
1 : imx219 [3280x2464]
    Modes: 'SRGGB10_CSI2P' : 640x480"""
        block = detector._extract_camera_block(output, 0)
        assert "imx296" in block
        assert "imx219" not in block

    def test_parse_legacy_format(self, detector):
        """Test parsing legacy raspistill format"""
        output = "Camera module found at /dev/video0"
        cameras = detector._parse_legacy_format(output)
        assert len(cameras) == 1
        assert cameras[0]["model"] == "Legacy Camera"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])

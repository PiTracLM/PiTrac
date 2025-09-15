#!/usr/bin/env python3
"""
PiTrac Camera Detection Module

Detects connected Raspberry Pi cameras and determines their types for PiTrac configuration.
Enhanced version with device tree parsing and robust detection methods.
"""

import json
import logging
import os
import re
import struct
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Any

logger = logging.getLogger(__name__)


class CameraDetector:
    """Detects and identifies connected Raspberry Pi cameras"""

    CAMERA_MODELS = {
        "ov5647": "Pi Camera v1.3",
        "imx219": "Pi Camera v2",
        "imx477": "Pi HQ Camera",
        "imx296": "Global Shutter Camera",
        "imx708": "Pi Camera v3",
    }

    PITRAC_TYPES = {"ov5647": 1, "imx219": 2, "imx477": 3, "imx296": 4, "imx708": 0}

    CAMERA_STATUS = {
        "ov5647": "DEPRECATED",
        "imx219": "DEPRECATED",
        "imx477": "DEPRECATED",
        "imx296": "SUPPORTED",
        "imx708": "UNSUPPORTED",
    }

    PITRAC_TYPE_PI_GS = 4
    PITRAC_TYPE_INNOMAKER = 5

    DT_ROOT = "/sys/firmware/devicetree/base"
    DT_ROOT_ALT = "/proc/device-tree"

    INNOMAKER_TRIGGER = "/usr/lib/pitrac/ImageProcessing/CameraTools/imx296_trigger"

    def __init__(self):
        self.pi_model = self._detect_pi_model()
        self.camera_cmd = self._get_camera_command()
        self.dt_root = self._get_device_tree_root()

    def _detect_pi_model(self) -> str:
        """Detect Raspberry Pi model - mirrors shell script logic exactly"""
        model = ""

        for dt_path in [self.DT_ROOT + "/model", self.DT_ROOT_ALT + "/model"]:
            try:
                if Path(dt_path).exists():
                    with open(dt_path, "rb") as f:
                        model = f.read().decode("utf-8", errors="ignore").strip("\x00")
                        break
            except Exception as e:
                logger.debug(f"Could not read {dt_path}: {e}")

        if not model:
            try:
                with open("/proc/cpuinfo", "r") as f:
                    for line in f:
                        if line.startswith("Model"):
                            model = line.split(":", 1)[1].strip()
                            break
            except Exception as e:
                logger.debug(f"Could not read /proc/cpuinfo: {e}")

        if "Raspberry Pi 5" in model or "Raspberry Pi Compute Module 5" in model:
            return "pi5"
        elif "Raspberry Pi 4" in model or "Raspberry Pi Compute Module 4" in model:
            return "pi4"
        elif "Raspberry Pi 3" in model or "Raspberry Pi Compute Module 3" in model:
            return "pi3"
        elif "Raspberry Pi 2" in model:
            return "pi2"
        elif "Raspberry Pi" in model:
            return "pi_other"
        else:
            return "unknown"

    def _get_camera_command(self) -> Optional[str]:
        """Get the appropriate camera command - mirrors shell script logic"""
        commands = [
            "rpicam-hello",
            "libcamera-hello",
            "rpicam-still",
            "libcamera-still",
            "raspistill",
        ]

        for cmd in commands:
            result = subprocess.run(["which", cmd], capture_output=True)
            if result.returncode == 0:
                return cmd

            for path in ["/usr/bin", "/usr/local/bin", "/opt/vc/bin"]:
                full_path = f"{path}/{cmd}"
                if Path(full_path).exists() and os.access(full_path, os.X_OK):
                    return full_path

        return None

    def _get_device_tree_root(self) -> Optional[Path]:
        """Find the device tree root directory"""
        for path in [self.DT_ROOT, self.DT_ROOT_ALT]:
            dt_path = Path(path)
            if dt_path.exists() and dt_path.is_dir():
                return dt_path
        return None

    def _run_camera_detection(self) -> Optional[str]:
        """Run the camera detection command and return output"""
        if not self.camera_cmd:
            logger.error("No camera detection tool found")
            return None

        commands_to_try = []

        if self.camera_cmd:
            commands_to_try.append(self.camera_cmd)

        if self.pi_model == "pi5" and self.camera_cmd != "rpicam-hello":
            commands_to_try.append("rpicam-hello")
        if self.camera_cmd != "libcamera-hello":
            commands_to_try.append("libcamera-hello")

        for i, cmd_path in enumerate(commands_to_try):
            try:
                cmd_name = os.path.basename(cmd_path)

                if cmd_name in [
                    "rpicam-hello",
                    "libcamera-hello",
                    "rpicam-still",
                    "libcamera-still",
                ]:
                    cmd = [cmd_path, "--list-cameras"]
                    if i == 0:
                        logger.info(f"Running camera detection with {cmd_name}")
                    else:
                        logger.info(f"Trying fallback detection with {cmd_name}")
                    logger.debug(f"Full command: {' '.join(cmd)}")
                    result = subprocess.run(cmd, capture_output=True, text=True, timeout=10, check=False)
                    if result.returncode == 0 and result.stdout:
                        logger.info(f"Camera detection successful with {cmd_name}")
                        logger.debug(f"Output length: {len(result.stdout)} bytes")
                        self.camera_cmd = cmd_path
                        return result.stdout
                    else:
                        if i == 0:
                            logger.info(f"Primary detection with {cmd_name} failed (return code: {result.returncode})")
                        else:
                            logger.debug(f"{cmd_path} failed with return code {result.returncode}")
                        if result.stderr and "ERROR" in result.stderr:
                            logger.warning(f"Camera detection error: {result.stderr[:200]}")
                        elif result.stderr:
                            logger.debug(f"stderr: {result.stderr[:200]}")
                elif cmd_name == "raspistill":
                    vcgencmd_result = subprocess.run(["which", "vcgencmd"], capture_output=True)
                    if vcgencmd_result.returncode == 0:
                        supported = subprocess.run(["vcgencmd", "get_camera"], capture_output=True, text=True)
                        if "supported=1" in supported.stdout:
                            return "0 : Legacy camera detected (check with libcamera tools for details)"
                        else:
                            logger.debug("raspistill reports no cameras available")
                    else:
                        logger.debug("vcgencmd not found for raspistill check")

            except subprocess.TimeoutExpired:
                logger.warning(f"Command {cmd_path} timed out")
                continue
            except FileNotFoundError:
                logger.debug(f"Command {cmd_path} not found")
                continue
            except Exception as e:
                logger.warning(f"Error running {cmd_path}: {e}")
                continue

        logger.warning("All camera detection methods failed")
        return None

    def _parse_camera_info(self, output: str) -> List[Dict]:
        """Parse camera information from libcamera output"""
        cameras = []

        camera_pattern = r"^(\d+)\s*:\s*(\w+)\s*\[([^\]]+)\](?:\s*\(([^)]+)\))?"

        for match in re.finditer(camera_pattern, output, re.MULTILINE):
            idx = int(match.group(1))
            sensor = match.group(2).lower()
            resolution = match.group(3)
            dt_path = match.group(4) if match.group(4) else None

            camera_info = self._extract_camera_block(output, idx)

            model_name = self.CAMERA_MODELS.get(sensor, "Unknown")
            pitrac_type = self.PITRAC_TYPES.get(sensor, 0)
            status = self.CAMERA_STATUS.get(sensor, "UNKNOWN")

            cfa = self._detect_color_mode(camera_info, sensor)

            if sensor == "imx296":
                if cfa == "COLOR":
                    pitrac_type = self.PITRAC_TYPE_PI_GS
                    description = "Raspberry Pi Global Shutter (Color)"
                elif cfa == "MONO":
                    if Path(self.INNOMAKER_TRIGGER).exists():
                        pitrac_type = self.PITRAC_TYPE_INNOMAKER
                        description = "InnoMaker IMX296 (Mono)"
                    else:
                        pitrac_type = self.PITRAC_TYPE_INNOMAKER
                        description = "IMX296 Mono (InnoMaker-compatible)"
                else:
                    pitrac_type = self.PITRAC_TYPE_PI_GS
                    description = "IMX296 (assumed color)"
            else:
                description = model_name

            port = self._detect_camera_port(idx, dt_path, camera_info)

            cameras.append(
                {
                    "index": idx,
                    "sensor": sensor,
                    "model": model_name,
                    "description": description,
                    "pitrac_type": pitrac_type,
                    "status": status,
                    "cfa": cfa,
                    "port": port,
                    "resolution": resolution,
                    "dt_path": dt_path,
                }
            )

        if not cameras and "Available cameras" in output:
            cameras = self._parse_legacy_format(output)

        return cameras

    def _extract_camera_block(self, output: str, idx: int) -> str:
        """Extract the full info block for a specific camera index"""
        lines = output.split("\n")
        camera_block = []
        in_block = False

        for line in lines:
            if f"{idx} :" in line:
                in_block = True
                camera_block.append(line)
            elif in_block:
                if re.match(r"^\d+\s*:", line):
                    break
                if line.strip():
                    camera_block.append(line)

        return "\n".join(camera_block)

    def _detect_color_mode(self, camera_info: str, sensor: str) -> str:
        """Detect if camera is color or mono based on CFA patterns"""
        if "MONO" in camera_info.upper():
            return "MONO"

        cfa_patterns = ["RGGB", "BGGR", "GRBG", "GBRG"]
        for pattern in cfa_patterns:
            if pattern in camera_info.upper():
                return "COLOR"

        if sensor in ["ov5647", "imx219", "imx477", "imx708"]:
            return "COLOR"

        return "UNKNOWN"

    def _extract_dt_path_from_info(self, info: str) -> Optional[Path]:
        """Extract device tree path from libcamera info - mirrors shell logic"""
        match = re.search(r"\((/base/.*)\)", info)
        if match:
            suffix = match.group(1)
            suffix = suffix.replace("/base", "")

            for root in [self.DT_ROOT, self.DT_ROOT_ALT]:
                candidate = Path(root + suffix)
                if candidate.exists() and candidate.is_dir():
                    return candidate

        return None

    def _detect_camera_port(self, idx: int, dt_path: Optional[str], info: str) -> str:
        """Detect which physical CSI port the camera is connected to"""
        dt_node = None
        if dt_path:
            dt_node = self._extract_dt_path_from_info(dt_path)

        if dt_node:
            port = self._dt_sensor_to_cam_port(dt_node)
            if port != "UNKNOWN":
                return port

        port = self._heuristic_port_from_path(info)
        if port != "UNKNOWN":
            return port

        if idx == 0:
            return "CAM0"
        elif idx == 1:
            return "CAM1"
        else:
            return f"CAM{idx}"

    def _dt_read_u32(self, filepath: Path) -> Optional[int]:
        """Read a uint32 from device tree property - mirrors shell dt_read_u32"""
        try:
            with open(filepath, "rb") as f:
                data = f.read(4)
                if len(data) == 4:
                    return struct.unpack(">I", data)[0]
        except Exception as e:
            logger.debug(f"Could not read u32 from {filepath}: {e}")
        return None

    def _dt_find_node_by_phandle(self, target: int) -> Optional[Path]:
        """Find device tree node by phandle - mirrors shell dt_find_node_by_phandle"""
        for base in [self.DT_ROOT, self.DT_ROOT_ALT]:
            if not Path(base).exists():
                continue

            try:
                for phandle_file in Path(base).rglob("phandle"):
                    val = self._dt_read_u32(phandle_file)
                    if val is not None and val == target:
                        return phandle_file.parent
            except Exception as e:
                logger.debug(f"Error searching phandles in {base}: {e}")

        return None

    def _dt_sensor_to_cam_port(self, sensor_node: Path) -> str:
        """Determine physical CSI port from device tree - mirrors shell logic exactly"""
        if not sensor_node.exists():
            return "UNKNOWN"

        ports_dir = sensor_node / "port"
        if (sensor_node / "ports").exists():
            ports_dir = sensor_node / "ports"
        elif not ports_dir.exists():
            ports_dir = sensor_node

        try:
            for endpoint in ports_dir.glob("*/endpoint@*"):
                if not endpoint.is_dir():
                    endpoint = ports_dir.glob("endpoint@*")

                remote_endpoint = endpoint / "remote-endpoint"
                if not remote_endpoint.exists():
                    continue

                phandle = self._dt_read_u32(remote_endpoint)
                if phandle is None:
                    continue

                remote_node = self._dt_find_node_by_phandle(phandle)
                if remote_node is None:
                    continue

                parent_port = remote_node.parent
                port_name = parent_port.name

                match = re.match(r"port@(\d+)", port_name)
                if match:
                    port_idx = int(match.group(1))
                    if port_idx == 0:
                        return "CAM0"
                    elif port_idx == 1:
                        return "CAM1"
                    else:
                        return f"CSI{port_idx}"
        except Exception as e:
            logger.debug(f"Error parsing device tree ports: {e}")

        return "UNKNOWN"

    def _heuristic_port_from_path(self, info: str) -> str:
        """Heuristic port detection for Pi 5 RP1 chip - exact match to shell"""
        if "i2c@88000" in info:
            return "CAM0"
        elif "i2c@80000" in info:
            return "CAM1"
        else:
            return "UNKNOWN"

    def _parse_legacy_format(self, output: str) -> List[Dict]:
        """Parse legacy raspistill format output"""
        cameras = []
        if "/dev/video0" in output:
            cameras.append(
                {
                    "index": 0,
                    "sensor": "unknown",
                    "model": "Legacy Camera",
                    "description": "Legacy camera detected via raspistill",
                    "pitrac_type": 4,
                    "status": "UNKNOWN",
                    "cfa": "UNKNOWN",
                    "port": "CAM0",
                    "resolution": "unknown",
                    "dt_path": None,
                }
            )
        return cameras

    def detect(self) -> Dict:
        """Main detection function - returns camera configuration"""
        logger.info(f"Starting camera detection on {self.pi_model} using {self.camera_cmd}")

        result = {
            "success": False,
            "pi_model": self.pi_model,
            "detection_tool": self.camera_cmd,
            "cameras": [],
            "configuration": {
                "slot1": {"type": 4, "lens": 1},
                "slot2": {"type": 4, "lens": 1},
            },
            "message": "",
            "warnings": [],
        }

        if not self._check_camera_tools():
            logger.warning("Camera detection tools not fully installed")
            result["warnings"].append("Camera detection tools not fully installed")

        logger.debug("Running camera detection command...")
        output = self._run_camera_detection()
        if not output:
            logger.warning("No output from camera detection tool")
            result["message"] = "No camera detection tool available or no cameras found"
            result["warnings"].append("Make sure libcamera is installed: sudo apt install libcamera-apps")
            return result

        logger.debug(f"Parsing camera info from output ({len(output)} bytes)")
        cameras = self._parse_camera_info(output)
        if not cameras:
            logger.info("No cameras detected by parsing tool output")
            result["message"] = "No cameras detected"
            result["warnings"].append("Check ribbon cable connections and camera_auto_detect=1 in config.txt")
            return result

        result["cameras"] = cameras
        result["success"] = True

        logger.info(f"Found {len(cameras)} camera(s)")
        for cam in cameras:
            logger.debug(
                f"Camera {cam['index']}: {cam['sensor']} - {cam['model']} on {cam['port']}, Status: {cam['status']}"
            )

        if len(cameras) >= 1:
            result["configuration"]["slot1"]["type"] = cameras[0]["pitrac_type"]
            logger.debug(f"Setting slot1 configuration to type {cameras[0]['pitrac_type']}")

        if len(cameras) >= 2:
            result["configuration"]["slot2"]["type"] = cameras[1]["pitrac_type"]
            logger.debug(f"Setting slot2 configuration to type {cameras[1]['pitrac_type']}")
        elif len(cameras) == 1:
            logger.warning("Only 1 camera detected. Single-Pi mode requires 2 cameras.")
            result["warnings"].append("Only 1 camera detected. Single-Pi mode requires 2 cameras.")

        supported_count = sum(1 for c in cameras if c["status"] == "SUPPORTED")
        if supported_count == len(cameras):
            result["message"] = f"Detected {len(cameras)} supported camera(s)"
            logger.info(f"All {len(cameras)} detected cameras are supported")
        elif supported_count > 0:
            result["message"] = f"Detected {len(cameras)} camera(s), {supported_count} supported"
            deprecated = [c for c in cameras if c["status"] == "DEPRECATED"]
            if deprecated:
                dep_list = ", ".join(c["model"] for c in deprecated)
                logger.warning(f"Deprecated cameras detected: {dep_list}")
                result["warnings"].append(f"Deprecated cameras detected: {dep_list}")
        else:
            result["message"] = f"Detected {len(cameras)} camera(s), but none are fully supported"
            logger.warning("No fully supported cameras detected - PiTrac requires IMX296-based cameras")
            result["warnings"].append("PiTrac requires IMX296-based Global Shutter cameras for best results")

        return result

    def _check_camera_tools(self) -> bool:
        """Check if required camera tools are installed"""
        tools = ["libcamera-hello", "rpicam-hello", "vcgencmd"]
        found = 0
        for tool in tools:
            if subprocess.run(["which", tool], capture_output=True).returncode == 0:
                found += 1
        return found > 0

    def get_camera_types(self) -> List[Dict]:
        """Get list of available camera types for UI"""
        return [
            {
                "value": 1,
                "label": "Pi Camera v1.3",
                "description": "OV5647 sensor (DEPRECATED)",
                "status": "deprecated",
            },
            {
                "value": 2,
                "label": "Pi Camera v2",
                "description": "IMX219 sensor (DEPRECATED)",
                "status": "deprecated",
            },
            {
                "value": 3,
                "label": "Pi HQ Camera",
                "description": "IMX477 sensor (DEPRECATED)",
                "status": "deprecated",
            },
            {
                "value": 4,
                "label": "Pi Global Shutter",
                "description": "IMX296 Color (RECOMMENDED)",
                "status": "supported",
            },
            {
                "value": 5,
                "label": "InnoMaker IMX296",
                "description": "IMX296 Mono",
                "status": "supported",
            },
        ]

    def get_lens_types(self) -> List[Dict]:
        """Get list of available lens types for UI"""
        return [
            {"value": 1, "label": "6mm", "description": "Standard 6mm lens (default)"},
            {"value": 2, "label": "3.6mm M12", "description": "3.6mm M12 lens"},
        ]

    def get_diagnostic_info(self) -> Dict[str, Any]:
        """Get comprehensive diagnostic information for troubleshooting"""
        diag = {
            "pi_model": self.pi_model,
            "camera_tool": self.camera_cmd,
            "tools_available": {},
            "config_files": {},
            "kernel_modules": {},
            "device_tree": {},
        }

        for tool in ["libcamera-hello", "rpicam-hello", "vcgencmd", "raspistill"]:
            result = subprocess.run(["which", tool], capture_output=True)
            diag["tools_available"][tool] = result.returncode == 0

        config_paths = ["/boot/config.txt", "/boot/firmware/config.txt"]
        for path in config_paths:
            if Path(path).exists():
                try:
                    with open(path) as f:
                        content = f.read()
                        diag["config_files"][path] = {
                            "exists": True,
                            "camera_auto_detect": "camera_auto_detect=1" in content,
                            "dtoverlay_imx296": "dtoverlay=imx296" in content,
                        }
                except Exception:
                    diag["config_files"][path] = {
                        "exists": True,
                        "error": "Could not read",
                    }
            else:
                diag["config_files"][path] = {"exists": False}

        try:
            lsmod = subprocess.run(["lsmod"], capture_output=True, text=True)
            if lsmod.returncode == 0:
                for module in ["imx296", "imx219", "bcm2835_unicam", "v4l2_common"]:
                    diag["kernel_modules"][module] = module in lsmod.stdout
        except Exception:
            pass

        return diag


def main():
    """Command-line interface for testing"""
    import argparse

    parser = argparse.ArgumentParser(description="PiTrac Camera Detection")
    parser.add_argument("--json", action="store_true", help="Output in JSON format")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("-q", "--quiet", action="store_true", help="Quiet output")
    parser.add_argument("--diagnostic", action="store_true", help="Show diagnostic information")
    parser.add_argument("--no-color", action="store_true", help="Disable colored output")
    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    elif args.quiet:
        logging.basicConfig(level=logging.WARNING)
    else:
        logging.basicConfig(level=logging.INFO)

    detector = CameraDetector()

    if args.diagnostic:
        diag = detector.get_diagnostic_info()
        if args.json:
            print(json.dumps(diag, indent=2))
        else:
            print("=== PiTrac Camera Diagnostic ===")
            print(f"Pi Model: {diag['pi_model']}")
            print(f"Camera Tool: {diag['camera_tool']}")
            print("\nTools Available:")
            for tool, available in diag["tools_available"].items():
                status = "[OK]" if available else "[X]"
                print(f"  {status} {tool}")
            print("\nConfig Files:")
            for path, info in diag["config_files"].items():
                if info["exists"]:
                    print(f"  {path}: exists")
                    if "camera_auto_detect" in info:
                        print(f"    camera_auto_detect: {info['camera_auto_detect']}")
                else:
                    print(f"  {path}: not found")
        return

    result = detector.detect()

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f"Pi Model: {result['pi_model']}")
        print(f"Detection Tool: {result['detection_tool']}")
        print(f"Status: {result['message']}")

        if result["warnings"]:
            print("\nWarnings:")
            for warning in result["warnings"]:
                print(f"  WARNING: {warning}")

        if result["cameras"]:
            print("\nDetected Cameras:")
            for cam in result["cameras"]:
                print(f"  Camera {cam['index']}:")
                print(f"    Sensor: {cam['sensor']}")
                print(f"    Model: {cam['model']}")
                print(f"    Description: {cam['description']}")
                print(f"    Port: {cam['port']}")
                print(f"    Resolution: {cam['resolution']}")
                print(f"    PiTrac Type: {cam['pitrac_type']}")
                print(f"    Status: {cam['status']}")
                if cam["cfa"] != "UNKNOWN":
                    print(f"    Color Mode: {cam['cfa']}")
                print()

            print("Recommended Configuration:")
            print(f"  export PITRAC_SLOT1_CAMERA_TYPE={result['configuration']['slot1']['type']}")
            print(f"  export PITRAC_SLOT2_CAMERA_TYPE={result['configuration']['slot2']['type']}")
        else:
            print("\nNo cameras detected!")
            print("\nTroubleshooting:")
            print("  1. Check ribbon cable connections and orientation")
            print("  2. Verify camera_auto_detect=1 in config.txt")
            print("  3. Power cycle the Raspberry Pi")
            print("  4. Run with --diagnostic for more information")

    if not result["cameras"]:
        sys.exit(1)  # No cameras detected
    elif not any(cam["status"] == "SUPPORTED" for cam in result["cameras"]):
        sys.exit(2)  # Cameras detected but none supported
    else:
        sys.exit(0)  # Success


if __name__ == "__main__":
    main()

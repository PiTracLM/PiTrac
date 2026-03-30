"""Mock picamera2 classes for testing without Pi hardware

Provides mock implementations of picamera2 classes to enable testing
in non-Pi environments (WSL, CI, Mac, etc.) where the actual picamera2
library cannot be installed.
"""
import logging

logger = logging.getLogger(__name__)


class MockPicamera2:
    """Mock for Picamera2 camera interface

    Provides the interface used by CameraStreamManager without requiring
    actual Raspberry Pi camera hardware.
    """

    def __init__(self, camera_num=0):
        """Initialize mock camera

        Args:
            camera_num: Camera index (0 or 1)
        """
        self.camera_num = camera_num
        self.configuration = None
        self.is_recording = False
        self.encoder = None
        self.output = None
        logger.debug(f"MockPicamera2 initialized for camera {camera_num}")

    def create_video_configuration(self, main=None):
        """Mock video configuration creation

        Args:
            main: Main stream configuration dict

        Returns:
            Configuration dict
        """
        config = {"main": main or {"size": (640, 480), "format": "RGB888"}}
        logger.debug(f"Created mock video config: {config}")
        return config

    def configure(self, config):
        """Mock configure camera

        Args:
            config: Configuration dict to apply
        """
        self.configuration = config
        logger.debug(f"Configured mock camera with: {config}")

    def start_recording(self, encoder, output):
        """Mock start recording

        Args:
            encoder: Video encoder instance
            output: Output destination
        """
        self.encoder = encoder
        self.output = output
        self.is_recording = True
        logger.debug(f"Mock camera {self.camera_num} started recording")

    def stop_recording(self):
        """Mock stop recording"""
        self.is_recording = False
        logger.debug(f"Mock camera {self.camera_num} stopped recording")

    def close(self):
        """Mock close camera"""
        self.is_recording = False
        self.encoder = None
        self.output = None
        logger.debug(f"Mock camera {self.camera_num} closed")


class MockJpegEncoder:
    """Mock for JpegEncoder

    Provides the interface for JPEG video encoding without actual encoding.
    """

    def __init__(self, quality=85):
        """Initialize mock encoder

        Args:
            quality: JPEG quality (0-100)
        """
        self.quality = quality
        logger.debug(f"MockJpegEncoder created with quality {quality}")


class MockFileOutput:
    """Mock for FileOutput that wraps a buffer

    Provides the interface for file output without actual file operations.
    """

    def __init__(self, output):
        """Initialize mock file output

        Args:
            output: Output buffer to wrap
        """
        self.output = output
        logger.debug("MockFileOutput created")

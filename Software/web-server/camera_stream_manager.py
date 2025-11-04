"""
Camera Stream Manager for PiTrac Web Server

Manages live camera preview streams using picamera2 for calibration workflow.
Only one camera stream can be active at a time to prevent resource conflicts.
"""

import io
import logging
from threading import Condition
from typing import Dict, Optional, Generator

from picamera2 import Picamera2
from picamera2.encoders import JpegEncoder
from picamera2.outputs import FileOutput

logger = logging.getLogger(__name__)


class StreamingOutput(io.BufferedIOBase):
    """Buffer for MJPEG frames with thread-safe access"""

    def __init__(self):
        self.frame = None
        self.condition = Condition()

    def write(self, buf):
        """Called by picamera2 encoder with each new frame"""
        with self.condition:
            self.frame = buf
            self.condition.notify_all()


class CameraStreamManager:
    """Manages camera streaming for live preview during calibration

    Only allows one camera stream at a time to prevent resource conflicts.
    Automatically stops streams when calibration starts or page navigation occurs.
    """

    def __init__(self, config_manager):
        """Initialize camera stream manager

        Args:
            config_manager: Configuration manager instance for camera settings
        """
        self.config_manager = config_manager
        self.active_camera: Optional[str] = None
        self.picam2: Optional[Picamera2] = None
        self.output: Optional[StreamingOutput] = None

    def start_stream(self, camera: str) -> Dict[str, str]:
        """Start streaming for specified camera

        Args:
            camera: Camera identifier ("camera1" or "camera2")

        Returns:
            Dict with status and camera ID

        Raises:
            ValueError: If camera ID is invalid
            RuntimeError: If camera cannot be initialized
        """
        if camera not in ["camera1", "camera2"]:
            raise ValueError(f"Invalid camera ID: {camera}")

        # Stop any existing stream first (only one at a time)
        if self.active_camera:
            logger.info(f"Stopping existing stream for {self.active_camera} before starting {camera}")
            self.stop_stream()

        try:
            # Map camera to picamera2 index
            # Camera1 is typically index 0, Camera2 is index 1
            camera_index = 0 if camera == "camera1" else 1

            logger.info(f"Starting stream for {camera} (picamera2 index {camera_index})")

            # Initialize picamera2
            self.picam2 = Picamera2(camera_index)

            # Configure for 640x480 streaming (good balance of quality/performance)
            config = self.picam2.create_video_configuration(
                main={"size": (640, 480), "format": "RGB888"}
            )
            self.picam2.configure(config)

            # Create streaming output buffer
            self.output = StreamingOutput()

            # Start recording JPEG frames to the output buffer
            self.picam2.start_recording(JpegEncoder(), FileOutput(self.output))

            self.active_camera = camera
            logger.info(f"Successfully started stream for {camera}")

            return {"status": "started", "camera": camera}

        except Exception as e:
            logger.error(f"Failed to start stream for {camera}: {e}", exc_info=True)
            # Cleanup on failure
            if self.picam2:
                try:
                    self.picam2.close()
                except Exception:
                    pass
                self.picam2 = None
            self.output = None
            self.active_camera = None
            raise RuntimeError(f"Failed to start camera stream: {e}")

    def stop_stream(self) -> Dict[str, str]:
        """Stop the active camera stream

        Returns:
            Dict with status and which camera was stopped
        """
        if not self.active_camera:
            return {"status": "no_stream_active"}

        camera = self.active_camera
        logger.info(f"Stopping stream for {camera}")

        try:
            if self.picam2:
                self.picam2.stop_recording()
                self.picam2.close()
                self.picam2 = None

            self.output = None
            self.active_camera = None

            logger.info(f"Successfully stopped stream for {camera}")
            return {"status": "stopped", "camera": camera}

        except Exception as e:
            logger.error(f"Error stopping stream for {camera}: {e}", exc_info=True)
            # Force cleanup even on error
            self.picam2 = None
            self.output = None
            self.active_camera = None
            return {"status": "error", "camera": camera, "message": str(e)}

    def generate_frames(self) -> Generator[bytes, None, None]:
        """Generate MJPEG frames for streaming

        Yields:
            MJPEG frame boundaries with JPEG data

        Raises:
            RuntimeError: If no stream is active
        """
        if not self.active_camera or not self.output:
            raise RuntimeError("No active camera stream")

        try:
            logger.debug(f"Starting frame generation for {self.active_camera}")
            while True:
                # Wait for new frame from camera
                with self.output.condition:
                    self.output.condition.wait()
                    frame = self.output.frame

                # Yield MJPEG formatted frame
                yield (
                    b'--FRAME\r\n'
                    b'Content-Type: image/jpeg\r\n'
                    b'Content-Length: ' + str(len(frame)).encode() + b'\r\n'
                    b'\r\n' + frame + b'\r\n'
                )

        except GeneratorExit:
            # Client disconnected - this is normal
            logger.debug(f"Client disconnected from {self.active_camera} stream")
            pass
        except Exception as e:
            logger.error(f"Error generating frames: {e}", exc_info=True)
            raise

    def is_streaming(self, camera: Optional[str] = None) -> bool:
        """Check if a stream is active

        Args:
            camera: Optional specific camera to check. If None, checks any stream.

        Returns:
            True if specified camera (or any camera) is streaming
        """
        if camera:
            return self.active_camera == camera
        return self.active_camera is not None

    def get_active_camera(self) -> Optional[str]:
        """Get the currently active camera stream

        Returns:
            Camera ID if streaming, None otherwise
        """
        return self.active_camera

    def cleanup(self):
        """Cleanup all resources - call on shutdown"""
        logger.info("Cleaning up camera stream manager")
        self.stop_stream()

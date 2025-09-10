"""Constants for PiTrac Web Server"""

from pathlib import Path

MPS_TO_MPH = 2.237
EXPECTED_DATA_LENGTH = 11

DEFAULT_BROKER = "tcp://localhost:61616"
STOMP_PORT = 61613
DEFAULT_USERNAME = "admin"
DEFAULT_PASSWORD = "admin"

HOME_DIR = Path.home()
PITRAC_DIR = HOME_DIR / ".pitrac"
IMAGES_DIR = HOME_DIR / "LM_Shares" / "Images"
CONFIG_FILE = PITRAC_DIR / "config" / "pitrac.yaml"

HEARTBEAT_INTERVAL = 30  # seconds
MAX_MESSAGE_SIZE = 1024 * 1024  # 1MB
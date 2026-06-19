"""Constants for PiTrac Web Server"""

import os
from pathlib import Path

MPS_TO_MPH = 2.237

HOME_DIR = Path(os.environ.get("HOME", str(Path.home())))
PITRAC_DIR = HOME_DIR / ".pitrac"
IMAGES_DIR = Path(os.environ.get("PITRAC_IMAGES_DIR", str(HOME_DIR / "LM_Shares" / "Images")))
DB_PATH = Path(os.environ.get("PITRAC_DB_PATH", str(PITRAC_DIR / "pitrac.db")))
SERVER_PORT = int(os.environ.get("PITRAC_WEB_PORT", "8080"))

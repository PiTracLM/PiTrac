#!/usr/bin/env python3
"""
PiTrac Web Server - Lightweight replacement for TomEE
Serves dashboard, handles ZeroMQ messages, and manages shot images
"""

import logging
import os
import uvicorn

LOG_LEVEL = os.getenv("PITRAC_WEB_LOG_LEVEL", "INFO").upper()
if LOG_LEVEL not in ["TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"]:
    LOG_LEVEL = "INFO"

if LOG_LEVEL == "TRACE":
    LOG_LEVEL = "DEBUG"

logging.basicConfig(
    level=getattr(logging, LOG_LEVEL),
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
)

from server import app  # noqa: E402

if __name__ == "__main__":
    uvicorn_level = LOG_LEVEL.lower()
    if uvicorn_level == "warning":
        uvicorn_level = "warn"
    elif uvicorn_level == "critical":
        uvicorn_level = "error"

    uvicorn.run("main:app", host="0.0.0.0", port=8080, reload=True, log_level=uvicorn_level)

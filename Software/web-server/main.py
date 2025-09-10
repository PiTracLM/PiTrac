#!/usr/bin/env python3
"""
PiTrac Web Server - Lightweight replacement for TomEE
Serves dashboard, handles ActiveMQ messages, and manages shot images
"""

import logging
import uvicorn

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)

from server import app

if __name__ == "__main__":
    uvicorn.run(
        "main:app",
        host="0.0.0.0",
        port=8080,
        reload=True,
        log_level="info"
    )
---
title: Build from Source
layout: default
nav_order: 1
parent: PiTrac Installation
grand_parent: Software
description: Complete guide to building PiTrac from source including cloning repository, running developer build, and verifying installation on Raspberry Pi.
keywords: build pitrac source, compile pitrac, developer build raspberry pi, install pitrac from git, pitrac build script
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# Build PiTrac from Source

Building from source is the current recommended installation method. It's ideal for development or if you want the latest features.

**Time Required:** 10-20 minutes on a Pi 5 (first installation, network-dependent)

**Difficulty:** Intermediate (script-automated, but requires command line comfort)

---

## Prerequisites

Before starting, ensure you have:

- **Raspberry Pi 5 with 8GB RAM** (recommended) or Pi 4 with 8GB
- **Raspberry Pi OS 64-bit** installed (Bookworm or Trixie)
  - See [Pi Setup Guide]({% link software/pi-setup.md %}) if not installed
- **Active internet connection** for downloading packages
- **Sudo privileges** on your Pi
- **At least 5GB free disk space** (10GB recommended)

{: .warning }
**Important**: You must complete [Raspberry Pi Setup]({% link software/pi-setup.md %}) before proceeding. The Pi must have OS installed, be updated, and have network access.

---

## Overview

The build process automatically:
- Installs all required system dependencies (~80+ packages)
- Extracts pre-built libraries (OpenCV, ActiveMQ, lgpio, msgpack)
- Compiles the PiTrac C++ binary
- Installs the Python web server and all dependencies
- Configures ActiveMQ message broker
- Sets up libcamera for dual Pi cameras
- Creates user directories and configuration files

---

## Step 1: Clone the Repository

Clone the PiTrac repository using HTTPS:

```bash
git clone https://github.com/PiTracLM/PiTrac.git
cd PiTrac
```

{: .note }
We use HTTPS instead of SSH to avoid requiring SSH key setup. Contributors can switch to SSH after cloning: `git remote set-url origin git@github.com:PiTracLM/PiTrac.git`

---

## Step 2: Run the Developer Build

Navigate to the packaging directory and run the developer installation:

```bash
cd packaging
sudo ./build.sh dev
```

{: .warning }
**Important**: You must use `sudo` as the script installs system packages and configures services.

---

## What Happens During Installation

The `build.sh dev` script performs these steps automatically:

### 1. Platform Validation

- Verifies you're running on a Raspberry Pi
- Checks for sudo privileges
- Confirms all pre-built artifacts are available

### 2. System Configuration

- Fixes known Pi OS issues (initramfs configuration bug)
- Updates package state
- Prepares the system for installation

### 3. Dependency Installation

**System Packages Installed** (~80+ packages):
- Build tools: `build-essential`, `meson`, `ninja-build`, `pkg-config`
- Boost libraries (1.74.0): `libboost-system`, `libboost-thread`, `libboost-filesystem`, and many others
- Camera libraries: `libcamera0.0.3`, `libcamera-dev`, `rpicam-apps` (Pi5) or `libcamera-apps` (Pi4)
- Video processing: FFmpeg libraries, image libraries (JPEG, PNG, TIFF)
- Message broker: `activemq`, `libapr1`, `libaprutil1`
- Python: `python3`, `python3-pip`, and related packages
- Configuration tools: `yq` (YAML query tool)

**Pre-built Dependencies Extracted**:

These are extracted from the `packaging/deps-artifacts/` directory:
- **OpenCV 4.11.0** - Computer vision library (Debian only has 4.6.0)
- **ActiveMQ-CPP 3.9.5** - C++ messaging client
- **lgpio 0.2.2** - GPIO library for Pi 5 (not in Debian repos)
- **msgpack-cxx 6.1.1** - Message serialization
- **ONNX Runtime 1.17.3** - AI inference engine

{: .note }
**Why pre-built?**: Building OpenCV from source takes 45-60 minutes. The pre-built artifacts are checked into the git repository so you don't have to wait.

### 4. Build PiTrac Binary

The script compiles the C++ launch monitor binary:
- Sets up proper build environment with library paths
- Applies Boost C++20 compatibility fix
- Detects stale builds and cleans if necessary
- Runs Meson build system with Ninja
- Compiles only changed files (incremental build)

**Build times**:
- **Total first install**: 10-20 minutes (all dependencies + services + build)
- **PiTrac binary build**: 2-5 minutes
- **Incremental rebuild**: 30 seconds - 2 minutes
- **Clean rebuild** (with `force` flag): 2-5 minutes

### 5. Installation

**Binaries and Tools**:
- `/usr/lib/pitrac/pitrac_lm` - Main launch monitor binary
- `/usr/bin/pitrac` - Unified CLI interface (Bashly-generated)
- `/usr/lib/pitrac/ImageProcessing/CameraTools/` - Camera utilities and scripts

**Configuration**:
- `/etc/pitrac/models/` - ONNX AI models for ball detection
- `/usr/lib/pitrac/web-server/configurations.json` - Configuration metadata (283 settings)

**Test Resources**:
- `/usr/share/pitrac/test-images/` - Sample test images (teed-ball.png, strobed.png)
- `/usr/share/pitrac/test-suites/` - Automated test suites

**User Directories**:
- `~/.pitrac/config/` - User configuration and calibration data
  - `user_settings.json` - Your configuration overrides
  - `calibration_data.json` - Camera calibration results (focal lengths, angles)
  - `generated_golf_sim_config.json` - Merged runtime configuration
- `~/.pitrac/state/` - Runtime state
- `~/.pitrac/calibration/` - Calibration tools (checkerboard images, scripts)
- `~/LM_Shares/Images/` - Captured images from shots
- `~/LM_Shares/WebShare/` - Web-accessible data

### 6. Camera Configuration

The script configures libcamera for dual Pi cameras:
- Copies IMX296 NOIR sensor files for your Pi model (4 or 5)
- Creates `rpi_apps.yaml` with extended timeout (1000ms)
- Sets `LIBCAMERA_RPI_CONFIG_FILE` environment variable
- Configures camera detection in boot config

### 7. ActiveMQ Message Broker Setup

ActiveMQ provides communication between PiTrac components:
- Installs ActiveMQ configuration from templates
- Sets up broker on `tcp://localhost:61616` (OpenWire protocol)
- Configures STOMP protocol on port 61613
- Enables and starts the `activemq.service`

**Verification**: The script checks that ActiveMQ is listening on port 61616.

### 8. Python Web Server Installation

The FastAPI-based web dashboard is installed:
- Copies web server code to `/usr/lib/pitrac/web-server/`
- Installs Python dependencies from `requirements.txt`:
  - `fastapi` - Modern web framework
  - `uvicorn` - ASGI server
  - `stomp.py` - ActiveMQ connectivity
  - `msgpack`, `pyyaml`, `websockets`, and more
- Configures `pitrac-web.service` to run as your user
- Restarts the service if it was already running

### 9. Service Configuration

The script sets up systemd services:
- `activemq.service` - Message broker (system service)
- `pitrac-web.service` - Web dashboard (user service)

{: .note }
**Note**: The main `pitrac.service` is no longer automatically installed. You control the launch monitor through the web interface Start/Stop buttons.

### 10. Cleanup and Verification

- Reloads systemd daemon
- Updates shared library cache (`ldconfig`)
- Displays installation summary

---

## Installation Output

During installation, you'll see progress like this:

```
====================================
 PiTrac Developer Build
====================================

✓ Platform validation
✓ Checking for pre-built artifacts
✓ Installing system dependencies (80+ packages)
✓ Extracting OpenCV 4.11.0
✓ Extracting ActiveMQ-CPP 3.9.5
✓ Extracting lgpio 0.2.2
✓ Configuring build environment
✓ Building PiTrac binary (this may take a few minutes)
✓ Installing binaries and tools
✓ Configuring libcamera
✓ Installing test resources
✓ Setting up ActiveMQ broker
✓ Installing Python web server
✓ Configuring services

====================================
 Installation Complete!
====================================

Next steps:
  1. Access web dashboard: http://raspberrypi.local:8080
  2. Start PiTrac from the web interface
  3. Run tests: pitrac test quick
  4. View status: pitrac status

For help: pitrac help
```

---

## Step 3: Verify Installation

After installation completes, verify everything is working:

### Check Service Status

```bash
# Check all services
pitrac status

# Or check individually
systemctl status activemq
systemctl status pitrac-web
```

**Expected output**:
- `activemq.service` - **active (running)**
- `pitrac-web.service` - **active (running)** or **inactive** (can start manually)

### Test Camera Detection

```bash
# Pi 5
rpicam-hello --list-cameras

# Pi 4
libcamera-hello --list-cameras
```

Should list 2 cameras if both are connected.

### Run Quick Test

```bash
pitrac test quick
```

This runs basic system tests to verify everything is configured correctly.

---

## Next Steps

**Installation Complete!**

**Continue to:**
- **[First Use Guide]({% link software/install/first-use.md %})** - Access web dashboard and start PiTrac

**Or:**
- **[Troubleshooting]({% link software/install/troubleshooting.md %})** - If you encountered errors

**Return to:**
- **[Installation Overview]({% link software/pitrac-install.md %})**

---

## Build Modes Reference

The `build.sh` script supports multiple modes:

```bash
# Developer install (recommended)
sudo ./build.sh dev

# Force clean rebuild
sudo ./build.sh dev force

# Build dependencies only
sudo ./build.sh deps

# Build PiTrac binary only
sudo ./build.sh build

# Clean build artifacts
sudo ./build.sh clean
```

For more details, see [Managing PiTrac]({% link software/install/managing.md %}).
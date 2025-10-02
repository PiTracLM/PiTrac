---
title: PiTrac Installation
layout: default
nav_order: 2
parent: Software
---

# PiTrac Installation Guide

This guide covers installing PiTrac on your Raspberry Pi. Choose the installation method that best fits your needs.

## Prerequisites

Before starting, ensure you have:

- **Raspberry Pi 5 with 8GB RAM** (recommended)
  - Raspberry Pi 4 with 8GB RAM also supported
- **Raspberry Pi OS Bookworm 64-bit** installed and configured
  - See [Pi Setup Guide]({% link software/pi-setup.md %}) for OS installation
- **Active internet connection** for downloading packages
- **Sudo privileges** on your Pi
- **At least 5GB free disk space** (10GB recommended for development)

---

# Installing from APT Repository

> **Coming Soon**: PiTrac will be available as a Debian package for easy installation via `apt`. This section will be updated when the package repository is available.

---

# Building from Source

Building from source is the current recommended installation method and is ideal for development or if you want the latest features.

## Overview

The build process:
- Installs all required system dependencies (~80+ packages)
- Extracts pre-built libraries (OpenCV, ActiveMQ, lgpio, msgpack)
- Compiles the PiTrac C++ binary
- Installs the Python web server and all dependencies
- Configures ActiveMQ message broker
- Sets up libcamera for dual Pi cameras
- Creates user directories and configuration files

**Expected time**: 5-10 minutes on a Pi 5 (first installation)

## Step 1: Clone the Repository

Clone the PiTrac repository using HTTPS:

```bash
git clone https://github.com/PiTracLM/PiTrac.git
cd PiTrac
```

> **Note**: We use HTTPS instead of SSH for the clone URL to avoid requiring SSH key setup. If you're a contributor with write access, you can switch to SSH after cloning using `git remote set-url origin git@github.com:PiTracLM/PiTrac.git`

## Step 2: Run the Developer Build

Navigate to the packaging directory and run the developer installation:

```bash
cd packaging
sudo ./build.sh dev
```

> **Important**: You must use `sudo` as the script installs system packages and configures services.

### What Happens During Installation

The `build.sh dev` script performs the following steps:

#### 1. Platform Validation
- Verifies you're running on a Raspberry Pi
- Checks for sudo privileges
- Confirms all pre-built artifacts are available

#### 2. System Configuration
- Fixes known Pi OS issues (initramfs configuration bug)
- Updates package state
- Prepares the system for installation

#### 3. Dependency Installation

**System Packages Installed**:
- Build tools: `build-essential`, `meson`, `ninja-build`, `pkg-config`
- Boost libraries (1.74.0): `libboost-system`, `libboost-thread`, `libboost-filesystem`, and many others
- Camera libraries: `libcamera0.0.3`, `libcamera-dev`, `rpicam-apps` (Pi5) or `libcamera-apps` (Pi4)
- Video processing: FFmpeg libraries, image libraries (JPEG, PNG, TIFF)
- Message broker: `activemq`, `libapr1`, `libaprutil1`
- Python: `python3`, `python3-pip`, and related packages
- Configuration tools: `yq` (YAML query tool)

**Pre-built Dependencies Installed**:
These are extracted from the `packaging/deps-artifacts/` directory:
- **OpenCV 4.11.0** - Computer vision library (Debian only has 4.6.0)
- **ActiveMQ-CPP 3.9.5** - C++ messaging client
- **lgpio 0.2.2** - GPIO library for Pi 5 (not in Debian repos)
- **msgpack-cxx 6.1.1** - Message serialization
- **ONNX Runtime 1.17.3** - AI inference engine

> **Why pre-built?**: Building OpenCV from source takes 45-60 minutes. The pre-built artifacts are checked into the git repository so you don't have to wait.

#### 4. Build PiTrac Binary

The script compiles the C++ launch monitor binary:
- Sets up proper build environment with library paths
- Applies Boost C++20 compatibility fix
- Detects stale builds and cleans if necessary
- Runs Meson build system with Ninja
- Compiles only changed files (incremental build)

**Build times**:
- First build: 2-5 minutes
- Incremental rebuild: 30 seconds - 2 minutes
- Clean rebuild (with `force` flag): 2-5 minutes

#### 5. Installation

**Binaries and Tools**:
- `/usr/lib/pitrac/pitrac_lm` - Main launch monitor binary
- `/usr/bin/pitrac` - Unified CLI interface (Bashly-generated)
- `/usr/lib/pitrac/ImageProcessing/CameraTools/` - Camera utilities and scripts

**Configuration**:
- `/etc/pitrac/pitrac.yaml` - Main configuration file
- `/etc/pitrac/golf_sim_config.json` - Simulator settings
- `/etc/pitrac/config/parameter-mappings.yaml` - Parameter definitions
- `/etc/pitrac/models/` - ONNX AI models for ball detection

**Test Resources**:
- `/usr/share/pitrac/test-images/` - Sample test images (teed-ball.png, strobed.png)
- `/usr/share/pitrac/test-suites/` - Automated test suites

**User Directories**:
- `~/.pitrac/config/` - User configuration overrides
- `~/.pitrac/state/` - Runtime state
- `~/.pitrac/calibration/` - Calibration data
- `~/LM_Shares/Images/` - Captured images from shots
- `~/LM_Shares/WebShare/` - Web-accessible data

#### 6. Camera Configuration

The script configures libcamera for dual Pi cameras:
- Copies IMX296 NOIR sensor files for your Pi model (4 or 5)
- Creates `rpi_apps.yaml` with extended timeout (1000ms)
- Sets `LIBCAMERA_RPI_CONFIG_FILE` environment variable
- Configures camera detection in boot config

#### 7. ActiveMQ Message Broker Setup

ActiveMQ provides communication between PiTrac components:
- Installs ActiveMQ configuration from templates
- Sets up broker on `tcp://localhost:61616` (OpenWire protocol)
- Configures STOMP protocol on port 61613
- Enables and starts the `activemq.service`

**Verification**: The script checks that ActiveMQ is listening on port 61616.

#### 8. Python Web Server Installation

The FastAPI-based web dashboard is installed:
- Copies web server code to `/usr/lib/pitrac/web-server/`
- Installs Python dependencies from `requirements.txt`:
  - `fastapi` - Modern web framework
  - `uvicorn` - ASGI server
  - `stomp.py` - ActiveMQ connectivity
  - `msgpack`, `pyyaml`, `websockets`, and more
- Configures `pitrac-web.service` to run as your user
- Restarts the service if it was already running

#### 9. Service Configuration

The script sets up systemd services:
- `activemq.service` - Message broker (system service)
- `pitrac-web.service` - Web dashboard (user service)

> **Note**: The main `pitrac.service` is no longer automatically installed. You control the launch monitor through the web interface Start/Stop buttons.

#### 10. Cleanup and Verification

- Reloads systemd daemon
- Updates shared library cache (`ldconfig`)
- Displays installation summary

### Installation Output

During installation, you'll see:
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
- `pitrac-web.service` - **active (running)** or **inactive** (you can start it manually)

### Test Camera Detection

```bash
# Pi 5
rpicam-hello --list-cameras

# Pi 4
libcamera-hello --list-cameras
```

You should see 2 cameras listed if your hardware is connected.

### Run Quick Test

```bash
pitrac test quick
```

This runs PiTrac against test images to verify the build is functional.

## Step 4: Access the Web Dashboard

The web interface provides control and monitoring for PiTrac.

### Start the Web Server

If not already running:

```bash
sudo systemctl start pitrac-web
```

Or use the CLI:

```bash
pitrac web start
```

### Access the Dashboard

Open a web browser and navigate to:

```
http://[YOUR_PI_IP_ADDRESS]:8080
```

To find your Pi's IP address:

```bash
hostname -I
```

Or use the hostname:

```
http://raspberrypi.local:8080
```

> **Note**: The default port is 8080. You can change this in the web server configuration.

## Step 5: Start PiTrac

From the web dashboard:

1. Navigate to the **PiTrac Process** section
2. Click **Start** to launch the launch monitor
3. Monitor the status indicators for Camera 1 and Camera 2

The web interface will show:
- Camera connection status
- ActiveMQ broker connectivity
- WebSocket connection to browser
- Real-time shot data when balls are hit

## Web Interface Overview

The PiTrac web dashboard provides comprehensive control:

### Main Dashboard
- **Real-time Shot Display**: Ball speed, carry distance, launch angles, spin rates
- **System Status Indicators**: WebSocket, ActiveMQ, cameras
- **Shot Images Gallery**: Browse captured images

### Configuration
- **Camera Settings**: Configure camera types, lens parameters, gain
- **Detection Methods**: Choose HoughCircles, YOLO, or YOLO+SAHI
- **Simulators**: Set up E6, GSPro, or TruGolf connectivity
- **Logging**: Control log levels and diagnostics
- **Calibration**: Fine-tune strobing, spin analysis

### Process Control
- **Start/Stop PiTrac**: Control launch monitor processes
- **Monitor Status**: Real-time health monitoring
- **Dual Camera Management**: Independent camera control

### Calibration Wizard
4-step calibration process:
1. Setup cameras
2. Verify ball placement
3. Run calibration
4. Review and save results

### Testing Tools
- Hardware testing (IR strobes, still images)
- Calibration verification
- Automated test suites

### Logs Viewer
- Real-time logs from all services
- Filter by service and log level
- Download logs for analysis

## Managing PiTrac

### Web Server Commands

```bash
# Start the web server
pitrac web start

# Stop the web server
pitrac web stop

# Restart the web server
pitrac web restart

# Check status
pitrac web status

# View logs
pitrac web logs

# Follow logs in real-time
pitrac web logs --follow
```

### Launch Monitor Control

**Use the web interface** to start/stop the PiTrac launch monitor:
- Navigate to "PiTrac Process" section
- Click Start/Stop buttons
- Monitor camera status indicators

### ActiveMQ Management

```bash
# Check broker status
systemctl status activemq

# Restart broker (if needed)
sudo systemctl restart activemq

# View ActiveMQ web console
# Open browser to: http://raspberrypi.local:8161/admin
# Default credentials: admin/admin
```

## File Locations

After installation:

**Binaries**:
- `/usr/bin/pitrac` - CLI interface
- `/usr/lib/pitrac/pitrac_lm` - Launch monitor binary
- `/usr/lib/pitrac/web-server/` - Python web application

**Configuration**:
- `/etc/pitrac/pitrac.yaml` - Main config (system-wide)
- `/etc/pitrac/golf_sim_config.json` - Simulator settings
- `~/.pitrac/config/` - User overrides

**User Data**:
- `~/.pitrac/state/` - Runtime state
- `~/.pitrac/logs/` - Application logs
- `~/LM_Shares/Images/` - Captured images
- `~/LM_Shares/WebShare/` - Web-accessible data

**Test Resources**:
- `/usr/share/pitrac/test-images/` - Sample images
- `/usr/share/pitrac/test-suites/` - Test suites

**AI Models**:
- `/etc/pitrac/models/` - ONNX models for ball detection

## Development Workflow

### Updating PiTrac

To get the latest changes from GitHub:

```bash
cd ~/PiTrac
git pull
cd packaging
sudo ./build.sh dev
```

### Incremental Builds

After making code changes, rebuild with:

```bash
cd ~/PiTrac/packaging
sudo ./build.sh dev
```

This performs an **incremental build** (only rebuilds changed files):
- Build time: 30 seconds - 2 minutes
- Web server is automatically updated
- Services are restarted if they were running

### Clean Rebuild

To force a complete rebuild from scratch:

```bash
sudo ./build.sh dev force
```

Use this if you encounter build errors or want to ensure a clean state.

### Build Artifacts

The `build.sh` script uses cached artifacts from `packaging/deps-artifacts/`:
- These are checked into Git using Git LFS
- If missing, pull them with: `git lfs pull`
- They contain pre-built OpenCV, ActiveMQ, lgpio, msgpack

## Troubleshooting

### Build Fails with Missing Artifacts

```bash
cd ~/PiTrac/packaging
git lfs pull
sudo ./build.sh dev
```

### Web Server Won't Start

Check status and logs:

```bash
systemctl status pitrac-web
journalctl -u pitrac-web -n 50
```

Common causes:
- Python dependencies not installed
- ActiveMQ not running
- Port 8080 already in use

Fix:
```bash
# Reinstall Python dependencies
cd /usr/lib/pitrac/web-server
sudo pip3 install -r requirements.txt --break-system-packages

# Restart ActiveMQ
sudo systemctl restart activemq

# Restart web server
sudo systemctl restart pitrac-web
```

### Cannot Access Web Interface

1. Verify web server is running:
   ```bash
   systemctl status pitrac-web
   ```

2. Check firewall isn't blocking port 8080:
   ```bash
   sudo ufw status
   ```

3. Confirm correct IP address:
   ```bash
   hostname -I
   ```

4. Try accessing locally first:
   ```bash
   curl http://localhost:8080
   ```

### ActiveMQ Connection Issues

Verify ActiveMQ is running:

```bash
systemctl status activemq
```

Check port 61616 is listening:

```bash
sudo netstat -tln | grep 61616
```

If not running:

```bash
sudo systemctl start activemq
sudo systemctl enable activemq
```

### Camera Not Detected

Check boot configuration:

```bash
# Pi 5
cat /boot/firmware/config.txt | grep camera

# Pi 4
cat /boot/config.txt | grep camera
```

Should have: `camera_auto_detect=1`

Verify libcamera environment:

```bash
echo $LIBCAMERA_RPI_CONFIG_FILE
```

Should point to correct pipeline (vc4 for Pi4, pisp for Pi5).

Test cameras:

```bash
# Pi 5
rpicam-hello --list-cameras

# Pi 4
libcamera-hello --list-cameras
```

### Build Errors

If build fails, try a clean rebuild:

```bash
cd ~/PiTrac/packaging
sudo ./build.sh dev force
```

Check you have enough disk space:

```bash
df -h
```

Need at least 5GB free.

## Next Steps

Once PiTrac is running:

1. **Calibrate Your Cameras**: Use the Calibration Wizard in the web interface
2. **Configure Your Simulator**: Set up E6, GSPro, or TruGolf in the Configuration section
3. **Test the System**: Hit test shots and verify shot detection
4. **Fine-tune Settings**: Adjust detection parameters for your lighting and setup
5. **Review Documentation**: Check the [Camera Calibration Guide]({% link camera/cameras.md %}) for advanced calibration

## Getting Help

- **CLI Help**: Run `pitrac help` for all available commands
- **System Status**: Run `pitrac status` to check all services
- **Logs**: Check logs with `pitrac web logs` or `journalctl -u pitrac-web`
- **Discord Community**: Join the [PiTrac Discord](https://discord.gg/j9YWCMFVHN)
- **GitHub Issues**: Report bugs at [github.com/PiTracLM/PiTrac](https://github.com/PiTracLM/PiTrac/issues)

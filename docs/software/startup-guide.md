---
title: Startup Guide
layout: default
nav_order: 2
parent: Software
---

# PiTrac Startup Guide

This guide will walk you through getting PiTrac up and running on your Raspberry Pi.

## Prerequisites

- Raspberry Pi 5 with 8GB RAM
- Raspberry Pi OS Bookworm 64-bit
- Active internet connection for installation
- Sudo privileges on your Pi

## Installation

### Step 1: Clone the Repository

First, clone the PiTrac repository to your Raspberry Pi:

```bash
git clone https://github.com/pitraclm/pitrac.git
cd PiTrac
```

### Step 2: Run Developer Installation

Navigate to the packaging directory and run the developer installation script with sudo:

```bash
cd packaging
sudo ./build.sh dev
```

This installation process will:
- Check and install all required build dependencies
- Extract pre-built dependency artifacts (OpenCV, ActiveMQ, lgpio, etc.)
- Build the PiTrac binary from source
- Install the PiTrac CLI tool at `/usr/bin/pitrac`
- Install and configure the Python web server
- Set up ActiveMQ message broker
- Configure all necessary services

The installation typically takes 5-10 minutes on a Pi 5.

### Step 3: Start the Web Interface

Once installation is complete, start the PiTrac web server:

```bash
pitrac web start
```

You can verify the web server is running with:

```bash
pitrac web status
```

### Step 4: Access the Web Dashboard

Open a web browser and navigate to:

```
http://[YOUR_PI_IP_ADDRESS]:8080
```

To find your Pi's IP address, you can run:

```bash
pitrac web url
```

This will display both the local URL (http://localhost:8080) and the network URL for accessing from other devices.

## Web Interface Overview

The PiTrac web dashboard provides comprehensive control and monitoring capabilities:

### Main Dashboard
- **Real-time Shot Display**: View live golf shot metrics including ball speed, carry distance, launch angles, and spin rates
- **System Status Indicators**: Monitor the health of WebSocket connection, ActiveMQ broker, and camera systems
- **Shot Images Gallery**: Browse captured images from each shot

### Configuration
Access the configuration section to:
- **Adjust Camera Settings**: Configure camera types, lens parameters, and gain settings
- **Select Detection Methods**: Choose between HoughCircles, YOLO, or YOLO+SAHI ball detection
- **Configure Simulators**: Set up connectivity to E6, GSPro, or TruGolf
- **Manage Logging**: Control log levels and diagnostic outputs
- **Calibrate System**: Fine-tune strobing, spin analysis, and calibration parameters

The configuration interface features:
- Search functionality to quickly find settings
- Basic/Advanced view modes
- Real-time validation with error messages
- Change tracking and batch save operations
- Import/Export for backup and restore

### Process Control
The PiTrac Process section allows you to:
- **Start/Stop PiTrac**: Control the launch monitor processes
- **Monitor Status**: View real-time process status and health
- **Manage Dual Cameras**: Independent control of Camera 1 and Camera 2

### Calibration Wizard
The calibration section provides a 4-step wizard:
1. **Setup**: Select cameras and prepare for calibration
2. **Verify**: Check ball placement with live imaging
3. **Calibrate**: Run automatic or manual calibration
4. **Complete**: Review results and save parameters

### Testing Tools
The Testing section includes tools for:
- **Hardware Testing**: Test IR strobes and capture still images
- **Calibration Testing**: Verify ball detection and camera alignment
- **System Testing**: Run automated test suites and connectivity checks

### Logs Viewer
Monitor system operation through the Logs section:
- View real-time logs from PiTrac cameras, ActiveMQ, and web server
- Filter by service and log level
- Download logs for offline analysis

## Managing PiTrac Services

### Web Server Commands

```bash
# Start the web server
pitrac web start

# Stop the web server
pitrac web stop

# Restart the web server
pitrac web restart

# Check web server status
pitrac web status

# View web server logs
pitrac web logs

# Follow logs in real-time
pitrac web logs --follow
```

### PiTrac Process Control

The PiTrac launch monitor processes are managed through the web interface:

1. Navigate to the "PiTrac Process" section in the web dashboard
2. Use the Start/Stop buttons to control the launch monitor
3. Monitor the status indicators for each camera

## File Locations

After installation, PiTrac components are located at:

- **Web Server Code**: `/usr/lib/pitrac/web-server/`
  - `server.py` - Main FastAPI application
  - `listeners.py` - ActiveMQ message handlers
  - `managers.py` - Shot and session management
  - `static/` - Frontend assets
  - `templates/` - HTML templates

- **Configuration Files**: `/etc/pitrac/`
  - `pitrac.yaml` - Main configuration

- **PiTrac Binary**: `/usr/lib/pitrac/pitrac_lm`

- **User Data**: `~/.pitrac/`
  - `config/` - User-specific configuration overrides
  - `logs/` - Application logs
  - `state/` - Runtime state files

- **Test Resources**: `/usr/share/pitrac/test-images/`

## Troubleshooting

### Web Server Won't Start

Check the service status and logs:

```bash
pitrac web status
pitrac web logs --follow
```

Common issues:
- Port 8080 already in use
- Python dependencies not installed
- ActiveMQ not running

### Cannot Access Web Interface

1. Verify the web server is running:
   ```bash
   systemctl status pitrac-web
   ```

2. Check the firewall isn't blocking port 8080:
   ```bash
   sudo ufw status
   ```

3. Confirm the correct IP address:
   ```bash
   hostname -I
   ```

### ActiveMQ Connection Issues

Verify ActiveMQ is running:

```bash
systemctl status activemq
sudo systemctl start activemq  # If not running
```

Check ActiveMQ is listening on port 61616:

```bash
netstat -tln | grep 61616
```

### Build Errors During Installation

If the build fails, try a clean rebuild:

```bash
cd packaging
sudo ./build.sh dev force
```

This forces a complete rebuild from scratch.

## Next Steps

Once PiTrac is running:

1. **Calibrate Your Cameras**: Use the Calibration Wizard in the web interface
2. **Configure Your Simulator**: Set up E6, GSPro, or TruGolf connectivity
3. **Test the System**: Run test shots using the Testing tools
4. **Fine-tune Settings**: Adjust detection parameters for your setup

For detailed configuration options and advanced features, refer to the [Configuration Guide](configuration-guide.md).

## Development and Updates

To update PiTrac with the latest changes:

```bash
cd ~/PiTrac
git pull
cd packaging
sudo ./build.sh dev
```

For incremental builds (faster, only rebuilds changed files):

```bash
sudo ./build.sh dev
```

For a complete clean rebuild:

```bash
sudo ./build.sh dev force
```

The web server will be automatically updated during the build process. If the service was running, it will be restarted with the new code.
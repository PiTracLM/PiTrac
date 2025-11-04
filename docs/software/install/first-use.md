---
title: First Use
layout: default
nav_order: 2
parent: PiTrac Installation
grand_parent: Software
description: First-time setup for PiTrac including accessing the web dashboard, starting the launch monitor, and using the web interface for configuration and control.
keywords: pitrac first use, web dashboard access, start pitrac, golf monitor setup, web interface tutorial
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# First Use Guide

After installing PiTrac, you're ready to access the web interface and start tracking shots!

**Time Required:** ~10 minutes

**Difficulty:** Beginner

---

## Step 1: Access the Web Dashboard

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

{: .note }
**Note**: The default port is 8080. You can change this in the web server configuration if needed.

---

## Step 2: Start PiTrac

From the web dashboard:

1. Navigate to the **PiTrac Process** section
2. Click **Start** to launch the launch monitor
3. Monitor the status indicators for Camera 1 and Camera 2

The web interface will show:
- Camera connection status
- ActiveMQ broker connectivity
- WebSocket connection to browser
- Real-time shot data when balls are hit

---

## Web Interface Overview

The PiTrac web dashboard provides comprehensive control across several sections:

### Main Dashboard

**Real-time Shot Display**:
- Ball speed and carry distance
- Launch angles (horizontal and vertical)
- Spin rates (backspin and sidespin)
- Shot trajectory visualization

**System Status Indicators**:
- WebSocket connection status
- ActiveMQ broker connectivity
- Camera 1 and Camera 2 status
- PiTrac process state

**Shot Images Gallery**:
- Browse captured images from recent shots
- View dual-camera synchronized captures
- Analyze ball position and strobing

### Configuration

**Camera Settings**:
- Configure camera types (Pi Global Shutter, Innomaker)
- Lens parameters (focal length, sensor size)
- Camera gain and exposure settings
- Flip and rotation adjustments

**Detection Methods**:
- **HoughCircles**: Traditional circle detection
- **YOLO**: AI-based ball detection
- **YOLO+SAHI**: Enhanced detection for small objects

**Simulator Integration**:
- **E6 Connect**: Configure E6 simulator connectivity
- **GSPro**: Set up GSPro communication
- **TruGolf**: Configure TruGolf interfaces
- Port and protocol settings

**Logging**:
- Control log levels (debug, info, warning, error)
- Enable/disable specific logging categories
- Diagnostic verbosity settings

**Calibration**:
- Fine-tune strobing parameters
- Adjust spin analysis thresholds
- Camera positioning settings
- Ball detection sensitivity

### Process Control

**Start/Stop PiTrac**:
- Launch the PiTrac process
- Stop running processes cleanly
- Monitor resource usage
- View process health

**Monitor Status**:
- Real-time health monitoring
- Service dependency checks
- Error alerts and warnings

**Dual Camera Management**:
- Independent camera control
- Per-camera configuration
- Camera status indicators

### Calibration Wizard

The calibration wizard provides a 4-step process:

**Step 1: Setup Cameras**
- Verify both cameras are detected
- Set camera types and lens parameters
- Confirm camera positioning

**Step 2: Verify Ball Placement**
- Check ball is visible in both cameras
- Ensure proper lighting
- Validate strobing

**Step 3: Run Calibration**
- Automated calibration sequence
- Takes multiple reference shots
- Calculates camera angles and distances

**Step 4: Review and Save Results**
- View calculated parameters
- Compare before/after accuracy
- Save calibration data

{: .note }
**Tip**: Run calibration after any camera position changes or hardware adjustments.

### Testing Tools

**Hardware Testing**:
- IR strobe test (verify strobes fire)
- Still image capture (test cameras)
- Ball detection test (verify algorithms)

**Calibration Verification**:
- Test shots against known references
- Accuracy validation
- Consistency checks

**Automated Test Suites**:
- Comprehensive system tests
- Regression testing
- Performance benchmarks

### Logs Viewer

**Real-time Logs**:
- View logs from all services
- Filter by service (pitrac, activemq, web-server)
- Filter by log level (debug, info, warning, error)

**Log Management**:
- Download logs for offline analysis
- Clear old logs
- Configure log retention

---

## First Shot Checklist

Before hitting your first shot:

- [ ] Web dashboard accessible at http://raspberrypi.local:8080
- [ ] Both cameras showing "Connected" status
- [ ] ActiveMQ broker showing "Connected"
- [ ] PiTrac process showing "Running"
- [ ] Cameras positioned and secured
- [ ] Ball on tee and visible in camera views
- [ ] IR strobes tested and working
- [ ] Simulator connected (if using E6/GSPro/TruGolf)

---

## Making Your First Shot

1. **Open the main dashboard** in your web browser
2. **Verify all systems are green** (cameras, ActiveMQ, WebSocket)
3. **Set up a ball** on the tee in view of both cameras
4. **Hit the shot!**
5. **Watch the dashboard** display real-time shot data

The web interface will show:
- Ball speed (mph or km/h)
- Carry distance (yards or meters)
- Launch angle (degrees)
- Launch direction (degrees)
- Backspin (rpm)
- Sidespin (rpm)
- Captured images from both cameras

---

## Understanding Shot Data

### Ball Speed
- **What it measures**: Initial velocity after impact
- **Typical values**: 100-180 mph for drivers
- **Accuracy**: ±1 mph with proper calibration

### Launch Angle
- **What it measures**: Vertical angle off ground
- **Typical values**: 8-15° for drivers
- **Lower = less carry, higher = more carry (to a point)**

### Launch Direction
- **What it measures**: Horizontal angle (left/right)
- **0° = straight, positive = right, negative = left**

### Spin
- **Backspin**: Affects carry distance and ball flight shape
- **Sidespin**: Causes hooks (negative) or slices (positive)
- **Typical driver backspin**: 2000-3000 rpm

---

## Next Steps

**Now that PiTrac is running:**

1. **Calibrate Your Cameras** - [Calibration Guide]({% link camera/cameras.md %})
2. **Configure Your Simulator** - Set up E6, GSPro, or TruGolf in Configuration section
3. **Hit Test Shots** - Verify shot detection and accuracy
4. **Fine-tune Settings** - Adjust detection parameters for your setup
5. **Check Managing Guide** - [Managing PiTrac]({% link software/install/managing.md %})

**If you encounter issues:**
- **[Troubleshooting Guide]({% link software/install/troubleshooting.md %})** - Common problems and solutions

**Return to:**
- **[Installation Overview]({% link software/pitrac-install.md %})**

---

## Quick Tips

**Improve Detection Accuracy**:
- Ensure consistent lighting (avoid windows, bright sunlight)
- Clean camera lenses regularly
- Use high-quality balls (avoid scuffed or dirty balls)
- Keep cameras stable and secure

**Web Interface Not Responding?**
```bash
# Check service status
systemctl status pitrac-web

# Restart if needed
sudo systemctl restart pitrac-web
```

**Camera Status Shows Disconnected?**
```bash
# Test cameras
rpicam-hello --list-cameras  # Pi 5
libcamera-hello --list-cameras  # Pi 4

# Check PiTrac logs
tail -f ~/.pitrac/logs/pitrac.log
```

**No Shot Data Appearing?**
- Verify ball is visible in both camera views
- Check IR strobes are firing (should see brief flashes)
- Ensure ActiveMQ is connected
- Try adjusting detection sensitivity in Configuration
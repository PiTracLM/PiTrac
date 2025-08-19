---
title: Running PiTrac
layout: default
nav_order: 3
parent: Software
description: How to run the PiTrac launch monitor after setup and building - single-Pi, dual-Pi modes, and testing
keywords: pitrac run, launch monitor, single pi, dual pi, camera setup, testing
toc: true
---

# Running PiTrac Launch Monitor

So you've built PiTrac. Time to see it in action. This guide covers everything from basic single-Pi operation to dual-Pi setups and testing without hardware.

## Prerequisites

Before running PiTrac, make sure you've completed:
1. [Automated Setup]({% link software/automated-setup.md %}) - All dependencies installed
2. Build PiTrac (option 3 in the menu) - The launch monitor is compiled

## The Easy Way: Using the Menu

The simplest way to run PiTrac is through the Dev menu:

```bash
cd ~/Dev
./run.sh
# Choose option 4: Run PiTrac Launch Monitor
```

You'll see a submenu with all the runtime options:
```
1) Run Single-Pi Setup
2) Run Two-Pi Setup (Camera 1)  
3) Run Two-Pi Setup (Camera 2)
4) Start Camera 1 (Background)
5) Start Camera 2 (Background)
6) View Running Processes
7) View Background Logs
8) Stop All PiTrac Processes
9) Test Strobe Light
10) Test Camera Trigger
11) Configure Run Settings
12) Show Command-Line Help
```

## Single-Pi Setup

The simplest configuration - everything runs on one Pi with one camera.

### Quick Start
From the menu, choose option 1 "Run Single-Pi Setup". That's it.

### What Happens
The system:
1. Checks for required services (ActiveMQ, TomEE)
2. Loads your environment variables
3. Verifies camera devices exist
4. Starts the launch monitor

You'll see output like:
```
[INFO] Starting PiTrac in Single-Pi mode
[INFO] Logging level: info
[INFO] System mode: kNormal
[SUCCESS] All runtime dependencies OK
```

Press Ctrl+C to stop.

### Command Line
Want to skip the menu?
```bash
~/Dev/scripts/run_pitrac.sh run
```

## Dual-Pi Setup

Got two Pis? This gives you two camera angles for better ball tracking.

### How It Works
- **Pi 1 (Primary)**: Runs Camera 1, controls triggering, sends requests
- **Pi 2 (Secondary)**: Runs Camera 2, responds to Camera 1 requests
- **Communication**: Via ActiveMQ message broker (not SSH)

### Setting Up Pi 2 (Camera 2)
On your secondary Pi:
```bash
cd ~/Dev
./run.sh
# Choose option 4, then option 3: "Run Two-Pi Setup (Camera 2)"
```

Or directly:
```bash
~/Dev/scripts/run_pitrac.sh cam2
```

**Important**: Start Camera 2 FIRST. It needs to be listening before Camera 1 starts.

### Setting Up Pi 1 (Camera 1)
On your primary Pi:
```bash
cd ~/Dev
./run.sh
# Choose option 4, then option 2: "Run Two-Pi Setup (Camera 1)"
```

Or directly:
```bash
~/Dev/scripts/run_pitrac.sh cam1
```

### Running Both on One Pi (Testing)
Need to test dual-Pi logic on a single machine? Use background mode:

1. Start Camera 2 in background (menu option 5)
2. Start Camera 1 in background (menu option 4)
3. View running processes (option 6)
4. Check logs (option 7)
5. Stop all when done (option 8)

The system manages PIDs and logs for you:
- Camera 1 log: `/tmp/pitrac_cam1.log`
- Camera 2 log: `/tmp/pitrac_cam2.log`

## Configuration

All runtime settings are in `~/Dev/scripts/defaults/run-pitrac.yaml`:

```yaml
# Pi configuration mode
pi_mode: "single"           # or "dual"

# Logging level
logging_level: "info"        # trace, debug, info, warning, error

# System mode
system_mode: "kNormal"       # kNormal, kTest, kCalibration

# Auto-restart on failure
auto_restart: 0              # 0=no, 1=yes
max_restarts: 3
restart_delay: 5

# Enable services
enable_web_server: 1         # Web interface
enable_msg_broker: 1         # ActiveMQ messaging
enable_e6: 0                 # E6 simulator
enable_gspro: 0              # GSPro simulator
```

Edit this file to change behavior, or choose option 11 in the menu for guided editing.

## Hardware Testing

### Test Strobe Light
Make sure your strobe is working:
```bash
~/Dev/scripts/run_pitrac.sh test-strobe
```

You should see dark-reddish pulses in the LED lens for 10 seconds.

### Test Camera Trigger
For dual-Pi setups, test camera synchronization:
```bash
~/Dev/scripts/run_pitrac.sh test-trigger
```

This shows instructions for:
1. Setting Camera 2 to external trigger mode
2. Running camera test on Camera 2
3. Running pulse test on Camera 1
4. Verifying Camera 2 captures when Camera 1 triggers

## Environment Variables

PiTrac needs several environment variables set. The run script loads these from your shell config (`.bashrc` or `.zshrc`):

- `PITRAC_ROOT` - Base directory for PiTrac software
- `PITRAC_BASE_IMAGE_LOGGING_DIR` - Where images are saved
- `PITRAC_WEBSERVER_SHARE_DIR` - Web server share directory
- `PITRAC_MSG_BROKER_FULL_ADDRESS` - ActiveMQ connection

If these aren't set, the script uses defaults but warns you.

## Using Existing Run Scripts

If you have custom run scripts in `RunScripts/`, the system uses those instead of command-line parameters:

- `runSinglePi.sh` - For single-Pi mode
- `runCam1.sh` - For Camera 1 in dual-Pi
- `runCam2.sh` - For Camera 2 in dual-Pi
- `runPulseTest.sh` - For strobe testing

## Auto-Restart Feature

Enable automatic restart on crashes:

```yaml
# In run-pitrac.yaml
auto_restart: 1
max_restarts: 3
restart_delay: 5
```

Or use restart commands:
```bash
~/Dev/scripts/run_pitrac.sh restart-single
~/Dev/scripts/run_pitrac.sh restart-cam1
~/Dev/scripts/run_pitrac.sh restart-cam2
```

## Troubleshooting Runtime Issues

### PiTrac Won't Start

**"PiTrac is not built!"**
- Run build from menu (option 3)
- Check binary exists: `ls $PITRAC_ROOT/ImageProcessing/build/pitrac_lm`

**"No camera devices found"**
- Check cameras: `ls /dev/video*`
- Run `rpicam-hello` to test
- Reconfigure cameras in menu option 2

### Services Not Running

**"ActiveMQ Broker not running"**
- Non-critical for basic operation
- Start manually: `sudo systemctl start activemq`
- Only needed for dual-Pi or simulators

**"TomEE web server not running"**
- Non-critical for basic operation
- Start manually: `sudo systemctl start tomee`
- Only needed for web interface

### Process Management

View all PiTrac processes:
```bash
pgrep -a pitrac_lm
```

Kill stuck processes:
```bash
pkill -f pitrac_lm
```

Check background logs:
```bash
tail -f /tmp/pitrac_cam1.log
tail -f /tmp/pitrac_cam2.log
```

## Command-Line Options

For direct control without the menu:

```bash
# Show all options
~/Dev/scripts/run_pitrac.sh help

# Basic commands
run           - Run based on configured mode
cam1          - Run Camera 1 (dual-Pi primary)
cam2          - Run Camera 2 (dual-Pi secondary)
test-strobe   - Test strobe light
test-trigger  - Show camera trigger test instructions

# With auto-restart
restart-single - Run single-Pi with auto-restart
restart-cam1   - Run Camera 1 with auto-restart
restart-cam2   - Run Camera 2 with auto-restart
```

## Testing Without Cameras

No hardware connected? Use the test processor - see the [Testing Without Hardware]({% link troubleshooting/testing-without-hardware.md %}) guide.

## Next Steps

PiTrac running? Great! Now:
1. [Camera Calibration]({% link camera/camera-calibration.md %}) - Get accurate measurements
2. [Simulator Integration]({% link integration/integration.md %}) - Connect to GSPro or E6
3. [Open Interface]({% link integration/open-interface.md %}) - Access the browser UI

Having issues? Check the [Troubleshooting Guide]({% link troubleshooting/troubleshooting.md %}).
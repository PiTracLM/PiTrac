---
title: Managing PiTrac
layout: default
nav_order: 3
parent: PiTrac Installation
grand_parent: Software
description: Complete guide to managing PiTrac including CLI commands, service management, file locations, updating software, and development workflows.
keywords: pitrac commands, manage pitrac services, file locations raspberry pi, update pitrac, development workflow
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# Managing PiTrac

Everything you need to know about managing, updating, and maintaining your PiTrac installation.

---

## Command Reference

### Web Server Commands

The web server provides the user interface and configuration management:

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

{: .warning }
**Important**: Use the web interface to start/stop the PiTrac launch monitor process. It is NOT a systemd service.

**From the web interface**:
- Navigate to "PiTrac Process" section
- Click Start/Stop buttons
- Monitor camera status indicators

**Direct command line** (advanced):
```bash
# Run manually (debugging)
/usr/lib/pitrac/pitrac_lm --logging_level=debug

# View PiTrac process logs
tail -f ~/.pitrac/logs/pitrac.log
```

### ActiveMQ Management

ActiveMQ provides message passing between components:

```bash
# Check broker status
systemctl status activemq

# Restart broker (if needed)
sudo systemctl restart activemq

# View ActiveMQ logs
journalctl -u activemq -f
```

**ActiveMQ Web Console**:
```
URL: http://raspberrypi.local:8161/admin
Default credentials: admin/admin
```

Use the web console to:
- Monitor queue depths
- View message statistics
- Inspect active connections
- Troubleshoot communication issues

### System Status

```bash
# Check all PiTrac services
pitrac status

# View system logs
journalctl -u pitrac-web -f
journalctl -u activemq -f

# Check camera detection
rpicam-hello --list-cameras  # Pi 5
libcamera-hello --list-cameras  # Pi 4

# Check disk space
df -h

# Check memory usage
free -h

# Monitor CPU temperature
vcgencmd measure_temp
```

---

## File Locations

Understanding where PiTrac stores its files helps with troubleshooting and maintenance.

### Binaries

**Installed executables**:
- `/usr/bin/pitrac` - CLI interface (Bashly-generated wrapper)
- `/usr/lib/pitrac/pitrac_lm` - Launch monitor binary (C++ application)
- `/usr/lib/pitrac/web-server/` - Python web application (FastAPI)

**Camera tools**:
- `/usr/lib/pitrac/ImageProcessing/CameraTools/` - Camera utilities and calibration scripts

### Configuration

**System-wide templates**:
- `/etc/pitrac/pitrac.yaml` - Main config template (reference only, don't edit)
- `/etc/pitrac/models/` - ONNX AI models for ball detection

**User configuration** (these are YOUR settings):
- `~/.pitrac/config/user_settings.json` - Your configuration overrides
- `~/.pitrac/config/calibration_data.json` - Camera calibration results
- `~/.pitrac/config/generated_golf_sim_config.json` - Runtime configuration (auto-generated)

{: .note }
**Configuration Best Practice**: Always use the web interface to change settings. The web server manages the three-tier configuration system and generates the runtime config automatically.

### User Data

**Runtime data**:
- `~/.pitrac/state/` - Runtime state files
- `~/.pitrac/logs/` - Application logs (pitrac.log, web-server.log)
- `~/.pitrac/run/` - PID files for process management

**Captured data**:
- `~/LM_Shares/Images/` - Captured images from shots
- `~/LM_Shares/WebShare/` - Web-accessible data

**Calibration resources**:
- `~/.pitrac/calibration/` - Calibration tools (checkerboard images, scripts)

### Test Resources

**Pre-installed test files**:
- `/usr/share/pitrac/test-images/` - Sample images (teed-ball.png, strobed.png)
- `/usr/share/pitrac/test-suites/` - Automated test suites

### Web Server Configuration

**Web server metadata**:
- `/usr/lib/pitrac/web-server/configurations.json` - Configuration metadata (283 settings with defaults, validation, descriptions)

This file defines all available settings, their types, defaults, and validation rules. The web interface uses this to dynamically generate the configuration UI.

---

## Development Workflow

### Updating PiTrac

To get the latest changes from GitHub:

```bash
cd ~/PiTrac
git pull
cd packaging
sudo ./build.sh dev
```

**What happens**:
- Downloads latest code from GitHub
- Detects if dependencies changed (rarely)
- Rebuilds only changed files (incremental)
- Updates web server if changed
- Restarts services if they were running

**Time**: 30 seconds - 2 minutes for incremental builds

### Incremental Builds

After making code changes locally:

```bash
cd ~/PiTrac/packaging
sudo ./build.sh dev
```

This performs an **incremental build**:
- Only rebuilds changed files
- Copies updated web server files
- Restarts services automatically
- Preserves configuration and calibration

**When to use**:
- After pulling updates
- After modifying C++ source
- After changing Python web server
- Testing local changes

### Clean Rebuild

To force a complete rebuild from scratch:

```bash
cd ~/PiTrac/packaging
sudo ./build.sh dev force
```

**What it does**:
- Deletes all build artifacts
- Rebuilds entire C++ binary
- Reinstalls all components
- Takes 2-5 minutes

**When to use**:
- Build errors that persist
- Dependency changes
- Switching between branches with significant changes
- Want to ensure clean state

### Build Artifacts

The `build.sh` script uses cached artifacts from `packaging/deps-artifacts/`:
- Pre-built OpenCV 4.11.0 (saves ~60 minutes)
- Pre-built ActiveMQ-CPP 3.9.5
- Pre-built lgpio 0.2.2
- Pre-built msgpack-cxx 6.1.1
- Pre-built ONNX Runtime 1.17.3

**If artifacts are missing**:
```bash
cd ~/PiTrac
git lfs pull
```

These artifacts are stored using Git LFS (Large File Storage) and are checked into the repository.

---

## Build Mode Reference

```bash
# Full developer install (recommended)
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

---

## Backup and Restore

### Backup Your Configuration

Save your calibration and settings:

```bash
# Create backup directory
mkdir -p ~/pitrac-backup

# Backup configuration
cp -r ~/.pitrac/config/ ~/pitrac-backup/

# Backup captured images (optional, can be large)
cp -r ~/LM_Shares/Images/ ~/pitrac-backup/
```

### Restore Configuration

```bash
# Restore configuration
cp -r ~/pitrac-backup/config/* ~/.pitrac/config/

# Restart services
sudo systemctl restart pitrac-web
```

---

## Disk Space Management

PiTrac can generate many images over time:

### Check Disk Usage

```bash
# Check overall disk space
df -h

# Check PiTrac data sizes
du -sh ~/.pitrac/
du -sh ~/LM_Shares/Images/
du -sh ~/.pitrac/logs/
```

### Clean Up Old Data

```bash
# Remove old images (be careful!)
rm ~/LM_Shares/Images/old-shot-*.png

# Clear old logs
rm ~/.pitrac/logs/*.log.old

# Clear PiTrac process logs
echo "" > ~/.pitrac/logs/pitrac.log
```

{: .warning }
**Important**: Only delete files you're sure you don't need. Configuration files and calibration data should be backed up before removal.

---

## Service Management

### Systemd Services

PiTrac uses two systemd services:

**activemq.service** - Message broker (system service)
```bash
sudo systemctl start activemq
sudo systemctl stop activemq
sudo systemctl restart activemq
sudo systemctl status activemq
sudo systemctl enable activemq   # Start on boot
sudo systemctl disable activemq  # Don't start on boot
```

**pitrac-web.service** - Web dashboard (system service)
```bash
sudo systemctl start pitrac-web
sudo systemctl stop pitrac-web
sudo systemctl restart pitrac-web
sudo systemctl status pitrac-web
sudo systemctl enable pitrac-web   # Start on boot
sudo systemctl disable pitrac-web  # Don't start on boot
```

{: .note }
**Note**: The PiTrac launch monitor process is NOT a service. It's controlled through the web interface.

---

## Uninstalling PiTrac

If you need to completely remove PiTrac:

### Stop All Services

```bash
# Stop web server
sudo systemctl stop pitrac-web
sudo systemctl disable pitrac-web

# Stop ActiveMQ
sudo systemctl stop activemq
sudo systemctl disable activemq
```

### Remove Binaries and Libraries

```bash
# Remove installed files
sudo rm -rf /usr/lib/pitrac/
sudo rm /usr/bin/pitrac
sudo rm -rf /etc/pitrac/
sudo rm -rf /usr/share/pitrac/
```

### Remove User Data

{: .warning }
**Warning**: This deletes all your configuration, calibration, and captured images!

```bash
rm -rf ~/.pitrac/
rm -rf ~/LM_Shares/
```

### Remove System Packages (Optional)

```bash
# Remove PiTrac-specific dependencies
sudo apt remove activemq libapr1 libaprutil1
```

{: .note }
Most dependencies (OpenCV, Boost, libcamera) are also used by other software. Only remove if you're sure nothing else needs them.

---

## Next Steps

**For common issues:**
- **[Troubleshooting Guide]({% link software/install/troubleshooting.md %})** - Solutions to common problems

**To improve your setup:**
- **[Camera Calibration]({% link camera/cameras.md %})** - Advanced calibration techniques
- **[Simulator Integration]({% link simulator-integration.md %})** - Connect to E6, GSPro, TruGolf

**Return to:**
- **[Installation Overview]({% link software/pitrac-install.md %})**
- **[First Use Guide]({% link software/install/first-use.md %})**

---

## Quick Command Reference

| Task | Command |
|------|---------|
| Start web server | `pitrac web start` |
| Stop web server | `pitrac web stop` |
| Check status | `pitrac status` |
| View logs | `pitrac web logs --follow` |
| Update PiTrac | `cd ~/PiTrac && git pull && cd packaging && sudo ./build.sh dev` |
| Clean rebuild | `sudo ./build.sh dev force` |
| Test cameras | `rpicam-hello --list-cameras` |
| Check disk space | `df -h` |
| Backup config | `cp -r ~/.pitrac/config/ ~/pitrac-backup/` |

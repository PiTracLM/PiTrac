---
title: Troubleshooting
layout: default
nav_order: 4
parent: PiTrac Installation
grand_parent: Software
description: Comprehensive troubleshooting guide for PiTrac installation issues including build failures, service problems, camera detection, and web interface access.
keywords: pitrac troubleshooting, fix pitrac errors, camera not detected, build fails, web server not starting
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# Installation Troubleshooting

Solutions to common issues encountered during and after PiTrac installation.

---

## Build Issues

### Build Fails with Missing Artifacts

**Symptom**: Build script reports missing pre-built artifacts

**Solution**:
```bash
cd ~/PiTrac/packaging
git lfs pull
sudo ./build.sh dev
```

**Why it happens**: Pre-built dependencies (OpenCV, ActiveMQ, etc.) are stored using Git LFS. If Git LFS wasn't installed when you cloned, these files won't be downloaded.

**Prevention**:
```bash
# Install Git LFS before cloning
sudo apt install git-lfs
git lfs install
```

### Build Errors with Dependencies

**Symptom**: Compilation fails with missing headers or libraries

**Solution**:
```bash
# Ensure all system dependencies are installed
cd ~/PiTrac/packaging
sudo apt update
sudo ./build.sh deps  # Reinstall dependencies

# Then rebuild
sudo ./build.sh dev force
```

**Common missing dependencies**:
- `build-essential` - C++ compiler
- `meson`, `ninja-build` - Build tools
- `pkg-config` - Library detection
- `libboost-*` - Boost libraries

### Insufficient Disk Space

**Symptom**: Build fails with "No space left on device"

**Check disk space**:
```bash
df -h
```

**Solution**:
```bash
# Need at least 5GB free, 10GB recommended

# Free up space
sudo apt clean
sudo apt autoremove

# Remove old logs
rm ~/.pitrac/logs/*.log.old

# Move images to external storage
mv ~/LM_Shares/Images/ /mnt/external/
```

### Build Hangs or Takes Too Long

**Symptom**: Build appears stuck or takes more than 30 minutes

**Possible causes**:
1. Building OpenCV from source instead of using pre-built (60+ minutes)
2. Low memory causing swapping
3. Network issues downloading packages

**Solution**:
```bash
# Verify pre-built artifacts exist
ls -lh packaging/deps-artifacts/

# Check memory usage
free -h

# If low memory, increase swap
sudo dphys-swapfile swapoff
sudo nano /etc/dphys-swapfile
# Set CONF_SWAPSIZE=2048
sudo dphys-swapfile setup
sudo dphys-swapfile swapon
```

---

## Service Issues

### Web Server Won't Start

**Symptom**: `pitrac web start` fails or service shows inactive

**Diagnosis**:
```bash
systemctl status pitrac-web
journalctl -u pitrac-web -n 50
```

**Common causes and solutions**:

**1. Python dependencies not installed**
```bash
cd /usr/lib/pitrac/web-server
sudo pip3 install -r requirements.txt --break-system-packages
sudo systemctl restart pitrac-web
```

**2. ActiveMQ not running**
```bash
sudo systemctl status activemq
sudo systemctl start activemq
sudo systemctl restart pitrac-web
```

**3. Port 8080 already in use**
```bash
# Check what's using port 8080
sudo netstat -tlnp | grep 8080

# Kill the process or change PiTrac port
# (Edit /usr/lib/pitrac/web-server/main.py to change port)
```

**4. File permissions**
```bash
# Fix ownership
sudo chown -R $USER:$USER /usr/lib/pitrac/web-server/
sudo systemctl restart pitrac-web
```

### ActiveMQ Connection Issues

**Symptom**: Web interface shows "ActiveMQ disconnected" or PiTrac can't send messages

**Verify ActiveMQ is running**:
```bash
systemctl status activemq
```

**Check port 61616 is listening**:
```bash
sudo netstat -tln | grep 61616
# Should show: tcp6  0  0 :::61616  :::*  LISTEN
```

**If not running**:
```bash
sudo systemctl start activemq
sudo systemctl enable activemq

# Check logs for errors
journalctl -u activemq -f
```

**If still not working**:
```bash
# Reinstall ActiveMQ configuration
cd ~/PiTrac/packaging
sudo ./src/lib/activemq-service-install.sh

# Restart
sudo systemctl restart activemq
```

### PiTrac Process Won't Start

**Symptom**: Click "Start" in web interface but process stays stopped

**Check logs**:
```bash
tail -f ~/.pitrac/logs/pitrac.log
```

**Common causes**:

**1. Cameras not detected**
```bash
# Test cameras
rpicam-hello --list-cameras  # Pi 5
libcamera-hello --list-cameras  # Pi 4

# Should show 2 cameras
```

**2. Configuration file errors**
```bash
# Check configuration exists
ls -l ~/.pitrac/config/

# Regenerate configuration through web interface
# Or manually:
rm ~/.pitrac/config/generated_golf_sim_config.json
# Restart web server to regenerate
```

**3. Binary permissions**
```bash
# Ensure binary is executable
sudo chmod +x /usr/lib/pitrac/pitrac_lm

# Test manually
/usr/lib/pitrac/pitrac_lm --logging_level=debug
```

---

## Camera Issues

### Camera Not Detected

**Symptom**: `rpicam-hello --list-cameras` shows no cameras or error

**Check boot configuration**:
```bash
# Pi 5
cat /boot/firmware/config.txt | grep camera

# Pi 4
cat /boot/config.txt | grep camera
```

**Should have**: `camera_auto_detect=1`

**If missing, add it**:
```bash
# Pi 5
sudo nano /boot/firmware/config.txt

# Pi 4
sudo nano /boot/config.txt

# Add this line:
camera_auto_detect=1

# Save and reboot
sudo reboot now
```

**Verify ribbon cable connection**:
- Power off Pi
- Check cable is fully inserted in both camera and Pi
- Check cable orientation (blue side up for most cameras)
- Check for damage to cable or connectors

**Check libcamera environment**:
```bash
echo $LIBCAMERA_RPI_CONFIG_FILE
```

Should point to correct pipeline:
- Pi 4: `/usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml`
- Pi 5: `/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml`

### Only One Camera Detected

**Symptom**: `rpicam-hello --list-cameras` shows 1 camera instead of 2

**Check both ribbon cables**:
- Verify both cameras are connected
- Check both CSI ports on the Pi
- Ensure both cables are secure

**Identify which camera is missing**:
```bash
# Pi 5
rpicam-hello --list-cameras
# Shows camera IDs and ports

# Try each camera individually
rpicam-hello --camera 0
rpicam-hello --camera 1
```

**Hardware checks**:
- Swap ribbon cables between cameras
- Test suspected bad camera on known-good port
- Try different ribbon cable

### Camera Images Too Dark/Bright

**Symptom**: Captured images are unusable due to exposure

**Solution**: Adjust camera settings in web interface
- Navigate to Configuration → Camera Settings
- Adjust "Gain" or "Exposure Time"
- For darker images: Increase gain (try 8-12)
- For brighter images: Decrease gain (try 2-4)
- Test with "Hardware Test" → "Capture Still Image"

**Lighting considerations**:
- IR strobes may need adjustment
- Avoid direct sunlight or bright windows
- Consistent indoor lighting works best

---

## Web Interface Issues

### Cannot Access Web Interface

**Symptom**: Browser can't connect to http://raspberrypi.local:8080

**1. Verify web server is running**:
```bash
systemctl status pitrac-web
# Should show "active (running)"
```

**2. Check firewall isn't blocking port 8080**:
```bash
sudo ufw status
# If active, allow port 8080:
sudo ufw allow 8080
```

**3. Confirm correct IP address**:
```bash
hostname -I
# Note the IP (e.g., 192.168.1.100)

# Try accessing with IP instead of hostname
# http://192.168.1.100:8080
```

**4. Test locally on the Pi**:
```bash
curl http://localhost:8080
# Should return HTML
```

**5. Check Pi is on same network**:
```bash
# From your computer, ping the Pi
ping raspberrypi.local

# Or use IP address
ping 192.168.1.100
```

### Web Interface Loads But Shows Errors

**Symptom**: Page loads but displays connection errors or missing data

**Check WebSocket connection**:
- Open browser developer console (F12)
- Look for WebSocket errors
- Verify WebSocket status indicator in UI

**Check ActiveMQ connectivity**:
- Verify ActiveMQ status indicator
- Check ActiveMQ logs: `journalctl -u activemq -f`

**Clear browser cache**:
```bash
# Hard refresh: Ctrl+Shift+R (or Cmd+Shift+R on Mac)
```

**Check web server logs**:
```bash
journalctl -u pitrac-web -n 100
# Look for Python errors or stack traces
```

### Configuration Changes Don't Take Effect

**Symptom**: Changes in web interface don't affect PiTrac behavior

**Solution**:
1. Some settings require PiTrac restart
2. Look for "Restart Required" indicator in UI
3. Stop and restart PiTrac process through web interface

**If still not working**:
```bash
# Check configuration was saved
cat ~/.pitrac/config/user_settings.json

# Verify generated config was updated
cat ~/.pitrac/config/generated_golf_sim_config.json

# Restart web server
sudo systemctl restart pitrac-web
```

---

## Shot Detection Issues

### No Shot Data Appearing

**Symptom**: Hit shots but no data appears in web interface

**Checklist**:
- [ ] PiTrac process is running (check status indicator)
- [ ] Both cameras show "Connected"
- [ ] ActiveMQ shows "Connected"
- [ ] Ball is visible in camera views
- [ ] IR strobes are firing (should see brief flashes)
- [ ] Ball is on tee in expected location

**Test ball detection**:
1. Navigate to Testing Tools
2. Run "Ball Detection Test"
3. Review results and adjust sensitivity if needed

**Check logs for clues**:
```bash
tail -f ~/.pitrac/logs/pitrac.log
# Look for:
# - Camera errors
# - Ball detection failures
# - Processing errors
```

### Inaccurate Shot Data

**Symptom**: Shot data seems wrong (speed too high/low, angles off)

**Calibration needed**:
1. Run Calibration Wizard in web interface
2. Verify camera positions haven't moved
3. Check calibration data: `cat ~/.pitrac/config/calibration_data.json`

**Environmental factors**:
- Lighting changes (time of day, room lights)
- Camera position shifted
- Ball type changed (different size/reflectivity)
- Net or objects in camera view

**Detection method**:
- Try different detection method (HoughCircles vs YOLO)
- Adjust detection sensitivity in Configuration

---

## System Performance Issues

### High CPU Usage

**Check what's using CPU**:
```bash
top
# Look for pitrac_lm, python3, or other processes
```

**Common causes**:
- Multiple PiTrac instances running
- Detection method too intensive (YOLO+SAHI on Pi 4)
- Logging level set too high (debug)

**Solutions**:
```bash
# Kill extra processes
pkill -f pitrac_lm

# Reduce logging verbosity in Configuration
# Switch to lighter detection method (HoughCircles)
```

### High Memory Usage

**Check memory**:
```bash
free -h
```

**If low on memory**:
```bash
# Increase swap space
sudo dphys-swapfile swapoff
sudo nano /etc/dphys-swapfile
# Set CONF_SWAPSIZE=2048
sudo dphys-swapfile setup
sudo dphys-swapfile swapon
```

**Consider**:
- Upgrade to Pi 5 with 8GB RAM
- Reduce image resolution in Configuration
- Use lighter detection method

### System Feels Slow

**Check temperature**:
```bash
vcgencmd measure_temp
# Should be < 80°C
```

**If overheating**:
- Ensure proper ventilation
- Add heatsinks or fan
- Reduce processing load

**Check disk I/O**:
```bash
iostat -x 2
# High %util means disk bottleneck
```

**Consider**:
- Upgrade to NVMe SSD (Pi 5)
- Use faster SD card (A2 class)
- Reduce image saving frequency

---

## Getting Additional Help

If you're still experiencing issues:

**1. Check Logs**:
```bash
# PiTrac process logs
tail -f ~/.pitrac/logs/pitrac.log

# Web server logs
journalctl -u pitrac-web -f

# ActiveMQ logs
journalctl -u activemq -f

# System logs
dmesg | tail -n 50
```

**2. Run System Status**:
```bash
pitrac status
```

**3. Gather System Information**:
```bash
# OS version
cat /etc/os-release

# Architecture (should be aarch64)
uname -m

# Disk space
df -h

# Memory
free -h

# Camera detection
rpicam-hello --list-cameras
```

**4. Get Help from Community**:
- **[Discord Community](https://discord.gg/j9YWCMFVHN)** - Active community support
- **[GitHub Issues](https://github.com/PiTracLM/PiTrac/issues)** - Report bugs or request features
- **[Troubleshooting Guide]({% link troubleshooting.md %})** - General troubleshooting

**When asking for help, include**:
- What you were trying to do
- What happened instead
- Error messages from logs
- System information (Pi model, OS version, architecture)
- Steps you've already tried

---

## Return To

- **[Installation Overview]({% link software/pitrac-install.md %})**
- **[First Use Guide]({% link software/install/first-use.md %})**
- **[Managing PiTrac]({% link software/install/managing.md %})**

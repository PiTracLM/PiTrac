---
title: Troubleshooting
layout: default
nav_order: 6
---

# Troubleshooting

Things not working? Let's fix them.

## Installation Problems

### "pitrac: command not found"

The PiTrac CLI didn't get installed or isn't in your PATH.

Try the full path:
```bash
/usr/bin/pitrac --version
```

If the file doesn't exist at all, the installation didn't complete properly. Re-run:
```bash
cd ~/Dev/packaging
sudo ./build.sh dev
```

### Dependencies Missing

If you see errors about missing libraries (lgpio, OpenCV, etc.), the dependency build failed.

Check if they're actually installed:
```bash
ls /usr/local/lib | grep lgpio
ls /usr/lib/pitrac/lib | grep opencv
```

If missing, rebuild dependencies:
```bash
cd ~/Dev/packaging
sudo ./build.sh dev
```

This takes about an hour. Go make coffee.

### Build Fails with "Permission Denied"

You need sudo for some operations. Make sure you're running:
```bash
sudo ./build.sh dev
```

### Services Won't Install

```bash
pitrac activemq status
pitrac web status
```

If these show "not found", the systemd services didn't install.

Manually install them:
```bash
cd /usr/lib/pitrac
sudo ./activemq-service-install.sh
sudo ./web-service-install.sh
```

## Camera Issues

### Cameras Not Detected

First, verify the Pi actually sees the cameras:

**Pi 5:**
```bash
rpicam-hello --list-cameras
```

**Pi 4:**
```bash
libcamera-hello --list-cameras
```

You should see your cameras listed. If not:
- Check physical connections
- Make sure cameras are powered
- Check `/boot/firmware/config.txt` (Pi 5) or `/boot/config.txt` (Pi 4)
- Look for `camera_auto_detect=1`
- Reboot if you just connected cameras

### Wrong Camera Type Selected

If cameras are detected but PiTrac can't use them:

1. Open Configuration in web UI
2. Check **Camera 1 Type** and **Camera 2 Type**
3. Try "Auto Detect" first
4. If that fails, manually select your camera model
5. Save changes
6. Stop and restart PiTrac LM

### Camera Gain/Brightness Issues

Images too dark or too bright?

**Too Dark:**
- Increase camera gain (Configuration → Cameras → kCamera1Gain/kCamera2Gain)
- Try values between 8-12
- Add more lighting if possible (better than high gain)

**Too Bright/Washed Out:**
- Decrease camera gain
- Try values between 3-6
- Reduce strobe intensity if you have control over that

**Noisy/Grainy:**
- Lower the gain
- Improve lighting instead
- Higher gain = more noise

### One Camera Works, Other Doesn't

Common in single-Pi mode with Camera 2.

Check:
- Is Camera 2 actually connected?
- Did you calibrate Camera 2? (takes longer, ~2 minutes)
- Are camera types set correctly for BOTH cameras?
- Check logs for Camera 2 specific errors

Some setups work fine with just Camera 1. You might not need Camera 2 depending on your goals.

## Ball Detection Problems

### Ball Not Detected

The most common problem. Here's the checklist:

**Lighting:**
- Ball needs good contrast with background
- Too dark = no detection
- Too bright = washed out, no detection
- Strobes working?

**Camera Position:**
- Is ball actually in the camera's field of view?
- Use Testing Tools → Capture Still Image to check
- Should see ball clearly in the center-ish area

**Search Center:**
- Configuration → Ball Detection → kSearchCenterX/Y
- These tell PiTrac where to look
- Capture a still image, note ball position (pixel coordinates)
- Set search center to those coordinates

**Ball Type:**
- White golf balls work best
- Colored/patterned balls can confuse detection
- Old scuffed balls might not work well

### Ball Detected But Wrong Objects Too

PiTrac picking up random circular things as balls?

Adjust the search area to narrow where it looks:
- Make kSearchCenterX/Y more precise
- Remove other circular objects from camera view
- Adjust detection thresholds (advanced - ask on Discord first)

### Ball Detection Slow

If it takes a long time to recognize ball placement:
- Search center might be way off
- Too many candidate objects in view
- Camera gain too low (image too dark)
- Adjust kSearchCenterX/Y to be more precise

## Calibration Failures

### Ball Not Found During Calibration

See "Ball Not Detected" above. Calibration needs to see the ball clearly before it can calibrate.

Quick fixes:
- Better lighting
- Adjust camera gain
- Check ball is in frame (use Testing → Capture Still Image)
- Make sure ball is on the tee and stationary

### Calibration Times Out

**Camera 1:** Should take ~30 seconds. If it times out:
- Ball moved during calibration
- Camera type wrong
- Check logs for errors

**Camera 2:** Takes 90-120 seconds in single-Pi mode. This is normal. Be patient.

If Camera 2 consistently times out past 2 minutes:
- System overloaded? (check CPU usage)
- Try on dual-Pi setup if you have one
- Some setups don't need Camera 2

### Calibration Completes But Results Wrong

Numbers look weird (focal length way off, strange angles)?

Could be:
- Ball moved during calibration
- Camera moved during calibration
- Wrong camera type selected
- Wrong lens type selected

Fix the issue and recalibrate. Don't try to manually adjust the calibration values - it won't work.

## Shot Reading Issues

### Speed Way Off

Shots reading 300 mph? Or 5 mph? Calibration problem or camera setup issue.

Check:
- Did you calibrate both cameras? (if you're using both)
- Camera types correct?
- Cameras moved since calibration?
- Ball detection working properly?

Sometimes you need to apply speed adjustments:
- Configuration → Ball Detection → kSpeedAdjustments
- Hit shots with a known launch monitor
- Calculate the adjustment percentage needed
- Apply it

### Launch Angle Wrong

Usually a calibration issue. Recalibrate if:
- Cameras moved
- Setup changed
- Angles are consistently off (not just one weird shot)

### Spin Detection Not Working

Spin is the hardest thing to measure accurately. Could be:
- Not enough ball images captured (strobes not bright enough?)
- Ball moving too fast
- Camera settings need tuning

This is advanced stuff - hit up Discord with your specific setup details.

### Some Shots Not Registered

Ball goes flying but PiTrac doesn't record it:
- Ball moving out of search area too quickly
- Detection sensitivity too low
- Strobes not firing properly
- Check logs for that shot - usually shows what happened

## Performance Problems

### Web Interface Slow/Unresponsive

Check if services are running:
```bash
pitrac web status
pitrac activemq status
```

Restart them if needed:
```bash
pitrac web restart
pitrac activemq restart
```

### Dashboard Not Updating

WebSocket connection might be dead.

Refresh the page. If that doesn't work:
```bash
pitrac web restart
```

### Pi Running Hot/Slow

Check CPU temperature:
```bash
vcgencmd measure_temp
```

Over 80°C? You need better cooling.

PiTrac does a lot of image processing. Make sure:
- Pi has a good, ACTIVE heatsink
- Not running in a hot enclosed space

### Shots Taking Forever to Process

Could be:
- Pi overloaded with other processes
- Check `top` to see what's using CPU
- Ball detection parameters set too loose (checking too many candidate objects)

## Service Issues

### pitrac.service Won't Start

This is the main service. If it won't start:

Check the logs:
```bash
journalctl -u pitrac -n 50
```

Common problems:
- Camera error (cameras not detected)
- Missing dependencies
- Config file errors
- Permission issues

### activemq.service Won't Start

ActiveMQ is the message broker. Usually reliable, but if it fails:

```bash
journalctl -u activemq -n 50
```

Try restarting:
```bash
sudo systemctl restart activemq
```

If it keeps failing:
```bash
sudo systemctl status activemq
```

Look for error messages. Port 61616 might be in use by something else.

### pitrac-web.service Won't Start

The web interface service.

```bash
journalctl -u pitrac-web -n 50
```

Common issues:
- Port 8080 already in use
- Python dependencies missing
- ActiveMQ not running (web service needs it)

Check dependencies:
```bash
cd /usr/lib/pitrac/web-server
pip3 install -r requirements.txt
```

## Network/Connectivity Issues

### Can't Access Web Interface

Try:
1. `http://{PI-IP}:8080` from another device
2. `http://localhost:8080` on the Pi itself

If localhost works but remote doesn't:
- Check Pi's IP address with `ifconfig`
- Firewall blocking port 8080?
- Both devices on same network?

If nothing works:
```bash
pitrac web status
```

Make sure service is actually running.

### Simulator Not Receiving Shots

See the [Simulator Integration]({% link simulator-integration.md %}) guide for detailed troubleshooting.

Quick checks:
- Simulator PC IP address correct?
- Ports correct? (2483 for E6, 921 for GSPro)
- Firewall not blocking?
- Network connection stable?

## Still Stuck?

If none of this helps, head to Discord. Bring:
- Description of the problem
- What you've tried
- Log output (use `pitrac logs` or check web interface)
- System info (Pi model, camera types, etc.)
- Screenshots if relevant

We'll figure it out. The PiTrac community is pretty helpful.

## Common "Gotchas"

Things that trip people up:

**Pi 4 vs Pi 5 Differences:**
- Config file location: `/boot/config.txt` (Pi 4) vs `/boot/firmware/config.txt` (Pi 5)
- Camera commands: `libcamera-*` (Pi 4) vs `rpicam-*` (Pi 5)
- GPIO chip number: 0 (Pi 4) vs 4 (Pi 5)

**Calibration Data Persistence:**
- Calibration data is stored separately from configuration
- Resetting config to defaults doesn't wipe calibration
- Good: You won't lose calibration accidentally
- Bad: Old calibration data persists even after fresh install
- Solution: Recalibrate if your setup changed

**Camera 2 Takes Forever:**
- Yes, 2 minutes is normal in single-Pi mode
- Background process overhead
- Not a bug, just how it works
- Be patient

**Services vs Manual Start:**
- systemd services (`pitrac.service`) for production use
- Manual start for development/debugging
- Don't run both at once
- If in doubt: `pitrac stop` then start fresh

**Left-Handed Mode:**
- Marked experimental for a reason
- Might need tweaking
- Spin calculations are tricky
- Ask on Discord for help if needed

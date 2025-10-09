---
title: Quickstart
layout: default
nav_order: 3
description: Get PiTrac running fast - install software, configure cameras, calibrate, and start tracking shots
---

# Quickstart Guide

So you've built your PiTrac hardware and you're ready to start smacking some golf balls. This guide walks you through the essential steps to get from bare Raspberry Pi to tracking shots.

## What You'll Need

Before starting, make sure you have:
- PiTrac hardware assembled (cameras mounted, PCB installed, enclosure complete)
- Raspberry Pi 5 with at least 8GB RAM
- Two cameras connected via CSI ribbons (Pi Global Shutter or InnoMaker IMX296)
- 64GB+ MicroSD card
- Network connection (Ethernet cable highly recommended for faster setup)
- Computer to access the Pi remotely

**Don't have hardware yet?** Check out the full [Getting Started]({% link getting-started.md %}) guide.

---

## Step 1: Set Up Raspberry Pi

You'll install Raspberry Pi OS and configure basic system settings.

{: .warning }
> **IMPORTANT - OS Version Requirements**
>
> PiTrac currently requires **Raspberry Pi OS (Legacy, 64-bit)** - the release based on Debian 12 (Bookworm).
>
> **Do NOT use:**
> - The latest Raspberry Pi OS (based on Debian 13 Trixie) - packages not yet updated
> - 32-bit versions - will not work

### Install the OS

1. Download [Raspberry Pi Imager](https://www.raspberrypi.com/software/) on your computer
2. Insert your MicroSD card
3. In Imager:
   - **Device**: Select your Pi model (Pi 4 or Pi 5)
   - **OS**: Navigate to "Raspberry Pi OS (other)" → Select either:
     - **"Raspberry Pi OS (Legacy, 64-bit)"** - Desktop version (easier for first-timers)
     - **"Raspberry Pi OS (Legacy, 64-bit) Lite"** - Command-line only (headless operation)
     - Both based on Debian 12 (Bookworm), Kernel 6.12
   - **Storage**: Select your MicroSD card (triple-check this!)

4. Click "Next" → "Edit Settings" to configure:
   - **Hostname**: `pitrac` (or whatever you prefer)
   - **Username**: Your choice (you'll use this to log in)
   - **Password**: Something secure you'll remember
   - **WiFi**: Your network credentials
   - **Enable SSH**: Check this box, use password authentication

5. Click "Save" → "Yes" → "Yes" to write the image

### First Boot

1. Insert the MicroSD into your Pi (never insert/remove while powered on)
2. Connect ethernet cable if available
3. Connect power supply

The Pi will take a few minutes to boot, expand the filesystem, and connect to your network. Be patient.

### Log In Remotely

Find your Pi's IP address (check your router's DHCP list or try `pitrac.local`) and SSH in:

```bash
ssh username@pitrac.local
# Or use the IP directly: ssh username@192.168.1.123
```

On Windows, use PuTTY or Windows Terminal.

### Update Everything

Once logged in, update the system:

```bash
sudo apt -y update
sudo apt -y upgrade
sudo reboot now
```

The Pi will reboot when done.

**Full details:** [Raspberry Pi Setup]({% link software/pi-setup.md %})

---

## Step 2: Install PiTrac Software

Now we'll install the actual PiTrac software - the launch monitor binary, web dashboard, and all dependencies.

### Quick Install

SSH back into your Pi after the reboot, then:

```bash
# Clone the repository
git clone https://github.com/PiTracLM/PiTrac.git
cd PiTrac/packaging

# Run the developer install (builds from source)
sudo ./build.sh dev
```

This installs:
- Launch monitor binary (`pitrac_lm`)
- Web server (Python FastAPI dashboard)
- ActiveMQ message broker
- Command-line tools (`pitrac` command)
- System services

The script handles all dependencies automatically - OpenCV, camera libraries, everything.

### Verify Installation

Check that everything's working:

```bash
# Check service status
pitrac status

# Run a quick test
pitrac test quick
```

You should see services starting up. If `pitrac-web` isn't running, start it:

```bash
pitrac web start
```

**Troubleshooting:** If the install fails, check [Installation Guide]({% link software/pitrac-install.md %}) for detailed steps and common issues.

---

## Step 3: Configure Cameras

PiTrac needs to know what cameras you have before anything will work.

### Verify Camera Detection

Make sure your Pi can see both cameras:

```bash
# Pi 5
rpicam-hello --list-cameras

# Pi 4
libcamera-hello --list-cameras
```

You should see 2 cameras listed. If not, check CSI cable connections and ensure `camera_auto_detect=1` is in `/boot/firmware/config.txt` (Pi 5) or `/boot/config.txt` (Pi 4). Reboot if you change the config.

### Access Web Interface

Open a browser and navigate to:

```
http://pitrac.local:8080
```

Or use your Pi's IP: `http://192.168.1.123:8080`

**Can't connect?**
- Check if web service is running: `pitrac web status`
- Start it if needed: `pitrac web start`
- Make sure you're using `http://` not `https://`

### Set Camera Types

1. Click the menu (3 dots in top right) → **Configuration**
2. In the left sidebar, click **Cameras**
3. Find **Camera 1 Type** and **Camera 2 Type**
4. Click **Auto Detect** - PiTrac will identify your cameras
5. If Auto Detect doesn't work, manually select from dropdown:
   - **InnoMaker CAM-MIPI327RAW** (recommended)
   - **Pi Global Shutter Camera** (also common)
6. Set **Lens Choice** to **6mm M12** (standard, unless you're using different lenses)
7. Click **Save Changes** at top right

**Important:** Stop and restart PiTrac for camera changes to take effect:
- Click "Stop Launch Monitor" in dashboard
- Wait for it to fully stop
- Click "Start Launch Monitor"

**More details:** [Camera Documentation]({% link camera/cameras.md %})

---

## Step 4: Calibrate Cameras

Calibration tells PiTrac the focal length and angles of your cameras. Without this, ball speed and launch angle will be completely wrong.

### What You'll Need

- Ball on tee in your hitting position
- Good lighting (strobes working)
- PiTrac stopped (calibration wizard starts it automatically)

### Run the Calibration Wizard

1. From the web dashboard, click menu → **Calibration**
2. Select **Camera 1** (always start here, it's faster)
3. Click **Next**

### Step 2: Verify Ball Placement

For Camera 1:
- Click **Check Ball Location** - should show green checkmark with coordinates
- Click **Capture Still Image** - verify ball is visible and in focus

If ball detection fails:
- Increase camera gain in Configuration → Cameras → kCamera1Gain (try 8-10)
- Check lighting
- Verify ball is actually on the tee
- Make sure camera is aimed at ball

Once successful, click **Next**.

### Step 3: Run Calibration

Pick **Auto Calibration** (recommended), then:
- Click **Start Auto Calibration**
- Don't move the ball or cameras
- Don't close the browser
- Watch the progress bar

**Camera 1:** Takes about 30 seconds

**Camera 2:** Takes 90-120 seconds (yes, really - single-Pi mode requires a background process)

### Step 4: Check Results

You'll see:
- **Status:** Success or Failed
- **Focal Length:** Should be 800-1200 for 6mm lens
- **Camera Angles:** Should be -20° to +20°

If numbers look way off, try calibrating again. If it keeps failing, check the troubleshooting section below.

Once successful, click **Return to Dashboard**.

### Repeat for Camera 2

Go through the same process for Camera 2. Remember it takes longer (~2 minutes), be patient.

**Full details:** [Auto-Calibration Guide]({% link camera/auto-calibration.md %})

---

## Step 5: Test It Out

Let's verify everything's working.

### Start PiTrac

From the dashboard, click **Start Launch Monitor** (if not already running).

Watch the status indicators at the top:
- ActiveMQ should be green
- Cam1 Process should be green
- Cam2 Process should be green

### Hit a Shot

1. Tee up a ball
2. Take a swing
3. Check the dashboard for shot data

**Expected results:**
- Driver: 80-120 mph ball speed
- 7-iron: 60-90 mph ball speed
- Launch angles: 5-20° depending on club

### If Shots Aren't Detecting

**Check lighting:**
- Are strobes firing? (You'll hear them click)
- Adjust camera gain if needed (Configuration → Cameras)

**Check ball detection:**
- Menu → Testing Tools
- Click "Capture Still Image" for each camera
- Ball should be clearly visible with good contrast

**Check logs:**
- Menu → Logging
- Look for errors in Cam1 or Cam2 logs

---

## Common Issues

### Cameras Not Detected
- Verify CSI cable connections
- Check boot config: `camera_auto_detect=1`
- Reboot after connecting cameras
- Try `rpicam-hello --list-cameras` to see what Pi sees

### Calibration Fails
- **Ball not detected:** Increase gain, improve lighting, check focus
- **Timeout:** Camera 2 really does take 2+ minutes, wait longer
- **Wrong results:** Ball moved during calibration? Try again

### Images Too Dark
- Increase camera gain (Configuration → Cameras → kCamera1Gain)
- Check strobe power and connections
- Verify strobes are firing

### Images Too Bright
- Decrease camera gain
- Reduce strobe intensity if adjustable

### Web Interface Won't Load
- Check if service is running: `pitrac web status`
- Start it: `pitrac web start`
- Verify you're using `http://` not `https://`
- Try IP address instead of hostname

---

## Next Steps

Once you're getting good shot data:

### Configure Your Simulator
Connect PiTrac to E6 Connect, GSPro, or TruGolf:
- **[Simulator Integration Guide]({% link simulator-integration.md %})**

### Fine-Tune Settings
Adjust ball detection, gains, and search areas:
- **[Using PiTrac Guide]({% link software/using-pitrac.md %})**

### Troubleshoot Issues
Deep dive into debugging tools and logs:
- **[Troubleshooting Guide]({% link troubleshooting.md %})**

---

## Need Help?

- **Discord:** [Join the PiTrac community](https://discord.gg/j9YWCMFVHN) - fastest way to get help
- **GitHub Issues:** [Report bugs](https://github.com/PiTracLM/PiTrac/issues)
- **Documentation:** Browse the nav menu for detailed guides on every topic

Most issues are lighting-related or calibration problems. Don't be afraid to recalibrate if something seems off - it only takes 3 minutes.

Now go hit some balls!

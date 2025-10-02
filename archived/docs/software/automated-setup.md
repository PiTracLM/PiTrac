---
title: Automated Setup
layout: default
nav_order: 2
parent: Software
description: Quick automated setup for PiTrac using the Dev installer system - get up and running in minutes instead of hours
keywords: pitrac automated setup, quick install, dev installer, raspberry pi automation
toc: true
---

# PiTrac Automated Setup

PLEASE NOTE:  This feature has been largely deprecated.  Please consider using the [Build System]({% link development/build-system.md %}).

Forget spending hours copying commands from tutorials. The Dev installer gets your Pi ready for PiTrac in about 30 minutes, and most of that is just waiting for stuff to compile.

## What You Get

Instead of manually installing 20+ packages and hoping you got the order right, the Dev system handles everything:
- All the software PiTrac needs
- In the right order
- With the right versions
- Configured correctly
- On Pi4, Pi5, or even x86 systems

## Before You Start

You'll need:
- A Raspberry Pi with fresh OS installed (Bookworm or Bullseye)
- Internet connection
- About 4GB free disk space
- 30-45 minutes (mostly waiting)

If you're setting up two Pis for the dual-camera system, you'll run this on both. The single-Pi setup? Just run it once.

## The Quick Way

Got your Pi booted and connected to the network? Here we go:

### Step 1: Get the Dev Folder

First, grab the Dev folder from the PiTrac repository. If you already cloned PiTrac:
```bash
cp -r /path/to/PiTrac/Dev ~/
```

Or download just the Dev folder:
```bash
cd ~
wget https://github.com/your-repo/PiTrac/archive/main.zip
unzip main.zip "PiTrac-main/Dev/*"
mv PiTrac-main/Dev ~/
rm -rf PiTrac-main main.zip
```

### Step 2: Launch the Installer

```bash
cd ~/Dev
chmod +x run.sh
./run.sh
```

That's it. After any initial installation of required software, you'll see a menu like this:

```
========================================
        PiTrac Installation Menu
========================================

Software Installation Status:
  OpenCV .............. [NOT INSTALLED]
  Libcamera ........... [NOT INSTALLED]
  ActiveMQ Broker ..... [NOT INSTALLED]
  ActiveMQ C++ ........ [NOT INSTALLED]
  TomEE ............... [NOT INSTALLED]
  LGPIO ............... [NOT INSTALLED]
  MessagePack ......... [NOT INSTALLED]

1) Install Software
2) Configure System
3) Verify Installations
4) View Logs
5) Exit

Choice:
```

### Step 3: Install Everything

Choose option 1, then select "Install All Required Dependencies". 

Select "Yes".  Now go grab a coffee. The installer will:
1. Check your system (disk space, internet, etc.)
2. Install system packages
3. Build OpenCV (this takes the longest)
4. Set up libcamera for your cameras
5. Install the message broker
6. Configure the Java environment
7. Set up the web server

You'll see progress bars for the long stuff:
```
Building OpenCV: [===========----------] 55% (247/450)
```

### Step 4: Configure Your System

After installation finishes and you navigate back to the main menu, choose option 2 "System Configuration" from the main menu:

1. **System Configuration** - Sets up GPU memory, boot options, hardware stuff
2. **Camera Configuration** - Detects your camera sensor, configures it
3. **PiTrac Environment** - Creates all the environment variables PiTrac needs
4. **Network Services** (optional) - If you're using NAS or network shares

### Step 5: Build PiTrac

Now for the actual PiTrac software. Choose option 3 "Build PiTrac".

This is where it all comes together. The installer will:
1. Clone the PiTrac repository (or update if you have it)
2. Set up all the environment variables 
3. Configure libcamera for your Pi model
4. Build the launch monitor with meson and ninja
5. Deploy the web interface (if TomEE is installed)

You'll see progress as it builds:
```
Building PiTrac: [================-----] 76% (342/450)
```

If the system complains about anything that was not installed, go back to the System Configuration and install any missing packages.
Takes about 10-15 minutes depending on your Pi.

### Step 6: Run PiTrac

Ready to see it work? Choose option 4 "Run PiTrac Launch Monitor".

This opens a submenu where you can:
- Run in single-Pi mode (one camera)
- Run dual-Pi setup (two cameras)
- Test the strobe light
- Test without cameras using test images

For detailed runtime options, see the [Running PiTrac]({% link software/running-pitrac.md %}) guide.

### Step 7: Verify Everything Works

Choose option 5 "Verify Installations". This actually tests each component:
- OpenCV: Compiles and runs a test program
- Libcamera: Checks camera detection
- ActiveMQ: Verifies the broker starts
- TomEE: Tests web server access

If anything shows as failed, check the logs (option 4).

## The Even Quicker Way (Non-Interactive)

Got multiple Pis to set up? Don't want to babysit the installer? Use non-interactive mode:

```bash
cd ~/Dev
# Install everything and build PiTrac
./run.sh --install-all --configure-all --build-pitrac --non-interactive
```

Or just the build step (if dependencies are already installed):
```bash
cd ~/Dev/scripts
./build_pitrac.sh --non-interactive
```

This uses all defaults and doesn't ask any questions. Perfect for:
- Automated deployments
- Docker containers
- CI/CD pipelines
- Setting up multiple Pis

## What If Something Goes Wrong?

The installer is pretty smart about failures:

### During Installation
If something fails, you'll see:
```
[ERROR] Failed to install OpenCV
Continue with remaining packages? (y/N):
```

Usually best to say no, fix the issue, then run again. The installer skips stuff that's already installed.

### After Installation
Check the logs:
```bash
cd ~/Dev
./run.sh
# Choose option 4 "View Logs"
```

Or manually:
```bash
cat ~/Dev/scripts/.install.log
```

### Common Issues and Fixes

**"No space left on device"**
- Need at least 4GB free
- OpenCV build needs temporary space too
- Try: `df -h` to check space

**"Unable to locate package"**
- Your package lists are out of date
- Fix: `sudo apt update`

**Camera not detected**
- Wrong camera config selected
- Rerun camera configuration
- Check camera cable connection

**Web interface won't load**
- TomEE didn't start properly
- Check: `systemctl status tomee`
- Logs: `/opt/tomee/logs/catalina.out`

## Customizing the Installation

Don't want everything? Need specific versions? The installer handles that too.

### Install Only What You Need

From the menu, choose "Install Individual Package" instead of "Install All". Pick just what you want:
- Just OpenCV for testing ball detection
- Just libcamera for camera testing
- Skip TomEE if you don't need the web interface

### Change Versions or Options

Each package has a config file in `~/Dev/scripts/defaults/`. For example, `opencv.yaml`:
```yaml
required-opencv-version: 4.11.0
build-examples: 1
enable-python: 1
```

Change these before running the installer to customize your setup.

### Different Pi Models

The installer automatically detects:
- Pi 4 vs Pi 5
- ARM vs x86 architecture
- Available memory and cores

It adjusts the installation accordingly. Pi 5? Gets different camera configs. Limited RAM? Uses fewer compile threads.

## After Installation

Once everything's installed, configured, and built, you're ready to rock:

1. **Run PiTrac** - Use menu option 4 or see [Running PiTrac]({% link software/running-pitrac.md %})
2. **Test Without Hardware** - Try the test processor (menu option 7) - see [Testing Guide]({% link troubleshooting/testing-without-hardware.md %})
3. **Test Cameras** - Run `rpicam-hello` to verify camera works
4. **Start Services** - ActiveMQ and TomEE should auto-start on boot
5. **Access Web UI** - Browse to `http://your-pi-ip:8080/golfsim/monitor`

## Updating Later

Need to update a component? The installer handles that too:

```bash
cd ~/Dev
./run.sh
# Choose "Install Software"
# Select the package to update
# It will detect the existing version and upgrade
```

Or force a reinstall:
```bash
cd ~/Dev/scripts
FORCE=1 ./install_opencv.sh
```

## For the Curious

Want to know what's actually happening? The installer:
- Uses dependency resolution (like apt but for PiTrac stuff)
- Checks for circular dependencies
- Installs in the correct order
- Logs everything for troubleshooting
- Can roll back failed installs

Check out `~/Dev/scripts/deps.conf` to see the dependency tree. Each install script is in `~/Dev/scripts/install_*.sh` if you want to see exactly what gets run.

## Rebuilding or Updating PiTrac

Need to pull the latest PiTrac code and rebuild?

```bash
cd ~/Dev
./run.sh
# Choose option 3: Build PiTrac
```

It'll pull the latest changes and rebuild. Want a different branch?

```bash
# Edit ~/Dev/scripts/defaults/pitrac-build.yaml
pitrac-branch: develop  # or whatever branch you need
```

Then run the build again. It'll switch branches and rebuild.

## Compared to Manual Setup

The manual way (see [Raspberry Pi Setup]({% link software/pi-setup.md %})) takes 2-3 hours and involves:
- 30+ manual steps
- Copy-pasting dozens of commands
- Figuring out why step 23 failed because you missed something in step 8
- Rebuilding OpenCV three times because you forgot a flag
- Manually cloning and building PiTrac
- Setting up environment variables by hand
- Hoping you got the camera config right

The automated way takes 45-60 minutes total:
- 30 minutes for dependencies
- 10 minutes for configuration
- 15 minutes to build PiTrac
- 5 minutes to verify everything

And all you do is pick menu options. The system handles the rest.

Your choice.


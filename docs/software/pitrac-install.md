---
title: PiTrac Installation
layout: default
nav_order: 2
parent: Software
has_children: true
description: Step-by-step PiTrac software installation guide including building from source, APT package installation, dependency management, and service configuration for the golf launch monitor.
keywords: install pitrac software, build from source raspberry pi, apt package install, golf monitor software setup, compile opencv raspberry pi
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# PiTrac Installation Guide

Install the PiTrac software on your Raspberry Pi and start tracking golf shots.

{: .note }
**Important**: Complete [Raspberry Pi Setup]({% link software/pi-setup.md %}) before installing PiTrac. Your Pi must have the OS installed, be updated, and have network access.

---

## Installation Overview

**What you'll install:**
- PiTrac launch monitor binary (C++)
- Web dashboard and configuration interface (Python/FastAPI)
- ActiveMQ message broker
- Camera libraries and configuration
- Test resources and calibration tools

**Time Required:** 10-20 minutes (building from source)

**Difficulty:** Intermediate (script-automated)

---

## Prerequisites

Before starting, ensure you have:

- **Raspberry Pi 5 with 8GB RAM** (recommended)
  - Raspberry Pi 4 with 8GB RAM also supported
- **Raspberry Pi OS 64-bit** installed and configured (Bookworm or Trixie)
  - See [Pi Setup Guide]({% link software/pi-setup.md %}) for OS installation
- **Active internet connection** for downloading packages
- **Sudo privileges** on your Pi
- **At least 5GB free disk space** (10GB recommended for development)

{: .warning }
**Critical**: You must have completed the [Raspberry Pi Setup]({% link software/pi-setup.md %}) before proceeding. This includes OS installation, system updates, and network configuration.

---

## Installation Methods

Choose the installation method that fits your needs:

### Build from Source (Recommended)

**→ [Build from Source Guide]({% link software/install/build-from-source.md %})**

Building from source is currently the recommended method. Ideal for:
- Getting the latest features
- Development and customization
- Understanding the build process

**What it does:**
- Clones PiTrac repository from GitHub
- Installs system dependencies (~80 packages)
- Extracts pre-built libraries (OpenCV, ActiveMQ, lgpio)
- Compiles PiTrac C++ binary
- Installs web server and services
- Configures cameras and message broker

**Time:** 10-20 minutes on Pi 5 (first install, network-dependent)

---

### APT Package Installation

**Coming Soon**: PiTrac will be available as a Debian package for easy installation via `apt`.

```bash
# Future installation (not yet available)
sudo apt update
sudo apt install pitrac
```

This will provide:
- One-command installation
- Automatic dependency resolution
- System updates through `apt upgrade`
- Simpler for end users

**Status**: Package infrastructure in development. Check [GitHub Releases](https://github.com/PiTracLM/PiTrac/releases) for availability.

---

## After Installation

Once PiTrac is installed, follow these guides in order:

### Step 1: First Use

**→ [First Use Guide]({% link software/install/first-use.md %})**

Learn how to:
- Access the web dashboard
- Start the PiTrac process
- Navigate the web interface
- Make your first shot
- Understand shot data

**Time:** ~10 minutes

---

### Step 2: Managing PiTrac

**→ [Managing PiTrac Guide]({% link software/install/managing.md %})**

Essential knowledge for:
- Using CLI commands
- Managing services (web server, ActiveMQ)
- Understanding file locations
- Updating PiTrac
- Development workflow
- Backup and restore

**Reference guide** - bookmark for when needed

---

### Step 3: Troubleshooting (If Needed)

**→ [Troubleshooting Guide]({% link software/install/troubleshooting.md %})**

Solutions for:
- Build failures
- Service issues (web server, ActiveMQ)
- Camera detection problems
- Web interface access
- Shot detection issues
- Performance problems

**Use when needed** - comprehensive problem-solving reference

---

## Quick Start Path

For experienced users who want the essentials:

1. **[Build from Source]({% link software/install/build-from-source.md %})**
   ```bash
   git clone https://github.com/PiTracLM/PiTrac.git
   cd PiTrac/packaging
   sudo ./build.sh dev
   ```

2. **[First Use]({% link software/install/first-use.md %})**
   - Access web dashboard: `http://raspberrypi.local:8080`
   - Start PiTrac from web interface
   - Hit test shots

3. **Calibrate** (after first use)
   - Run Calibration Wizard in web interface
   - See [Camera Calibration]({% link camera/cameras.md %}) for details

---

## System Architecture

Understanding what PiTrac installs helps with troubleshooting:

### Services
- **activemq.service** - Message broker (system service)
- **pitrac-web.service** - Web dashboard (system service)
- **pitrac_lm process** - Launch monitor (controlled via web UI, NOT a service)

### Key Directories
```
/usr/lib/pitrac/          # Binaries and web server
/etc/pitrac/              # System configuration templates
~/.pitrac/config/         # Your configuration and calibration
~/.pitrac/logs/           # Application logs
~/LM_Shares/Images/       # Captured shot images
```

### Dependencies
- **System packages**: ~80 packages (Boost, libcamera, FFmpeg, Python, etc.)
- **Pre-built libraries**: OpenCV 4.11.0, ActiveMQ-CPP 3.9.5, lgpio 0.2.2, msgpack, ONNX Runtime
- **Python packages**: FastAPI, uvicorn, stomp.py, websockets, and more

---

## What's Next?

After completing installation:

**Essential:**
1. **[First Use Guide]({% link software/install/first-use.md %})** - Access web interface and make first shot
2. **Calibration Wizard** - Run through web interface for accurate measurements

**Optional:**
- **[Camera Calibration]({% link camera/cameras.md %})** - Advanced calibration techniques
- **[Simulator Integration]({% link simulator-integration.md %})** - Connect to E6, GSPro, TruGolf
- **[Express Path]({% link quickstart.md %})** - Streamlined setup for experienced users

**Reference:**
- **[Managing PiTrac]({% link software/install/managing.md %})** - Commands, file locations, updates
- **[Troubleshooting]({% link software/install/troubleshooting.md %})** - Problem-solving guide

---

## Getting Help

**For installation issues:**
- Check the **[Troubleshooting Guide]({% link software/install/troubleshooting.md %})** first
- Review logs: `pitrac web logs` or `journalctl -u pitrac-web`
- Verify prerequisites were completed

**Community support:**
- **[Discord Community](https://discord.gg/j9YWCMFVHN)** - Active community for questions and help
- **[GitHub Issues](https://github.com/PiTracLM/PiTrac/issues)** - Report bugs or request features
- **[General Troubleshooting]({% link troubleshooting.md %})** - Broader system issues

**CLI help:**
```bash
pitrac help       # Show all commands
pitrac status     # Check service status
```

---

## Installation Workflow

```
┌─────────────────────────────────────┐
│  Prerequisites Complete?            │
│  • Pi OS installed                  │
│  • System updated                   │
│  • Network configured               │
└───────────┬─────────────────────────┘
            │
            v
┌─────────────────────────────────────┐
│  Build from Source                  │
│  • Clone repository                 │
│  • Run build.sh dev                 │
│  • 10-20 minutes                    │
└───────────┬─────────────────────────┘
            │
            v
┌─────────────────────────────────────┐
│  First Use                          │
│  • Access web dashboard             │
│  • Start PiTrac                     │
│  • Make first shot                  │
└───────────┬─────────────────────────┘
            │
            v
┌─────────────────────────────────────┐
│  Calibration                        │
│  • Run Calibration Wizard           │
│  • Verify accuracy                  │
│  • Fine-tune settings               │
└─────────────────────────────────────┘
```

---

## Return To

- **[Software Overview]({% link software/software.md %})** - Software section home
- **[Raspberry Pi Setup]({% link software/pi-setup.md %})** - Pi OS installation and configuration

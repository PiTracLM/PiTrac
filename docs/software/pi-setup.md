---
title: Raspberry Pi Setup
layout: default
nav_order: 1
parent: Software
has_children: true
description: Complete guide for setting up Raspberry Pi computers for PiTrac, including OS installation, first login, system updates, and optional advanced configurations.
keywords: raspberry pi setup, PiTrac installation, pi OS configuration, raspberry pi first boot, SSH setup
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# Raspberry Pi Setup & Configuration

This guide covers setting up your Raspberry Pi from a blank SD card to a fully configured system ready for PiTrac installation.

{: .note }
**Quick Start**: This guide covers Raspberry Pi OS installation and initial system configuration. Once complete, proceed to **[Install PiTrac Software]({% link software/pitrac-install.md %})**.

---

## Overview

**What you'll do:**
1. Install Raspberry Pi OS using Raspberry Pi Imager
2. Log in and perform initial updates
3. (Optional) Configure advanced features like NVMe boot or NAS mounting

**Time Required:**
- **Essential setup**: ~45 minutes
- **With optional configs**: ~75 minutes

**Difficulty:** Beginner-friendly with step-by-step instructions

---

## System Requirements

### Required Hardware

- **Raspberry Pi 5** with at least 8GB RAM (Pi 4 also supported)
- **MicroSD card** - 64GB minimum (32GB may work but 64GB+ recommended)
- **Power supply** - Official Raspberry Pi power supply recommended
- **Network connection** - Ethernet cable strongly recommended for initial setup

### Recommended (Optional)

- **Monitor, keyboard, mouse** - Helpful for first boot, even if running headless later
- **NVMe HAT + SSD** - For significantly faster performance (Pi 5 only)
- **Second computer** - For SSH access and easier copy-paste from documentation
- **NAS or file server** - For development work and safer file storage

---

## Setup Process

### Step 1: Install Operating System

Install Raspberry Pi OS using Raspberry Pi Imager with proper configuration for PiTrac.

**→ [Install Raspberry Pi OS]({% link software/pi-setup/install-os.md %})**

**What you'll do:**
- Download and use Raspberry Pi Imager
- Choose correct OS version (64-bit Bookworm or Trixie)
- Configure hostname, username, WiFi, and SSH
- Image your SD card and first boot

**Time:** ~30 minutes

---

### Step 2: First Login & Updates

Log into your Pi (via SSH or console) and perform essential system updates.

**→ [First Login & Updates]({% link software/pi-setup/first-login.md %})**

**What you'll do:**
- Connect via SSH or console
- Update system packages
- Verify sudo privileges
- Check system configuration

⏱ **Time:** ~15 minutes

---

### Step 3: Advanced Configuration (Optional)

Configure optional features for enhanced performance and development workflows.

**→ [Advanced Configuration]({% link software/pi-setup/advanced.md %})**

**Optional features:**
- **NVMe Boot** - Boot from SSD for 5-10x speed improvement
- **NAS Mounting** - Mount remote drives for safer development
- **Samba Server** - Share files between Pis
- **SSH Keys** - Passwordless authentication
- **Git Setup** - Configure for shared drives

**Time:** Varies by feature (15-60 minutes)

{: .note }
These are optional. PiTrac works fine without them.

---

## Current System Architecture

Modern PiTrac uses a simplified architecture:

- **Single Pi setup** is now standard
- All services run on one Raspberry Pi 5
- Legacy dual-Pi configurations still supported but not recommended
- Pre-built packages handle dependencies automatically

You **do not** need to manually build OpenCV, ActiveMQ, or other dependencies - the installation process handles everything.

---

## After Setup

Once your Pi is set up, continue to:

**→ [Install PiTrac Software]({% link software/pitrac-install.md %})**

This will install the launch monitor binary, web dashboard, and all required services.

---

## Quick Reference

### Essential Commands

```bash
# Update system
sudo apt update && sudo apt -y upgrade

# Check OS version
cat /etc/os-release

# Check architecture (must be aarch64)
uname -m

# Find IP address
hostname -I

# Reboot
sudo reboot now

# Shutdown
sudo poweroff
```

### Network Access

**SSH from another computer:**
```bash
# Using hostname (usually works)
ssh <username>@pitrac.local

# Using IP address
ssh <username>@192.168.1.100
```

**Find Pi's IP address:**
- Check router's DHCP client list
- On Pi with monitor: `hostname -I`
- Network scanner: `nmap` or Angry IP Scanner

---

## Troubleshooting

**Can't find Pi on network:**
- Wait 5 minutes after first boot
- Check router for new DHCP clients
- Verify ethernet cable connected
- Check WiFi credentials if using wireless
- Try connecting a monitor to see boot progress

**SD card won't boot:**
- Verify you selected 64-bit OS
- Check SD card isn't corrupted
- Try re-imaging with Raspberry Pi Imager
- Ensure SD card is properly inserted
- Try different SD card

**SSH connection refused:**
- Verify SSH was enabled during OS installation
- Check Pi is on network: `ping pitrac.local`
- Wait longer - first boot takes 3-4 minutes
- Try IP address instead of hostname
- Check firewall isn't blocking port 22

**System updates fail:**
- Check internet connection: `ping google.com`
- Verify DNS works: `nslookup google.com`
- Check disk space: `df -h`
- Try: `sudo apt update --fix-missing`

---

## Need Help?

- **Discord Community**: [Join the PiTrac Discord](https://discord.gg/j9YWCMFVHN)
- **GitHub Issues**: [Report issues](https://github.com/PiTracLM/PiTrac/issues)
- **Troubleshooting Guide**: [Common Issues]({% link troubleshooting.md %})

---

## What's Next?

After completing Pi setup:

1. **OS Installed** - Raspberry Pi OS running
2. **System Updated** - All packages current
3. **Network Configured** - SSH access working
4. **Install PiTrac** - [Next: Install PiTrac Software]({% link software/pitrac-install.md %})
---
title: Getting Started
layout: default
nav_order: 2
description: Step-by-step guide to building and setting up your PiTrac golf launch monitor
---

# Getting Started with PiTrac

Welcome to PiTrac! This guide will walk you through the complete process of building your own DIY golf launch monitor.

## Build Process Overview

Building a PiTrac system involves three main phases:

1. **Hardware Assembly** - Gather parts, assemble PCB, 3D print enclosure
2. **Raspberry Pi Setup** - Install and configure Raspberry Pi OS
3. **Software Installation** - Install PiTrac software and calibrate cameras

Each phase builds on the previous one, so follow them in order for the best experience.

---

## Phase 1: Hardware Assembly

Before you can run PiTrac, you'll need to build the physical hardware.

### Step 1: Gather Parts

Review the complete parts list and order all required components.

**→ [Parts List]({% link hardware/parts-list.md %})**

You'll need:
- Raspberry Pi 5 (8GB recommended)
- Dual Pi cameras (Global Shutter or IMX296)
- PCB components and enclosure materials
- Power supplies and cables

### Step 2: PCB Assembly

Order and assemble the custom PCB that controls the IR strobes and camera triggers.

**→ [PCB Assembly Guide]({% link hardware/pcb-assembly.md %})**

Learn where to get the PCB files, how to order from manufacturers, and assembly instructions.

### Step 3: 3D Print Enclosure

Print the enclosure and mounting hardware for your PiTrac.

**→ [3D Printing Guide]({% link hardware/3d-printing.md %})**

Download the STL files and get recommendations for print settings.

### Step 4: Final Assembly

Put all the pieces together to complete your PiTrac hardware.

**→ [Assembly Guide]({% link hardware/assembly-guide.md %})**

Follow step-by-step instructions to assemble cameras, PCB, enclosure, and wiring.

---

## Phase 2: Raspberry Pi Setup

With hardware complete, prepare your Raspberry Pi.

### Install Raspberry Pi OS

Install the operating system and configure basic settings like SSH, WiFi, and user accounts.

**→ [Pi Setup Guide]({% link software/pi-setup.md %})**

This guide covers:
- Using Raspberry Pi Imager
- Choosing between Desktop and Lite versions (64-bit only)
- Configuring SSH and network access
- Initial system updates

**Time Required:** 30-45 minutes (including OS imaging)

---

## Phase 3: Software Installation

Install the PiTrac software on your configured Raspberry Pi.

### Install PiTrac

Choose your installation method and get PiTrac running.

**→ [Installation Guide]({% link software/pitrac-install.md %})**

Options:
- **Build from Source** - Clone the repository and compile (recommended for now)
- **APT Package** - Simple package installation (coming soon)

This includes:
- Installing all dependencies
- Building the launch monitor binary
- Setting up the web dashboard
- Configuring services

**Time Required:** 5-10 minutes (build from source)

### Calibrate Cameras

Fine-tune your camera setup for accurate shot tracking.

**→ [Camera Calibration]({% link camera/cameras.md %})**

Use the built-in calibration wizard to:
- Verify camera detection
- Calibrate lens distortion
- Set up ball detection parameters
- Test shot tracking

---

## Quick Reference

Once you're up and running, these guides will help you use and maintain PiTrac:

- **[Using PiTrac]({% link software/using-pitrac.md %})** - Operating the system day-to-day
- **[Troubleshooting]({% link troubleshooting.md %})** - Common issues and solutions
- **[Hardware Overview]({% link hardware/hardware.md %})** - Detailed hardware documentation

---

## Need Help?

- **Discord Community**: [Join the PiTrac Discord](https://discord.gg/j9YWCMFVHN)
- **GitHub Issues**: [Report bugs or request features](https://github.com/PiTracLM/PiTrac/issues)
- **Documentation**: Browse the navigation menu for detailed guides

Ready to start? Head to the [Parts List]({% link hardware/parts-list.md %}) to begin your build!
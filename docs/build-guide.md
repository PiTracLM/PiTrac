---
title: Build Your PiTrac
layout: default
nav_order: 2
description: Complete step-by-step guide to building your PiTrac DIY golf launch monitor from ordering parts to tracking your first shot. Hardware assembly, software installation, and camera calibration included.
keywords: build golf launch monitor, DIY launch monitor tutorial, raspberry pi golf setup, launch monitor assembly guide, golf tracker build instructions
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# Build Your PiTrac

Welcome to PiTrac! This guide will walk you through the complete process of building your own DIY golf launch monitor.

## Build Process 

Building a PiTrac system involves three main phases:

1. **Hardware Assembly** - Gather parts, assemble PCB, 3D print enclosure
2. **Raspberry Pi Setup** - Install and configure Raspberry Pi OS
3. **Software Installation** - Install PiTrac software and calibrate cameras

Each phase builds on the previous one, so follow them in order for the best experience.

## Supporting Tutorial Videos

We're developing companion videos to the written documentation to better visually show how to build your PiTrac.  Here's what we have so far (no link means this video is still in the works):

- Part  1 - Introduction to PiTrac 
- Part 2A - 3D Printing the PiTrac Tower  
- Part 2B - [3D Printing the BaseBox] (https://youtu.be/DwA4mXsMMAY)
- Part 3B - [Tower Build] (https://youtu.be/8COCvxQgKR8)
- Part 4A - [Component Installation] (https://youtu.be/93e-ukvGhJ0)
- Part 4B - [PiTrac Software Setup] (https://youtu.be/M0pMmwSKbK4)
- Part 4C - [Optional Software] (https://youtu.be/0mW1fRwRDdU)
- Part 4D - [Final Assembly] (https://youtu.be/sgJq0n0NwYA)
- Part 5A - De-Distortion Configuration 
- Part 5B - Calibration 

Alternative to part 2A - 4A and 4D:
- Enclosure V3 - Assembly (https://youtu.be/bVOIPczu9Nk?si=cu9BIsXWRHtM7RDG)

Please note that the written documentation is meant to be the most up-to-date and accurate information on building PiTrac systems.  If there's a discrepancy between that documentation nand the vidoes, rely on the documents.  Or, ask a question on the Discord server!

---

## Phase 1: Hardware Assembly

Before you can run PiTrac, you'll need to build the physical hardware.
Depending on your preferences, choose between different enclosure design variants:

<div style="display: flex; gap: 10px; justify-content: center;">
  <div style="text-align: center;">
    <strong>V1</strong><br>
    <img src="https://github.com/user-attachments/assets/fbdc9825-b340-47b5-83ad-6c58d4588f34" width="250">
  </div>
  <div style="text-align: center;">
    <strong>V2</strong><br>
    <img src="assets/images/PiTracV2EnclosureWithGolfBall.png" width="250">
  </div>
  <div style="text-align: center;">
    <strong>V3</strong><br>
    <img src="assets/images/V3_Enclosure/PiTrac_V3-Enclosure_crop.png" width="250">
  </div>
</div>

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

**→ [Enclosure V2 3D Printing Guide]({% link hardware/3d-printing.md %})** 

or 

**→ [Enclosure V3 Printing Guide]({% link hardware/Enclosure_Version3_printing.md %})**

Download the STL files and get recommendations for print settings.

### Step 4: Final Assembly

Put all the pieces together to complete your PiTrac hardware.

**→ [Enclosure V2 Assembly Guide]({% link hardware/assembly-guide.md %})**

or 

**→ [Enclosure V3 Assembly Guide]({% link hardware/Enclosure_Version3_assembly.md %})**

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

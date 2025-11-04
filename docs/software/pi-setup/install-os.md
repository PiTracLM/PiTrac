---
title: Install Raspberry Pi OS
layout: default
nav_order: 1
parent: Raspberry Pi Setup
grand_parent: Software
description: Step-by-step guide to installing Raspberry Pi OS using Raspberry Pi Imager including OS selection, configuration settings, and first boot for PiTrac.
keywords: raspberry pi imager, install pi OS, bookworm trixie setup, pi OS configuration, raspberry pi first boot
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# Install Raspberry Pi OS

This guide walks through installing Raspberry Pi OS using Raspberry Pi Imager with all necessary configuration for PiTrac.

**Time Required:** ~30 minutes (including imaging and first boot)

**Difficulty:** Beginner-friendly

---

## OS Version Requirements

{: .warning }
> **CRITICAL - OS Version Requirements**
>
> PiTrac requires **Raspberry Pi OS 64-bit**
>
> - **Supported versions:** Debian 12 (Bookworm) or Debian 13 (Trixie)
> - **System:** 64-bit (required)
>
> **Do NOT use:**
> - 32-bit versions - will not work with PiTrac

## Desktop or Lite?

Both work fine:
- **Desktop**: Has GUI, easier for first-timers
- **Lite**: Command-line only, uses fewer resources

Choose based on your comfort level with Linux command line.

---

## Installation Steps

### 1. Prepare Your Hardware

Before starting:
- Pi powered off (unplugged)
- Cat5/6 ethernet cable connected to your local network (recommended for faster downloads)
- Micro SD card connected to your PC/Mac via USB adapter
- Use 64GB card minimum (32GB may work but 64GB recommended)

### 2. Download Raspberry Pi Imager

- Download from [raspberrypi.com/software](https://www.raspberrypi.com/software/)
- Install for your operating system (Windows, macOS, or Ubuntu)
- Launch the application

### 3. Select Your Device

- Click **"CHOOSE DEVICE"**
- Select either **"Raspberry Pi 4"** or **"Raspberry Pi 5"** depending on your hardware
- This optimizes the OS for your specific Pi model

### 4. Choose the Operating System

- Click **"CHOOSE OS"**
- Select **ONE** of the following 64-bit versions:
  - **"Raspberry Pi OS (64-bit)"** - Desktop with GUI (Debian 13 Trixie)
  - **"Raspberry Pi OS (64-bit) Lite"** - Command-line only (Debian 13 Trixie)
  - **"Raspberry Pi OS (Legacy, 64-bit)"** - Desktop with GUI (Debian 12 Bookworm)
  - **"Raspberry Pi OS (Legacy, 64-bit) Lite"** - Command-line only (Debian 12 Bookworm)

Both Bookworm (Debian 12) and Trixie (Debian 13) are fully supported.

{: .note }
**Important**: Must be 64-bit version. Do not use 32-bit versions.

### 5. Select Storage

- Click **"CHOOSE STORAGE"**
- Select your Micro SD card

{: .warning }
**WARNING**: Triple-check this is your SD card and NOT your computer's hard drive! Everything on this storage will be erased.

### 6. Configure OS Customization Settings

- Click **"NEXT"**
- When prompted "Would you like to apply OS customisation settings?", click **"EDIT SETTINGS"**

#### GENERAL Tab

**Set hostname:**
- Choose a descriptive hostname to identify your Pi on the network
- Examples: `pitrac`, `pitrac-main`, `rsp01`, `rsp02`
- This will be how you connect to the Pi (e.g., `ssh pitrac.local`)

**Set username and password:**
- **Username**: Enter a username for your Pi account
  - Recommended: `pitrac` or your preferred username
  - This will be `<PiTracUsername>` throughout documentation
- **Password**: Choose a strong password
  - This is what you'll use to log in via SSH or console
  - Write this down securely - you'll need it for first login
  - Make sure "Password" checkbox is enabled

**Configure wireless LAN (WiFi):**
- **SSID**: Your WiFi network name
- **Password**: Your WiFi password
- **Wireless LAN country**: Select your country (required for regulatory compliance)
- Even if using ethernet, configuring WiFi provides a backup connection method

**Set locale settings:**
- **Time zone**: Select your timezone (e.g., `America/New_York`)
- **Keyboard layout**: Select your keyboard layout (e.g., `us` for US English)

#### SERVICES Tab

**Enable SSH:**
- Check the box for "Enable SSH"
- This allows you to remotely connect to your Pi
- Select "Use password authentication"
- SSH is required for headless operation and remote management
- You can set up key-based authentication later for enhanced security

#### OPTIONS Tab

**Eject media when finished:**
- Enable this option to safely eject the SD card after writing

**Enable telemetry:**
- Optional - sends anonymous usage statistics to Raspberry Pi Foundation

### 7. Start the Imaging Process

- Review all settings carefully
- Click **"SAVE"** to save your customization settings
- Click **"YES"** to apply OS customisation settings
- Click **"YES"** again to confirm you want to erase the SD card
- The imaging process will begin

**Time estimate**: 15-25 minutes depending on SD card speed and internet connection

The imager will:
- Download the OS image if not cached
- Write the image to your SD card
- Verify the write was successful
- Apply your custom settings
- Eject the card (if enabled)

---

## First Boot

### 8. Prepare for Boot

Once the SD card is written and verified:
- Eject the SD card from your computer
- Insert the Micro SD card into your Pi's card slot
  - **Never** insert or remove the SD card while the Pi is powered on
- If you have keyboard, mouse, and monitor, connect them now
  - Even for headless setups, having a monitor for first boot helps troubleshoot issues
- Connect the ethernet cable (if using wired network)
- Finally, connect the power supply to boot the Pi

### 9. First Boot Process

The first boot takes 2-3 minutes as the Pi:
- Expands the filesystem to use the full SD card
- Applies your custom settings (hostname, username, WiFi, SSH)
- Generates SSH host keys
- Connects to WiFi/network
- Resizes partitions

The Pi will automatically reboot once during this process.

**What you'll see:**
- **Desktop version**: LXDE desktop after boot completes
- **Lite version**: Login prompt at the console
- **Headless**: Wait 3-4 minutes then try to SSH

### 10. Find Your Pi's IP Address

For SSH connection, you need the Pi's IP address:

**Option 1** - If you have a monitor connected:
```bash
hostname -I
```

**Option 2** - Check your router's DHCP client list

**Option 3** - Use hostname with mDNS (usually works):
```bash
ssh <PiTracUsername>@<hostname>.local
# Example: ssh pitrac@pitrac.local
```

**Option 4** - Use a network scanner like `nmap` or "Angry IP Scanner"

---

## Next Steps

**OS Installation Complete!**

Continue to: **[First Login & Updates]({% link software/pi-setup/first-login.md %})**

Or return to: **[Pi Setup Overview]({% link software/pi-setup.md %})**

---

## Troubleshooting

**Can't find Pi on network:**
- Wait 5 minutes - first boot takes time
- Check router for new DHCP clients
- Try connecting monitor to see boot progress
- Verify ethernet cable is connected
- Check WiFi credentials if using wireless

**Imager fails to write:**
- Try a different SD card
- Check if SD card is write-protected
- Run imager as administrator
- Verify SD card is not corrupted

**SD card won't eject:**
- Safely eject from your OS before removing
- On Windows: Right-click drive → Eject
- On Mac: Drag to trash or use Eject button
- On Linux: Use `umount` command
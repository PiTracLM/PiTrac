---
title: First Login & Updates
layout: default
nav_order: 2
parent: Raspberry Pi Setup
grand_parent: Software
description: Initial login to Raspberry Pi, system updates, and basic configuration for PiTrac including SSH access and sudo privileges.
keywords: raspberry pi first login, SSH raspberry pi, pi system update, sudo privileges, remote login pi
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# First Login & Updates

After installing Raspberry Pi OS, you need to log in and perform initial system updates.

**Time Required:** ~15 minutes

**Difficulty:** Beginner

---

## Login Options

You have two ways to access your Pi:

### Option 1: Direct Login (With Monitor & Keyboard)

If you have a monitor, keyboard, and mouse connected:

1. You'll see either the desktop (Desktop version) or login prompt (Lite version)
2. Enter your username and password (configured during OS installation)
3. For Desktop version, you're logged in automatically after entering credentials
4. For Lite version, you'll see a command prompt after successful login

### Option 2: Remote Login via SSH

SSH (Secure Shell) allows you to access your Pi remotely from another computer.

**Prerequisites:**
- Pi must be on same network as your computer
- SSH must be enabled (configured during OS installation)
- Know your Pi's hostname or IP address

**From Windows:**
```bash
# Using PuTTY or Windows Terminal
ssh <PiTracUsername>@<hostname>.local
# Example: ssh pitrac@pitrac.local

# Or with IP address
ssh <PiTracUsername>@192.168.1.100
```

**From Mac/Linux:**
```bash
# Open Terminal
ssh <PiTracUsername>@<hostname>.local
# Example: ssh pitrac@pitrac.local
```

**First SSH Connection:**
- You'll see a message about host authenticity (ECDSA key fingerprint)
- Type `yes` to continue
- Enter your password when prompted
- You won't see characters while typing password - this is normal

{: .note }
**Tip**: Logging in remotely makes it easy to copy-paste commands from this documentation directly into your Pi terminal.

---

## Initial System Updates

After logging in, immediately update your system to get the latest security patches and software versions.

### Update Package Lists

```bash
sudo apt update
```

This downloads information about the newest versions of packages and their dependencies.

### Upgrade Installed Packages

```bash
sudo apt -y upgrade
```

The `-y` flag automatically answers "yes" to prompts. This can take 5-15 minutes depending on how many packages need updating.

### Reboot

After updates complete, reboot to ensure all changes take effect:

```bash
sudo reboot now
```

Your SSH connection will close. Wait 1-2 minutes, then log back in.

---

## Verify Sudo Privileges

The user account you created during OS installation should automatically have sudo (administrator) privileges. Let's verify:

### Test Sudo Access

```bash
sudo whoami
```

If configured correctly, this should output: `root`

### If Sudo Doesn't Work

If you get an error like "`<username> is not in the sudoers file`", you need to add your user to the sudo group.

**From another account with sudo access:**
```bash
sudo usermod -aG sudo <PiTracUsername>
```

**Or edit sudoers file directly** (advanced):
```bash
su -  # Switch to root
visudo
# Add this line:
# <PiTracUsername> ALL=(ALL:ALL) ALL
```

More guidance: [How to grant sudo privileges](https://askubuntu.com/questions/168280/how-do-i-grant-sudo-privileges-to-an-existing-user)

---

## Verify System Information

Check your Pi is configured correctly:

### Check OS Version

```bash
cat /etc/os-release
```

Look for:
- `VERSION_CODENAME` should be `bookworm` (Debian 12) or `trixie` (Debian 13)
- `PRETTY_NAME` should say "Raspberry Pi OS"

### Check Architecture

```bash
uname -m
```

Should output: `aarch64` (64-bit ARM)

{: .warning }
If you see `armv7l` or `armhf`, you're running 32-bit OS. PiTrac requires 64-bit - you must re-image your SD card.

### Check Available Disk Space

```bash
df -h
```

Look for the root filesystem (`/`):
- Should show most of your SD card size (minus OS overhead)
- Ensure you have at least 10GB free for PiTrac installation

---

## Next Steps

**Basic Setup Complete!**

**Essential Next Step:**
- **[Install PiTrac Software]({% link software/pitrac-install.md %})** - Install the launch monitor software

**Optional Advanced Configuration:**
- **[Advanced Setup]({% link software/pi-setup/advanced.md %})** - NVMe boot, NAS mounting, SSH keys

**Return to:**
- **[Pi Setup Overview]({% link software/pi-setup.md %})**

---

## Troubleshooting

**Can't SSH to Pi:**
- Verify Pi is powered on and network cable connected
- Check router to confirm Pi has IP address
- Try using IP address instead of hostname
- Verify SSH was enabled during OS installation
- Ping the Pi: `ping <hostname>.local` or `ping <ip-address>`

**Updates failing:**
- Check internet connection: `ping google.com`
- Verify DNS is working: `nslookup google.com`
- Try different mirror: `sudo apt update --fix-missing`
- Check disk space: `df -h`

**Permission denied errors:**
- Verify you're using `sudo` for system commands
- Check sudo privileges as shown above
- Ensure you're logged in as the correct user: `whoami`

**Forgot password:**
- If you have physical access, you can reset via recovery mode
- Otherwise, you'll need to re-image the SD card
- This is why it's important to write down your password!
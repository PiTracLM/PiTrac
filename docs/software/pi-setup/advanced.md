---
title: Advanced Configuration
layout: default
nav_order: 3
parent: Raspberry Pi Setup
grand_parent: Software
description: Advanced Raspberry Pi configuration for PiTrac including NVMe boot setup, NAS drive mounting, Samba server configuration, and SSH key authentication.
keywords: raspberry pi nvme boot, NAS mount raspberry pi, samba server pi, SSH keys raspberry pi, pi advanced setup
og_image: /assets/images/logos/PiTrac_Square.png
last_modified_date: 2025-01-04
---

# Advanced Configuration

**Optional configurations for enhanced performance and development workflows.**

{: .note }
These are **optional** configurations. PiTrac works fine without them. Only proceed if you need these specific features.

**What's covered:**
- NVMe SSD boot for faster performance
- NAS drive mounting for safer development
- Samba server for file sharing between Pis
- SSH key authentication for passwordless login
- Git configuration for shared drives

---

## NVMe Boot Setup

Boot from an NVMe SSD for significantly faster performance compared to SD cards.

**Benefits:**
- 5-10x faster read/write speeds
- More reliable than SD cards
- Larger storage capacity

**Requirements:**
- Raspberry Pi 5 (Pi 4 requires USB boot, not NVMe)
- NVMe HAT or adapter board
- NVMe M.2 SSD drive
- Already have working Pi with OS on SD card

**Time Required:** ~30 minutes

---

### Installation Steps

**1. Install Hardware**

With Pi powered off:
- Install NVMe HAT/adapter per manufacturer instructions
- Insert NVMe SSD into M.2 slot
- Ensure secure connections

**2. Enable PCIe Interface**

Boot the Pi from SD card and enable PCIe:

```bash
cd /boot/firmware/
sudo cp config.txt config.txt.ORIGINAL
sudo nano /boot/firmware/config.txt
```

Add before the last `[all]` section:
```
# Enable the PCIe External Connector
dtparam=pciex1
```

Alternative (more memorable):
```
dtparam=nvme
```

{: .note }
For non-HAT+ adapters, add `PCIE_PROBE=1` on the first non-commented line.

Save and reboot:
```bash
sudo reboot now
```

**3. Verify NVMe Detection**

After reboot:
```bash
lsblk
```

You should see `nvme0n1` listed:
```
NAME        MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS
mmcblk0     179:0    0  29.7G  0 disk
|-mmcblk0p1 179:1    0   512M  0 part /boot/firmware
`-mmcblk0p2 179:2    0  29.2G  0 part /
nvme0n1     259:0    0 238.5G  0 disk
```

**4. Configure Boot Order**

Use raspi-config for easiest setup:
```bash
sudo raspi-config
```

Navigate to:
- **Advanced Options** → **Boot Order**
- Select **NVMe/USB Boot** (boots from NVMe first)
- Exit and don't reboot yet

**5. Copy OS to NVMe**

**Desktop version (GUI):**
- Open: **Applications → Accessories → SD Card Copier**
- Source: SD card
- Destination: NVMe drive
- Click "Start"
- Wait 10-15 minutes

**Lite version (command line):**
```bash
# Use dd or similar tool - see online guides
# Or temporarily boot Desktop to use SD Card Copier
```

**6. Boot from NVMe**

```bash
sudo poweroff
```

Remove power, **remove the SD card**, then power on.

Pi should boot from NVMe - much faster!

**Troubleshooting:**
- If it doesn't boot, reinsert SD card and check boot order
- Verify NVMe was detected in step 3
- Check [Geekworm NVMe guide](https://wiki.geekworm.com/NVMe_SSD_boot_with_the_Raspberry_Pi_5)

---

## NAS Drive Mounting

Mount a remote NAS drive for safer development and easier file sharing.

**Benefits:**
- Keep important files off the Pi (Pis can be fragile)
- Easy file access from multiple computers
- Centralized backup location

**Requirements:**
- NAS or file server with NFS or CIFS/SMB
- Network connectivity between Pi and NAS
- NAS credentials (username/password)

---

### Setup NFS Mount (Recommended)

NFS is simpler and faster for Linux-to-Linux connections.

**1. Create Mount Point**

```bash
sudo mkdir /mnt/PiTracShare
```

**2. Backup fstab**

```bash
cd /etc
sudo cp fstab fstab.original
sudo chmod 600 /etc/fstab
```

**3. Edit fstab**

```bash
sudo nano /etc/fstab
```

Add at the end:
```
<NAS_IP_ADDRESS>:/<NAS_SHARE_NAME> /mnt/PiTracShare nfs _netdev,auto 0 0
```

Example:
```
10.0.0.100:/NAS_Share_Drive /mnt/PiTracShare nfs _netdev,auto 0 0
```

**4. Mount**

```bash
sudo systemctl daemon-reload
sudo mount -a
```

**5. Verify**

```bash
ls -la /mnt/PiTracShare
```

Should show NAS contents.

---

### Setup CIFS/SMB Mount (Windows NAS)

For Windows file servers or Samba shares.

**1. Install CIFS utils**

```bash
sudo apt install cifs-utils
```

**2. Create Mount Point**

```bash
sudo mkdir /mnt/PiTracShare
```

**3. Edit fstab**

```bash
sudo nano /etc/fstab
```

Add at the end:
```
//<NAS_IP>/<SHARE_NAME> /mnt/PiTracShare cifs username=<USERNAME>,password=<PASSWORD>,workgroup=WORKGROUP,users,exec,auto,rw,file_mode=0777,dir_mode=0777,user_xattr 0 0
```

{: .warning }
**Security Warning**: Storing passwords in fstab is insecure. Only do this on private networks. For better security, use credentials file.

**4. Mount**

```bash
sudo systemctl daemon-reload
sudo mount -a
```

---

## Samba Server Setup

Set up Samba to share directories between Pis or with other computers.

**Use case:** Share images/data between two-Pi setups (legacy configurations).

**Requirements:**
- Two Raspberry Pis on same network
- Designated "server" Pi (faster Pi recommended)

---

### On the Server Pi

**1. Install Samba**

```bash
sudo apt-get install samba samba-common-bin
sudo systemctl restart smbd
sudo systemctl status smbd
```

Should show "active (running)".

**2. Create Shared Directory**

```bash
mkdir -p ~/LM_Shares/WebShare
mkdir ~/LM_Shares/Images
```

**3. Configure Samba**

```bash
sudo nano /etc/samba/smb.conf
```

Add at the bottom:
```
[LM_Shares]
path = /home/<PiTracUsername>/LM_Shares
writeable = Yes
create mask = 0777
directory mask = 0777
public = no
```

**4. Set Samba Password**

```bash
sudo smbpasswd -a <PiTracUsername>
```

Enter your Pi user password.

**5. Restart Samba**

```bash
sudo systemctl restart smbd
```

---

### On the Client Pi

**1. Create Mount Point**

```bash
mkdir ~/LM_Shares
```

**2. Edit fstab**

```bash
sudo nano /etc/fstab
```

Add:
```
//<SERVER_PI_IP>/LM_Shares /home/<PiTracUsername>/LM_Shares cifs username=<USERNAME>,password=<PASSWORD>,workgroup=WORKGROUP,users,exec,auto,rw,file_mode=0777,dir_mode=0777,user_xattr 0 0
```

**3. Mount**

```bash
sudo systemctl daemon-reload
sudo mount -a
```

**4. Verify**

```bash
ls -la ~/LM_Shares
```

Should show Images and WebShare directories from server Pi.

---

## SSH Key Authentication

Set up passwordless SSH login using SSH keys.

**Benefits:**
- No password typing for every login
- More secure than password authentication
- Required for some automation scripts

{: .warning }
**Security Note**: Only use this on your private network with a secure computer.

---

### Setup SSH Keys

**1. On Your Computer (Not the Pi)**

Generate SSH key pair:

**Windows (using PuTTY):**
- Launch PuTTYgen
- Click "Generate"
- Move mouse to generate randomness
- Save private key (keep secure!)
- Copy public key text

**Mac/Linux:**
```bash
ssh-keygen -t rsa -b 4096
# Press Enter for default location
# Optional: Enter passphrase for extra security
```

**2. On the Pi**

Create SSH directory:
```bash
install -d -m 700 ~/.ssh
```

Edit authorized keys:
```bash
nano ~/.ssh/authorized_keys
```

Paste your public key (starts with `ssh-rsa`).

Set permissions:
```bash
chmod 644 ~/.ssh/authorized_keys
```

**3. Test Connection**

From your computer:
```bash
ssh <PiTracUsername>@pitrac.local
```

Should log in without password!

---

## Git Configuration

Configure Git for shared drive development.

**Only needed if:**
- Using Git with NAS/shared drives
- Experiencing "unsafe repository" warnings
- Using Git Desktop or Visual Studio

**Setup:**

```bash
git config --global --add safe.directory "*"
```

This tells Git to trust all repositories.

{: .warning }
Only use this on drives you fully control. Not for public/shared systems.

---

## Next Steps

**Advanced Configuration Complete!**

**Continue to:**
- **[Install PiTrac Software]({% link software/pitrac-install.md %})** - Install the launch monitor

**Return to:**
- **[Pi Setup Overview]({% link software/pi-setup.md %})**

---

## Troubleshooting

**NVMe not detected:**
- Check HAT is properly seated
- Verify M.2 SSD is inserted fully
- Check `/boot/firmware/config.txt` has `dtparam=pciex1`
- Try different boot order settings

**NAS mount fails:**
- Verify NAS IP is correct: `ping <NAS_IP>`
- Check NAS share exists and is accessible
- For NFS: Ensure NFS is enabled on NAS
- For CIFS: Verify username/password
- Check `/var/log/syslog` for mount errors

**Samba connection fails:**
- Verify Samba is running: `sudo systemctl status smbd`
- Check firewall isn't blocking port 445
- Verify server Pi IP in client fstab
- Test connection: `smbclient -L //<SERVER_IP> -U <USERNAME>`

**SSH key not working:**
- Verify public key is in `~/.ssh/authorized_keys`
- Check permissions: `ls -la ~/.ssh/`
- Ensure key format is correct (starts with `ssh-rsa`)
- Try verbose SSH: `ssh -v <PiTracUsername>@pitrac.local`
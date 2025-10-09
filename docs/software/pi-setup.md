---
title: Raspberry Pi Setup
layout: default
nav_order: 1
parent: Software
description: Complete guide for setting up Raspberry Pi computers for PiTrac, including OS installation, software dependencies, camera configuration, and network setup.
keywords: raspberry pi setup, PiTrac installation, linux configuration, camera setup, libcamera, activemq, tomee
toc: true
---

# PiTrac - Raspberry Pi Setup and Configuration

> **Quick Start**: This guide covers Raspberry Pi OS installation and initial system configuration. Once complete, proceed to the [Startup Guide]({% link software/pitrac-install.md %}) to install PiTrac.

## Table of Contents

- [Summary](#summary)
- [Necessary & Recommended Components](#necessary--recommended-components)
  - [Standard Setup](#standard-setup)
    1. [Environment](#environment)
    2. [Operating System](#operating-system)
    3. [Log Into Pi](#log-into-pi)
    4. [Remote Log Into Pi](#remote-log-into-pi)
    5. [Sudo Privileges](#sudo-privileges)
    6. [Install NVME Board](#install-nvme-board) *(Optional)*
    7. [NAS Drive Setup and Mounting](#nas-drive-setup-and-mounting) *(Optional)*
    8. [Samba Server Setup](#samba-server-setup) *(Optional)*
    9. [SSH Stored Key](#ssh-stored-key) *(Optional - recommended)*
    10. [Additional Setup](#additional-setup)
    11. [Git and GitHub](#git-and-github)

## Summary

This guide covers the **initial Raspberry Pi setup** for PiTrac, from installing the operating system to configuring your Pi for network access and basic system administration. It's designed for users of all experience levels, from beginners to experienced developers.

**What's covered in this guide:**
- Installing Raspberry Pi OS (Desktop or Lite, 64-bit only)
- Using Raspberry Pi Imager to configure SSH, WiFi, username, and password
- Initial system updates and user configuration
- Optional advanced configurations (NVMe boot, NAS mounting, multi-Pi setups)

**What's NOT covered here:**
- Installing PiTrac software → See the [Startup Guide]({% link software/pitrac-install.md %})
- Camera calibration → See the [Camera Documentation]({% link camera/cameras.md %})
- Hardware assembly → See the [Assembly Guide]({% link hardware/assembly-guide.md %})

**Current System Architecture:**
- **Single Pi setup** is now standard (legacy dual-Pi configurations are still supported but not recommended for new builds)
- All services (launch monitor, message broker, web interface) run on one Raspberry Pi
- Simplified installation using pre-built packages

> **Note**: Modern PiTrac uses a streamlined installation process with pre-built packages. You no longer need to manually build dependencies like OpenCV, ActiveMQ, or other libraries - the installation process handles everything automatically.

## Necessary & Recommended Components

- A Raspberry Pi 5 with at least 8 GB of memory  
- A Micro SD card with at least 64Gig  
- Especially if you are planning on using the larger Pi 5 as a development environment, an NVMe hat with an NVMe SSD drive is a great investment.  
- Power supplies for the Pi’s  
- Network cabling if using the Pi’s wired ethernet port (recommended \- some of the packages we’ll be downloading are large and slow over WiFi)  
- Monitor, keyboard and mouse to connect to the Pi (recommended, but can also run the Pi ‘headless’ without any direct connections  
- Especially if running headless, a Mac, PC, or other Linux machine that you will use to interact with the Pi, along with a terminal tool to login to the Pi, such as Putty.  
- Visual Studio (optional) for your PC or MAC  
  - Most of the PiTrac system runs not only in the Pi, but can also (mostly) run from a Visual Studio platform on a PC or Mac.  The more comfortable graphical  programming environment in VS is great for testing and debugging and coding new features into the same code base.  
- A separate file server, such as a NAS is highly recommended if you expect to be making changes to the PiTrac system.  Pi’s are a little fragile, so keeping the PiTrac files elsewhere in a safer environment (and then mounting those files on the Pi remotely) is a good practice.  
  - We typically have a separate server that we use both from the Pi and also from a PC running Visual Studio that is used to help debugging.  
  - It’s wise to think of the Pi as a temporary, write-only device that could be erased at any time.

### Standard Setup

#### Environment

1. Create a secure, static-safe environment to run your Pi’s on. 3D-printing the two Pi-side “floors” from the plans on GitHub is one way to provide this environment, and you’ll need to print them at some point anyway.

#### Operating System

2. **Installing Raspberry Pi OS**

   {: .warning }
   > **CRITICAL - OS Version Requirements**
   >
   > PiTrac currently requires **Raspberry Pi OS (Legacy, 64-bit)**
   >
   > - **Debian version:** 12 (Bookworm)
   > - **Kernel version:** 6.12
   > - **System:** 64-bit
   >
   > **Do NOT use:**
   > - The latest Raspberry Pi OS (based on Debian 13 Trixie) - PiTrac packages not yet updated for this version
   > - 32-bit versions - will not work with PiTrac

   **Why the Legacy version?**

   The latest Raspberry Pi OS moved to Debian 13 (Trixie), but PiTrac's dependency packages haven't been updated yet. The Legacy version based on Debian 12 (Bookworm) is what's currently supported and tested.

   **Desktop or Lite?**

   Both work fine. Desktop has a GUI and is easier for first-timers. Lite is command-line only and uses fewer resources.


   **Installation Steps**

   a. **Prepare your hardware**
      - Start with the Pi powered off (unplugged)
      - Have a Cat5/6 ethernet cable connected to your local network if possible (recommended for faster downloads)
      - On your PC/Mac, connect a Micro SD card via USB adapter
      - Use a 64GB card minimum (32GB may work but 64GB recommended for room to expand)

   b. **Download and install Raspberry Pi Imager**
      - Download from [raspberrypi.com/software](https://www.raspberrypi.com/software/)
      - Install for your operating system (Windows, macOS, or Ubuntu)
      - Launch the application

   c. **Select your device**
      - Click "CHOOSE DEVICE"
      - Select either "Raspberry Pi 4" or "Raspberry Pi 5" depending on your hardware
      - This optimizes the OS for your specific Pi model

   d. **Choose the operating system**
      - Click "CHOOSE OS"
      - Navigate to "Raspberry Pi OS (other)"
      - Select **ONE** of the following Legacy versions:
        - **"Raspberry Pi OS (Legacy, 64-bit)"** - Desktop version with GUI
        - **"Raspberry Pi OS (Legacy, 64-bit) Lite"** - Command-line only
      - Based on Debian 12 (Bookworm), Kernel 6.12
      - **Important**: Must be Legacy version. Do not use regular Raspberry Pi OS (Trixie) or 32-bit versions.

   e. **Select storage**
      - Click "CHOOSE STORAGE"
      - Select your Micro SD card
      - **WARNING**: Triple-check this is your SD card and NOT your computer's hard drive! Everything on this storage will be erased.

   f. **Configure OS customization settings**
      - Click "NEXT"
      - When prompted "Would you like to apply OS customisation settings?", click **"EDIT SETTINGS"**
      - If you're American, feel free to ignore the British spelling of "customisation" ;)

      **In the GENERAL tab:**

      1. **Set hostname**
         - Choose a descriptive hostname to identify your Pi on the network
         - Examples: `pitrac`, `pitrac-main`, `rsp01`, `rsp02`
         - This will be how you connect to the Pi (e.g., `ssh pitrac.local`)

      2. **Set username and password**
         - **Username**: Enter a username for your Pi account
           - Recommended: `pitrac` or your preferred username
           - This will be `<PiTracUsername>` throughout this documentation
         - **Password**: Choose a strong password
           - This is what you'll use to log in via SSH or at the console
           - Write this down securely - you'll need it for first login
           - Make sure "Password" checkbox is enabled

      3. **Configure wireless LAN (WiFi)**
         - **SSID**: Your WiFi network name
         - **Password**: Your WiFi password
         - **Wireless LAN country**: Select your country (required for regulatory compliance)
         - This allows the Pi to connect to WiFi on first boot
         - Even if using ethernet, configuring WiFi provides a backup connection method

      4. **Set locale settings**
         - **Time zone**: Select your timezone (e.g., `America/New_York`)
         - **Keyboard layout**: Select your keyboard layout (e.g., `us` for US English)

      **In the SERVICES tab:**

      1. **Enable SSH**
         - Check the box for "Enable SSH"
         - This allows you to remotely connect to your Pi
         - Select "Use password authentication"
         - SSH is required for headless operation and remote management
         - You can set up key-based authentication later for enhanced security (see step 10)

      **In the OPTIONS tab:**

      1. **Eject media when finished**
         - Enable this option to safely eject the SD card after writing
      2. **Enable telemetry**
         - This is optional and sends anonymous usage statistics to Raspberry Pi Foundation

   g. **Start the imaging process**
      - Review all settings carefully
      - Click "SAVE" to save your customization settings
      - Click "YES" to apply OS customisation settings
      - Click "YES" again to confirm you want to erase the SD card
      - The imaging process will begin

      ⏱**Time estimate**: 15-25 minutes depending on your SD card speed and internet connection

      The imager will:
      - Download the OS image if not cached
      - Write the image to your SD card
      - Verify the write was successful
      - Apply your custom settings
      - Eject the card (if enabled)

   h. **First boot**
      - Once the SD card is written and verified, eject it from your computer
      - Insert the Micro SD card into your Pi's card slot
         - **Never** insert or remove the SD card while the Pi is powered on
      - If you have a keyboard, mouse, and monitor, connect them now
         - Even for headless setups, having a monitor for first boot helps troubleshoot any issues
      - Connect the ethernet cable (if using wired network)
      - Finally, connect the power supply to boot the Pi

   i. **First boot process**
      - The first boot takes 2-3 minutes as the Pi:
        - Expands the filesystem to use the full SD card
        - Applies your custom settings (hostname, username, WiFi, SSH)
        - Generates SSH host keys
        - Connects to WiFi/network
        - Resizes partitions
      - The Pi will automatically reboot once during this process
      - **Desktop version**: You'll see the LXDE desktop after boot completes
      - **Lite version**: You'll see a login prompt at the console
      - If using headless, wait 3-4 minutes then try to SSH (see next step)

   j. **Find your Pi's IP address** (for SSH connection)
      - **Option 1**: If you have a monitor connected, log in and run:
        ```bash
        hostname -I
        ```
      - **Option 2**: Check your router's DHCP client list
      - **Option 3**: Use hostname with mDNS (usually works on local networks):
        ```bash
        ssh <PiTracUsername>@<hostname>.local
        # Example: ssh pitrac@pitrac.local
        ```
      - **Option 4**: Use a network scanner like `nmap` or "Angry IP Scanner"

#### Log into Pi

4. Log into the Pi using whatever credentials you expect to use to run PiTrac (the `<PiTracUserName>`).  
   a. If running headless, remotely login using PuTTY or an SSH tool of your choice.  
      1. Logging in from whatever computer you are reading this setup document on will make it easy to copy-paste from this document into files on the Pi.  
      2. For example:  
         ```bash
         putty rsp02 -l <username>
         ```  
         (the boot image should already allow PuTTY)  
   b. If running directly with a monitor and keyboard, click on the updates icon near the top-right to make sure everything is up to date.  
   c. Install everything to get up to date.  
      - Or, equivalently, do the following from the command line:  
         ```bash
         sudo apt -y update
         sudo apt -y upgrade
         sudo reboot now
         ```  
         (to make sure everything is updated)

#### Remote Log into Pi

5. Remotely login (to be able to paste from this setup document):  
   ```bash
   putty rsp01 -l <username>
   ```  
   (the boot image should already allow PuTTY)  
   Then, follow the instructions below…

#### Sudo Privileges

6. If necessary, make sure that `<PiTracUserName>` has sudo privileges.  
   a. Some guidance [here](https://askubuntu.com/questions/168280/how-do-i-grant-sudo-privileges-to-an-existing-user).

#### Install NVME Board

7. **To Install an NVME Board on the Pi** [Optional, and probably only for the Pi 5 (confusingly referred to as the “Pi 1” computer in the PiTrac project)]:  
   a. If you have an SSD drive, best to get it up and booting now before you install everything on the slower, smaller MicroSD card instead.  
   b. See also the instructions here, which will work in most cases: [NVMe SSD boot with the Raspberry Pi 5](https://wiki.geekworm.com/NVMe_SSD_boot_with_the_Raspberry_Pi_5)  
      Although the instructions below should work as well.  
   c. With the Pi off, install the NVMe Board and NVMe SSD drive per instructions of whatever board you are using.  
   d. Power up and enable the PCIe interface (your instructions may differ):  
      1. `cd /boot/firmware/`  
      2. `sudo cp config.txt config.txt.ORIGINAL`  
      3. By default the PCIe connector is not enabled.  
      4. To enable it, add the following option into `/boot/firmware/config.txt` before the last “[all]” at the end of the file and reboot (`sudo reboot now`):  
         - `# Enable the PCIe External Connector.`  
         - `dtparam=pciex1`  
         - A more memorable alias for `pciex1` exists, so you can alternatively add `dtparam=nvme` to the `/boot/firmware/config.txt` file.  
      e. After the reboot, we will image the NVMe drive.  
      5. First, ***if using a non-HAT+ adapter***, add on the first non-commented line of `/boot/firmware/config.txt`:  
         - `PCIE_PROBE=1` (see instructions for your device)  
      6. Change `BOOT_ORDER` to `BOOT_ORDER=0xf416` (to boot off NVM first), OR — better yet:  
         1. `sudo raspi-config`  
         2. Go to the Advanced Options → Boot Order  
         3. Select whatever order you want, usually NVMe card first.  
      7. Shutdown, remove power to the Pi, and reboot. Afterward, an `lsblk` command should show something like this (see last line):  
         ```
         pitrac@rsp05:~ $ lsblk    

         NAME        MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS    

         mmcblk0     179:0    0  29.7G  0 disk    

         |-mmcblk0p1 179:1    0   512M  0 part /boot/firmware    

         `-mmcblk0p2 179:2    0  29.2G  0 part /    

         nvme0n1     259:0    0 238.5G  0 disk    
         ```  
      8. At this point, the NVMe drive should be accessible, and we will make a copy (image) of the bootup MicroSD card onto the SSD drive.  
      9. From the Pi Graphical Desktop, **Applications → Accessories → SD Card Copier** on the main screen, run the SD Card Copier program, and copy the OS to the NVMe SSD. There’s no need to select the separate UUID option.  
         - If running headless, see the internet for other ways to image the SSD.  
      10. Power down, remove power, then remove the SSD card.  
      11. When you turn the power on, the Pi should reboot from the SSD drive, and it should be pretty quick!

#### NAS Drive Setup and Mounting

8. **Setup mounting of a remote NAS drive (or similar)**  
   a. Many folks use an external NAS drive for development so that you can’t lose everything if an individual Pi has an issue. An external drive also allows for easier transfers of files to the Pi from another computer that can also see that drive.  
   b. The remote drive will store the development environment, though you can obviously set up the PiTrac not to need a separate drive once you have everything working. However, it’s really a good idea to have the development and test environment on a different computer than on the individual Pis.  
   c. There are many ways to automatically mount a removable drive to a Pi. The following is just one way that assumes you have a NAS with NFS services enabled and with a shareable drive that the Pi can read/write to.  
      - **NOTE:** If this Pi will be anywhere in a public network, do not include your password in the fstab!  
      d. `sudo mkdir /mnt/PiTracShare`  
      e. `cd /etc`  
      f. `sudo cp fstab fstab.original`  
      g. `sudo chmod 600 /etc/fstab` *(to try to protect any passwords in the file)*  
      h. `sudo vi fstab`  
   d. If using NFS (usually easier), put the following in the fstab file:  
      - `<NAS IP Address>:/<NAS Shared Drive Name> /mnt/PiTracShare nfs _netdev,auto 0 0`  
      - **Example:**  
        - `10.0.0.100:/NAS_Share_Drive /mnt/PiTracShare nfs _netdev,auto 0 0`  
   e. If using CIFS:  
      1. Add the following to `/etc/fstab` after the last non-comment line, replacing `PWD` and other things in `[]` with the real password and info:  
         - `//<NAS IP Address>/<NAS Shared Drive Name> /mnt/PiTracShare cifs username=[PiTracUserName],password=[PWD],workgroup=WORKGROUP,users,exec,auto,rw,file_mode=0777,dir_mode=0777,user_xattr 0 0`  
      2. `sudo systemctl daemon-reload`  
      3. `sudo mount -a`  
      4. If there’s an error, make sure the password is correct.  
      5. `ls -al /mnt/PiTracShare` should show any files there.

#### Samba Server Setup

9. **Setup Samba server** (to allow the two Pis to share a folder between themselves)  
   a. This allows the Pis to serve out directories to each other to share information like debugging pictures.  
   b. See [this guide](https://pimylifeup.com/raspberry-pi-samba/) for the basics.  
   c. We suggest the faster Pi 5 (or whatever will be connected to Camera 1) be the Pi from which the shared directory is shared.  
   d. On the Pi from which the directory will be shared:  
      1. `sudo apt-get install samba samba-common-bin`  
      2. `sudo systemctl restart smbd`  
      3. `sudo systemctl status smbd` *(should show “active (running)”)*  
      4. Create the directory structure to be shared:  
         - `mkdir -p /home/<PiTracUsername>/LM_Shares/WebShare`  
         - `mkdir /home/<PiTracUsername>/LM_Shares/Images`  
      5. `sudo vi /etc/samba/smb.conf` and add at the bottom:  
         ```
         [LM_Shares]
         path = /home/<PiTracUsername>/LM_Shares
         writeable = Yes
         create mask = 0777
         directory mask = 0777
         public = no
         ```  
      6. `sudo smbpasswd -a <PiTracUsername>` *(enter the same password as the PiTracUsername)*  
      7. `sudo systemctl restart smbd`  
   e. On the Pi to which the directory will be shared:  
      1. Add the following to `/etc/fstab` after the last non-comment line, replacing `PWD` and other info:  
         - `//<Pi 1’s IP Address>/LM_Shares /home/<PiTracUser>/LM_Shares cifs username=[PiTracUserName],password=[PWD],workgroup=WORKGROUP,users,exec,auto,rw,file_mode=0777,dir_mode=0777,user_xattr 0 0`  
      2. `mkdir /home/<PiTracUsername>/LM_Shares` *(this will be the Pi 2 mount point)*  
      3. `sudo systemctl daemon-reload`  
      4. `sudo mount -a`  
      5. Check to make sure the second Pi can “see” the other Pi’s LM_Shares sub-directories (Images and WebShare).


#### SSH Stored Key

10. **Setup SSH to use a stored key** (optional, but really useful to avoid having to type a password every time)  
    1. **WARNING –** This step assumes your PiTrac is secure in your own network and that the machine you use to log in is not used by others (given that this helps automate remote logins).  
    2. If not already, remotely log into the Pi from the machine where you’re reading this document.  
    3. Create an SSH directory:  
       ```bash
       install -d -m 700 ~/.ssh
       ```  
    4. Install PuTTY on the remote (non-Pi Mac/PC) machine that you’ll use to log in.  
    5. Use the PuTTYgen utility to generate a public key. This is just a long text string or two.  
    6. Edit `~/.ssh/authorized_keys` and paste in the public key for PuTTY.  
    7. (Alternatively, you can just use the mount to get a copy of the file from another Pi.)  
    8. The key would have been generated using PuTTYgen.  
    9. The file should simply have each key (no spaces!) preceded on the same line with `ssh-rsa `.  
    10. Set permissions:  
        ```bash
        sudo chmod 644 ~/.ssh/authorized_keys
        ```  

#### Additional Setup

11. If you don’t already have your development world set up the way you want it, we suggest trying some of the environments/tools at the bottom of these instructions labeled “[**Nice-to-Haves for an easy-to-use development environment**](#nice-to-haves)”.

#### Git and GitHub

12. **Git and GitHub**  
    1. If the project will be hosted on a shared drive, and you 100% control that drive and it’s not public, then let GitHub know that we’re all family here. On the Pi and on whatever computer you log in from, do:  
       ```bash
       git config --global --add safe.directory "*"
       ```  
    2. Otherwise, Git Desktop and Visual Studio often have problems.

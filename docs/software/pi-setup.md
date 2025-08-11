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

- [Summary](#summary)  
- [Necessary & Recommended Components](#necessary--recommended-components)  
  - [Standard Setup](#standard-setup)  
    1. [Environment](#environment)  
    2. [Operating System](#operating-system)  
    3. [Log Into Pi](#log-into-pi)  
    4. [Remote Log Into Pi](#remote-log-into-pi)  
    5. [Sudo Priviledges](#sudo-priviledges)  
    6. [Install NVME Board](#install-nvme-board)  
    7. [NAS Drive Setup and Mounting](#nas-drive-setup-and-mounting)  
    8. [Samba Server Setup](#samba-server-setup)  
    9. [SSH Stored Key](#ssh-stored-key)  
    10. [Additional Setup](#additional-setup)  
    11. [Git and Github](#git-and-github)  
    12. [Clock Configuration](#clock-configuration)  
    13. [Build and Install OpenCV](#build-and-install-opencv)  
    14. [Install Boost](#install-boost)  
    15. [Build and Install LGPIO](#build-and-install-lgpio)  
    16. [Build and Install Libcamera](#build-and-install-libcamera)  
    17. [Build RPICAM-Apps](#build-rpicam-apps)  
    18. [Install Java OpenJDK](#install-java-openjdk)  
    19. [Install MsgPack](#install-msgpack)  
    20. [Install ActiveMQ C++ CMS](#install-activemq-c-cms)  
    21. [Install ActiveMQ Broker](#install-activemq-broker)  
    22. [Install Maven](#install-maven)  
    23. [Install Tomee](#install-tomee)  
    24. [Install Launch Monitor Dependencies](#install-launch-monitor-dependencies)  
- [Build Launch Monitor](#build-launch-monitor)  
  - [Setup PiTrac](#setup-pitrac)

## Summary

These instructions are targeted toward folks who do not have a lot of experience building software systems in the Pi Operating System and who could benefit from more step-by-step direction. Someone who’s familiar with using tools like meson and ninja to build software can likely skip over many of these steps. However, the instructions contain a number of idiosyncratic steps and configuration requirements that are particular to PiTrac.
r
These instructions start with a Raspberry Pi with nothing on it, and are meant to describe all the steps to get from that point to a working, compiled version of PiTrac.  PiTrac currently requires two Raspberry Pi’s, so the majority of these instructions will have to be repeated twice.  Because the ‘smaller’ Pi system that connects to Camera 2 is the only Pi that handles the Tomcat/Tomee web-based GUI for the system, there are a few more steps for that system.

NOTE - The new "single-pi" version of PiTrac does not have its own documentation yet.  Until then, please note that these instructions can work when using only a single Pi.  You just need to do everything with what we consider here to be the "Pi 1" system even if the instructions refer to the Pi 2 system.  For example, if a step in this document says "Log into the Pi 2 computer...", you will just log into the (only) Pi 1 system.

## Necessary & Recommended Components

- A Raspberry Pi 4 and a Pi 5 with at least 4 GB of memory (8 GB recommend for the Pi 5\)  
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

2. **Raspian O/S and Pi Initialization**  
   a. Start with the Pi powered off (unplugged). Have a Cat5/6 cable connected to your local network plugged in if possible.  
   b. On a PC, connect a Micro SD card via USB to use to boot the Pi for the first time.  
      1. Use a 64 GB card so we have room to expand  
   c. Install and run the [RPi Imager utility](https://www.raspberrypi.com/software/)  
   d. Select Pi 4 or 5 for the device depending on what you have, for Operating System choose the 64-bit OS. Make sure the “Storage” is pointing to the MicroSD card (and not something like your hard drive!), as it will be overwritten entirely. Hit NEXT.  
   e. Answer “EDIT SETTING” when asked if you want to apply customisations  
      1. If you are American, ignore the clearly-incorrect spelling of “customization.” ;)  
   f. In the **GENERAL** tab:  
      1. Select a hostname that will easily distinguish between the two Pi’s in the system, such as `rsp01`, `rsp02`, etc.  
      2. Add a `<PiTracUsername>` username that will be used to compile and run PiTrac, and that can log into your NAS, if you’re using a server (recommended). E.g., “pitrac” as a user (or just use “pi”).  
         - This will be the username and password necessary to log into the system for the first time, so double-check you’ve verified both.  
         - Use the actual username whenever you see `<PiTracUsername>` below.  
      3. Make sure the wireless LAN credentials are set up in case you can’t connect a hard line.  
   g. In the **SERVICES** tab:  
      1. Enable SSH and use password authentication.  
   h. After setting up the customizations, select YES to "apply OS customisation settings" to the card setup and start the write process.  

      ⏱️ **Time estimate:** About 20 minutes  

   i. Once the SD Card is written and verified, if you have a keyboard, mouse, and monitor, hook those up first. This can all be done via a remote login, but it’s nice to have a full user setup from the beginning if there are any issues.  
   j. Insert the Micro SD card into the Pi and start up the Pi by plugging in the power (**do not** insert or disconnect the SD card when the Pi is on).  

   k. The first bootup takes a while. Better to monitor it with an attached monitor if possible.

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

#### Clock Configuration

13. **Configure the clock to not vary** (as our timing is based on it!)  
    1. `cd /boot/firmware`  
    2. `sudo cp config.txt config.txt.ORIGINAL`  
    3. `sudo vi config.txt`  
    4. For Pi 4 & 5:  
       - Set `force_turbo=1` in `/boot/firmware/config.txt`  
       - Example:  
         ```
         # PiTrac MOD - force fast clock even at low load
         force_turbo=1
         ```  
    5. For Pi 5 also add:  
       - `arm_boost=1` in `/boot/firmware/config.txt`

#### Build and Install OpenCV

14. **Install and build OpenCV** (for both Python and C++)  
    a. Latest version of OpenCV as of this writing (late 2024) is 4.10.  
    b. See: [itslinuxfoss.com - Install OpenCV Debian](https://itslinuxfoss.com/install-opencv-debian/) for more information on installing.  
    c. If necessary, increase swap space to have around 6 GB of usable space. For a 4 GB or larger Pi, you can skip this step and just go to compiling:  
       1. See: [QEngineering - Install OpenCV 4.5 on Raspberry Pi 64 OS](https://qengineering.eu/install-opencv-4.5-on-raspberry-64-os.html)  
          - Enlarge the boundary (`CONF_MAXSWAP`):  
            ```bash
            sudo nano /sbin/dphys-swapfile
            ```  
          - Give the required memory size (`CONF_SWAPSIZE`):  
            ```bash
            sudo nano /etc/dphys-swapfile
            ```  
          - Reboot afterwards:  
            ```bash
            sudo reboot
            ```  
       2. See also: [OpenCV Docs - Linux Install](https://docs.opencv.org/3.4/d7/d9f/tutorial_linux_install.html)  
    d. Compile OpenCV:  
       1. `mkdir ~/Dev`  
       2. `cd Dev` (this is where we will compile the packages PiTrac needs)  
       3. See [QEngineering guide](https://qengineering.eu/install-opencv-4.5-on-raspberry-64-os.html).  
       4. You can use the fully automated script from the above webpage, though you might learn more if you follow the steps in the guide (which mirror the script).  
          - The script is named `OpenCV-4-10-0.sh` and is available as described in the above URL.  
          - In the script, change the following before running:  
            ```
            INSTALL_C_EXAMPLES=ON
            INSTALL_PYTHON_EXAMPLES=ON
            ```  
          - If your Pi only has 4 GB or less, change `-j4` to `-j 2` to prevent the compile process from consuming all the memory.  
          - Run the script and review the output to make sure there were no errors.  
          - The script takes quite a while to run on some Pis.  
       5. Ensure the script runs the `sudo make install` step at the end.

#### Install Boost

15. **Install Boost** (a set of utilities that PiTrac uses)  
    1. Install the current version of the Boost development environment:  
       ```bash
       sudo apt-get install libboost1.74-all
       ```  
    2. **NOTE:** This next step should no longer be needed. Only do this if the `meson setup build` step does not work. Create a `boost.pc` file to tell Meson how to find Boost files when PiTrac is compiled:  
       ```bash
       sudo vi /usr/share/pkgconfig/boost.pc
       ```  
       Place the following in it:  
       ```
       # Package Information for pkg-config
       # Path to where Boost is installed
       prefix=/usr
       # Path to where libraries are
       libdir=${prefix}/lib
       # Path to where include files are
       includedir=${prefix}/boost
       Name: Boost
       Description: Boost provides free peer-reviewed portable C++ source libraries
       Version: 1.74.0   # OR WHATEVER VERSION YOU DOWNLOAD
       Libs: -L${libdir} -lboost_filesystem -lboost_system -lboost_timer -lboost_log -lboost_chrono -lboost_regex -lboost_thread -lboost_program_options
       Cflags: -isystem ${includedir}
       ```  
    3. Finally, because of a problem when compiling Boost under C++20 (which PiTrac uses), add:  
       ```cpp
       #include <utility>
       ```  
       as the last include before the line that says `namespace boost` in the `awaitable.hpp` file at `/usr/include/boost/asio/awaitable.hpp`:  
       ```bash
       sudo vi /usr/include/boost/asio/awaitable.hpp
       ```  
       *(This is a hack, but works for now.)*


#### Build and Install LGPIO

16. **Install and build lgpio** (this is a library to work with the GPIO pins of the Pi)  
    1. `cd ~/Dev`  
    2. `wget http://abyz.me.uk/lg/lg.zip`  
    3. `unzip lg.zip`  
    4. `cd lg`  
    5. `make`  
    6. `sudo make install`  
    7. Create `/usr/lib/pkgconfig/lgpio.pc` containing the following:  
       ```bash
       # Package Information for pkg-config
       prefix=/usr/local
       exec_prefix=${prefix}
       libdir=${exec_prefix}/lib
       includedir=${prefix}/include/
       Name: lgpio
       Description: Open Source GPIO library
       Version: 1.0.0
       Libs: ${exec_prefix}/lib/liblgpio.so
       Cflags: -I${includedir}
       ```  
    8. Enable the SPI pins on the Pi:  
       1. `sudo raspi-config`  
       2. Select **3 Interface Option**  
       3. Select **14 SPI Enable/Disable**  
       4. Select **Yes** on the next screen  
       5. Finish  

#### Build and Install Libcamera

17. **Install and build libcamera** (for C++ camera control)  
    1. Install prerequisites:  
       - `sudo apt-get install -y libevent-dev`  
       - `sudo apt install -y pybind11-dev`  
       - `sudo apt -y install doxygen`  
       - `sudo apt install -y python3-graphviz`  
       - `sudo apt install -y python3-sphinx`  
       - `sudo apt install -y python3-yaml python3-ply`  
       - `sudo apt install -y libavdevice-dev`  
       - `sudo apt install -y qtbase5-dev libqt5core5a libqt5gui5 libqt5widgets5`  
    2. **NEW – March 7 2024** – Use [Raspberry Pi Documentation](https://www.raspberrypi.com/documentation/computers/camera_software.html#building-libcamera-and-rpicam-apps)  
       - **BUT:**  
         1. Perform the `git clone` at `~/Dev` where we’ve been building the other software.  
         2. Do **not** install Boost dev as a prerequisite (we built it already above).  
         3. When done (after the install step), run `sudo ldconfig` to refresh the shared libraries.  
         4. On the Pi 4 (if it has less than 6 GB memory), add `-j 2` at the end of the `ninja -C build` command to limit memory usage. Example:  
            ```bash
            ninja -C build -j 2
            ```  
            - On low-memory Pis, if you run out of memory, the computer may freeze and require a hard reboot.  
         5. Libcamera can be very verbose for higher logging levels if you are using an early version. We suggest disabling excessive logging by editing the file:  
            ```
            libcamera/subprojects/libpisp/src/libpisp/common/logging.hpp
            ```  
            Replace its contents with:  
            ```cpp
            #pragma once

            #include <cassert>

            #ifndef PISP_LOGGING_ENABLE
            #define PISP_LOGGING_ENABLE 0
            #endif

            #if PISP_LOGGING_ENABLE
            #define PISP_LOG(sev, stuff) do { } while(0)
            #else
            #define PISP_LOG(sev, stuff) do { } while(0)
            #endif

            #define PISP_ASSERT(x) assert(x)

            namespace libpisp
            {
                void logging_init();
            }

            #ifdef PISP_LOG
            #undef PISP_LOG
            #endif

            #define PISP_LOG(sev, stuff) do { } while(0)
            ```  
            If the file does not exist, ignore this step.  
    3. If the build fails at the last install step, see [Meson Issue #7345](https://github.com/mesonbuild/meson/issues/7345) for a possible solution:  
       - Export the following environment variable and rebuild:  
         ```bash
         export PKEXEC_UID=99999
         cd build && sudo ninja install
         ```  

#### Build RPICAM-Apps

18. **Build rpicam-apps**  
    1. See: [Raspberry Pi Documentation](https://www.raspberrypi.com/documentation/computers/camera_software.html#building-libcamera-and-rpicam-apps) with these changes:  
       - Add `-Denable_opencv=enabled` to the Meson build step (we installed OpenCV and want to use OpenCV-based post-processing).  
       - We don’t need to re-install most of the prerequisites listed on the Pi website. Just install:  
         ```bash
         sudo apt install -y libexif-dev
         ```  
    2. Specifically, run:  
       ```bash
       cd ~/Dev
       git clone https://github.com/raspberrypi/rpicam-apps.git
       cd rpicam-apps
       meson setup build -Denable_libav=enabled -Denable_drm=enabled -Denable_egl=enabled -Denable_qt=enabled -Denable_opencv=enabled -Denable_tflite=disabled -Denable_hailo=disabled
       meson compile -C build
       sudo meson install -C build
       sudo ldconfig   # only necessary on the first build
       ```  


#### Install Java OpenJDK

19. Install recent java (for activeMQ)  
    1. `sudo apt install openjdk-17-jdk openjdk-17-jre`

#### Install MsgPack

20. Install msgpack  
    1. Info at:  [https://github.com/msgpack/msgpack-c/wiki/v1\_1\_cpp\_packer\#sbuffer](https://github.com/msgpack/msgpack-c/wiki/v1_1_cpp_packer#sbuffer)  
    2. cd \~/Dev  
    3. git clone [https://github.com/msgpack/msgpack-c.git](https://github.com/msgpack/msgpack-c.git)  
    4. For some reason, the above does not grab all the necessary files. So, also go here: [https://github.com/msgpack/msgpack-c/tree/cpp\_master](https://github.com/msgpack/msgpack-c/tree/cpp_master) and click on “Code” and down load the zip file into the \~/Dev directory  
    5. unzip /mnt/PiTracShare/dev/tmp/msgpack-c-cpp\_master.zip  
    6. `cd msgpack-c-cpp_master`  
    7. `cmake -DMSGPACK_CXX20=ON .`  
    8. `sudo cmake --build . --target install`  
    9. `sudo /sbin/ldconfig`

#### Install ActiveMQ C++ CMS

21. Install ActiveMQ C++ CMS messaging system (on both Pi’s)  
    1. This code allows PiTrac to talk to the ActiveMQ message broker  
    2. Pre-requisites:  
       1. `sudo apt -y install libtool`  
       2. `sudo apt-get -y install libssl-dev`  
       3. `sudo apt-get -y install libapr1-dev`  
       4. `sudo apt install -y libcppunit-dev`  
       5. `sudo apt-get install -y autoconf`  
    3. Download and unzip [activemq-cpp-library-3.9.5-src.tar.gz](http://www.apache.org/dyn/closer.lua/activemq/activemq-cpp/3.9.5/activemq-cpp-library-3.9.5-src.tar.gz)  
       1. This version is a little old, but we’re not aware of a newer one  
       2. May also able to do:  
          1. git clone [https://gitbox.apache.org/repos/asf/activemq-cpp.git](https://gitbox.apache.org/repos/asf/activemq-cpp.git) if the available version is new enough (3.9.5 or later)  
       3. ([https://activemq.apache.org/components/cms/developers/building](https://activemq.apache.org/components/cms/developers/building) has more information on the installation process, if necessary)  
    2. `cd ~/Dev`  
    3. `gunzip /mnt/PiTracShare/tmp/activemq-cpp-library-3.9.5-src.tar.gz` (or wherever you put the .gz zip file)  
    4. `cd activemq-cpp-library-3.9.5`  
    5. `./autogen.sh`  
    6. `./configure`  
    7. `make`  
    8. `sudo make install`

#### Install ActiveMQ Broker

22. Install ActiveMQ Broker (need only do on the Pi 2 system, as it is the only system that will be running the broker ?)  
    1. We will install the binary (non-source code version)  
    2. Get Apache Pre-Reqs (most should already have been installed)  
       1. `sudo apt -y install libapr1-dev`  
       2. `sudo apt -y install libcppunit-dev`  
       3. `sudo apt -y install doxygen`  
       4. `sudo apt -y install e2fsprogs`  
       5. `sudo apt -y install maven`  
    3. [https://activemq.apache.org/components/classic/download/](https://activemq.apache.org/components/classic/download/) has the source code zip file that you will want to download with the ActiveMQ Broker  
       1. E.g., [apache-activemq-6.1.4-bin.tar.gz](https://www.apache.org/dyn/closer.cgi?filename=/activemq/6.1.4/apache-activemq-6.1.4-bin.tar.gz&action=download)  
    2. Follow these instructions to install (but NOT the source-install option - we're just installing the executables, not building them):  
       1. [https://activemq.apache.org/version-5-getting-started.html\#installation-procedure-for-unix](https://activemq.apache.org/version-5-getting-started.html#installation-procedure-for-unix)  
       2. Set the following environment variable to ensure you don’t run out of memory:  
          1. `export MAVEN_OPTS=-Xmx1024M` (ignore for RaspberryPi 5 8GB)
       3. We suggest you install activemq at `/opt`, so…  
          1. `cd /opt`  
          2. `sudo tar xvf /mnt/PiTracShare/tmp/apache\*.tar`  (or wherever you put the tarball file  
       2. Test it manually once, and then we’ll start it automatically later:  
          1. `cd /opt/apache-activemq` 
          2. `sudo ./bin/activemq start`   (NOTE \- must start in main directory to ensure that the files like logs get created in the correct place)  
          3. Wait a half-minute and then check the data/activemq.log file to make sure everything is good  
          4. `netstat -an|grep 61616` should then return “LISTEN”  
          5. `sudo ./bin/activemq stop`  
       2. Setup for remote access  
          1. `cd conf`  
          2. `sudo cp jetty.xml jetty.xml.ORIGINAL`  
          3. `sudo vi jetty.xml jetty.xml`  
             1. Search for the line that has `127.0.0.1` and replace with whatever the IP address is for the Pi this is all  running on.  
             2. Search for the line that begins with “ Enable this connector if you wish to use https with web console”  
             3. Uncomment the next section by removing the \!-- and -- at the beginning and end of the bean.  The section should then look like  \<bean id="Secure blah blah blah, and then at the end, \</bean\>  
          4. `cd .. && sudo ./bin/activemq start`  
          5. Log into the broker console from another machine by: http://\<Pi IP address or name\>:8161/admin  
             1.The default login is typically admin/admin
             2. If this works, the broker is setup correctly  
       2. Setup ActiveMQ to run automatically on startup  
          1. `sudo vi /etc/systemd/system/activemq.service` and add:
            ```bash 
            # Verify your version in the directory path name
            [Unit]  
            Description=ActiveMQ  
            After=network.target  
            [Service]  
            User=root  
            Type=forking  
            Restart=on-failure
            ExecStart=/opt/apache-activemq/bin/activemq start  
            #ExecStop=/opt/apache-activemq/bin/activemq stop  
            KillSignal=SIGCONT  
            [Install]  
            WantedBy=multi-user.target
            ```
          2. `sudo systemctl daemon-reload`  
          3. `sudo systemctl start activemq`  
          4. `sudo systemctl enable activemq`  
          5. `sudo reboot now`   (to test the auto-start) (use whatewver apache directory you chose, of course) 
          6. After the system comes back, do the following to verify it’s working:  
             1. `sudo /opt/apache-activemq-6.1.4/bin/activemq status`  (should say it’s running)  
             2. And from a browser, check 

#### Install Maven

23. Install maven for building servlets on Tomcat/Tomee  
    1. `sudo apt -y install maven`

#### Install Tomee

24. Install Tomee (on the cam2 system only)  
    1. Use the “Plume” version that supports JMS  
    2. Get the Tomee binary here:  [https://tomee.apache.org/download.html](https://tomee.apache.org/download.html)  
    3. cd /opt  
    4. sudo unzip /mnt/PiTracShare/tmp/apache-tomee-10.0.0-M3-plume.zip  (or whatever version you’re using)  
    5. sudo mv apache-tomee-plume-10.0.0-M3 tomee      \[or whatever version\]  
    6. `sudo chmod -R 755 tomee`  
       1. **WARNING** \- Only use this technique if you’re on a secure, private platform.  It’s easier to simply allow read-writes from other than the root user, but there’s other (better) ways of doing this too.  This is just a simple hack for a home system.  
    7. `cd tomee`
    8. `sudo chmod -R go+w webapps` (so that the tomcat uses can deploy webapps  
    9. `sudo vi conf/tomcat-users.xml` and add before the last line (\</tomcat-users\>)  
      ```xml
      <role rolename="tomcat"/>
      <role rolename="admin-gui"/>
      <role rolename="manager-gui"/>
      <user username="tomcat" password="tomcat" roles="tomcat,admin-gui,manager-gui"/>
      ``` 
      10. Add a systemctl daemon script to /etc/systemd/system/tomee.service so that tomee will start on boot. `sudo vi /etc/systemd/system/tomee.service
```bash
[Unit]  
Description=Apache TomEE  
After=network.target  
[Service]  
User=root  
Type=forking  
#Environment=JAVA_HOME=/usr/lib/jvm/default-java  
Environment=JAVA_HOME=/usr/lib/jvm/java-1.17.0-openjdk-arm64  
Environment=CATALINA_PID=/opt/tomee/temp/tomee.pid  
Environment=CATALINA_HOME=/opt/tomee  
Environment=CATALINA_BASE=/opt/tomee  
Environment=CATALINA_OPTS='-server'  
Environment=JAVA_OPTS='-Djava.awt.headless=true'  
ExecStart=/opt/tomee/bin/startup.sh  
ExecStop=/opt/tomee/bin/shutdown.sh  
KillSignal=SIGCONT  
[Install]  
WantedBy=multi-user.target
```

11. `cd webapps/manager/META-INF; sudo cp context.xml context.xml.ORIGINAL` [just in case]  

12. Update /opt/tomee/webapps/manager/META-INF/context.xml to allow ".*" instead of just 127.0…. Replace the whole regex string. The result should simply be allow=".*" on that line  
```xml
<Context antiResourceLocking="false" privileged="true" >
  <CookieProcessor className="org.apache.tomcat.util.http.Rfc6265CookieProcessor"
                   sameSiteCookies="strict" />
  <Valve className="org.apache.catalina.valves.RemoteAddrValve"
         allow=".*" />
  <Manager sessionAttributeValueClassNameFilter="java\.lang\.(?:Boolean|Integer|Long|Number|String)|org\.apache\.catalina\.filters\.CsrfPreventionFilter\$LruCache(?:\$1)?|java\.util\.(?:Linked)?HashMap"/>
</Context>
```

13. Disable local host access logging. Otherwise, it will fill up that log quickly. To do so, comment out the following section in /opt/tomee/conf/server.xml:  
```xml
<Valve className="org.apache.catalina.valves.AccessLogValve" directory="logs" prefix="localhost_access_log" suffix=".txt" pattern="%h %l %u %t &quot;%r&quot; %s %b" />
```

14. Comment out this section by adding <!-- to the beginning of the section and --> to the end of the section.  
15. For more details, see https://help.harmanpro.com/disabling-local-host-access-logs-in-tomcat.  

16. Add a new document base/root to allow access to the shared mounted drive:  
    1. Edit `/opt/tomee/conf/server.xml` and just before the `</Host>` near the end of the file, insert the following, with the <PiTracUserName> replaced with the name you use:  
    2. `<Context docBase="/home/<PiTracUserName>/LM_Shares/WebShare" path="/golfsim/WebShare" />`  
    3. This will allow the Tomee system to access a directory that is outside of the main Tomee installation tree.  
    4. NOTE - if the shared directory that is mounted off of the other Pi does not exist, Tomee may not be able to start  

    Example section in server.xml:  
```xml
      <Host name="localhost"  appBase="webapps"
            unpackWARs="true" autoDeploy="true">

        <!-- SingleSignOn valve, share authentication between web applications -->
        <!--
        <Valve className="org.apache.catalina.authenticator.SingleSignOn" />
        -->

        <!-- Access log processes all example. -->
        <!-- <Valve className="org.apache.catalina.valves.AccessLogValve" directory="logs"
               prefix="localhost_access_log" suffix=".txt"
               pattern="%h %l %u %t &quot;%r&quot; %s %b" />  -->

        <Context docBase="/home/pitrac/LM_Shares/WebShare" path="/golfsim/WebShare" />

      </Host>
    </Engine>
  </Service>
</Server>
```

17. Allow symbolic linking. In conf/context.xml, add before the end:  
    1. `<Resources allowLinking="true" />`  

Example context.xml:  
```xml
<!-- The contents of this file will be loaded for each web application -->
<Context>

    <!-- Default set of monitored resources. If one of these changes, the web application will be reloaded. -->
    <WatchedResource>WEB-INF/web.xml</WatchedResource>
    <WatchedResource>WEB-INF/tomcat-web.xml</WatchedResource>
    <WatchedResource>${catalina.base}/conf/web.xml</WatchedResource>

    <!-- Uncomment this to enable session persistence across Tomcat restarts -->
    <!--
    <Manager pathname="SESSIONS.ser" />
    -->

    <Resources allowLinking="true" />

</Context>
```

18. Install the systemctl service we just created and start it:  
    1. `sudo systemctl daemon-reload`  
    2. `sudo systemctl enable tomee`  
    3. `sudo systemctl start tomee`  
    4. `sudo systemctl status tomee.service`  
    5. Check startup logs with: `sudo tail -f /opt/tomee/logs/catalina.out`  
    6. Login from a web console: `http://<Pi-with-Tomee>:8080/manager/html` (default user/pass: tomcat/tomcat — change if not in a private network).  

19. <font color=#ff0000>Warning:</font> On Mac Chrome may not allow you to connect from outside the hosted Raspberry Pi. Safari should work.

#### Install Launch Monitor Dependencies

20. Install other Launch Monitor dependencies:  
    - Formatting library because the currently-packaged gcc12.2 in Debian Unix doesn’t have the C++20 format capability yet.  
      - `sudo apt -y install libfmt-dev`


#### Build Launch Monitor

26. **Build the PiTrac Launch Monitor!**  
    1. Download the PiTrac repository, including the source code under the “Software” subdirectory if you haven’t already  
       1. We usually use a subdirectory called ~/Dev under the home directory of the PiTrac user to house any cloned repositories such as PiTrac.    
       2. You can do the copy by going to github and downloading the .zip  file, or from the command-line on the Pi using something like:  
          1. `cd ~/Dev`  
          2. `git clone https://github.com/jamespilgrim/PiTrac.git`  
       3. NOTE - If you do you plan to do any code changes, you may want to create a fork from the main repository and then clone that into your Pi.  
    2. Install Remaining Prerequisites and Setup Environment:  
       1. Setup the `PITRAC_ROOT` and other environment variables.  For example set PITRAC_ROOT to point to the “Software/LMSourceCode” directory of the PiTrac build.  That is one directory “up” from the “ImageProcessing” directory that contains the main PiTrac meson.build file. The other environment variables listed below (with example values) should be set according to your network and environment. 
          1. E.g., include in your .zshrc or .bashrc (or whatever shell you use) the following, with the camera types set to your equipment type:  
```
export PITRAC_ROOT=/Dev/PiTrac/Software/LMSourceCode  
export PITRAC_BASE_IMAGE_LOGGING_DIR=~/LM_Shares/Images/
export PITRAC_WEBSERVER_SHARE_DIR=~/LM_Shares/WebShare/
export PITRAC_MSG_BROKER_FULL_ADDRESS=tcp://10.0.0.41:61616
# Only uncomment and set the following if connecting to the
# respective golf sim (e.g., E6/TruGolf, GSPro, etc.)
#export PITRAC_E6_HOST_ADDRESS=10.0.0.29
#export PITRAC_GSPRO_HOST_ADDRESS=10.0.0.29

# Tell any rpicam apps where to get their configuration info (including, e.g., any timeout settings)
export LIBCAMERA_RPI_CONFIG_FILE=/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml

# For Single-Pi configurations (most) specify that both cameras are Official Pi GS cameras with 6mm lenses
export PITRAC_SLOT1_CAMERA_TYPE=4
export PITRAC_SLOT2_CAMERA_TYPE=4

# Specify that both cameras are (for now) Innomaker GS cameras with 3.6mm lenses
#export PITRAC_SLOT1_CAMERA_TYPE=6
#export PITRAC_SLOT2_CAMERA_TYPE=6
```

       2. sudo apt-get -y install libraspberrypi-dev raspberrypi-kernel-headers
       3. Add extended timeout to `rpi_apps.yaml` file so that even if an external trigger doesn’t fire for a really long time, the libcamera library won’t time-out:  
          1. (**NOTE** for Pi 5, use `/usr/share/libcamera/pipeline/rpi/pisp` instead of `/usr/share/libcamera/pipeline/rpi/vc4`, below)  
```
          2. `cd  /usr/share/libcamera/pipeline/rpi/vc4`  
          3. `sudo cp  rpi_apps.yaml  rpi_apps.yaml.ORIGINAL`
```
          4. In both `/usr/local/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml` and `usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml`, (to the extent they exist) at the end of the pipeline section, add the following (including the last comma!)  
             ``` bash
             "camera_timeout_value_ms": 1000000,
             ```  
             NOTE - For the Pi 5, you may only have an "example.yaml" file in the above directories.  If so, just copy it to rpi_apps.yaml.
       2. Get the latest `imx296_noir.json` into `/usr/share/libcamera/ipa/rpi/pisp or ...rpt/vc4` (located inside ImageProcessing folder)
          1. For the Pi 4:  
             1. `sudo cp imx296_noir.json.PI_4_FOR_VC4_DIRECTORY /usr/share/libcamera/ipa/rpi/vc4/imx296_noir.json`  
          2. For the Pi 5:  
             1. `sudo cp imx296_noir.json.PI_5_FOR_PISP_DIRECTORY /usr/share/libcamera/ipa/rpi/pisp/imx296_noir.json`  
    2. Go to the directory called ImageProcessing below whatever `PITRAC_ROOT` directory path you will be using to compile.  E.g.,   
       1. `cd $PITRAC_ROOT/ImageProcessing`  
    3. `chmod +x create_closed_source_objects.sh`  
    4. `meson setup build`  
       1. If there are any missing libraries, ensure that the pre-requisites were all successfully built and installed and that any corresponding pkgconfig files were created correctly per the steps above.  
    5. `ninja -C build`       (add -j 2 if compiling in 4GB or less)  
    6. If the build completes successfully, try a quick sanity check:  
       1. `build/pitrac_lm --help`    
       2. The app should return the available command-line options

#### Setup PiTrac GUI

26. First, make sure you've setup the required environment variables (especially PITRAC_ROOT and PITRAC_WEBSERVER_SHARE_DIR) and the directory values in the golf_sim_config.json file.  
27. Setup the PiTrac-specific code package for the PiTrac GUI on the Tomee server  
    1. Log into the Pi 2 computer where the Tomee instance is running and make sure that $PITRAC_ROOT and other PITRAC_xxxx environment variables are set correctly.
    2. Make sure Tomee is running:  
       1. `sudo systemctl status tomee`  
    3. `cd ~/Dev`  
    4. `mkdir WebAppDev`  
    5. `cd WebAppDev`
    6. `cp $PITRAC_ROOT/ImageProcessing/golfsim_tomee_webapp/refresh_from_dev.sh .`  
    7. If necessary, create the refresh file yourself.
    2. Run that refresh script.   
       1. `chmod 755 refresh_from_dev.sh;./refresh_from_dev.sh`  
    3. Tell the MonitorServlet where to find its configuration file  
    2. Create the “.war” package for Tomee  
       1. `mvn package`  
       3. Move golfsim.war to Tomee webapps and deploy.  
       10. Confirm you can see the PiTrac GUI at:
           `http://<Pi-2-IP>:8080/golfsim/monitor?...`  

**CONGRATULATIONS!** - At this point, you've built the PiTrac software.


### Nice-To-Haves

**Nice-to-Haves for an easier-to-use development environment**

2. The following steps are only for someone who’s a little new to linux and doesn’t already have a development environment setup the way they like it.  The following are just a few tools that (for the authors of the PiTrac project) seem to make things a little more efficient.  This setup deals with things like easy command-recall, file and command completion,making vi a little more like Visual Studio (for better or worse!), etc.  
3. Z-shell and OhMyZa  
   1. Connect to your raspberry Pi with SSH  
   2. Install zsh :  
      1. sudo apt-get update && sudo apt-get install zsh  
      2. Edit your passwd configuration file to tell which shell to use for user pi :  
         1. sudo vi /etc/passwd and change /bin/bash to /bin/zsh for the <PiTracUserName> (usually the last line)  
         2. **WARNING:** Double-check the line in passwd is correct - you won’t be able to ssh back in if not!  
         3. logout  
   2. Reconnect to your raspberry, and  
      1. If on login Zsh asks about the .z files, select 0 to create an empty .zshrc  
      2. check that zsh is the shell with echo $0.  
   2. Install OhMyZsh :  
      1. sh -c "$(wget https://raw.githubusercontent.com/robbyrussell/oh-my-zsh/master/tools/install.sh -O -)"  
   2. Disconnect from your instance and reconnect/re-login to it.  
   3. Turn off  any zsh processing for any large git directories (otherwise,  it will make “cd” into a git directory freeze)  
      1. cd <whatever directory you plan to build PiTrac in>  
      2. git config --add oh-my-zsh.hide-status 1  
      3. git config --add oh-my-zsh.hide-dirty 1  
      4. NOTE - you may have to do this later if you don’t have the directory picked out yet  
2. Install neovim  
   1. Install NeoVim:  
      1. sudo apt-get install neovim  
         2. sudo apt-get install python3-neovim  
   2. Install vundle into NVIM, (and not vim, as many online instructions assume!):  
      1. git clone https://github.com/VundleVim/Vundle.vim.git ~/.config/nvim/bundle/Vundle.vim  
      2. Configure neovim - Vundle does not work perfectly with nvim unless you make some changes  
         1. Vi ~/.config/nvim/bundle/Vundle.vim/autoload/vundle.vim and change  
         2. Change $HOME/.vim/bundle (should be near the end of the file) to $HOME/.config/nvim/bundle  
      3. Note these comments:  
         1. https://gist.github.com/lujiacn/520e3e8abfd1c1b39c30399222766ee8  
         2. https://superuser.com/questions/1405420/i-really-need-help-installing-vundle-for-neovim  
      2. Create the file /home/<PiTracUserName>/.config/nvim/init.vim and add:  
         1. set nocompatible              " be iMproved, required  
         2. filetype off                  " required  
         3. " set the runtime path to include Vundle and initialize  
         4. set rtp+=~/.config/nvim/bundle/Vundle.vim  
         5. call vundle#begin()            " required  
         6. Plugin 'VundleVim/Vundle.vim'  " required  
         7. " ===================  
         8. " my plugins here  
         9. " ===================  
         10. Plugin 'scrooloose/nerdtree'  
         11. Plugin 'valloric/youcompleteme'  
         12. " Plugin 'dracula/vim'  
         13. " ===================  
         14. " end of plugins  
         15. " ===================  
         16. call vundle#end()               " required  
         17. filetype plugin indent on       " required  
      2. Add any other plugins you want.  The above example establishes these two  
         1. Plugin 'scrooloose/nerdtree'  
         2. Plugin 'valloric/youcompleteme'  
      2. Run vim and type :PluginInstall     in order to install the plug ins.  It will take a few moments to process.  
      3. (Ignore anything on the net that refers to .vimrc - that’s not applicable if using nvim.

Example .zshrc file – The highlighted bits at the end should be the only thing you need to add:  

```zsh
# If you come from bash you might have to change your $PATH.
export PATH=.:/$HOME/bin:/usr/local/bin:$PATH

# Path to your oh-my-zsh installation.
export ZSH="$HOME/.oh-my-zsh"

# Set name of the theme to load --- if set to "random", it will
# load a random theme each time oh-my-zsh is loaded, in which case,
# to know which specific one was loaded, run: echo $RANDOM_THEME
# See https://github.com/ohmyzsh/ohmyzsh/wiki/Themes
ZSH_THEME="robbyrussell"

# Set list of themes to pick from when loading at random
# Setting this variable when ZSH_THEME=random will cause zsh to load
# a theme from this variable instead of looking in $ZSH/themes/
# If set to an empty array, this variable will have no effect.
# ZSH_THEME_RANDOM_CANDIDATES=( "robbyrussell" "agnoster" )

# Uncomment the following line to use case-sensitive completion.
# CASE_SENSITIVE="true"

# Uncomment the following line to use hyphen-insensitive completion.
# Case-sensitive completion must be off. _ and - will be interchangeable.
# HYPHEN_INSENSITIVE="true"

# Uncomment one of the following lines to change the auto-update behavior
# zstyle ':omz:update' mode disabled  # disable automatic updates
# zstyle ':omz:update' mode auto      # update automatically without asking
# zstyle ':omz:update' mode reminder  # just remind me to update when it's time

# Uncomment the following line to change how often to auto-update (in days).
# zstyle ':omz:update' frequency 13

# Uncomment the following line if pasting URLs and other text is messed up.
# DISABLE_MAGIC_FUNCTIONS="true"

# Uncomment the following line to disable colors in ls.
# DISABLE_LS_COLORS="true"

# Uncomment the following line to disable auto-setting terminal title.
# DISABLE_AUTO_TITLE="true"

# Uncomment the following line to enable command auto-correction.
# ENABLE_CORRECTION="true"

# Uncomment the following line to display red dots whilst waiting for completion.
# You can also set it to another string to have that shown instead of the default red dots.
# e.g. COMPLETION_WAITING_DOTS="%F{yellow}waiting...%f"
# Caution: this setting can cause issues with multiline prompts in zsh < 5.7.1 (see #5765)
# COMPLETION_WAITING_DOTS="true"

# Uncomment the following line if you want to disable marking untracked files
# under VCS as dirty. This makes repository status check for large repositories
# much, much faster.
# DISABLE_UNTRACKED_FILES_DIRTY="true"

# Uncomment the following line if you want to change the command execution time
# stamp shown in the history command output.
# You can set one of the optional three formats:
# "mm/dd/yyyy"|"dd.mm.yyyy"|"yyyy-mm-dd"
# or set a custom format using the strftime function format specifications,
# see 'man strftime' for details.
# HIST_STAMPS="mm/dd/yyyy"

# Would you like to use another custom folder than $ZSH/custom?
# ZSH_CUSTOM=/path/to/new-custom-folder

# Which plugins would you like to load?
# Standard plugins can be found in $ZSH/plugins/
# Custom plugins may be added to $ZSH_CUSTOM/plugins/
# Example format: plugins=(rails git textmate ruby lighthouse)
# Add wisely, as too many plugins slow down shell startup.
plugins=(git)

source $ZSH/oh-my-zsh.sh

# User configuration

# export MANPATH="/usr/local/man:$MANPATH"

# You may need to manually set your language environment
# export LANG=en_US.UTF-8

# Preferred editor for local and remote sessions
# if [[ -n $SSH_CONNECTION ]]; then
#   export EDITOR='vim'
# else
#   export EDITOR='mvim'
# fi

# Compilation flags
# export ARCHFLAGS="-arch x86_64"

# Set personal aliases, overriding those provided by oh-my-zsh libs,
# plugins, and themes. Aliases can be placed here, though oh-my-zsh
# users are encouraged to define aliases within the ZSH_CUSTOM folder.
# For a full list of active aliases, run `alias`.
#
# Example aliases
# alias zshconfig="mate ~/.zshrc"
# alias ohmyzsh="mate ~/.oh-my-zsh"
#

alias cdlm='cd  /mnt/PiTracShare/GolfSim/LM/ImageProcessing'
alias cdlml='cd  ~/Dev/LaunchMonitor/ImageProcessing'
alias cdlc='cd  /mnt/PiTracShare/GolfSim/LibCamTest'
alias cdserv='cd /usr/share/tomcat9-examples/examples/WEB-INF/classes/async'
alias ll='ls -al'
function findtext() {
grep -rni "$1" .
}

# Some useful navigation aliases

alias pushdd="pushd \$PWD > /dev/null"
alias cd='pushdd;cd'
alias ssh='ssh -A'
alias soc='source ~/.bashrc'
#below to go back to a previous directory (or more)
alias popdd='popd >/dev/null'
alias cd.='popdd'
alias cd..='popdd;popdd'
alias cd...='popdd;popdd;popdd'
alias cd....='popdd;popdd;popdd;popdd'
#below to remove directories from the stack only (do not 'cd' anywhere)
alias .cd='popd -n +0'
alias ..cd='popd -n +0;popd -n +0;popd -n +0'

# Enable vi mode
bindkey -v # Enable vi mode
```
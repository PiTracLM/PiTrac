**PiTrac \- Raspberry Pi Setup and Configuration**

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

These instructions start with a Raspberry Pi with nothing on it, and are meant to describe all the steps to get from that point to a working, compiled version of PiTrac.  PiTrac currently requires two Raspberry Pi’s, so the majority of these instructions will have to be repeated twice.  Because the ‘smaller’ Pi system that connects to Camera 2 is the only Pi that handles the Tomcat/Tomee web-based GUI for the system, there are a few more steps for that system.

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

1. Create a secure, static-safe environment to run your Pi’s on.  3D-Printing the two Pi-Side “Floors” from the plans on github is one way to provide this environment, and you’ll need to print them at some point anyway.

#### Operating System

2. Raspian O/S and Pi Initialization  
   a. Start with the Pi powered off (unplugged).  Have a Cat5/6 cable that is connected to your local network plugged in if possible.  
   b. On a PC, connect a Micro SD card via USB to use to boot the Pi for the first time.  
   1. Use a 64GB card so we have room to expand  
      c. Install and run the [RPi Imager utility](https://www.raspberrypi.com/software/)  
      d. Select Pi 4 or 5 for the device depending on what you have, for Operating System choose the 64-bit OS.  Make sure the “Storage” is pointing to the MicroSD card (and not something like your hard-drive\!), as it will be overwritten entirely.  Hit NEXT.  
      e. Answer “EDIT SETTING” when asked if you want to apply customisations  
   2. If you are American, ignore the clearly-incorrect spelling of “customization.” ;)  
   3. In the GENERAL tab,:  
      1. Select a hostname that will easily distinguish between the two Pi’s in the system, such as rsp01, rsp02, etc.  
      2. Add a \<PiTracUser\> username that will be used to compile and run PiTrac, and that can log into your NAS, if you’re using a server (recommended).  E.g., “pitrac” as a user (or just use “pi”)  
         1. This will be the username and password necessary to log into the system for the first time, so double-check you’ve verified both.  
         2. Use the actual user name whenever you see \<PiTracUser\> below  
      3. Make sure the wireless LAN credentials are setup in case you can’t connect a hard line  
   4. In the SERVICES tab,  
      1. Enable SSH and use password authentication  
         f. After setting up the customizations, select YES to “apply OS customisation settings” to the card setup and start the write process.  Should take about 20 minutes.  
         g. Once the SD Card is written and verified, if you have a keyboard, mouse, and monitor, hook those up first.  This can all be done via a remote login, but it’s nice to be able to have a full user setup from the beginning if there are any issues.  
         h. Insert the Micro SD card into the Pi and start up the Pi by plugging in the power (don’t insert or disconnect the SD card when the Pi is on\!)..  
   2. The first bootup takes a while.  Better to be able to monitor it with an attached monitor if possible.

#### Log into Pi

4. Log into the Pi using whatever credentials you expect to use to run PiTrac (the \<PiTracUserName\>  
   a. If running headless, remotely login using putty or a ssh tool of your choice  
   1. Logging in from whatever computer you are reading this setup document on will make it easy to copy-paste from this document into files on the Pi  
   2. For example,  
      1. `putty rsp02 -l \<username\>`    (the boot image should already allow putty)  
         b. If running directly with a monitor and keyboard, click on updates icon near top-right to make sure everything is up to date  
   3. Install everything to get up to date  
      c. Or, equivalently, do the following from the command line:  
   4. `sudo apt -y update`  
   5. `sudo apt -y upgrade`  
   6. `sudo reboot now`      (to make sure everything is updated)

#### Remote Log into Pi

5. Remotely login (to be able to paste from this setup document)  
   a. `putty rsp01 -l \<username\>`    (the boot image should already allow putty)  
   b. Then, follow the instructions below…

#### Sudo Priviledges

6. If necessary, make sure that \<PiTracUserName\> has sudo privileges  
   a. Some guidance [here](https://askubuntu.com/questions/168280/how-do-i-grant-sudo-privileges-to-an-existing-user).

#### Install NVME Board

7. To Install an NVME Board on the Pi  \[Optional, and probably only for the Pi 5 (confusingly referred to as the “Pi 1” computer in the PiTrac project)\]:  
   a. If you have a SSD drive, best to get it up and booting now  
   b. See also the instructions here, which will work in most cases: [https://wiki.geekworm.com/NVMe\_SSD\_boot\_with\_the\_Raspberry\_Pi\_5](https://wiki.geekworm.com/NVMe_SSD_boot_with_the_Raspberry_Pi_5)  
   Although the instructions below should work as well.  
   c. With the Pi off, Install the NVMe Board and NVMe SSD drive per instructions of whatever board you are using.  
   d. Power up and Enable the PCIe interface (your instructions may differ):  
   1. `cd /boot/firmware/`  
   2. `sudo cp config.txt config.txt.ORIGINAL`  
   3. By default the PCIe connector is not enabled.  
   4. To enable it you should add the following option into /boot/firmware/config.txt before the last “\[all\]” at the end of the file and reboot (sudo reboot now):  
      1. \# Enable the PCIe External Connector.  
      2. dtparam=pciex1  
         1. A more memorable alias for pciex1 exists, so you can alternatively add dtparam=nvme to the /boot/firmware/config.txt file.  
            e. After the reboot, we will image the NVMe drive  
   5. First, ***if using a non-HAT+ adapter***, add on first non-commented line of /boot/firmware/config.txt:   PCIE\_PROBE=1  (see instructions for you device)  
   6. Change BOOT\_ORDER to BOOT\_ORDER=0xf416 (to boot off NVM first), OR \- better yet,  
      1. sudo raspi-config  
      2. Go to the Advanced Options /s Boot Order  
      3. Select whatever order you want, usually NVMe card first  
   2. Shutdown, remove power to the Pi, and reboot.  Afterward, an lsblk command should show something like this (see last line):  
        
      1\. pitrac@rsp05:\\\~ $ lsblk    
        
      2\. NAME        MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS    
        
      3\. mmcblk0     179:0    0  29.7G  0 disk    
        
      4\. |-mmcblk0p1 179:1    0   512M  0 part /boot/firmware    
        
      5\. \\\`-mmcblk0p2 179:2    0  29.2G  0 part /    
        
      6\. nvme0n1     259:0    0 238.5G  0 disk    
        
   3. At this point, the NVMe drive should be accessible, and we will make a copy (image) of the bootup Micro SD card onto the SSD drive  
   4. From the Pi Graphical Desktop, Applications \=\>Accessories \=\>SD Card Copier on the main screen, run the SD Card Copier program, and copy the OS to the NVME ssd.  There’s no need to select the separate UUID option.  
      1. If running headless, see the internet for other ways to image the SSD  
   2. Power down, remove power, then remove the SSD card  
   3. When you turn the power on, the Pi should reboot from the SSD drive, and it should be pretty quick\!

#### NAS Drive Setup and Mounting

8. Setup mounting of a remote NAS drive (or similar)  
   a. To use for development so that you can’t lose everything if the Pi has an issue.  Also allows for easier transfers of files to the Pi from another computer.  
   b. The remote drive will store the development environment, though you can obviously set up the PiTrac not to need a separate drive once you have everything working.  However, it’s really a good idea to have the development and test environment on a different computer than on the individual Pi’s.  
   c. There are many ways to automatically mount a removable drive to a Pi.  The following is just one way that assumes you have a NAS with NFS services enabled and with a shareable drive that the Pi can read/write to.  
   1. NOTE:  If this Pi will be anywhere in a public network, obviously do not include your password in the fstab\!  
      d. `sudo mkdir /mnt/PiTracShare`  
      e. `cd /etc`  
      f. `sudo cp fstab fstab.original`  
      g. `sudo chmod 600 /etc/fstab`   \[to try protect any passwords in the file\]  
      h. `sudo vi fstab`  
   2. If using NFS (seems easier):  
      1. \<NAS IP Address\>:/\<NAS Shared Drive Name\> /mnt/PiTracShare nfs \_netdev,auto 0 0  
      2. For example:  
         1. 10.0.0.100:/NAS\_Share\_Drive /mnt/PiTracShare nfs \_netdev,auto 0 0  
   3. If using CIFS:  
      1. Add the following to /etc/fstab after the last non-comment line, replacing PWD and other things in \[\]’s with the real pwd and info  
         1. //\<NAS IP Address\>:/\<NAS Shared Drive Name\> /mnt/PiTracShare cifs username=\[PiTracUserName\],password=\[PWD\],workgroup=WORKGROUP,users,exec,auto,rw,file\_mode=0777,dir\_mode=0777,user\_xattr 0 0  
            i. `sudo systemctl daemon-reload`  
            j. `sudo mount -a`  
      2. If there’s an error, make sure the password is correct  
      3. ls \-al /mnt/PiTracShare    should show any files there

#### Samba Server Setup

9. Setup Samba server (to allow the two Pi’s to share a folder between themselves)  
   a. Need to allow the Pi’s to serve out directories to the other Pi to share information like debugging pictures from one Pi to the other  
   b. See [https://pimylifeup.com/raspberry-pi-samba/](https://pimylifeup.com/raspberry-pi-samba/) for the basics  
   c. We suggest the faster Pi 5 (or whatever will be connected to Camera 1\) be the Pi from which the shared directory is shared.  
   d. For the Pi from which the directory will be shared, something like:  
   1. sudo apt-get install samba samba-common-bin  
   2. sudo systemctl restart smbd  
   3. sudo systemctl status smbd  
      1. Should show “active (running)”  
   2. mount \-t cifs  
   3. Create the directory structure that the two Pis will share (this helps facilitate transfer of debugging images between the two Pis)  
      1. mkdir /home/\<PiTracUser\>/LM\_Shares  
      2. mkdir /home/\<PiTracUser\>/LM\_Shares/GolfSim\_Share  
      3. mkdir /home/\<PiTracUser\>/LM\_Shares/Images  
   2. sudo vi /etc/samba/smb.conf   and add the following lines at the bottom  
      1. \[LM\_Shares\]  
      2. path \= /home/\<PiTracUser\>/LM\_Shares  
      3. writeable=Yes  
      4. create mask=0777  
      5. directory mask=0777  
      6. public=no  
   2. sudo smbpasswd \-a \<PiTracUsername\>  
      1. Enter the same password you used for the PiTracUsername  
   2. sudo systemctl restart smbd  
      e. For the Pi to which the directory will be shared:  
   3. Add the following to /etc/fstab after the last non-comment line, replacing PWD and other things in \[\]’s with the real pwd and info for the PiTracUserName and password  
      1. //\<Pi 1’s IP Address\>/LM\_Shares /home/\<PiTracUser\>/LM\_Shares cifs username=\[PiTracUserName\],password=\[PWD\],workgroup=WORKGROUP,users,exec,auto,rw,file\_mode=0777,dir\_mode=0777,user\_xattr 0 0  
   2. sudo systemctl daemon-reload  
   3. sudo mount \-a  
   4. Check to make sure the second Pi can “see” the other Pi’s LM\_Shares sub-directories (Images and GolfSim\_Share)

#### SSH Stored Key

10. Setup ssh to use a stored key make it easier to login securely and quickly (w/o a pwd) \[optional, but really useful to avoid having to type a password every time\]  
    1. **WARNING \-** This step assumes your PiTrac is secure in your own network and that the machine you use to log in is not used by others (given that this helps automate remote logins)  
    2. If not already, remotely log into the Pi from the machine where you’re reading this document  
    3. Create an ssh file  
       1. install \-d \-m 700 \~/.ssh  
    4. Install putty on the remote (non-Pi Mac/PC) machine that you’ll use to log in  
    5. Use the puttygen utility to generate a public key.  This is just a long text string or two  
    6. vi \~/.ssh/authorized\_keys and paste in the public key for putty  
    7. (or can just use the mount to get a copy of the file from another Pi)  
    8. The key would have been generated using puttygen  
    9. The file should simply have each key (no spaces\!)  preceded on the same line with “ssh-rsa ”  
    10. sudo chmod 644 \~/.ssh/authorized\_keys

#### Additional Setup

11. If you don’t already have your development world setup the way you want it, we suggest trying now some of the environments/tools at the bottom of these instructions labeled “[**Nice-to-Haves for an easy-to-use development environment**](#nice-to-haves)”

#### Git and Github

12. Git and GitHub  
    1. If the project will be hosted on a shared drive, and you 100% control of that drive and it’s not public, then let github know that we’re all family here.  On the pi and on whatever computer you log in from, do::  
       1. git config \--global \--add safe.directory "\*"  
       2. Otherwise, Git desktop and Visual Studio often have problems

#### Clock Configuration

13. Configure the clock to not vary (as our timing is based on it\!)  
    1. cd /boot/firmware  
    2. sudo cp config.txt config.txt.ORIGINAL  
    3. sudo vi config.txt:  
    4. For Pi 4 & 5:  
       1. Set force\_turbo=1   in /boot/firmware/config.txt  
       2. E.g.,  
       3. \# PiTrac MOD \-  force fast clock even at low load  
       4. force\_turbo=1  
    5. For Pi 5 also add  
       1. arm\_boost=1 in /boot/firmware/config.txt

#### Build and Install OpenCV

14. Install and build OpenCV \- for both python and C++  
    a. Latest version of OpenCV as of this writing (late 2024\) is 4.10  
    b. See e.g., [https://itslinuxfoss.com/install-opencv-debian/](https://itslinuxfoss.com/install-opencv-debian/) for more information on installing  
    c. If necessary, increase swap space to have around 6 Gig of usable space.  For a 4 Gig or larger Pi, you can skip this step and just go to compiling  
    1. See [https://qengineering.eu/install-opencv-4.5-on-raspberry-64-os.html](https://qengineering.eu/install-opencv-4.5-on-raspberry-64-os.html)  
       1. \# enlarge the boundary (CONF\_MAXSWAP)  
       2. $ sudo nano /sbin/dphys-swapfile  
       3. \# give the required memory size (CONF\_SWAPSIZE)  
       4. $ sudo nano /etc/dphys-swapfile  
       5. \# reboot afterwards  
       6. $ sudo reboot  
    2. See also [https://docs.opencv.org/3.4/d7/d9f/tutorial\\\_linux\\\_install.html](https://docs.opencv.org/3.4/d7/d9f/tutorial\\_linux\\_install.html)  
       d. Compile OpenCV  
    3. mkdir \~/Dev  
    4. cd Dev    (this is where we will compile the packages PiTrac needs)  
    5. See [https://qengineering.eu/install-opencv-4.5-on-raspberry-64-os.html](https://qengineering.eu/install-opencv-4.5-on-raspberry-64-os.html)  
    6. You can use the fully-automated script from the above webpage, though you might learn more if you follow the steps in the guide (which mirror the script).  
       1. The script is named OpenCV-4-10-0.sh and is available as described in the above URL.  
       2. In the OpenCV-4-10-0.sh script, it’s useful for testing to change the following before running:  
          1. INSTALL\_C\_EXAMPLES=ON  
          2. INSTALL\_PYTHON\_EXAMPLES=ON  
       3. In addition, if your Pi only has 4 GB or less, change the “-j4” to “-j 2” to prevent the compile process from consuming all the memory.  
       4. Run the script and review the output to make sure there were no errors.  The script takes quite a while to run on some Pi’s.  
    2. Ensure the script runs the sudo make install step at the end after the script runs

#### Install Boost

15. Install Boost (a set of utilities that PiTrac uses)  
    1. Install the current version of the boost development environment  
       1. sudo apt-get install libboost1.74-all  
    2. Create a boost.pc file to tell meson how to find boost files when PiTrac is compiled  
       1. sudo vi  /usr/share/pkgconfig/boost.pc    and in it place:  
          1. \# Package Information for pkg-config  
          2. \# Path to where Boost is installed  
          3. prefix=/usr  
          4. \# Path to where libraries are  
          5. libdir=${prefix}/lib  
          6. \# Path to where include files are  
          7. includedir=${prefix}/boost  
          8. Name: Boost  
          9. Description: Boost provides free peer-reviewed portable C++ source libraries  
          10. Version: 1.74.0   \# ← OR WHATEVER VERSION YOU DOWNLOAD  
          11. Libs: \-L${libdir} \-lboost\_filesystem \-lboost\_system \-lboost\_timer \-lboost\_log \-lboost\_chrono \-lboost\_regex \-lboost\_thread \-lboost\_program\_options  
          12. Cflags: \-isystem ${includedir}  
    2. Finally, because of a problem when compiling boost under C++20 (which PiTrac uses), add “\#include \<utility\> as the last include before the line that says “name space boost” in the awaitable.hpp file at /usr/include/boost/asio/awaitable.hpp”  
       1. sudo vi /usr/include/boost/asio/awaitable.hpp  
       2. This is a hack, but works for now.

#### Build and Install LGPIO

16. Install and build lgpio (this is a library to work with the GPIO pins of the Pi)  
    1. cd \~/Dev  
    2. wget [http://abyz.me.uk/lg/lg.zip](http://abyz.me.uk/lg/lg.zip)  
    3. unzip lg.zip  
    4. cd lg  
    5. make  
    6. sudo make install  
    7. Create a /usr/lib/pkgconfig/lgpio.pc containing the following:  
       1. \# Package Information for pkg-config  
       2. prefix=/usr/local  
       3. exec\_prefix=${prefix}  
       4. libdir=${exec\_prefix}/lib  
       5. includedir=${prefix}/include/  
       6. Name: lgpio  
       7. Description: Open Source GPIO library  
       8. Version: 1.0.0  
       9. Libs: ${exec\_prefix}/lib/liblgpio.so  
       10. Cflags: \-I${includedir}  
    2. Enable the SPI pins on the Pi  
       1. sudo raspi-config  
       2. Select 3 Interface Option  
       3. Select 14 SPI Enable/Disable  
       4. Select Yes on the next screen  
       5. Finish

#### Build and Install Libcamera

17. Install and build libcamera (for c++ camera control)  
    1. Install Prerequisites  
       1. sudo apt-get install \-y libevent-dev  
       2. sudo apt install \-y pybind11-dev  
       3. sudo apt \-y install doxygen  
       4. sudo apt install \-y python3-graphviz  
       5. sudo apt install \-y python3-sphinx  
       6. sudo apt install **\-y** python3-yaml python3-ply  
       7. sudo apt install \-y libavdevice-dev  
       8. sudo apt install \-y qtbase5-dev libqt5core5a libqt5gui5 libqt5widgets5  
    2. NEW \- MARCH 7 2024 \- Use [https://www.raspberrypi.com/documentation/computers/camera\_software.html\#building-libcamera-and-rpicam-apps](https://www.raspberrypi.com/documentation/computers/camera_software.html#building-libcamera-and-rpicam-apps)  
       1. BUT \-  
          1. Perform the git clone at \~/Dev where we’ve been building the other software  
          2. Do not install boost dev as a prerequisite- we built it already above  
          3. When done (after the install step), do sudo ldconfig to refresh the shared libraries  
          4. On the Pi 4 (if it has less than 6GB memory), add “-j 2” at the end of the ninja \-C build command to limit the amount of memory used during the build.  E.g., ninja \-C build \-j2  
             - On low-memory Pi’s, if you run out of memory, the computer will often just freeze and hang, requiring a hard-reboot  
       2. If the build fails at the last install step, see: [https://github.com/mesonbuild/meson/issues/7345](https://github.com/mesonbuild/meson/issues/7345) for a possible solution.  Specifically, exporting the following environment variable and re-building.  
          1. `export PKEXEC\_UID=99999`  
          2. `cd build && sudo ninja install`

#### Build RPICAM-Apps

18. Build rpicam-apps:  
    1. See the following for instructions, but with a couple exceptions…[https://www.raspberrypi.com/documentation/computers/camera\_software.html\#building-libcamera-and-rpicam-apps](https://www.raspberrypi.com/documentation/computers/camera_software.html#building-libcamera-and-rpicam-apps)  
       1. BUT, we will add `-Denable_opencv=enabled` to the meson build step because we have installed OpenCV and will wish to use OpenCV-based post-processing stages  
       2. Also, we don’t need to re-install most of the prerequisites listed in the Pi website.  Just do:  
          1. sudo apt install \-y libexif-dev  
    2. Specifically, do this:  
       1. cd \~Dev  
       2. git clone [https://github.com/raspberrypi/rpicam-apps.git](https://github.com/raspberrypi/rpicam-apps.git)  
       3. cd rpicam-apps  
       4. meson setup build \-Denable\_libav=enabled \-Denable\_drm=enabled \-Denable\_egl=enabled \-Denable\_qt=enabled \-Denable\_opencv=enabled \-Denable\_tflite=disabled \-Denable\_hailo=disabled  
       5. meson compile \-C build  
       6. sudo meson install \-C build  
       7. sudo ldconfig \# this is only necessary on the first build

#### Install Java OpenJDK

19. Install recent java (for activeMQ)  
    1. sudo apt install openjdk-17-jdk openjdk-17-jre

#### Install MsgPack

20. Install msgpack  
    1. Info at:  [https://github.com/msgpack/msgpack-c/wiki/v1\_1\_cpp\_packer\#sbuffer](https://github.com/msgpack/msgpack-c/wiki/v1_1_cpp_packer#sbuffer)  
    2. cd \~/Dev  
    3. git clone [https://github.com/msgpack/msgpack-c.git](https://github.com/msgpack/msgpack-c.git)  
    4. For some reason, the above does not grab all the necessary files. So, also go here: [https://github.com/msgpack/msgpack-c/tree/cpp\_master](https://github.com/msgpack/msgpack-c/tree/cpp_master) and click on “Code” and down load the zip file into the \~/Dev directory  
    5. unzip /mnt/PiTracShare/dev/tmp/msgpack-c-cpp\_master.zip  
    6. cd msgpack-c-cpp\_master  
    7. cmake \-DMSGPACK\_CXX20=ON .  
    8. sudo cmake \--build . \--target install  
    9. sudo /sbin/ldconfig

#### Install ActiveMQ C++ CMS

21. Install ActiveMQ C++ CMS messaging system (on both Pi’s)  
    1. This code allows PiTrac to talk to the ActiveMQ message broker  
    2. Pre-requisites:  
       1. sudo apt \-y install libtool  
       2. sudo apt-get \-y install libssl-dev  
       3. sudo apt-get \-y install libapr1-dev  
       4. sudo apt install \-y libcppunit-dev  
       5. sudo apt-get install \-y autoconf  
    3. Download and unzip [activemq-cpp-library-3.9.5-src.tar.gz](http://www.apache.org/dyn/closer.lua/activemq/activemq-cpp/3.9.5/activemq-cpp-library-3.9.5-src.tar.gz)  
       1. This version is a little old, but we’re not aware of a newer one  
       2. May also able to do:  
          1. git clone [https://gitbox.apache.org/repos/asf/activemq-cpp.git](https://gitbox.apache.org/repos/asf/activemq-cpp.git) if the available version is new enough (3.9.5 or later)  
       3. ([https://activemq.apache.org/components/cms/developers/building](https://activemq.apache.org/components/cms/developers/building) has more information on the installation process, if necessary)  
    2. cd \~/Dev  
    3. gunzip /mnt/PiTracShare/tmp/activemq-cpp-library-3.9.5-src.tar.gz (or wherever you put the .gz zip file)  
    4. cd activemq-cpp-library-3.9.5  
    5. ./autogen.sh  
    6. ./configure  
    7. make  
    8. sudo make install

#### Install ActiveMQ Broker

22. Install ActiveMQ Broker (need only do on the Pi 2 system, as it is the only system that will be running the broker ?)  
    1. We will install the binary (non-source code version)  
    2. Get Apache Pre-Reqs (most should already have been installed)  
       1. sudo apt \-y install libapr1-dev  
       2. sudo apt \-y install libcppunit-dev  
       3. sudo apt \-y install doxygen  
       4. sudo apt \-y install e2fsprogs  
       5. sudo apt \-y install maven  
    3. [https://activemq.apache.org/components/classic/download/](https://activemq.apache.org/components/classic/download/) has the source code zip file that you will want to download with the ActiveMQ Broker  
       1. E.g., [apache-activemq-6.1.4-bin.tar.gz](https://www.apache.org/dyn/closer.cgi?filename=/activemq/6.1.4/apache-activemq-6.1.4-bin.tar.gz&action=download)  
    2. Follow these instructions to install:  
       1. [https://activemq.apache.org/version-5-getting-started.html\#installation-procedure-for-unix](https://activemq.apache.org/version-5-getting-started.html#installation-procedure-for-unix)  
       2. Set the following environment variable to ensure you don’t run out of memory:  
          1. export MAVEN\_OPTS=-Xmx1024M  
       3. We suggest you install activemq at /opt, so…  
          1. cd /opt  
          2. sudo tar xvf /mnt/PiTracShare/tmp/apache\*.tar  (or wherever you put the tarball file  
       2. Test it manually once, and then we’ll start it automatically later:  
          1. cd /opt/apache-activemq\*  
          2. sudo ./bin/activemq start   (NOTE \- must start in main directory to ensure that the files like logs get created in the correct place)  
          3. Wait a half-minute and then check the data/activemq.log file to make sure everything is good  
          4. netstat \-an|grep 61616      should then return “LISTEN”  
          5. sudo ./bin/activemq stop  
       2. Setup for remote access  
          1. cd conf  
          2. sudo cp jetty.xml jetty.xml.ORIGINAL  
          3. sudo vi jetty.xml jetty.xml  
             1. Search for the line that has 127.0.0.1 and replace with whatever the IP address is for the Pi this is all  running on.  
             2. Search for the line that begins with “ Enable this connector if you wish to use https with web console”  
             3. Uncomment the next section by removing the \!-- and → at the beginning and end of the bean  
          4. cd .. ; sudo ./bin/activemq start  
          5. Log into the broker console from another machine by: https://\<Pi IP address or name\>:8161/admin  
             1. If this works, the broker is setup correctly  
       2. Setup ActiveMQ to run automatically on startup  
          1. sudo vi /etc/systemd/system/activemq.service   and add:  
             1. \[Unit\]  
             2. Description=ActiveMQ  
             3. After=network.target  
             4. \[Service\]  
             5. User=root  
             6. Type=forking  
             7. ExecStart=/opt/apache-activemq-6.1.4/bin/activemq start  
             8. ExecStop=/opt/apache-activemq-6.1.4/bin/activemq stop  
             9. KillSignal=SIGCONT  
             10. \[Install\]  
             11. WantedBy=multi-user.target  
          2. sudo systemctl daemon-reload  
          3. sudo systemctl start activemq  
          4. sudo systemctl enable activemq  
          5. sudo reboot now   (to test the auto-start)  
          6. After the system comes back, do the following to verify it’s working:  
             1. sudo /opt/apache-activemq-6.1.4/bin/activemq status  (should say it’s running)  
             2. And from a browser, check 

#### Install Maven

23. Install maven for building servlets on Tomcat/Tomee  
    1. sudo apt \-y install maven

#### Install Tomee

24. Install Tomee (on the cam2 system only)  
    1. Use the “Plume” version that supports JMS  
    2. Get the Tomee binary here:  [https://tomee.apache.org/download.html](https://tomee.apache.org/download.html)  
    3. cd /opt  
    4. sudo unzip /mnt/PiTracShare/tmp/apache-tomee-10.0.0-M3-plume.zip  (or whatever version you’re using)  
    5. sudo mv apache-tomee-plume-10.0.0-M3 tomee      \[or whatever version\]  
    6. sudo chmod \-R 755 tomee  
       1. **WARNING** \- Only use this technique if you’re on a secure, private platform.  It’s easier to simply allow read-writes from other than the root user, but there’s other (better) ways of doing this too.  This is just a simple hack for a home system.  
    7. cd tomee  
    8. sudo chmod \-R go+w webapps (so that the tomcat uses can deploy webapps  
    9. sudo vi conf/tomcat-users.xml and add before the last line (\</tomcat-users\>)  
       1. \<role rolename="tomcat"/\>  
       2. \<role rolename="admin-gui"/\>  
       3. \<role rolename="manager-gui"/\>  
       4. \<user username="tomcat" password="tomcat" roles="tomcat,admin-gui,manager-gui"/\>  
    2. Add a systemctl daemon script to /etc/systemd/system/tomee.service so that tomee will start on boot. sudo vi /etc/systemd/system/tomee.service  
       1. \[Unit\]  
       2. Description=Apache TomEE  
       3. After=network.target  
       4. \[Service\]  
       5. User=root  
       6. Type=forking  
       7. \#Environment=JAVA\_HOME=/usr/lib/jvm/default-java  
       8. Environment=JAVA\_HOME=/usr/lib/jvm/java-1.17.0-openjdk-arm64  
       9. Environment=CATALINA\_PID=/opt/tomee/temp/tomee.pid  
       10. Environment=CATALINA\_HOME=/opt/tomee  
       11. Environment=CATALINA\_BASE=/opt/tomee  
       12. Environment=CATALINA\_OPTS='-server'  
       13. Environment=JAVA\_OPTS='-Djava.awt.headless=true'  
       14. ExecStart=/opt/tomee/bin/startup.sh  
       15. ExecStop=/opt/tomee/bin/shutdown.sh  
       16. KillSignal=SIGCONT  
       17. \[Install\]  
       18. WantedBy=multi-user.target  
    2. Update /opt/tomee/webapps/manager/META-INF/context.xml to allow “.\*” instead of just 127.0….  Replace the whole regex string  
       1. The result should simply be allow=".\*" on that line  
       2. sudo cp context.xml context.xml.ORIGINAL    \[just in case\]  
    2. Add a new document base/root to allow access to the shared mounted drive:  
       1. Edit conf/server.xml and just before the /Host\> near the end of the file, put:  
       2. \<Context docBase="/home/\<PiTracUserName\>/LM\_Shares/Images" path="/golfsim/Images" /\>  
       3. This will allow the Tomee system to access a directory that is outside of the main Tomee installation tree.  This directory will be used to get debugging images from the other Pi into the web-based GUI that this Pi will be serving up.  
       4. NOTE \- if the shared directory that is mounted off of the other Pi does not exist, Tomee may not be able to start  
    2. Allow symbolic linking.  In conf/context.xml, add before the end:  
       1. \<Resources allowLinking="true" /\>  
    2. Install the systemctl siervice we just created and start it:  
       1. sudo systemctl daemon-reload  
       2. sudo systemctl enable tomee  
       3. sudo systemctl start tomee  
       4. sudo systemctl status tomee.service  
       5. Try the following to see how things are starting and to fix any problems:  
          1. sudo tail \-f /opt/tomee/logs/catalina.out  
       6. Next login from a web console:   http://\<Pi-with-Tomee\>:8080/manager/html  
          1. user-name/pwd is by default tomcat/tomcat

#### Install Launch Monitor Dependencies

25. Install other Launch Monitor dependencies  
    1. Formatting library because the currently-packaged gcc12.2 in Debian unix doesn’t have the c++20 format capability yet  
       1. **`sudo apt`** `-y install libfmt-dev`

#### Build Launch Monitor

26. **Build the PiTrac Launch Monitor\!**  
    1. Download the PiTrac repository, including the source code under the “Software” subdirectory if you haven’t already  
       1. We usually use a subdirectory called \~/Dev under the home directory of the PiTrac user to house any cloned repositories.  You will probably use an equivalent directory in the current GitHub repo, e.g., PiTrac/Software  
       2. You can do the copy by going to github and downloading the .zip  file, or from the command-line on the Pi using something like:  
          1. cd \~/Dev  
          2. git clone https://github.com/jamespilgrim/PiTrac.git  
       3. NOTE \- If you do you plan to do any code changes, you may want to create a fork from the main repository and then clone that into your Pi.  
    2. Install Remaining Prerequisites and Setup Environment:  
       1. Setup the PITRAC\_ROOT environment variable to point to the “Software” directory of the PiTrac build.  That is one directory “up” from the “ImageProcessing” directory that contains the main PiTrac meson.build file.  
          1. E.g., include in your .zshrc or .bashrc or whatever shell you use:  
             1. export PITRAC\_ROOT=/Dev/PiTrac/Software/LM Source Code  
       2. sudo apt-get \-y install libraspberrypi-dev raspberrypi-kernel-headers  
       3. Add extended timeout to  rpi\_apps.yaml file so that even if an external trigger doesn’t fire for a really long time, the libcamera library won’t time-out:  
          1. (**NOTE** for Pi 5, use /usr/share/libcamera/pipeline/rpi/pisp instead of /usr/share/libcamera/pipeline/rpi/vc4, below)  
          2. cd  /usr/share/libcamera/pipeline/rpi/vc4  
          3. sudo cp  rpi\_apps.yaml  rpi\_apps.yaml.ORIGINAL  
          4. In both /usr/local/share/libcamera/pipeline/rpi/vc4/rpi\_apps.yaml and /usr/share/libcamera/pipeline/rpi/vc4/rpi\_apps.yaml, at the end of the pipeline section, add the following (including the last comma\!)  
             1. \# Custom timeout value (in ms) for camera to use. This overrides  
             2. \# the value computed by the pipeline handler based on frame  
             3. \# durations.  
             4. \#  
             5. \# Set this value to 0 to use the pipeline handler computed  
             6. \# timeout value.  
             7. \#  
             8. "camera\_timeout\_value\_ms": 1000000,  
          5. The first file deals with the local file from the libcamera build, and the second one is the for libcamera that is installed with the O/S.  We will set both, just in case.  
       2. Get the latest imx296\_noir.json into /usr/share/libcamera/ipa/rpi/pisp  
          1. For the Pi 4:  
             1. sudo cp imx296\_noir.json /usr/share/libcamera/ipa/rpi/vc4  
          2. For the Pi 5:  
             1. sudo cp imx296\_noir.json /usr/share/libcamera/ipa/rpi/pisp  
    2. Go to the directory called ImageProcessing in whatever directory path you will be using to compile.  E.g.,   
       1. cd $PITRAC\_ROOT/ImageProcessing  
    3. meson setup build  
       1. If there are any missing libraries, ensure that the pre-requisites were all successfully built and installed and that any corresponding pkgconfig files were created correctly per the steps above.  
    4. ninja \-C build       (add \-j 2 if compiling in 4GB or less)  
    5. If the build completes successfully, try a quick sanity check to ensure that any shared libraries are correctly linked at that the PiTrac application can at least run:  
       1. build/pitrac\_lm –help  
       2. The app should return the available command-line options

#### Setup PiTrac GUI

26. First, make sure you’ve setup the required IP and directory values in the golf\_sim\_config.json file.  Instructions are [here](https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/PiTrac%20configuration%20and%20the%20golf_sim_config.json%20file.md).  
    1. The value tells the web-based PiTrac GUI where to look for the .json configuration file, which the GUI in turn uses to know where to look for certain shared image files.  
27. Setup the PiTrac-specific code package for the PiTrac GUI on the Tomee server  
    1. Log into the Pi 2 computer where the Tomee instance is running  
    2. Make sure Tomee is running:  
       1. sudo systemctl status tomee   (hit ‘q’ to exit)  
    3. cd \~/Dev   (or whatever the root of your development environment is)  
    4. mkdir WebAppDev  
    5. cd WebAppDev  
    6. vi refresh\_from\_dev.sh     (a new file) and put this in it:  
       1. \# After running this script, then do a "mvn package" to compile and then  
       2. \# /opt/tomee/bin/restart.sh  
       3. mkdir \-p src/main/{webapp/WEB-INF,java/com/verdanttechs/jakarta/ee9}  
       4. cp $PITRAC\_ROOT/ImageProcessing/golfsim\_tomee\_webapp/src/main/java/com/verdanttechs/jakarta/ee9/MonitorServlet.java ./src/main/java/com/verdanttechs/jakarta/ee9/  
       5. cp $PITRAC\_ROOT//ImageProcessing/golfsim\_tomee\_webapp/src/main/webapp/WEB-INF/\*.jsp ./src/main/webapp/WEB-INF  
       6. cp $PITRAC\_ROOT//ImageProcessing/golfsim\_tomee\_webapp/src/main/webapp/\*.html ./src/main/webapp  
       7. cp $PITRAC\_ROOT//ImageProcessing/golfsim\_tomee\_webapp/pom.xml .  
       8. \# Also pull over the current .json configuration file to make sure that the webapp is looking at the correct version.  
       9. cp $PITRAC\_ROOT//ImageProcessing/golf\_sim\_config.json  \~/LM\_Shares/GolfSim\_Share/  
    2. Run the new script to bring over the java and other web-based GUI files:   
       1. . ./refresh\_from\_dev.sh  
       2. NOTE that the above script will also move a copy of the golf\_sim\_config.json file into the shared directory that the GUI can access in order to get information about its run-time environment.  
    3. Tell the MonitorServlet where to find its configuration file  
       1. vi ./src/main/webapp/index.html  
       2. Change the FPITRAC\_USERNAME to be whatever the PiTrac user’s name is on the system.  That line in the index.html file tells the java servlet where to find the json configuration file.  
          1. Alternatively, you can just create a browser bookmark to point to the servlet with the correct filename  
    2. Create the “.war” package for Tomee  
       1. mvn package  
       2. NOTE:  The first time this is performed, it will take a few minutes to gather up all the required packages from the internet  
       3. This process will create a “golfsim.war” file in the “target” directory.  That file will then have to be “deployed” into tomee by using the manager console at http://\<Name-or-IP-address-of-Pi-with-Tomee\>:8080/manager/html  
       4. Your browser should have a window titled “Tomcat Web Application Manager”.  You may have to use the default login of tomcat/tomcat  
       5. Copy the .war file to a place that is visible on the computer from where your browser is logged into the tomee console.  
       6. Select “Choose File” in the section in the Tomee manager console labeled “WAR file to deploy”.  Select the .war file and then wait a moment until it’s name is displayed.  Then push the “Deploy” button.  
       7. In a moment, the “golfsim” app should show up on the list.  Click it.  
       8. If you get a “HTTP Status 404 – Not Found” error, try:  
          1. cd /opt/tomee/webapps  
          2. sudo chmod \-R 777 golfsim  
          3. sudo systemctl restart tomee  (the first error will ‘stick’ otherwise)  
       9. Alternatively, you can just manually move the .war file into place and Tomee should pick it up.  Wait 30 seconds or so after doing this to give Tomee a moment to see the new war file and deploy it.  
          1. cd /opt/tomee/webapps  
          2. sudo cp \~/Dev/WebAppDev/target/golfsim.war .  
       10. Confirm you can see the PiTrac GUI by entering the following into your browser:  
           1. http://\<The-Pi-2-name-or-IP\>:8080/golfsim/monitor?config\_filename=%2Fhome%2Fmleary%2FLM\_Shares/%2FGolfSim\_Share%2Fgolf\_sim\_config.json  
       11. You should see the PiTrac GUI

**CONGRATULATIONS\!** \- At this point, you’ve (hopefully) built the PiTrac software.  Please see the [Startup Documentation](https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/PiTrac%20Start-Up%20Documentation.md) for how to get PiTrac working\!

### Nice-To-Haves

**Nice-to-Haves for an easier-to-use development environment**

2. The following steps are only for someone who’s a little new to linux and doesn’t already have a development environment setup the way they like it.  The following are just a few tools that (for the authors of the PiTrac project) seem to make things a little more efficient.  This setup deals with things like easy command-recall, file and command completion,making vi a little more like Visual Studio (for better or worse\!), etc.  
3. Z-shell and OhMyZa  
   1. Connect to your raspberry Pi with SSH  
   2. Install zsh :  
      1. sudo apt-get update && sudo apt-get install zsh  
      2. Edit your passwd configuration file to tell which shell to use for user pi :  
         1. sudo vi /etc/passwd and change /bin/bash to /bin/zsh for the \<PiTracUserName\> (usually the last line)  
         2. **WARNING:** Double-check the line in passwd is correct \- you won’t be able to ssh back in if not\!  
         3. logout  
   2. Reconnect to your raspberry, and  
      1. If on login Zsh asks about the .z files, select 0 to create an empty .zshrc  
      2. check that zsh is the shell with echo $0.  
   2. Install OhMyZsh :  
      1. sh \-c "$(wget [https://raw.githubusercontent.com/robbyrussell/oh-my-zsh/master/tools/install.sh](https://raw.githubusercontent.com/robbyrussell/oh-my-zsh/master/tools/install.sh) \-O \-)"  
   2. Disconnect from your instance and reconnect/re-login to it.  
   3. Turn off  any zsh processing for any large git directories (otherwise,  it will make “cd” into a git directory freeze)  
      1. cd \<whatever directory you plan to build PiTrac in\>  
      2. git config \--add oh-my-zsh.hide-status 1  
      3. git config \--add oh-my-zsh.hide-dirty 1  
      4. NOTE \- you may have to do this later if you don’t have the directory picked out yet  
2. Install neovim  
   1. Install NeoVim:  
      1. sudo apt-get install neovim  
         2\. sudo apt-get install python3-neovim  
   2. Install vundle into NVIM, (and not vim, as many online instructions assume\!):  
      1. git clone [https://github.com/VundleVim/Vundle.vim.git](https://github.com/VundleVim/Vundle.vim.git) \~/.config/nvim/bundle/Vundle.vim  
      2. Configure neovim \- Vundle does not work perfectly with nvim unless you make some changes  
         1. Vi \~/.config/nvim/bundle/Vundle.vim/autoload/vundle.vim and change  
         2. Change $HOME/.vim/bundle (should be near the end of the file) to $HOME/.config/nvim/bundle  
      3. Note these comments:  
         1. [https://gist.github.com/lujiacn/520e3e8abfd1c1b39c30399222766ee8](https://gist.github.com/lujiacn/520e3e8abfd1c1b39c30399222766ee8)  
         2. [https://superuser.com/questions/1405420/i-really-need-help-installing-vundle-for-neovim](https://superuser.com/questions/1405420/i-really-need-help-installing-vundle-for-neovim)  
      2. Create the file /home/\<PiTracUserName\>/.config/nvim/init.vim and add:  
         1. set nocompatible              " be iMproved, required  
         2. filetype off                  " required  
         3. " set the runtime path to include Vundle and initialize  
         4. set rtp+=\~/.config/nvim/bundle/Vundle.vim  
         5. call vundle\#begin()            " required  
         6. Plugin 'VundleVim/Vundle.vim'  " required  
         7. " \===================  
         8. " my plugins here  
         9. " \===================  
         10. Plugin 'scrooloose/nerdtree'  
         11. Plugin 'valloric/youcompleteme'  
         12. " Plugin 'dracula/vim'  
         13. " \===================  
         14. " end of plugins  
         15. " \===================  
         16. call vundle\#end()               " required  
         17. filetype plugin indent on       " required  
      2. Add any other plugins you want.  The above example establishes these two  
         1. Plugin 'scrooloose/nerdtree'  
         2. Plugin 'valloric/youcompleteme'  
      2. Run vim and type :PluginInstall     in order to install the plug ins.  It will take a few moments to process.  
      3. (Ignore anything on the net that refers to .vimrc \- that’s not applicable if using nvim.

Example .zshrc file – The highlighted bits at the end should be the only thing you need to add:  
\# If you come from bash you might have to change your $PATH.  
export PATH=.:/$HOME/bin:/usr/local/bin:$PATH

\# Path to your oh-my-zsh installation.  
export ZSH="$HOME/.oh-my-zsh"

\# Set name of the theme to load \--- if set to "random", it will  
\# load a random theme each time oh-my-zsh is loaded, in which case,  
\# to know which specific one was loaded, run: echo $RANDOM\_THEME  
\# See [https://github.com/ohmyzsh/ohmyzsh/wiki/Themes](https://github.com/ohmyzsh/ohmyzsh/wiki/Themes)  
ZSH\_THEME="robbyrussell"

\# Set list of themes to pick from when loading at random  
\# Setting this variable when ZSH\_THEME=random will cause zsh to load  
\# a theme from this variable instead of looking in $ZSH/themes/  
\# If set to an empty array, this variable will have no effect.  
\# ZSH\_THEME\_RANDOM\_CANDIDATES=( "robbyrussell" "agnoster" )

\# Uncomment the following line to use case-sensitive completion.  
\# CASE\_SENSITIVE="true"

\# Uncomment the following line to use hyphen-insensitive completion.  
\# Case-sensitive completion must be off. \_ and \- will be interchangeable.  
\# HYPHEN\_INSENSITIVE="true"

\# Uncomment one of the following lines to change the auto-update behavior  
\# zstyle ':omz:update' mode disabled  \# disable automatic updates  
\# zstyle ':omz:update' mode auto      \# update automatically without asking  
\# zstyle ':omz:update' mode reminder  \# just remind me to update when it's time

\# Uncomment the following line to change how often to auto-update (in days).  
\# zstyle ':omz:update' frequency 13

\# Uncomment the following line if pasting URLs and other text is messed up.  
\# DISABLE\_MAGIC\_FUNCTIONS="true"

\# Uncomment the following line to disable colors in ls.  
\# DISABLE\_LS\_COLORS="true"

\# Uncomment the following line to disable auto-setting terminal title.  
\# DISABLE\_AUTO\_TITLE="true"

\# Uncomment the following line to enable command auto-correction.  
\# ENABLE\_CORRECTION="true"

\# Uncomment the following line to display red dots whilst waiting for completion.  
\# You can also set it to another string to have that shown instead of the default red dots.  
\# e.g. COMPLETION\_WAITING\_DOTS="%F{yellow}waiting...%f"  
\# Caution: this setting can cause issues with multiline prompts in zsh \< 5.7.1 (see \#5765)  
\# COMPLETION\_WAITING\_DOTS="true"

\# Uncomment the following line if you want to disable marking untracked files  
\# under VCS as dirty. This makes repository status check for large repositories  
\# much, much faster.  
\# DISABLE\_UNTRACKED\_FILES\_DIRTY="true"

\# Uncomment the following line if you want to change the command execution time  
\# stamp shown in the history command output.  
\# You can set one of the optional three formats:  
\# "mm/dd/yyyy"|"dd.mm.yyyy"|"yyyy-mm-dd"  
\# or set a custom format using the strftime function format specifications,  
\# see 'man strftime' for details.  
\# HIST\_STAMPS="mm/dd/yyyy"

\# Would you like to use another custom folder than $ZSH/custom?  
\# ZSH\_CUSTOM=/path/to/new-custom-folder

\# Which plugins would you like to load?  
\# Standard plugins can be found in $ZSH/plugins/  
\# Custom plugins may be added to $ZSH\_CUSTOM/plugins/  
\# Example format: plugins=(rails git textmate ruby lighthouse)  
\# Add wisely, as too many plugins slow down shell startup.  
plugins=(git)

source $ZSH/oh-my-zsh.sh

\# User configuration

\# export MANPATH="/usr/local/man:$MANPATH"

\# You may need to manually set your language environment  
\# export LANG=en\_US.UTF-8

\# Preferred editor for local and remote sessions  
\# if \[\[ \-n $SSH\_CONNECTION \]\]; then  
\#   export EDITOR='vim'  
\# else  
\#   export EDITOR='mvim'  
\# fi

\# Compilation flags  
\# export ARCHFLAGS="-arch x86\_64"

\# Set personal aliases, overriding those provided by oh-my-zsh libs,  
\# plugins, and themes. Aliases can be placed here, though oh-my-zsh  
\# users are encouraged to define aliases within the ZSH\_CUSTOM folder.  
\# For a full list of active aliases, run \`alias\`.  
\#  
\# Example aliases  
\# alias zshconfig="mate \~/.zshrc"  
\# alias ohmyzsh="mate \~/.oh-my-zsh"  
\#

alias cdlm='cd  /mnt/PiTracShare/GolfSim/LM/ImageProcessing'  
alias cdlml='cd  \~/Dev/LaunchMonitor/ImageProcessing'  
alias cdlc='cd  /mnt/PiTracShare/GolfSim/LibCamTest'  
alias cdserv='cd /usr/share/tomcat9-examples/examples/WEB-INF/classes/async'  
alias ll='ls \-al'  
function findtext() {  
grep \-rni "$1" .  
}

\# Some useful navigation aliases

alias pushdd="pushd \\$PWD \> /dev/null"  
alias cd='pushdd;cd'  
alias ssh='ssh \-A'  
alias soc='source \~/.bashrc'  
\#below to go back to a previous directory (or more)  
alias popdd='popd \>/dev/null'  
alias cd.='popdd'  
alias cd..='popdd;popdd'  
alias cd...='popdd;popdd;popdd'  
alias cd....='popdd;popdd;popdd;popdd'  
\#below to remove directories from the stack only (do not 'cd' anywhere)  
alias .cd='popd \-n \+0'  
alias ..cd='popd \-n \+0;popd \-n \+0;popd \-n \+0'

\# Enable vi mode  
bindkey \-v \# Enable vi mode  

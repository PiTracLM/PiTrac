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
    5. [Sudo Privileges](#sudo-privileges)  
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

These instructions are targeted toward folks who do not have a lot of experience building software systems in the Pi Operating System and who could benefit from more step-by-step direction. Someone who's familiar with using tools like meson and ninja to build software can likely skip over many of these steps. However, the instructions contain a number of idiosyncratic steps and configuration requirements that are particular to PiTrac.

These instructions start with a Raspberry Pi with nothing on it, and are meant to describe all the steps to get from that point to a working, compiled version of PiTrac.  PiTrac currently requires two Raspberry Pi's, so the majority of these instructions will have to be repeated twice.  Because the 'smaller' Pi system that connects to Camera 2 is the only Pi that handles the Tomcat/Tomee web-based GUI for the system, there are a few more steps for that system.

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

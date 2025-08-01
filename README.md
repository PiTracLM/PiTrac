**Introduction to PiTrac:**

- [Introduction](#introducing-pitrac)
  - [Project Page](#project-page)
- [PiTrac - Start Here](#getting-started)
- [PiTrac - Setup and Configuration](#setup-and-configuration)
- [PiTrac - Join the Discord](#join-the-discord)

## Introducing PiTrac
Introducing [PiTrac](https://hackaday.io/project/195042-pitrac-the-diy-golf-launch-monitor) \- the world’s first (free\!) open-source golf launch monitor (as far as we know). 

![image](https://github.com/user-attachments/assets/fbdc9825-b340-47b5-83ad-6c58d4588f34)

PiTrac uses low-cost Raspberry Pi(\*) computers and cameras to determine golf ball launch speed, angles and spin in three dimensions.  PiTrac interfaces with both GSPro and E6/TruGolf simulators, and its output is also accessible on a stand-alone web-based app.  \[We’ve reached out to 2k/TopGolf, but no response yet.\]

PiTrac uses off-the-shelf hardware, and includes a [parts list](https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/PiTrac%20-%20DIY%20LM%20%20Parts%20List.md) with links to potential suppliers.  The only custom part is a small printed circuit board.  The fabrication instructions for that PCB are included in the open-source distribution and it can be manufactured for a few dollars.  The two Pi computers and cameras are the most expensive parts, and cost around $250 in total.

PiTrac is not a commercial product for sale–the full design is being released as open source on GitHub for folks to build themselves.  It’s not easy, but if you’re handy with a soldering iron, can figure out how to 3D print the parts, and are willing to burrow into the Linux operating system to compile and install software, you should be able to create your own PiTrac\!  

We are hoping that we can inspire a community of developers to help test and continue PiTrac’s development.  This is a really immature project right now.  The basic features usually work reliably, but the current release is a bit dodgy.  We’re looking for folks to try building their own PiTracs and help us improve the documentation and design to make it easier for other people to do the same.

## Project Page
Please visit our [project page](https://hackaday.io/project/195042-pitrac-the-diy-golf-launch-monitor) and also our YouTube channel [here](https://www.youtube.com/@PiTrac) for more details and videos.  The [GitHub repository](https://github.com/jamespilgrim/PiTrac) is in the works and already has the 3D printed part designs, the hardware design and initial software documentation.  If you’re interested in more of the details and some of PiTrac’s development history, please look through the [project logs](https://hackaday.io/project/195042-pitrac-the-diy-golf-launch-monitor#menu-logs).  

Finally, any help at our [support page](https://ko-fi.com/Pitrac) would be appreciated to continue this work and complete the release process.

(\*) Raspberry Pi is a trademark of Raspberry Pi Ltd.  The PiTrac project is not endorsed, sponsored by or associated with Raspberry Pi or Raspberry Pi products or services.

## [Getting Started](/Documentation/.START_HERE/PiTrac%20–%20START%20HERE.md)
The process of building your own PiTrac DIY Launch Monitor is described here at a high level. There are several more-detailed instruction documents elsewhere in the PiTrac repository that are referred to below for various sub-assemblies and tasks like compiling the code and preparing the build environment.

## [Setup and Configuration](/Documentation/Raspberry%20Pi%20Setup%20and%20Configuration.md)
The instructions which are targeted toward getting setup on a step-by-step basis. These instructions start with a Raspberry Pi with nothing on it, and are meant to describe all the steps to get from that point to a working, compiled version of PiTrac.  PiTrac currently requires two Raspberry Pi’s, so the majority of these instructions will have to be repeated twice.  Because the ‘smaller’ Pi system that connects to Camera 2 is the only Pi that handles the Tomcat/Tomee web-based GUI for the system, there are a few more steps for that system.

## [Join the Discord](https://discord.gg/gMQcBBQYHT)
The PiTrac discord is where you can get help or show off your PiTrac build, discuss features and get to know other PiTrac builders. PiTrac is still a new project, so the Discord is a way to clear up any fuzzy parts of the initial ecosystem. Click the link above or copy this invite link in Discord: `https://discord.gg/gMQcBBQYHT`.

---
title: Home
layout: home
nav_order: 1
description: PiTrac - Open source DIY golf launch monitor using Raspberry Pi and global shutter cameras. Build your own golf simulator with ball speed, launch angle, and spin tracking.
keywords: golf launch monitor, DIY golf simulator, raspberry pi golf, open source launch monitor, golf ball tracking, launch angle measurement, spin rate measurement
---

![PiTrac Logo]({{ '/assets/images/logos/PiTrac.png' | relative_url }}){: width="300"}

# PiTrac - The World's First Open-Source Golf Launch Monitor

The PiTrac project is a fully-functional golf launch monitor that avoids the need for expensive high-shutter-speed and high frame-rate cameras. We've developed techniques for high-speed, infrared, strobe-based image capture and processing using low-cost cameras such as the Pi Global Shutter camera (~US$50 retail) and Raspberry Pi single board computers.

![PiTrac Overview](https://github.com/user-attachments/assets/fbdc9825-b340-47b5-83ad-6c58d4588f34)

## What Does PiTrac Do?

PiTrac determines golf ball launch speed, angles, and spin in 3 axes. Its output is accessible on a stand-alone web-based app, and interfaces to popular golf-course simulators including **GsPro** and **E6/TruGolf** are working. [We've reached out to 2k/TopGolf, but no response yet.]

## Is PiTrac For You?

PiTrac is **not a commercial product** for sale – the full design is being released as open source on GitHub for folks to build themselves. The two Pi computers and cameras are the most expensive parts, costing around **$250 in total**. PiTrac uses off-the-shelf hardware with a [parts list]({% link hardware/parts-list.md %}) including supplier links. The only custom part is a small printed circuit board that can be manufactured for a few dollars.

**It's not easy**, but if you're handy with a soldering iron, can figure out how to 3D print the parts, and are willing to burrow into the Linux operating system to compile and install software, you should be able to create your own PiTrac!

## Community & Development

We are hoping to inspire a community of developers to help test and continue PiTrac's development. This is still a young project – the basic features usually work reliably, but the current release needs polish. We're looking for folks to build their own PiTracs and help us improve the documentation and design.

### Resources
- **[Hackaday Project Page](https://hackaday.io/project/195042-pitrac-the-diy-golf-launch-monitor)** - Project details and development logs
- **[Reddit Community](https://www.reddit.com/r/Golfsimulator/comments/1hnwhx0/introducing_pitrac_the_open_source_launch_monitor/)** - Discussion and support
- **[YouTube Channel](https://www.youtube.com/@PiTrac)** - Videos and tutorials
- **[GitHub Repository](https://github.com/jamespilgrim/PiTrac)** - Source code, 3D models, and hardware designs
- **[Support PiTrac](https://ko-fi.com/Pitrac)** - Help fund continued development
- **[Project Wish List](https://www.amazon.com/registries/gl/guest-view/11PSDIVICY8UX)** - Equipment needs

## Documentation Sections

- **[Getting Started]({% link getting-started.md %})** - Learn about PiTrac, project status, and roadmap
- **[Hardware]({% link hardware/hardware.md %})** - Parts list, assembly guides, and 3D printing
- **[Software]({% link software/software.md %})** - Raspberry Pi setup, configuration, and startup
- **[Integration]({% link integration/integration.md %})** - Connect to golf simulators and third-party systems
- **[Troubleshooting]({% link troubleshooting/troubleshooting.md %})** - Debugging guides and common issues

<p align="center">Introduction to PiTrac's Enclosure
&nbsp;
<iframe width="640" height="420" src="https://www.youtube.com/embed/1pX95VoKsS4?si=O_Mzlwz3F93mBZXC" frameborder="0" allowfullscreen></iframe>
</p>

---

*(\*) Raspberry Pi is a trademark of Raspberry Pi Ltd. The PiTrac project is not endorsed, sponsored by or associated with Raspberry Pi or Raspberry Pi products or services.*

----

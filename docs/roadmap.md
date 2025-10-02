---
title: Roadmap
layout: default
nav_order: 1.3
parent: Home
---

# PiTrac Roadmap (as of June 23, 2025)

Where's the PiTrac project headed?  What version should I build right now?  Should I wait for some new feature or development before buying parts or pulling out the soldering iron?

All good questions.  This document will try to answer them.  That said, much of the roadmap is unknown because the project is evolving with every contribution that folks make.

The following is a summary of where we are, followed by a list of the future features and developments that we're currently aware of.  Please submit any new ideas in the [#feature-wishlist-discussion](https://discord.com/channels/1324384654731509770/1325494997285343357) area of the [PiTrac Discord Server](https://discord.gg/vGuyAAxXJH).   Skip to Section 2 if you just want the answer to what most folks should (probably) be building right now.

## 1. Where We Are Now

Currently, PiTrac is at a few crossroads.  The **first crossroad** is hardware related.  Specifically, we're working to move from a system built by cobbling together a bunch of off-the-shelf (OTS) parts to a more streamlined, custom system.  Primarily, this system will be enabled by developing a PiTrac "Compute Board."  That printed circuit board will contain all the power-adapters, network connections, strobe-light switching and other electronics that PiTrac needs.  The board will probably cost US $80 or more (populated except for Pi's), but should reduce the overall cost.  The lower cost will result from a higher degree of integration that removes the need for separate power adapters, an OTS LED driver, etc.

The Compute Board system will have a single power connection instead of what currently is usually a bulky power strip with a half-dozen AC power adapters plugged into it and a mess of wires running to the PiTrac.  The Compute Board will have plug-in sockets to hold and connect one or two Raspberry Pi 5 compute modules.  All great–however:  the main problem right now is that the Compute Board is pretty complex, will require a manufacturing partner, and will likely still be several months out before it's done.

At the same time, some folks will always enjoy putting things together themselves with parts that they can configure, re-use and re-purpose and without most of the hardware being pretty much completed already.  This is still the only option that is currently available.  The only "custom" hardware for this option is a small "Connect Board" that folks can have manufactured (or purchased from other builders) for a few dollars.  We'd like to always continue to support both of the above two options.

The **second crossroad** is related to the 3D-printed enclosure that houses all the cameras, computers, etc.  Of course, we didn't even have a printed enclosure at first.  This was the first PiTrac:

![First PiTrac](https://github.com/user-attachments/assets/1aed70a7-c2f3-4246-b187-45b76ab97ffa)

For makers who are interested in experimenting, of course, a custom-built enclosure may be a lot of fun.  And perhaps variants like overhead PiTracs will spring from such work.

The first 'real' enclosure was bulky and didn't really even work for folks in countries with larger power adapter bricks like the U.K.  It did, however, allow for most all of the power adapters and other parts to be set together in a single unit.  It is also setup by default to use all OTS parts and separate Pi SBCs.

![First Enclosure](https://github.com/user-attachments/assets/c7b1e9d0-f601-43c8-9089-259e23390879)

The newest enclosure (cleverly referred to as "Version 2") is much more streamlined:

![Version 2 Enclosure](https://github.com/user-attachments/assets/24271598-5d8e-49e1-bd36-f2859efe7523)

Regarding the enclosure, we expect most folks will migrate to Version 2 regardless of whether they ultimately wait to use the future Compute Board and Pi compute modules or if they build the system from individual Pi 5 single board computers (SBCs), an LED driver, and the Connector Board.

But here too, there's a decision to make.  The Version 2 enclosure will someday house the self-contained Compute Board with a single power supply for that board that will fit in the base box, and the default Version 2 model is setup to receive the screws that the (unreleased) Compute Board expects for its mounting holes:

![Compute Board Vision](https://github.com/user-attachments/assets/85cdea24-81bc-4dab-93c6-850ec1a42e49)

But the Version 2 enclosure can also support builds using *OTS parts* instead of the Compute Board.  As such, part of the decision of which road to take right now is whether to wait for the above-pictured vision of PiTrac to emerge, or instead use the Version 2 enclosure with the currently-available off-the-shelf system.  Most folks are opting for that option now.

This second Version 2 option typically houses the large LED driver in the base of the system, with several AC power adapters kept externally with wires running to the tower.  To go that way, you'll need an enclosure "tower" variant that is fitted for the separate Pi 5 SBCs.  See [here](https://github.com/pitraclm/pitrac/tree/main/3D%20Printed%20Parts/Enclosure%20Version%202/Tower-Connector-Board-Variant).

![Tower Variant](https://github.com/user-attachments/assets/d040da0a-5363-475d-8114-5715e5935a62)

Note that for some LED drivers, the basebox dimensions may have to be enlarged to fit those drivers, as they are larger than the Meanwell 24v power supply that the Version 2 enclosure was originally designed to hold.

The **final crossroad** has a much clearer path forward.  With some help from the Raspberry Pi folks, we've finally been able to get the system running on just a single Raspberry Pi 5 SBC that is connected to both of the PiTrac cameras.  We expect almost everyone to go with that option to save money and reduce complexity.  That's true even for folks who have already built (or are building) their PiTracs with two Pi's.  However, the very real possibility of a PiTrac with stereoscopic vision is likely to still require two Pi 5's.  Don't give your currently-extra Pi away!

Finally, there are many core parts of PiTrac that we are still actively researching and (hopefully) improving.  Builders may want to modify their PiTrac builds to help support or work on some of those areas.   Section 3 of this document discusses some of these activities.

## 2. What should I build now?

### If you want to get building right now (and try to minimize buying anything that may become obsolete)
Likely best to build a Version 2 enclosure using the original Connector Board with a single Pi 5 SBC and with an external LED driver and power adapters.

- It might not be as compact/pretty, but the result should continue to be supported for a long time to come
- The LED driver will be unnecessary someday when the Compute Board is available.  But that won't happen for months, and the cost of the Compute Board and a Pi 5 compute module will be greater than the cost of the LED driver.

### If you're happy to wait until the system is more stable
The future Compute Board with the Version 2 enclosure will be pretty and pretty awesome.   The cost should be minimized here, too.  Though wouldn't it be fun to start building a PiTrac now? :)

### You're looking to plow new ground and experiment
Same as for option (a), above.  And consider just taking the existing 3D parts you most need and coming up with your own enclosure!

## 3. Where We Are (Hopefully!) Going

The following are new features, ideas, hair-brained ideas and potential areas of focus for PiTrac's continuing development.  We are already working on many of these areas.

### Enclosures & Physical Configuration
1. Overhead-mounted PiTrac
2. 3 or 4-camera enclosures to allow higher accuracy (at a higher price point)
3. A power-brick type outboard module that will cleanly house all the hardware that doesn't fit on the Version 2 (Connector Board) enclosure.
4. Get the system working when both cameras pointed straight out (necessary for, e.g., left-handed playing).  **FIXED** as of July 16, 2025.

### Left-handed processing
1. Complete the code changes to process left-handed shots.  **FIXED** as of July 16, 2025.

### Hardware
1. Improve ability to drive the LED strobes smaller circuitry that is better suited to the very short duty cycles we are using

### Software
1. Improve the ball-circle identification process to be more accurate w/r/t ball radius and in ignoring false positives
2. Implement stereoscope camera processing
3. Improve the lens de-distortion process and make it easier to do

### Optics
1. Test and standardize on the Innomaker camera instead of the Pi version.  Innomaker is much easier to modify to be externally triggered.  As of July 16, 2025 this mostly works, but we are waiting on a fix for a problem that does not allow two cameras attached to the same Pi 5, with one externally-triggered, and one internally-triggered.
2. Design to use 3.6mm M12 lens to capture a wider range of movement

### Documentation
1. Catch up!  Too many ideas and changes are not documented.
2. Support code-level documentation by using Doxygen tags throughout the code base
3. Organize the now-too-many documents into, e.g., sub-folders
4. Make document changes easier.  For example, use Bookstack.

### Supply chain
1. Develop better, cheaper sources for all of the parts used by PiTrac, and try to better support non-US builders.

### Simulators
1. Support and integrate with any open-source golf simulators such as the work on JaySim
2. Improve the number of commercial sims that PiTrac can integrate with
3. Improve the level of integration with other sims such as club selection

### Testing
1. Work is being done to move the architecture to a more modern footing, including automated testing that will run whenever anything is checked in.
**PiTrac – Start Reading Here**

The process of building your own PiTrac DIY Launch Monitor is described here at a high level.   There are several more-detailed instruction documents elsewhere in the PiTrac repository that are referred to below for various sub-assemblies and tasks like compiling the code and preparing the build environment.

First a bit of a caveat – this is not a beginner project.  You’ll need to be at least a little familiar with Linux and to be able to, for example, use an editor like vi that is available on Linux as well as have some experience compiling code in that environment.  Also, please read the [“What is PiTrac” missive](https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/PiTrac%20-%20What%20is%20it.md) to make sure you understand the nature of the project.  We hope you’ll build one of your own PiTracs, but want to make sure you know things like the fact it only works for right-hand players right now (hopefully fixed early next year) and that you’ll have to do a little careful soldering that will void some warranties on the Pi Camera\! 😊  We’re also certain that there are still lots of mistakes in the documentation (not to mention the code).  So, you’ll have to expect that there will be some hiccups.  We are hoping to be able to rely on our first builders to help us correct the early issues, so please check the Issues section in github and email with any questions to [pitrac.lm@gmail.com](mailto:pitrac.lm@gmail.com). 

If you'd like to check out the software without needing Raspberry Pi's or building the enclosure, you can do that, too, in a limited way.  In order to just build the software and step through it's processing (with pre-made images instead of using live cameras), please read  [PiTrac - Debugging and Code Walk-Thrroughs](https://https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/PiTrac%20-%20Debugging%20and%20Code%20Walk-Throughs.md).

With that said, here is what you need to do to make your own PiTrac:

[1\. Download the PiTrac Repository](#1-download-the-pitrac-repository)

[2\. Gather the Parts You’ll Need](#2-gather-the-parts-you’ll-need)

[3\. Setup the Raspberry Pi Computers That You Will Use in the PiTrac](#3-setup-the-raspberry-pi-computers-that-you-will-use-in-the-pitrac)

[4\. Print the Parts That Make Up the PiTrac Enclosure](#4-print-the-parts-that-make-up-the-pitrac-enclosure)

[5\. Assemble Your PiTrac](#5-assemble-your-pitrac)

[6\. Perform Startup Testing and Final Configuration](#6-perform-startup-testing-and-final-configuration)

# 1. Download the PiTrac Repository

If you haven’t already done so, download the entire current PiTrac repository.  This can be accomplished using any one of several github utilities.  Exactly how you do this will depend on your particular environment, but here’s a few ways:

* 1\.       If you’re on an Apple or Unix computer, you probably already have git installed.  Just open a terminal window.  Change directories to where you want to land your copy of the PiTrac repository, and do:  
  * a.       git clone https://github.com/jamespilgrim/PiTrac.git  
* 2\.       For Windows (and the Mac, for that matter), there’s lots of YouTube videos.  [Here’s a decent one](https://www.youtube.com/watch?v=7ouVv6PFZGc&t=281).  Once you’ve installed git, you can use one of the tools described in the video to clone the PiTrac project as described above for Macs.  
* 3\.       You can also install Microsoft Visual Studio, which includes git management functionality.

# 2. Gather the Parts You’ll Need

We’ve listed the basic components that you’ll need to build a PiTrac in the [Parts List here](https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/PiTrac%20-%20DIY%20LM%20%20Parts%20List.md).  We (at least currently) don’t sell any of these components. 

The one component that you won’t be able to just buy off the shelf is the little Connector Board printed circuit board.  However, the manufacturing input files are [here](https://github.com/jamespilgrim/PiTrac/tree/main/Hardware/Connector%20Board), and if you send those to a company like [www.jlcpcb.com](http://www.jlcpcb.com/) or [www.pcbway.com](http://www.pcbway.com/), they will often include a small run of this PCB on the unused parts of larger boards they are making for other customers.  This is usually available for only a few dollars, and the slowest shipping option to the US (for example) is also only a few dollars.  It can take a couple of weeks for this process, so you might want to get it going soonest.

The other components can be purchased at places like [www.adafruit.com](http://www.adafruit.com/) and [www.pimoroni.com](http://www.pimoroni.com/) , not to mention Amazon and directly from the [Raspberry Pi folks](https://www.raspberrypi.com/products/raspberry-pi-5/).  Most of the little nuts and bolts are best purchased as part of bulk kits.  See the [Component List](https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/PiTrac%20-%20DIY%20LM%20%20Parts%20List%20.md).

NOTE:  You may want to skim through the rest of the documents to see if you want to modify the parts list.

 

# 3. Setup the Raspberry Pi Computers That You Will Use in the PiTrac

The next step is to get your Pi computers up and running and to install the software packages that will be needed to build the PiTrac software.  This process is done first, before putting together the 3D-printed enclosure that the Pi computers are eventually mounted to.

See the document for how to prepare the Pi’s and build the PiTrac executables [here](https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/Raspberry%20Pi%20Setup%20and%20Configuration.md).

NOTE: If you are going to have someone else 3D print the enclosure, it’s probably not a bad idea to get that started while you work on getting the Pi’s setup.

 

# 4. Print the Parts That Make Up the PiTrac Enclosure

If you have your own 3D Printer (or access to one), you can print your own 3D parts using the [3D design models](https://github.com/jamespilgrim/PiTrac/tree/main/3D%20Printed%20Parts/Enclosure%20Models) and [assembly instructions](https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/DIY%20LM%20Enclosure%20Assembly.zip).  We know that some folks have also had some luck having a third-party printing company take the models and documentation and print the parts for a fee.

The parts are large enough that you’ll need several spools of printer filament, not to mention a few days to get everything printed.  This is our first project open-sourcing 3D part designs, so be ready for some hiccups here.

 

# 5. Assemble Your PiTrac

Once you have PiTrac compiled on each of the two Pi’s, you’re ready to attach the Pi’s to the 3D-printed PiTrac enclosure and assemble the final(ish\!) launch monitor.  See the [assembly instructions](https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/DIY%20LM%20Enclosure%20Assembly.md).  The assembly requires that the cameras be calibrated as the LM is being built.  For that reason, the PiTrac software (which performs some of that calibration) needs to be ready to go before you start putting things together.

 

# 6. Perform Startup Testing and Final Configuration

Use [this document](https://github.com/jamespilgrim/PiTrac/blob/main/Documentation/PiTrac%20Start-Up%20Documentation.md) to perform some final sanity-checks and testing.  That document also details how to get PiTrac running.

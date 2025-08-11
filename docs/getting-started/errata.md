---
title: Errata and Future Hopes
layout: default
nav_order: 5
parent: Getting Started
---

# Errata and Future Hopes for the DIY LM

**Errata:**

* The LM is not enabled for left-handed golfers.  This is embarrassing, but we simply haven’t had the time to complete this capability. And we have no left-handed friend golfers to test it.  In addition, the Camera 1 has to be pointed the other direction to make this work (at least currently).  Repositioning that camera is not easy.  Better cameras and faster processing and triggering of Camera 2 in the future may not need this side-tilt at all, and would make it much easier to be non-handist..  
* Very low, worm-burner shots are often not analyzed correctly, because the system loses the ball in the hitting mat background visual noise.  
* Highly-angled shots that end up with the ball very close to or relatively far from the LM are often not processed correctly, as the ball is not well-focused.  
* Need to get rid of all the compile warnings when building in Visual Studio.  They’re mostly long/int conversion type warnings.

**New Features:**

1. Slow-motion motion club impact (GitHub Issue #34)  
2. Auto-calibration.  Set the cameras off in some reasonable directions, place a ball at a known spot, and let the system do the rest.

**Future Hopes:**

* Reliability  
  * The system is totally dependent on good circle detection of the sometimes-overlapping strobed ball images.  And the OpenCV Hough transform that we’re using is terribly touchy and often performs poorly in recognizing what (to the human eye) are the most obvious circles in the image.  We really need to improve this.  When the Hough transform is not working well, it really hobbles the entire system.  
  * The strobe-ball image processing is just too complex and hard to understand and maintain.  
* Testing  
  * Complete automated regression testing in an extensible test suite, including against a static stash of images with known outcomes  
  * Manual QA testing checklist instructions for IRL swing testing accompanied by non-interfering third-party LMs  
  * Automated performance benchmarks  
  * Perform side-by-side testing with a good radar-based LM  
* Power Supply  
  * The current retail-power-adapter strategy isn’t working well, and is probably much more expensive than it needs to be.  We need a custom-ish power supply that can plug into wall-outlet AC and put out all the various voltages that are needed.  Would be great if this power supply could put out the constant-current DC 12V needed for the strobe light as well.  
  * We need a power switch!  And a switch for the external LED strip, too.   
* Enclosure  
  * Easier-to-join enclosure halves.  It’s hard to get to the bolts to tighten the halves together  
  * The supports for the lower-power-side floor’s overlap joint are a pain to remove.  Maybe print at a different angle?  
  * The seams on the 3D printed enclosure are unsightly.  Would be great to be able to print in a way that makes it look a little more professional.  Maybe more lap joints?  
  * Access to the Pi’s is difficult even with the ports..   
  * The inter-floor screws currently overhang into the layer interiors, which might present a safety issue.  Perhaps add a little bump on the side of the inner wall that the screws can end into.  
  * Removing the supports on the power-side floor lap joint is painful.  Is there a better design or way to print?  
  * Mount the Pi(s) to a tray we can slide them into the enclosure on rails, and use a screw to hold it in.  Note - We'd need longer camera ribbons  
* Cameras  
  * Calibration is too difficult and takes too much time.  How to improve?  
  * As new, higher-resolution global shutters come onto the market, it will be great to integrate them into the system.   Maybe we won’t need the angled Camera 1 in order to watch the teed-up ball!  
  * The field of focus is too narrow.  A better lens might help.  
  * Having a variant of PiTrac that uses a high frame-rate / low shutter time would be a great option for builders who don’t mind paying a little more for the LM.  This could obviate the need for a second camera and Pi, and would also make it a lot easier to switch between right and left-hand golfers.  
    * In fact, as GS cameras come down in price, this may be the direction the entire project heads to.  
* Documentation  
  * Switch to something like Doxygen for a lot of the project documents  
  * Create UML-like class structure definitions.  
  * Document the top 5 issues and see if we can get folks to fix them!  
    * An easy add would be to figure out the Carry value.  
* Strobe:  
  * The 12V power supply for the LEDs seems like overkill given the short pulses that PiTrac uses.  Do we even need a constant-current  supply?  Could we just use a cheap 12V AC adapter for a couple dollars?    
  * Could we over-drive the LEDs by a few volts to get brighter pulses?  This would help with a number of things, including picture sharpness for fast balls.  Most LED’s can handle higher voltage for short periods of time without significantly shortening their lifespans.  
* Hardware:  
  * Must the Connector Board really have its own 5V power source?  Is there a better circuit that will protect the Pi’s from the 12V source and each other?  Can we run the board from the 12V supply?  
  * A fail-safe (temperature based? Dead-man timing?) for the strobe light to make sure it never stays on would be a great safety feature.  
* Performance (Speed)  
  * The time between when the ball is hit and when the picture-taking and strobe-pulsing starts is too long.  The ball has already moved halfway across the field of view.  A faster FPS camera would help, of course.

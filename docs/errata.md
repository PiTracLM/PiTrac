---
title: Errata and Future Hopes
layout: default
nav_order: 1.4
parent: Home
---

# Errata and Future Hopes for the DIY LM

**Errata:**

* Very low, worm-burner shots are often not analyzed correctly, because the system loses the ball in the hitting mat background visual noise.
* Highly-angled shots that end up with the ball very close to or relatively far from the LM are often not processed correctly, as the ball is not well-focused.
* Need to get rid of all the compile warnings when building in Visual Studio.  They're mostly long/int conversion type warnings.

**New Features:**

1. Slow-motion club impact (GitHub Issue #34)

**Future Hopes:**

* Reliability
  * ~~The system is totally dependent on good circle detection~~ We now offer YOLO neural network ball detection as an alternative to the traditional Hough transform approach. YOLO is accessible via the web UI (Configuration → Ball Detection → Detection Method) and is generally faster and more robust, especially in challenging lighting conditions or with overlapping ball images. The Hough transform option is still available and works well for many setups, but when it's touchy it can hobble the system. The YOLO option should help significantly with reliability.
  * The strobe-ball image processing is still complex and could use simplification and better documentation to make it easier to understand and maintain.
* Testing
  * Complete automated regression testing in an extensible test suite, including against a static stash of images with known outcomes
  * Manual QA testing checklist instructions for IRL swing testing accompanied by non-interfering third-party LMs
  * Automated performance benchmarks
  * Perform side-by-side testing with a good radar-based LM
* Power Supply
  * The V2 Connector Board has made big improvements here—single +5V input (Meanwell LRS-75-5), integrated boost converter for LED voltage, adjustable current limiting, and hardware-enforced 10% duty cycle for thermal protection. This replaces multiple power supplies and the separate constant-current LED driver.
* Enclosure
  * Easier-to-join enclosure halves.  It's hard to get to the bolts to tighten the halves together
  * The supports for the lower-power-side floor's overlap joint are a pain to remove.  Maybe print at a different angle?
  * The seams on the 3D printed enclosure are unsightly.  Would be great to be able to print in a way that makes it look a little more professional.  Maybe more lap joints?
  * Access to the Pi's is difficult even with the ports.
  * The inter-floor screws currently overhang into the layer interiors, which might present a safety issue.  Perhaps add a little bump on the side of the inner wall that the screws can end into.
  * Removing the supports on the power-side floor lap joint is painful.  Is there a better design or way to print?
  * Mount the Pi(s) to a tray we can slide them into the enclosure on rails, and use a screw to hold it in.  Note - We'd need longer camera ribbons
* Cameras
  * ~~Calibration is too difficult and takes too much time~~ Auto-calibration is now available through the web UI! It's a 4-step wizard that automatically calculates focal lengths and camera angles—no tape measures, no manual JSON editing, no shell scripts. Manual calibration is still supported for advanced users, but the auto-calibration wizard is the recommended approach and works well.
  * As new, higher-resolution global shutters come onto the market, it will be great to integrate them into the system.   Maybe we won't need the angled Camera 1 in order to watch the teed-up ball!
  * The field of focus is too narrow.  A better lens might help.
  * Having a variant of PiTrac that uses a high frame-rate / low shutter time would be a great option for builders who don't mind paying a little more for the LM.  This could obviate the need for a second camera and Pi, and would also make it a lot easier to switch between right and left-hand golfers.
    * In fact, as GS cameras come down in price, this may be the direction the entire project heads to.
* Configuration
  * ~~No easy way to configure the system without hand-editing massive JSON files~~ The web UI at `{PI-IP}:8080/config` now provides full configuration management! 283 settings organized into categories, search functionality, live validation, import/export, and proper diff view showing what you've changed from defaults. No more manual JSON editing for most users. This is a massive improvement.
* Documentation
  * Switch to something like Doxygen for a lot of the project documents
  * Create UML-like class structure definitions.
  * Document the top 5 issues and see if we can get folks to fix them!
    * An easy add would be to figure out the Carry value.
* Strobe:
  * ~~The 12V power supply for the LEDs seems like overkill~~ The V2 Connector Board has adjustable voltage output (~15V to ~42V range) and current limiting via potentiometers, replacing the need for a separate constant-current supply. But the question remains: could we use even cheaper/simpler power for such short pulses?
  * Could we over-drive the LEDs by a few volts to get brighter pulses?  This would help with a number of things, including picture sharpness for fast balls.  Most LED's can handle higher voltage for short periods of time without significantly shortening their lifespans.
* Hardware:
  * ~~Must the Connector Board really have its own 5V power source?~~ The V2 board now uses a single +5V input that safely powers both Pi units without opto-coupler isolation (since they share power, no isolation needed). This is a significant simplification from V1.
  * ~~A fail-safe for the strobe light~~ The V2 board includes dual 555 timer circuit that enforces a 10% maximum duty cycle, preventing thermal runaway. This is a hardware-enforced safety feature.
* Performance (Speed)
  * The time between when the ball is hit and when the picture-taking and strobe-pulsing starts is too long.  The ball has already moved halfway across the field of view.  A faster FPS camera would help, of course.

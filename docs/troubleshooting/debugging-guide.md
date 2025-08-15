---
title: Debugging Guide
layout: default
nav_order: 1
parent: Troubleshooting
---

# PiTrac - Debugging and Code Walk-Throughs (Non-Pi Build/Run Environment)

{: .warning }
**TBD - THIS DOCUMENT IS UNDER CONSTRUCTION**

This document is for folks who:

* Don’t have a Raspaberry Pi and haven’t assembled a physical PiTrac, but still want to look into how the code works.  
  * Or doesn’t work. Sometimes the latter. :/  
* Saw some unexpected results from PiTrac and want to figure out what went wrong.  
  * For example – Why is the calculated speed 659.75 mph? Did I really hit the ball with a forward spin of 454,623 RPM? Why didn’t the system “see” my shot correct? And so on…  
* Would like to develop and test some improved algorithms for ball identification or spin analysis, etc. without doing so on the actual Pi-based LM.  
  * Boy, would we love that to happen…

> NOTE - Running the PiTrac system in Windows is something you’d probably only do if you’re reasonably proficient with C++, Visual Studio, and debugging work in general.

## Workflow

1. Crank up the logging in the system running on the Pi’s in order to produce more intermediate image artifacts so we can figure out where things go wrong.  
2. Use some of those images as input to a debugging version of the system.  
3. Use the debugging environment to bypass having to use the cameras to take a picture, and instead focus on the post-picture processing.  
4. Try different processing parameters in `golf_sim_config.json` or source code.  
5. Optionally, use workbench programs to fine-tune parameters.

If you don’t have a physical Pi and PiTrac, you’ll just need a couple example images.

## Prerequisites

Windows instructions (Mac similar):

1. **Install Visual Studio** (e.g., 2022 Preview)  
2. **Install OpenCV 4.10**  
   - Download from [opencv.org/releases](https://opencv.org/releases)  
   - See [installation guide](https://docs.opencv.org/4.x/d3/d52/tutorial_windows_install.html)  
3. **Install Boost**  
   - Download prebuilt binaries from [SourceForge](https://sourceforge.net/projects/boost/files/boost-binaries/) or build from source  
4. **Configure VS Project**  
   - Set `Include Directories` (e.g., `E:\Dev_Libs\opencv\build\include;E:\Dev_Libs\boost_1_87_0`)  
   - Set `Library Directories`  
   - Define preprocessor constants:  
     ```
     BOOST_BIND_GLOBAL_PLACEHOLDERS
     BOOST_ALL_DYN_LINK
     BOOST_USE_WINAPI_VERSION=0x0A00
     _DEBUG
     _CONSOLE
     ```
   - Set PATH for runtime:  
     ```
     PATH=%PATH%;E:\Dev_Libs\opencv\build\x64\vc16\bin;E:\Dev_Libs\boost_1_87_0\lib64-msvc-14.3
     ```

## Command-Line Parameters Example

```bash
--show_images 1 --lm_comparison_mode=0 --logging_level trace --artifact_save_level=all --wait_keys 1 --system_mode camera1_test_standalone --search_center_x 800 --search_center_y 550
```

## Example Debug Session

1. Move relevant images to `kPCBaseImageLoggingDir`  
2. Open `PiTrac.sln` in Visual Studio  
3. In `golf_sim_config.json` set:  
   ```json
   "testing": {
     "kTwoImageTestTeedBallImage": "gs_log_img__log_ball_final_found_ball_img.png",
     "kTwoImageTestStrobedImage": "log_cam2_last_strobed_img.png"
   }
   ```
4. In `lm_main.cpp`, enable:  
   ```cpp
   testAnalyzeStrobedBalls();
   ```
5. Set breakpoints in `testAnalyzeStrobedBalls()` or `GolfSimCamera::ProcessReceivedCam2Image`

## Hough Circle Detection Playground

Use the Playground project to experiment with detection parameters:

- Match `kBaseTestDir` and `kTestImageFileName` to your test image
- Set constants to match parameters from the main PiTrac log  
- Run and adjust sliders to tune detection

Once tuned, update `golf_sim_config.json` accordingly.

---

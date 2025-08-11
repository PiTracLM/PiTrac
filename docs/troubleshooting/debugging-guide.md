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
  *  Or doesn’t work.  Sometimes the latter. :/  
* Saw some unexpected results from PiTrac and want to figure out what went wrong.  
  * For example – Why is the calculated speed 659.75 mph?  Did I really hit the ball with a forward spin of 454,623 RPM?  Why didn’t the system “see” my shot correct?  And so on…  
* Would like to develop and test some improved algorithms for ball identification or spin analysis, etc. without doing so on the actual Pi-based LM.  
  * Boy, would we love that to happen…

NOTE \- Running the PiTrac system in Windows is something you’d probably only do if you’re reasonably proficient with C++, Visual Studio, and debugging work in general.  And we hope there’s some of those folks on the project\! 🙂

So how does this work?  Well, if you already have a PiTrac and want to work on a particular problematic set of camera images, the primary work flow is:

1. Crank up the logging in the system running on the Pi’s in order to produce more intermediate image artifacts so we can figure out where things go wrong.  
2. Use some of those images as input to a debugging version of the system.  The debugging environment is typically running in Visual Studio on a PC, or a Mac-based system.  
3. Use the debugging environment to bypass having to use the cameras to take a picture, and instead focus on the post-picture processing.  In this environment, it’s easy to step through the code, produce additional image artifacts, and generally figure out what’s going on.  
4. Try different processing parameters (in golf\_sim\_config.json or elsewhere) and/or change the source code to make improvements.  
5. If necessary, use one of the workbench programs to play with the processing parameters in real time to see what works best.  Then take those settings an import into PiTrac

If you don’t have a physical Pi and PiTrac, you’ll just need a couple example images, which can be found here.

**Prerequisites:**

In order to build the PiTrac code in a non-Pi environment, you’ll need to set up a few things.  These instructions will focus on Windows, but the Mac should be similar.

1. Install Visual Studio (we’re using the 2022 Preview currently)  
     
2. Install OpenCV 4.10  
     
   1. You can just download the binaries at [https://opencv.org/releases](https://opencv.org/releases).  Building the source locally has its advantages for debugging, but takes a while.  Instructions are [here](https://docs.opencv.org/4.x/d3/d52/tutorial_windows_install.html)  
   2. [This site](https://christianjmills.com/posts/opencv-visual-studio-getting-started-tutorial/windows/) has some good information on installation and setup

   

3. Install Boost, for example the pre-build windows libraries from [here](https://sourceforge.net/projects/boost/files/boost-binaries/) (though you can also download the source and build it yourself).  
   1. If you’re using the pre-built package, just unzip it

   

4. Setup the Windows VS project to point to the correct include and library directories  
     
   1. In VS, go to Project/ImageProcessing Properties:  
        
   2. ![image](https://github.com/user-attachments/assets/429ce409-f7b9-49de-83df-71835daf7bff)
 
        
      1. For example:    E:\\Dev\_Libs\\opencv\\build\\include;E:\\Dev\_Libs\\boost\_1\_87\_0  
   3. Point to the correct libraries:  
      ![image](https://github.com/user-attachments/assets/43ffd981-dac5-48ab-96c4-f412d42b0005)

        
      1. For example, E:\\Dev\_Libs\\opencv\\build\\x64\\vc16\\lib\\opencv\_world4100d.lib;$(CoreLibraryDependencies);%(AdditionalDependencies)  
         * \[NOTE \-The \*d.lib is for debug libraries.  If not debugging, then remove the “d”.  And do not mix \* and \*d \!

      

   4. In the Linker→General→Additional Library Directories, add:  
      1. ![image](https://github.com/user-attachments/assets/8ec82d6d-9508-4850-bf4e-887a3953f2b4)

      2. For example:  E:\\Dev\_Libs\\opencv\\build\\x64\\vc16\\lib;E:\\Dev\_Libs\\boost\_1\_87\_0\\stage\\lib  
   5. Set up the compiler defines that help pick the correct boost bindings:  
      1. ![image](https://github.com/user-attachments/assets/5f68600d-4b34-4b7d-b3ee-492d08fa7d1f)

      2. BOOST\_BIND\_GLOBAL\_PLACEHOLDERS;BOOST\_ALL\_DYN\_LINK;BOOST\_USE\_WINAPI\_VERSION=0x0A00;\_DEBUG;\_CONSOLE;%(PreprocessorDefinitions)  
   6. Point to the correct run-time binaries and libraries:  (and note that the suggested opencv instructions cited above do not use this step and instead copy files around (less desirable)  
        
   7. ![image](https://github.com/user-attachments/assets/efe6f950-0868-48ea-934a-807c421162b6)
  
        
      1. For example, PATH=%PATH%;E:\\Dev\_Libs\\opencv\\build\\x64\\vc16\\bin;E:\\Dev\_Libs\\boost\_1\_87\_0\\lib64-msvc-14.3

   

2. Setup the command-line parameters (for example):  
   1.![image](https://github.com/user-attachments/assets/b15657e8-d3d3-4c8f-a652-b6273d49e017)

     
   2. For example, the following is a good starting place for stepping through how the Pi1/Camera 1 system processes the golf ball images.  
      1. \--show\_images 1 \--lm\_comparison\_mode=0 \--logging\_level trace   \--artifact\_save\_level=all \--wait\_keys 1 \--system\_mode camera1\_test\_standalone  \--search\_center\_x 800  \--search\_center\_y 550  
   3. The above set of command line params is a good setting for debugging, because the “--wait\_keys 1” will allow you to step through the intermediate debugging images by hitting any key to continue, but without having to step through all the code that creates those images. It’s useful to set a breakpoint just before where you think there’s a problem, and then let the program run, but sort of throttle the system by having the wait\_keys setting allow you to visually understand what happened before that break point (at a high level).  
      1. NOTE \- The key-press pauses for the images can sometimes interfere with the debugging console window (which is also processing keystrokes).  
   3. The :” \--search\_center\_x 800  \--search\_center\_y 550” tells the system where to look for the teed-up ball.  It will have to be set consistent with whatever teed-up-ball-image you’re using here.  
   4. Set Tools-\>Options-\>Debugging-\>Automatically close the console so the console window (and any important messages) does not go away immediately at the end of the program.

   

2. To test, compile everything with Ctrl-Shift-B and then hit F10 to step into the first line of the program and see where it goes from there\!

**Example use of the Windows/Mac Visual Studio Development Platform**

Let’s try a real-life example from a problem in a relatively brightly-lit test environment where the system was not locating all of the balls that it should have.  For this test, the config file logging parameters look like:

"gs\_config": {

"logging": {  

	"kLogIntermediateExposureImagesToFile": 1,  

	"kLogIntermediateSpinImagesToFile": 0,  

	"kLogWebserverImagesToFile": 1,  

	"kLogDiagnosticImagesToUniqueFiles": 0,  

	"kLinuxBaseImageLoggingDir": "/home/\<PiTracUserName\>/LM\\\_Shares/Images/",  

	"kPCBaseImageLoggingDir": "M:\\\\Dev\\\\PiTrac\\\\Software\\\\LMSourceCode\\\\Images\\\\"  

},

With those settings, a particular shot produced on an actual PiTrac generated the following image files in \~/LM\_Shares/Images on the Pi’s.  If you don’t have any images, there are some similar examples in the PiTrac/Software/Images directory.

log\_cam2\_last\_strobed\_img:  
![image](https://github.com/user-attachments/assets/ea72116e-be62-4c55-9a48-7feb75c2e121)


ball\_exposure\_candidates:  
![image](https://github.com/user-attachments/assets/2b4a6aa1-0824-4bef-b8dd-4498ba0bf350)


Spin\_ball\_1\_gray\_image1 and spin\_ball\_2\_gray\_image1:  
![image](https://github.com/user-attachments/assets/4f104b52-071c-435d-accf-5164e31ccc33)![image](https://github.com/user-attachments/assets/882238d3-1924-45cc-9fc6-e6e2aaac7b94)



You should also have an image of where the teed-up ball was, as this particular example is going to try to go through all the processing, including calculating where the ball was initially (and then, later, in comparison to the strobed balls.  A typical starting image is something like:  
gs\_log\_img\_\_log\_ball\_final\_found\_ball\_img.png

![image](https://github.com/user-attachments/assets/e1aec32b-2f87-4d34-b1cb-c899aa610b48)
 (we often just reuse some old starting image if we’re just focused on debugging the strobed image processing.  
NOTE \- The teed-up picture does have some interaction with the downstream strobed-image processing.  The teed-up image gives the system an idea of how far from the monitor the ball is starting.  That Z-axis value is then used to choose a range of ball-radii to be looking for in the strobed picture.

The strobed picture looks fairly solid, but at least two questions come to mind:

1. Why didn’t PiTrac identify the two ball images between No. 3 and No. 4?  
2. Why did the system decide to compare balls 1 and 2 for spin analysis instead of 2 and 3 which appear to be clearer?  In particular the spin\_ball\_1\_gray\_image1 has some weird coloration, probably from some overlap from golfer’s leg behind the ball.

In order to figure this out, we’ll use the picture from cam 2 as input to the processing pipeline and then follow that pipeline to figure out what’s wrong.

The following instructions are for Visual Studio 2022 in Windows, but should be similar for other platforms.

1. Move the relevant images (above) to whatever your kPCBaseImageLoggingDir is.  
     
2. Open the Visual Studio solution for PiTrac.  This is the file PiTrac.sln in PiTrac/Software/  
     
3. Tell the system about what image(s) you are going to use as test input. For our case, where we’re using the above images, we’ll set both of the test input files to the same file name.  In the golf\_sim\_config.json file, set:  
     
   1. "testing": {  
      1. "kTwoImageTestTeedBallImage": "gs\_log\_img\_\_log\_ball\_final\_found\_ball\_img.png",  
   2. "kTwoImageTestStrobedImage": "log\_cam2\_last\_strobed\_img.png",

   

4. In the lm\_main.cpp file, setup the code to run a “testAnalyzeStrobedBalls()” test.  That test will basically go through all of the processing that the Pi 1 system would do once it received an image from the Pi 2 system.  
     
   1. Near the bottom of the run\_main function (\~line 1653\)  
   2. // Comment in whichever tests you want to run in Windows here    
   3. testAnalyzeStrobedBalls();  

   

2. Set a break-point at the start of the testAnalyzeStrobedBalls() function and hit F5 to run.  
     
   1. NOTE \- You may have to do a little debugging to make sure the code gets to that function first.  
   2. ![image](https://github.com/user-attachments/assets/30d04ce0-45ee-44ce-ae89-540a7e912cae)


   

2. Another good place to put a break-point to start is here at bool GolfSimCamera::ProcessReceivedCam2Image  
     
3. ![image](https://github.com/user-attachments/assets/c261ba8a-b0a9-4015-aa11-67e38f6c924c)

     
4. After stepping through the Teed-up ball processing in GetCalibratedBall(), the system will start into the strobed image processing.  
     
5. In our case, the initial identification ball circles (which uses a technique called Hough identification) results in the following:  
     
6. ![image](https://github.com/user-attachments/assets/0d74d288-ee73-4c1d-9a5d-2da3c8516cfb)
  
     
7. This is ***not*** a good start.  It’s likely there will be some missed-ball identifications, such as those near the bottom of the screen (which should be filtered out later), but we may want to see if we can provide better settings to improve this starting point.  
     
8. To help out debugging and tuning there are some “Playground” projects that we’ve created that allow a user to try out different settings for the Hough Circle Detection and other processes.  These are essentially copies of some of the pertinent code from the main project into a little project that is easier to manipulate.  
     
9. To use the playgrounds effectively, you’ll first want to take the parameters that are being used in the PiTrac process that you’re debugging and then import those settings into the Playground so that you can, well, ‘play’ with them to figure out how to get a better result.  
     
10. In our case, we set a breakpoint at the call to cv::HoughCircles() in ball\_image\_proc.cpp, or else just make sure debug logging is set to “trace”, and look in the resulting trace log for the parameters that are printed out just before the HoughCircles call.  
      
    1. \[2024-12-30 11:20:37.495892\] (0x00005aa8) \[trace\] Executing houghCircles with currentDP \= 1.700000, minDist \= 18, param1 \= 130.000000, param2 \= 96.000000, minRadius \= 48, maxRadius \= 120  
    2. Note that the system uses an adaptive technique where some of the more sensitive parameters such as param2 are adjusted until there are a reasonable number of circles being returned (usually around 20, but this can be configured in the PiTrac .json file).

Hough Circle Detection Playground:

2. In the “main” method of playground.cpp, set kBaseTestDir and the kTestImageFileName variable to an image of interest.  For example, the same image and test directory we were just using above.  
     
3. Next set the initial trackbar values to the same values used in the HoughCircles call, above.  This is done by setting the similarly-named constants near the top of the Playground.cpp file such as:  
     
   1. int kStrobedEnvironmentHoughDpParam1Int \= 17;  //  Will be divided by 10 if using HOUGH\_GRADIENT\_ALT.   Either 1.7 or \*1.8\* tremendously helps the accuracy\! 1.9 can work ok for external.  1.3 is good for stationary ball (cam1)  
   2. int kStrobedEnvironmentBallCurrentParam1Int \= 130;  
   3. int kStrobedEnvironmentBallMinParam2Int \= 28; // 60 for external  
   4. int kStrobedEnvironmentBallMaxParam2Int \= 140;  
   5. int kStrobedEnvironmentBallStartingParam2Int \= 96;  
   6. int kStrobedEnvironmentBallParam2IncrementInt \= 4;  
   7. int kStrobedEnvironmentMinimumSearchRadiusInt \= 48; // 59; //  60;  // gets bad fast as this widens.  I'd try 56 and 85 for now  
   8. int kStrobedEnvironmentMaximumSearchRadiusInt \= 120;  // 148;  // 80;

   

2. When you run the playground, several windows will come up, showing the pre-HoughCircles filtered image and the resulting identified circles.  This image should be identical to the image you would see in the main PiTrac code (assuming the Playground code is the same in relevant part).  
     
3. At this point, you can try changing the code (in this case we changed the “HOUGH::GRADIENT” method to be “HOUGH::GRADIENT\_ALT” instead) moving the sliders in the control window to see what effect the altered parameters have on the results.  For our example, we found that changing the value of the “” helps a lot.  
     
4. The result was:  
     
   1. ![image](https://github.com/user-attachments/assets/0b3e141c-ec35-4d35-8436-3766a87d4988)


   

2. Finally, the new parameters are pushed into the golf\_sim\_config.json file to see how they work in the actual PiTrac LM.  Note that some of the values are scaled, such as the Hough Param2, which is 100 times smaller for the HOUGH::GRADIENT\_ALT method, and the DpParam1 which is always 10 times smaller.  
     
   1. "kStrobedBallsUseAltHoughAlgorithm": 1,


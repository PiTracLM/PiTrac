**PiTrac \- Debugging and Code Walk-Throughs (Non-Pi Build/Run Environment)**

**TBD \- THIS DOCUMENT IS UNDER CONSTRUCTION**

This document is for folks who:

* Saw some unexpected results from PiTrac and want to figure out what went wrong.    
  * For example – Why is the calculated speed 659.75 mph?  Did I really hit the ball with a forward spin of 454,623 RPM?  Why didn’t the system “see” my shot correct?  And so on…  
* Want to step through an example in the code to see how it works.  Or doesn’t work.  Sometimes the latter. :/  
* Would like to test some improved algorithms for ball identification or spin analysis, etc. without doing so on the actual Pi-based LM.

Running the system in Windows, however, is something you’d only do if you’re reasonably proficient with C++, Visual Studio, and debugging work in general.  And we hope there’s some of those folks on the project\! 🙂

So how does this work?  Well, the primary work flow is:

1. Crank up the logging in the system running on the Pi’s in order to produce more intermediate image artifacts so we can figure out where things go wrong.  
2. Use some of those images as input to a debugging version of the system.  The debugging environment is typically running in Visual Studio on a PC, or a Mac-based system.  
3. Use the debugging environment to bypass having to use the cameras to take a picture, and instead focus on the post-picture processing.  In this environment, it’s easy to step through the code, produce additional image artifacts, and generally figure out what’s going on.  
4. Try different processing parameters (in golf\_sim\_config.json or elsewhere) and/or change the source code to make improvements.  
5. If necessary, use one of the workbench programs to play with the processing parameters in real time to see what works best.  Then take those settings an import into PiTrac

Prerequisites:

In order to build on a non-Pi environment, you’ll need to setup a few things.  These instructions will focus on Windows, but the Mac should be similar.

1. Install Visual Studio (we’re using the 2022 Preview currently)  
2. Install OpenCV 4.10  
   1. You can just download the binaries at [https://opencv.org/releases](https://opencv.org/releases).  Building the source locally has its advantages for debugging, but takes a while.  Instructions are [here](https://docs.opencv.org/4.x/d3/d52/tutorial_windows_install.html)  
   2. [This site](https://christianjmills.com/posts/opencv-visual-studio-getting-started-tutorial/windows/) has some good information on installation and setup	  
3. Install Boost  
4. Setup the Windows VS project to point to the correct include and library directories  
   1. In VS, go to Project/ImageProcessing Properties:  
   2. ![image](https://github.com/user-attachments/assets/cf670299-aaa0-405b-849b-5c13e3043f29)

   3. Point to the correct libraries:  
   ![image](https://github.com/user-attachments/assets/c0c19c80-51ae-4ecc-b84b-5436bf8a0dd1)

      1. For example, E:\\Dev\_Libs\\opencv\\build\\x64\\vc16\\lib\\\*d.lib;$(CoreLibraryDependencies);%(AdditionalDependencies)    \[NOTE \-The \*.d is for debug libraries.  If not debugging, then remove the “d”.  And do not mix \* and \*d \!  
   5. Point to the correct run-time binaries and libraries:  (and note that the suggested opencv instructions cited above do not use this step and instead copy files around (less desirable)  
   6. ![image](https://github.com/user-attachments/assets/91633d86-a14b-41ab-9052-25cf97ac28de)
 
      1. For example, PATH=%PATH%;E:\\Dev\_Libs\\opencv\\build\\x64\\vc16\\bin  
5. Setup the command-line parameters (for example):  
   1.![image](https://github.com/user-attachments/assets/d3ba7ab7-e58d-400d-a15a-55221635c409)
  
   2. For example, the following is a good starting place for stepping through how the Pi1/Camera 1 system processes the golf ball images.   
      1. \--show\_images 1 \--lm\_comparison\_mode=0 \--logging\_level trace   \--artifact\_save\_level=all \--wait\_keys 1 \--system\_mode camera1\_test\_standalone  \--search\_center\_x 800  \--search\_center\_y 550   
   3. The above is a good setting for debugging, because the “--wait\_keys 1” will allow you to step through the intermediate debugging images by hitting any key to continue, but without having to step through all the code that creates those images. It’s useful to set a breakpoint just before where you think there’s a problem, and then let the program run, but sort of throttle the system by having the wait\_keys setting allow you to see what happened before that break point (at a high level).  
      1. NOTE \- The key-press pauses for the images can sometimes interfere with the debugging console window (which is also processing keystrokes).  
   4. The :” \--search\_center\_x 800  \--search\_center\_y 550” tells the system where to look for the teed-up ball.  It will have to be set consistent with whatever teed-up-ball-image you’re using here.  
   5. Set Tools-\>Options-\>Debugging-\>Automatically close the console so the console window (and any important messages) does not go away immediately at the end of the program.  
6. To test, compile everything with Ctrl-Shift-B and then hit F10 to step into the first line of the program and see where it goes from there\!

Let’s try a real-life example from a problem in a relatively brightly-lit test environment where the system was not locating all of the balls that it should have.  For this test, the config file logging parameters look like:

"gs\_config": {

	"logging": {  
		"kLogIntermediateExposureImagesToFile": 1,  
		"kLogIntermediateSpinImagesToFile": 0,  
		"kLogWebserverImagesToFile": 1,  
		"kLogDiagnosticImagesToUniqueFiles": 0,  
		"kLinuxBaseImageLoggingDir": "/home/PiTracUserName/LM\_Shares/Images/",  
		"kPCBaseImageLoggingDir": "M:\\\\Dev\\\\PiTrac\\\\Software\\\\LMSourceCode\\\\Images\\\\"  
	},

With those settings, a particular shot produced the following two image files in LM\_Shares/Images on the Pi’s was:

log\_cam2\_last\_strobed\_img:  
![image](https://github.com/user-attachments/assets/0178f125-92cd-4581-9210-0feb651d8fc2)
 
ball\_exposure\_candidates:  
![image](https://github.com/user-attachments/assets/39bf3957-091f-4f27-8916-29b063d273fb)


Spin\_ball\_1\_gray\_image1 and spin\_ball\_2\_gray\_image1:  
![image](https://github.com/user-attachments/assets/aab16121-265c-483a-89f1-4449de5988e8)![image](https://github.com/user-attachments/assets/7418212b-dd5e-4c16-aa76-7a725ef69353)



You should also have an image of where the teed-up ball was, as this particular example is going to try to go through all the processing, including calculating where the ball was initially (and then, later, in comparison to the strobed balls.  A typical starting image is something like:  
gs\_log\_img\_\_log\_ball\_final\_found\_ball\_img.png

![image](https://github.com/user-attachments/assets/da606a12-4906-4a7d-b06b-b530374f3104)
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
   2. 	"kTwoImageTestStrobedImage": "log\_cam2\_last\_strobed\_img.png",  
        
4. In the lm\_main.cpp file, setup the code to run a “testAnalyzeStrobedBalls()” test.  That test will basically go through all of the processing that the Pi 1 system would do once it received an image from the Pi 2 system.  
   1. Near the bottom of the run\_main function (\~line 1653\)  
   2.     // Comment in whichever tests you want to run in Windows here  
   3.     testAnalyzeStrobedBalls();  
   4.   
5. Set a break-point at the start of the testAnalyzeStrobedBalls() function and hit F5 to run.  
   1. NOTE \- You may have to do a little debugging to make sure the code gets to that function first.  
   2. ![image](https://github.com/user-attachments/assets/d6f88d50-a284-446e-86bc-51e4d986d212)

6. Another good place to put a break-point to start is here at bool GolfSimCamera::ProcessReceivedCam2Image  
7. ![image](https://github.com/user-attachments/assets/b5428bf5-812b-4694-a616-8ee8ab8289f8)

8. After stepping through the Teed-up ball processing in GetCalibratedBall(), the system will start into the strobed image processing.  
9. In our case, the initial identification ball circles (which uses a technique called Hough identification) results in the following:  
10. ![image](https://github.com/user-attachments/assets/9729d3aa-a744-4d9d-a19f-31ab2672c16b)
  
11. This is ***not*** a good start.  It’s likely there will be some missed-ball identifications, such as those near the bottom of the screen (which should be filtered out later), but we may want to see if we can provide better settings to improve this starting point.  
12. To help out debugging and tuning there are some “Playground” projects that we’ve created that allow a user to try out different settings for the Hough Circle Detection and other processes.  These are essentially copies of some of the pertinent code from the main project into a little project that is easier to manipulate.  
13. To use the playgrounds effectively, you’ll first want to take the parameters that are being used in the PiTrac process that you’re debugging and then import those settings into the Playground so that you can, well, ‘play’ with them to figure out how to get a better result.  
14. In our case, we set a breakpoint at the call to cv::HoughCircles() in ball\_image\_proc.cpp, or else just make sure debug logging is set to “trace”, and look in the resulting trace log for the parameters that are printed out just before the HoughCircles call.  
    1. \[2024-12-30 11:20:37.495892\] (0x00005aa8) \[trace\] Executing houghCircles with currentDP \= 1.700000, minDist \= 18, param1 \= 130.000000, param2 \= 96.000000, minRadius \= 48, maxRadius \= 120  
    2. Note that the system uses an adaptive technique where some of the more sensitive parameters such as param2 are adjusted until there are a reasonable number of circles being returned (usually around 20, but this can be configured in the PiTrac .json file).

Hough Circle Detection Playground:

1. In the “main” method of playground.cpp, set kBaseTestDir and the kTestImageFileName variable to an image of interest.  For example, the same image and test directory we were just using above.  
2. Next set the initial trackbar values to the same values used in the HoughCircles call, above.  This is done by setting the similarly-named constants near the top of the Playground.cpp file such as:  
   1. int kStrobedEnvironmentHoughDpParam1Int \= 17;  //  Will be divided by 10 if using HOUGH\_GRADIENT\_ALT.   Either 1.7 or \*1.8\* tremendously helps the accuracy\! 1.9 can work ok for external.  1.3 is good for stationary ball (cam1)  
   2. int kStrobedEnvironmentBallCurrentParam1Int \= 130;  
   3. int kStrobedEnvironmentBallMinParam2Int \= 28; // 60 for external  
   4. int kStrobedEnvironmentBallMaxParam2Int \= 140;  
   5. int kStrobedEnvironmentBallStartingParam2Int \= 96;  
   6. int kStrobedEnvironmentBallParam2IncrementInt \= 4;  
   7. int kStrobedEnvironmentMinimumSearchRadiusInt \= 48; // 59; //  60;  // gets bad fast as this widens.  I'd try 56 and 85 for now  
   8. int kStrobedEnvironmentMaximumSearchRadiusInt \= 120;  // 148;  // 80;  
3. When you run the playground, several windows will come up, showing the pre-HoughCircles filtered image and the resulting identified circles.  This image should be identical to the image you would see in the main PiTrac code (assuming the Playground code is the same in relevant part).  
4. At this point, you can try changing the code (in this case we changed the “HOUGH::GRADIENT” method to be “HOUGH::GRADIENT\_ALT” instead) moving the sliders in the control window to see what effect the altered parameters have on the results.  For our example, we found that changing the value of the “” helps a lot.  
5. The result was:  
   1. ![image](https://github.com/user-attachments/assets/787be451-5360-427f-98fc-5d484c8d936b)
  
6. Finally, the new parameters are pushed into the golf\_sim\_config.json file to see how they work in the actual PiTrac LM.  Note that some of the values are scaled, such as the Hough Param2, which is 100 times smaller for the HOUGH::GRADIENT\_ALT method, and the DpParam1 which is always 10 times smaller.  
   1. "kStrobedBallsUseAltHoughAlgorithm": 1,  
   2.   
   3.   
   4. 


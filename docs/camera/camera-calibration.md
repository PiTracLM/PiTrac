---
title: Camera Calibration
layout: default
nav_order: 1
parent: Camera
---

# PiTrac - Camera Calibration

![][image1]      ![][image2]    
(the above pictures feature PiTrac in its early incarnation using 2x4s for its enclosure)

Camera 2 Calibration Setup:  
![][image3]![][image4]![][image5]

**Camera Setup & Calibration Guide**

{: .warning }
🔧 **Difficulty:** Camera calibration is noted in the project errata as being "too difficult and takes too much time." Consider using the [Auto-Calibration](auto-calibration.md) process instead for a more streamlined experience.

NOTE:  As of May 6, 2025, the portion of this guide that deals with measuring the focal length and camera angles is deprecated.  The focusing and de-distortion information is still correct (and necessary).  However, once the cameras are focused and you have generated the de-distortion matrices and entered them into your golf_sim_config.json file, please perform the rest of the calibration by following the [Auto-Calibration Instructions](auto-calibration.md).
1. Overview  
   1. The PiTrac system needs to know the angles of the cameras and the relative distances between them in order to locate the ball in space.  Camera calibration helps establish these values.  
   2. During this calibration, various distances are measured from each camera to a test point in front of the camera.  Those values are used, along with the calibration spreadsheet (which computes additional, derived values) to determine a set of calibration values.  Those values are then given to the PiTrac monitor by entering them in the golf\_sim\_config.json configuration file, which PiTrac reads when it starts up.
   
   📥 **[Download Calibration Spreadsheet](../assets/PiTrac-Camera-Calibration-Worksheet.xlsx)** - Excel worksheet for calibration calculations  
   3. The basic idea is to center a known point in 3D space in the camera lens and then measure the distances to that point.  That in turn determines the camera’s angles.  This process also establishes the effective focal distance of the camera, which in turn is used to establish the distance of the ball from the cameras.  
   4. After the marker points are used to aim the cameras, an actual golf-ball is used to calibrate focus and focal length.  
   5. It’s best to calibrate the cameras in the environment in which you expect to use the LM.   
   6. As part of the calibration process, the fish-eye-like distortions that are caused by most lenses need to be accounted for.  These distortions are particularly evident around the edges of an image.  Multiple images of a hand-held checker-board pattern are used to determine a couple of matrices that are used by the PiTrac to essentially “undistort” the images taken by the cameras.  
   7. The distances in this guide are in meters unless otherwise specified.  
   8. Note that “calibration” as used in this document is a broader process than just the intrinsic/extrinsic calibration that sets up matrices to perform de-distortion on cameras (although undistortion is a part of it).  
2. Initial Setup  
   1. Use a lens-cleaning cloth and appropriate cleaning liquid to make sure the lens are spotless.  
   2. If possible, make sure the Pi computers are hard-wired into the network and the network switch (if any) is turned on.  
   3. Before doing anything, log into the system remotely and perform a sanity check to make sure the camera is up and running correctly.  For example, take a picture using:  
      1. PiTracCameraTools/lcGS.sh test.jpg  
      2. So long as a picture is taken, things are probably operating correctly.  
   4. Attach a keyboard, mouse and monitor to whatever Pi Camera is being calibrated using the front and side Pi Access ports on the monitor’s walls.    
      1. If your Pi is set up to display a terminal clearly on a different computer (such as using VNC viewer), you may be able to avoid this connection.  
   5. If the monitor does not come up with an image after the mouse is wriggled, it may be that the system was booted at a time that the monitor was not present.  In that case, reboot.  
   6. Place a small piece of transparent tape onto your monitor screen, with a dot on it so that the dot is positioned to be at the exact center of the screen.  This dot will be referred to as the “marker dot”, and its role is simply to indicate where the center of the image is.  If this dot is at the same point as some feature that the camera sees, that feature is in the center of the camera’s view. Some people will use a remote terminal program like VNC Viewer instead of connecting a monitor directly to the Pi.  In this case, just make sure that you monitor aspect ratio and any window borders will not skew the true center calibration spot.  
   7. Loosen the thumb screws on the lenses (if necessary) and rotate the lens to the full-open aperture position.  For the 6mm lens in the parts list, the aperture screw is the one furthest from the camera.  Tighten the aperture screw back down and loosen the focus screw.  
3. Camera 2  
   1. Note \- Camera 2 closely follows the Camera 1 calibration  
   2. Lens Un-Distortion  
      1.   
   3. Measuring Distances and Angles  
      1. Remove the visible-light cut (long pass) filter from the front of the camera for now.  It’s easier to calibrate the camera in the visible light spectrum.  
      2. Ensure that the triggering for the camera is not in external mode (where it usually will be when the camera2 system is running).    
         1. If necessary, do:    sudo \~/CameraTools/setCameraTriggerInternal.sh   
      3. Set up a marker point (such as a cross hairs) on a piece of paper attached to an object or stand so that the point is about 15 cm above the ground and roughly centered in the view of the camera with the camera at a slight upward tilt and facing straight out.  The point should be a shape that will be crisp, sharp, and useful for focusing the camera.  
      4. Follow the instructions for Camera 1 (read that first), below, but use the following modifications when recording the various measurements:  
      5. ![][image6]  
      6. Camera 2 Measurements (values are typical/exemplary)  
      7. \-X (the negative value of X) is the distance to the left (looking at the monitor) from the point on the floor directly under the center of *camera 2* (facing the LM as a right-handed golfer) to the marker dot. ***It’s usually 0*** because the point will be centered (horizontally) in the camera’s view. Because this is relative to the OTHER camera, X will usually effectively be \~-3 or –4 centimeters away from the other camera because the camera 1 is offset because it is twisted.    
         1. This will be made up later when we set the "kCamera2OffsetFromCamera1OriginMeters" parameter: \[ **0.03**, \-0.19, 0.0 \],  
         2. X \= 0.0 (typical)  
      8. Y is the distance from the center of the camera 2 lens to the level of the hitting mat (for camera1)  
         1. Y \= .109  (typical)  
      9. Z is the distance ***straight out horizontally-level*** from the front of the lens to the vertical plane of the marker.  This measurement is ***not*** “as the crow flies” in a straight line from the lens to the marker point.  See pictures.  The front of the camera lens is \~1cm back from the bottom of the base, which may be helpful if you are measuring from the bottom of the base.  
         1. Z \= .55 (typical)  
      10. E1 is the elevation from the floor/mat to the marker point in the air.  Somewhat above the Y value.  
          1. E1 \= 0.16  
      11. E2 is the elevation from camera 2 to the marker point in the air.  E2 will be entered into the PiTrac JSON configuration file as the 2nd parameter for e.g.,  "kCamera2PositionsFromOriginMeters": \[ 0.0, **\-0.051,** 0.46 \]  
          1. E2 \= E1 \- Y \= 0.051.  
          2. This will help calculate the YDeg angle.  However, because of measurement inaccuracy, it may still be necessary to manually adjust that angle. \[TBD\]  
      12. H is the distance from the camera lens directly (as the crow flies) to the marker point.   
          1. H \= .56  
      13. In the Calibration Spreadsheet, X\_Degrees will normally be 0 (camera facing straight out).  Conversely, Y\_Degrees \= ATAN(E-Y) and will usually be around 5 degrees (camera tilted slightly upward in order to catch fast-rising balls) (5.3 degrees for the above example values)  
      14. Otherwise, this process same as for camera 1, but due to the (typical) either horizontal or up-facing camera, will have to create a marker-aiming spot in mid-air by drawing on a box or something that can be propped up in front of the camera2  
      15. Enter the following data into the JSON file:  
          1. "calibration.kCamera2CalibrationDistanceToBall": \<Z\>  (e.g., 0.55)  
          2. "cameras.kCamera2PositionsFromOriginMeters": \[ \<X\>, \-\<E2\>, \<Z\> \]  
          3. "kCamera2Angles": \[ \<XDeg\>, \<YDeg, e.g. 6.3 degrees\> \],  (from spreadsheet)  
   4. Calibrate Camera Focal Length  
      1. Calibration requires that the LM be turned on.  When you do so, first check to make sure that the LED strobe is not on.  You can tell if it is on because the LEDs will glow a dull red (even for IR LEDs, there’s a little light on the high-frequency end of the visible spectrum).  
      2. Next we will calibrate the focal length of the camera 2 by using the known diameter of a golf ball and the diameter of the ball as seen by the camera in terms of pixels.  
         1. Use the “Determine the camera’s focal length” process for camera 1, below  
      3. Run “runCam2BallCalibration.sh” script and take the resulting average focal length.  Make sure the ball is well lit with a good contrasting color behind it (use the preview function if necessary)  
         1. Certain types of black felt also show up black behind the ball and create great contrast.  
         2. If you get errors, such as “Attempted to draw mask area outside image”, check to ensure the golf ball is in the middle of the camera image.  
         3. Check for the images in the “logging.kLinuxBaseImageLoggingDir” directory.  Can also try \--logging\_level=trace instead of info in the script.  
         4. After the final average value has been computed, you may have to hit Ctrl-C to stop the program  
         5. Also, the “Calibrated focal length” measurement that is output by the program should remain pretty close to the same number from measurement to measurement.  So, for example (ignore the “mm”),   
            1. \[2024-12-05 15:50:42.048685\] (0x0000007fb6135040) \[info\] Calibrated focal length for distance 0.550000 and Radius: 67.788940 is **6.094049**mm.  
            2. \[2024-12-05 15:50:42.048996\] (0x0000007fb6135040) \[info\] Focal Length \= **6.094049**.  
            3. \[2024-12-05 15:50:43.441564\] (0x0000007fb6135040) \[info\] Calibrated focal length for distance 0.550000 and Radius: 67.803833 is 6.095388mm  
            4. If the value is varying largely, the lighting may not be sufficient.  
      4. Enter this value into the JSON file, e.g., "kCamera2FocalLength": 6.155454 (or whatever the value is).  
   5. Confirm Correct Camera Calibration  
      1. Keep the ball in the spot it was in when the focal length was calibrated for now, as the location is known.  We want to ensure the system is ‘finding’ the ball at that location.  
      2. Run “runCam2BallLocation.sh” script on the Pi connected to the camera being calibrated  to make sure the angles and distances and processing are correct  
         1. Generally, the IR filter should be removed if the ball does not have good contrast with the background.  
         2. The ball-search location should work in the center:   \--search\_center\_x 723 \--search\_center\_y 544 .  If the script doesn’t return values, check the log\_view\_final\_search\_image\_for\_Hough.png file.  
         3. Remember, that the distances will be approximately to the center of the ball FROM THE CAMERA, so will be a little shorter than the distance to the LM in the X direction.  
         4. Check to make sure that the X, Y , and Z values (in meters) are correct.  
         5. Also, the “Radius” measurement that is output by the program should remain pretty close to the same number from measurement to measurement.  So, for example,   
            1. \[2024-12-05 15:43:57.884875\] (0x0000007f8ea3d040) \[info\] Found Ball \- (X, Y, Z) (in cm): 0.013771, 0.060536, 0.552454. **Radius: 67.741455**  
            2. \[2024-12-05 15:44:00.758463\] (0x0000007f8ea3d040) \[info\] Found Ball \- (X, Y, Z) (in cm): 0.013772, 0.060538, 0.552480. **Radius: 67.738266**  
            3. \[2024-12-05 15:44:03.616166\] (0x0000007f8ea3d040) \[info\] Found Ball \- (X, Y, Z) (in cm): 0.013772, 0.060539, 0.552489. **Radius: 67.737129**  
         6. To ensure everything is working correctly, trying moving the ball 10cm up  and/or left or right and/or closer or further from the camera and then ensure that the runCam2BallLocation program responds with correct position information.  Accuracy should be at least within a centimeter for most measurements.

   

4. Camera 1 distances and angles (also referenced by and relevant for Camera 2 process, above)  
   1. Only for Camera 1, plug in the exterior LED strip at the bottom of the launch monitor.  For these calibration steps, the more light the better.  
   2. Start the previewGS.sh script (which runs libcamera.hello) for cam1, or previewGS\_noir.sh script for cam2.  This will allow you to see the marker in real time as the camera is positioned and focused.  
   3. Pick an appropriate nominal tee-up spot to use as the calibration marker point for camera 2\.  The exact placement is not important, but positioning the ball near where it will be teed should help make the calibration more accurate.  
      1. Typical is 60cm to the right and around 50-60 cm in front of the LM (looking at the LM from where the player would stand, looking down into its cameras).  Other distances can work as well, but the balance is between being close enough to get a good view of the ball, and far enough that the field of view is sufficiently broad to capture enough (at least 4) image imprints as the strobe light fires.
   4. Use a tape measure to accurately determine the position from the front-center of the LM to the point where the ball will be expected to be teed up as follows:  
      1. Note \- The ball will likely be right in front of camera2 (and a 10-20 centimeters in the air), but for camera 1, it will be a couple feet to the right of camera1 and on the ground.  
      2. Further back (to the right of the LM for right-handed golfers) gives more time for the LM to “see” a fast ball in the camera2 before it goes too far.  The LM should be able to operate regardless of exactly where the marker point is, but a point close to the typical tee-off makes the calibration in that area more accurate.  
      3. Place a marker (e.g., a sticky-note with a 3mm dot or crosshair) at the point on the ground where the nominal tee point will be.  If you are calibrating on a hitting mat.  A short bit of wire insulation pushed into the hitting mat works well, too.  The point just has to be well enough marked to be able to see it in the center of the preview screen.  
      4. Camera 1 Example Setup:  
         1. ![][image7]  
   5. Loosen and then move the camera mount until the preview screen on the monitor shows the marker dot directly behind the taped-on dot on the monitor.  The idea is to center the marker in the camera view so that the camera is known to be aimed directly at the spot.  Continue to keep the point centered as you tighten the gimbal screws (a small 90-degree M4 hex wrench makes this easier).  
      1. **NOTE** \- Sometimes the mount is sticky and only wants to point to a particular point near, but not at the marker.  In that case, let the camera do what it wants to do, and just re-measure where the point is (it might be a few millimeters one way or the other), and then use those measurements, below.  
      2. When tightening the base of the Pi Camera 1 (top floor), you may have to loosen and move the strobe light that is immediately below the camera to get room to use a needle-nose pliers to tighten the bold.   
   6. Measure the following distances from the camera being calibrated to the marker.   **NOTE** \- these values have slightly different definitions for camera 2 \- reference that process elsewhere in this document when calibrating that camera.  
      1. Refer to the following diagram regarding the necessary calibration measurements for Camera 1:  
      2. ![][image8]  
      3. X is distance to the right of the LM (facing the LM as a right-handed golfer) to the marker point  
         1. X \= .60 (typical) (distances here are in meters)  
         2. For camera 2, the X should reflect the effective X-axis difference between the two cameras (because camera 1 is twisted to one side from center).  So, even if the camera 2 is centered, it’s X is likely to be 3 or 4 cm.  
         3. The “Origin” referenced in the .json file (e.g., kCamera1PositionsFromOriginMeters is technically arbitrary, but this system considers it as the point on the floor directly below where the camera 1 (or 2\) is focused.  
      4. Y is the distance from the middle of the camera1 lens to the level of the hitting mat (for camera1) or the aiming point (for camera2).  
         1. Y \= .275 (typical)    
      5. Z is the distance straight out from the front of the LM unit to the plane of the marker  
         1. Z \= .56  
      6. R is the distance from the point on the floor directly below the front center of the camera lens to the marker point on the floor (see diagram above).  This is one side of a triangle, with the other two sides being the line from the camera to the point on the floor, and the third side as “H”, below.  
         1. R \= .83  
         2. NOTE \- because of the tilt angle of Camera 1, the point on the floor will technically be hidden below the base of the LM.  Usually, we just measure from the edge of the base and add the additional couple of centimeters to make up for the offset.  For the same reason, the X distance on the floor for that camera will be a few centimeters less than the same point would be for camera 2   
      7. H is the distance from the camera lens direct (as the crow flies) to the marker point  
         1. H \= .87  
      8. Set the X, Y and Z values in the configuration .JSON file: (below values are typical)  
         1. (in “cameras” section) "kCamera1PositionsFromOriginMeters": \[ \<X\>, \<Y\>, \<Z\> \] for example, \[0.60, 0.275, 0.56 \],   (x,y and z in cm), and  
         2. "kCamera1Angles": \[ \<XDeg\>, \<YDeg\> \], for example, \[46.97, \-17.44\]  (from spreadsheet)  
   7. Determine the camera’s focal length  
      1. Place a ball on or at the marker.    
         1. For Camera 1, the ball will be immediately above the marker.    
         2. For Camera 2, create a support that will hold the ball so that its center in space is where the marker point existed in space before it was replaced by an actual ball.  Put the   
         3. Example setups:  Camera 2:  
         4. ![][image9]  
         5. Camera 1:  
         6. ![][image10]  
      2. Ensure that the area around the ball has good contrast.  For example, put down some black felt around the ball or use a white ball on a green hitting mat.   
      3. For Camera 1, turn the LED strip on the LM base on to make sure there’s sufficient light for good exposures.  
      4. Re-Measure H to ensure it’s correct \- it is the distance on a line straight out from the camera lens to the ball center (at or near where the marker point was)  
      5. Set the H distance into the appropriate parameter in the “calibration” section of the golf\_sim\_config.json file:   
         1. For camera 1, "kCamera1CalibrationDistanceToBall".  \~87cm is typical   
         2. For camera 2, "kCamera2CalibrationDistanceToBall". \~55cm is typical  
      6. For camera 2, install the light filter and holder on the lens.  
      7. Focus the lens as well as possible and lock in the focus by tightening the thumb screw closest to the camera (the other screw should already have been tightened).  Using the brand-marking or number printed on the ball can help this process.  This will establish the focal distance in the next step.  
      8. IF NOT ALREADY DONE AT LEAST ONCE, PERFORM CAMERA LENS UNDISTORTION PROCESS (bottom of this document)  
      9. Ensure the ball is well-lit, especially near its bottom.  
         1. For camera 2, getting as much sunlight in as possible can help provide sufficient IR to see well, or an incandescent light can also help.  
      10. Run the “runCam1Calibration.sh” or “runCam2Calibration.sh” script to get the focal length.    
          1. It will take multiple measurements and average them.  
          
          ⏱️ **Time estimate:** About 1 minute for measurement averaging  
      11. Set the resulting focal length into the .JSON file.    
          1. E.g., "kCamera1FocalLength": 5.216    would be typical  
   8. Determine the x & y (pan and tilt) camera1 angles for the configuration .JSON file  
      1. \[See [https://docs.google.com/spreadsheets/d/1igc2V7oq-hGpFP5AKGjyzc2osLVpYHp969iPyHGttJ8/edit\#gid=423758471](https://docs.google.com/spreadsheets/d/1igc2V7oq-hGpFP5AKGjyzc2osLVpYHp969iPyHGttJ8/edit#gid=423758471) for automatic calculations\]  
      2. X/pan is positive as the camera twists to face back to where the ball is teed (as the camera goes counter-clockwise viewed from above the LM).    
      3. Y/tilt  is negative as the camera starts to face down.   
      4. The angles are measured from the bore of the camera if it were facing straight out at no angle and level with the ground  
      5. XDeg \= 90 \- atan(Z/X)   YDeg \= \-(90 \- asin(R/H))   OR, for camera2, YDeg \= ATan(Y/R), e.g, atan(4.5/40) \= 6.42.   
         1. For example, for camera 1:  
            1. XDeg \= 56.31 , YDeg \= \-24.46  
         2. For example, for camera 2:  
            1. X \= \-0.03  Y \= 0.13  Z \= 0.40   H \= 0.42  R \=    
      6. Set the values in the configuration .JSON file:  
         1. (in “cameras” section) "kCamera1Angles": \[ 54.7, \-22.2 \],   (x,y  or pan, tilt)  
   9. Place a ball ball near the mark on the tee-up spot so that there is a straight line from the camera to the mark that runs through the center of the ball (thus the ball will be slightly in front of the marker)  
   10. Measure the distances to the center of the ball  
   11. In the JSON file, set calibration.kCamera1CalibrationDistanceToBall to the distance to the ball in meters, e.g,. 0.81   
   12. Run “runCam1BallLocation.sh” script to make sure the angles and distances and processing are correct  
       1. The ball-search location should work in the center:   \--search\_center\_x 723 \--search\_center\_y 544 .  If the script doesn’t return values, check the log\_view\_final\_search\_image\_for\_Hough.png file.  
       2. Remember, that the distances will be approximately to the center of the ball FROM THE CAMERA, so will be a little shorter than the distance to the LM in the X direction.

**Camera Lens “Undistortion” Process**

1. See, for example, [here](https://www.youtube.com/watch?v=H5qbRTikxI4) and [here](https://www.youtube.com/watch?v=_-BTKiamRTg) for background on this process.  
2. The undistortion code and scripts are under the CalibrateCameraDistortions folder.  The scripts rely upon the libcamera-still utility, so make sure that is working first.  
3. Print out a copy of the 7x10 square checker board included in that directory on a sheet of paper. Note that you want to print it to scale.  Mount the checkerboard on a piece of cardboard or slip it into a binder so that you can easily hold it and it will stay flat.    
4. When calibrating Camera 2, adding a bright incandescent light can provide additional IR light to help make the images clearer.  Also, change the tuning file in the take\_calibration\_shots.sh script from:  
   1.  \--tuning-file=/usr/share/libcamera/ipa/rpi/pisp/imx296.json  to  
   2.  \--tuning-file=/usr/share/libcamera/ipa/rpi/vc4/imx296\_noir.json (the \_noir is only applicable for Camera 2\)  
5. When calibrating Camera 1, ensure the room is well lit.  Turning on the LED strips on the LM often produces too much light and reflection.  
6. To take the pictures, make sure your camera is focused at the distance of where the ball will be.  It’s best to have the Pi console window visible so that you can see when to move the board and avoid blurs.   
7. Next, we need a bunch of images of the checkerboard at about the distance we expect the golf ball to be imaged by the cameras.  These images should include the board in the middle and nearer the edges of the images, and must each include all of the checkerboard, not just a part of it.  
8. Sample setup   
   1. ![][image11]  
9. Then, run the take\_calibration\_shots.sh script, specifying, for example, ./images/cam1 as the output directory.  Twenty pictures is usually sufficient.    
   1. → mkdir images/cam2  
   2. → take\_calibration\_shots.sh ./images/cam2/gs\_calibation\_ 20  
10. The script will repeatedly pause and take a picture and save it to the output directory.  Each time the script says “READY”, quickly move the checkerboard to a new location.  Each move, try to rotate the board a little bit about the image plane so that it is in a different orientation each time.  Also, try tilting the board a little forward and back randomly.  This should result in a set of images that are named gs\_calibrate\_\<nn\>, where nn is an increasing number.  The images should look like:  
11. ![][image12]![][image13]![][image14]![][image15]  
12. Look through the images and delete any where the checkerboard is not fully visible.  Partially-visible board can stall the next step of this process.  
13. Now that the pictures are ready, run the python processing script on those pictures to come up with the matrices that will be necessary to setup the PiTrac configuration .json file.  To run, do:  
    1. First, edit the python script to point to the images just gathered, for example:  
       1. images \= glob.glob('./images/cam1/\*.png')  
    2. → rm caliResult\*.png (to remove any earlier calibration sanity-check files)  
    3. Pick an output image to use as a test case:  
       1. cp images/cam2/gs\_calibation\_1.png test\_image\_for\_undistortion.png  
    4. → python  CameraCalibration.py  
    5. Alternatively, there is a Visual Studio .sln solution file that you can open up in Studio to step through the code if there is a problem.  
14. After processing, there will be several files output to the current working directory.  If you compare test\_image\_for\_undistortion and caliResult2.png, you can see the before and after results of applying the computed un-distortion matrices.  The un-distorted picture should have each checkerboard square look the same size and they should look square, not rounded.  The lines of rows and columns should be straight in the undistorted image.  caliResult1.png shows how the original image is “warped” to un-distort it  
15. If that looks good, take the information in the files distortion.txt and cameraMatrix.txt and copy the values into the two parameters below (changing ‘Camera2’ for ‘Camera1’ if this is for camera 2).  These parameters are in the “cameras” subsection of golf\_sim\_config.json:  
    1. "kCamera1CalibrationMatrix": \[  
    2. \[ 1.748506644661262953e+03, 0.000000000000000000e+00, 6.325374407393054526e+02 \],  
    3. \[ 0.000000000000000000e+00, 1.743341748687922745e+03, 4.075677927449370941e+02 \],  
    4. \[ 0.000000000000000000e+00, 0.000000000000000000e+00, 1.000000000000000000e+00 \]  \],  
    5. "kCamera1DistortionVector": \[  \-0.504763   0.173410   0.001554   0.000916   0.355220 \],  
16. Do the same for camera 1 and camera 2


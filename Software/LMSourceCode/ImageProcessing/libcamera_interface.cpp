
/*****************************************************************//**
 * \file   libcamera_interface.cpp
 * \brief  Main interface to both PiTrac cameras (using the libcamera library)
 * 
 * \author PiTrac
 * \date   February 2025
 *********************************************************************/

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#ifdef __unix__  // Ignore in Windows environment

#include <chrono>


#include <opencv2/calib3d/calib3d.hpp>

// TBD - May need to be before ball_watcher, as there is a mman.h conflict
#include "gs_ipc_system.h"


#include "ball_watcher.h"
#include "ball_watcher_image_buffer.h"
#include "still_image_libcamera_app.hpp"
#include "gs_club_data.h"

#include "image/image.hpp"

#include <boost/circular_buffer.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "gs_camera.h"
#include "camera_hardware.h"
#include "gs_options.h"
#include "gs_config.h"
#include "logging_tools.h"

#include <libcamera/logging.h>
#include "motion_detect.h"
#include "libcamera_interface.h"
#include "ball_image_proc.h"


namespace golf_sim {

    using lci = LibCameraInterface;


    uint LibCameraInterface::kMaxWatchingCropWidth = 96;
    uint LibCameraInterface::kMaxWatchingCropHeight = 88;

    double LibCameraInterface::kCamera1Gain = 6.0;
    double LibCameraInterface::kCamera1HighFPSGain = 15.0;
    double LibCameraInterface::kCamera1Contrast = 1.0;
    double LibCameraInterface::kCamera2Gain = 6.0;
    double LibCameraInterface::kCamera2ComparisonGain = 0.8;
    double LibCameraInterface::kCamera2StrobedEnvironmentGain = 0.8;
    double LibCameraInterface::kCamera2Contrast = 1.0;
    double LibCameraInterface::kCamera2CalibrateOrLocationGain = 1.0;
    double LibCameraInterface::kCamera2PuttingGain = 4.0;
    double LibCameraInterface::kCamera2PuttingContrast = 1.0;
    std::string LibCameraInterface::kCameraMotionDetectSettings = "./assets/motion_detect.json";

    long LibCameraInterface::kCamera1StillShutterTimeuS = 15000;
    long LibCameraInterface::kCamera2StillShutterTimeuS = 15000;

    // Default values are based on empirical measurements using a 6mm lens
    int kCroppedImagePixelOffsetLeft = -5;
    int kCroppedImagePixelOffsetUp = -13;

    // The system will start in a full-screen watching mode, but ensure 
    // we set it up once just in case
    LibCameraInterface::CropConfiguration LibCameraInterface::camera_crop_configuration_ = kCropUnknown;
    cv::Vec2i LibCameraInterface::current_watch_resolution_;
    cv::Vec2i LibCameraInterface::current_watch_offset_;

    LibCameraInterface::CameraConfiguration LibCameraInterface::libcamera_configuration_[] = { LibCameraInterface::CameraConfiguration::kNotConfigured, LibCameraInterface::CameraConfiguration::kNotConfigured };

    LibcameraJpegApp* LibCameraInterface::libcamera_app_[] = { nullptr, nullptr };

    bool camera_location_found_ = false;
    int previously_found_media_number_ = -1;
    int previously_found_device_number_ = -1;

    void SetLibCameraLoggingOff() {

        // Unless we want REALLY detail information, just tell libcamera to be quiet,
        // even for higher-level logging that libcamera might otherwise want to emit.

        if (GolfSimOptions::GetCommandLineOptions().logging_level_ != kTrace) {
            libcamera::logSetTarget(libcamera::LoggingTargetNone);

            /* TBD - Not working, so avoid the extra log message for now
                libcamera::logSetLevel("*", "ERROR");
                libcamera::logSetLevel("", "ERROR");
            */
            RPiCamApp::verbosity = 0;
        }
    }

    /**
     * \brief  Once a ball has been identified in the image, this method will continuously watch the
     * area where the ball is by taking images as quickly as possible and comparing each
     * image to the prior image.  See motion_detect_stage.cpp for details on detection.
     * As soon as movement is detected, signals are sent to the camera 2 and strobe to take
     * a picture.
     *
     * \param ball The teed-up ball that was previously located in the image
     * \param image The image with the ball
     * \param motion_detected Returns whether motion was detected at the time the method ended
     * \return True iff no error occurred.
     */
    bool WatchForHitAndTrigger(const GolfBall& ball, cv::Mat& image, bool& motion_detected) {

        const CameraHardware::CameraModel  camera_model = GolfSimCamera::kSystemSlot1CameraType;
        const CameraHardware::LensType camera_lens_type = GolfSimCamera::kSystemSlot1LensType;

        // TBD - refactor this to get rid of the dummy camera necessity
        GolfSimCamera c;
        c.camera_hardware_.init_camera_parameters(GsCameraNumber::kGsCamera1, camera_model, camera_lens_type);

        if (!WatchForBallMovement(c, ball, motion_detected)) {
            GS_LOG_MSG(error, "Failed to WatchForBallMovement.");
            return false;
        }

        // We have access to the set of frames before and after the hit, so process
        // club data here

        if (!GolfSimClubData::ProcessClubStrikeData(RecentFrames)) {
            GS_LOG_MSG(warning, "Failed to GolfSimClubData::ProcessClubStrikeData(RecentFrames().");
            // TBD - Ignore for now
            // return false;
        }

        return true;
    }

    bool LibCameraInterface::SendCamera2PreImage(const cv::Mat& raw_image) {

        // TBD -Not currently implemented
        return false;

        /*** 
        // We must undistort here, because we are going to immediately send the pre-image and the receiver
        // may not know what camera (and what distortion matrix) is in use.
        CameraHardware::CameraModel  camera_model = CameraHardware::PiGS;
        cv::Mat return_image = undistort_camera_image(raw_image, GsCameraNumber::kGsCamera2, camera_model);

        // Send the image back to the cam1 system
        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kCamera2ReturnPreImage);
        ipc_message.SetImageMat(return_image);
        GolfSimIpcSystem::SendIpcMessage(ipc_message);

        // Save the image for later analysis
        LoggingTools::LogImage("", return_image, std::vector < cv::Point >{}, true, "log_cam2_last_pre_image.png");
        ***/
        return true;
    }


    bool WatchForBallMovement(GolfSimCamera& camera, const GolfBall& ball, bool& motion_detected) {

        if (!GolfSimClubData::Configure()) {
            GS_LOG_TRACE_MSG(warning, "Failed to GolfSimClubData::Configure()");
            return false;
        }

        // Setup the camera to watch at a high FPS by reducing the portion of the sensor that will
        // be processed in each frame (cropping)

        // Will be setup when camera is configured for cropping, then is used in the ball-watcher-loop
        RPiCamEncoder app;

        if (!ConfigCameraForCropping(ball, camera, app)) {
            GS_LOG_MSG(error, "Failed to ConfigCameraForCropping.");
            return false;
        }

        // Prepare the camera to watch the small ROI at a high frame rate
        // This flag will be set here locally, but the sending of strobe 
        // pulses will be done within the motion-detection stage to reduce
        // latency.
        motion_detected = false;

        try
        {
            if (!ball_watcher_event_loop(app, motion_detected)) {
                GS_LOG_MSG(error, "ball_watcher_event_loop failed to process.");
            }
        }
        catch (std::exception const& e)
        {
            GS_LOG_MSG(error, "ERROR: *** " + std::string(e.what()) + " ***");
            return false;
        }

        uint frameIndex = 0;
        unsigned int numFramesToShow = 10;

        if (motion_detected) {
            std::string frame_information;
            float average_frame_rate = 0.0;
            float slowest_frame_rate = 10000.0;
            float fastest_frame_rate = -10000.0;

            for (auto& it : boost::adaptors::reverse(RecentFrames)) {
                cv::Mat& mostRecentFrameMat = it.mat;

                frame_information += "Frame " + std::to_string(frameIndex) + ": Framerate = " + std::to_string(it.frameRate) + "\n";
                average_frame_rate += it.frameRate;

                if (it.frameRate < slowest_frame_rate) {
                    slowest_frame_rate = it.frameRate;
                }

                if (it.frameRate > fastest_frame_rate) {
                    fastest_frame_rate = it.frameRate;
                }

                if (mostRecentFrameMat.empty()) {
                    GS_LOG_TRACE_MSG(warning, "Sequence No. " + std::to_string(it.requestSequence) + " was empty.");
                }


                frameIndex++;
            }

            average_frame_rate /= RecentFrames.size();
        }

        return true;
    }



    bool ConfigCameraForCropping(GolfBall ball, GolfSimCamera& camera, RPiCamEncoder& app) {

        // First, determine the cropping window size

        // watching_crop_width & Height defines the size of the cropping window, which will be a sub-region of the
        // camera's full resolution.
        float watching_crop_width = 0;
        float watching_crop_height = 0;

        // If we're trying to get club strike image data, we'll need to expand the image size beyond
        // whatever small cropping window we would otherwise have used.
        // Doing so is likely to slow down the frame rate, however.
        if (GolfSimClubData::kGatherClubData) {
            watching_crop_width = GolfSimClubData::kClubImageWidthPixels;
            watching_crop_height = GolfSimClubData::kClubImageHeightPixels;
        }
        else {
            // If we're not trying to gather club-strike image data, use the largest
            // cropping area that will still allow for the maximum FPS
            watching_crop_width = LibCameraInterface::kMaxWatchingCropWidth;
            watching_crop_height = LibCameraInterface::kMaxWatchingCropHeight;
        }

        // One issue here is that the current GS camera will not crop any smaller than 98x88.  So, if the ball is smaller
        // than that, we still have to go with the 98x88 and deal with the smaller ball later within the ROI processing

        // We will later want to ensure the ball is not so small that the inscribed watching area ( for high FPS )
        // is larger than the ball and could pick up unrelated movement outside of the ball.
        // We will deal with this when determining the ROI
        uint largest_inscribed_square_side_length_of_ball = (double)(CvUtils::CircleRadius(ball.ball_circle_)) * sqrt(2);

        // Reduce the size of the inscribed square a little bit to ensure the motion detection ROI will be within the ball
        largest_inscribed_square_side_length_of_ball *= 0.9;

#ifdef NOT_DOING_THIS_NOW

        uint largest_inscribed_square_side_length_of_ball = (double)(CvUtils::CircleRadius(ball.ball_circle_)) * sqrt(2);
        GS_LOG_TRACE_MSG(trace, "largest_inscribed_square_side_length_of_ball is: " + std::to_string(largest_inscribed_square_side_length_of_ball));

        // If we are not gathering club data, then the cropping window is a fixed size.  And if that size
        // is too large, reduce it to the size of the ball.
        if (!GolfSimClubData::kGatherClubData) {
            if (largest_inscribed_square_side_length_of_ball < watching_crop_width) {
                GS_LOG_TRACE_MSG(trace, "Decreasing cropping window width because largest ball square side = " + std::to_string(largest_inscribed_square_side_length_of_ball));
                watching_crop_width = largest_inscribed_square_side_length_of_ball;
            }
            if (largest_inscribed_square_side_length_of_ball < watching_crop_height) {
                GS_LOG_TRACE_MSG(trace, "Decreasing cropping window height because largest ball square side = " + std::to_string(largest_inscribed_square_side_length_of_ball));
                watching_crop_height = largest_inscribed_square_side_length_of_ball;
            }
        }

#endif

        // Starting with Pi 5, the crop height and width have to be divisible by 2. 
        // Enforce that here
        watching_crop_width += ((int)watching_crop_width % 2);
        watching_crop_height += ((int)watching_crop_height % 2);

        // TBD - After all that, and just for the current GS camera, we'll just set the cropping size to the smalllest possible size (for FPS)
        // and either center the ball within that area, or put the ball in the bottom-right of the
        // viewport if we want to see the club data.

        // Now determine the cropping window's offset within the full camera resolution image
        // This offset will be based on the position of the ball
        // The cropOffset is where, within the full-resolution image, the top-left corner of the 
        // cropping window is.

        float ball_x = CvUtils::CircleX(ball.ball_circle_);
        float ball_y = CvUtils::CircleY(ball.ball_circle_);

        // Assume first is that the ball will be centered in the cropping window, then tweak
        // it next if we're in club strike mode. Club strike imaging may require an offset.
        // NOTE - the crop offset is from the bottom right!  Not the top-left.

        float crop_offset_x = 0.0;
        float crop_offset_y = 0.0;

        // TBD - Not sure why the cropped window has to be skewed like this, but otherwise, the ball
        // is often off-center.
        const float crop_offset_scale_adjustment_x = 1.0; // 0.99;
        const float crop_offset_scale_adjustment_y = 1.0; //0.99;

        GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kCroppedImagePixelOffsetLeft", kCroppedImagePixelOffsetLeft);
        GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kCroppedImagePixelOffsetUp", kCroppedImagePixelOffsetUp);

        const float crop_offset_adjustment_x = kCroppedImagePixelOffsetLeft; // pixels
        const float crop_offset_adjustment_y = kCroppedImagePixelOffsetUp; // pixels

        // The video resolution is a little different than the still-photo resolution.
        // So scale the center of the ball accordingly.
        float x_scale = crop_offset_scale_adjustment_x; // NOT IMPLEMENTED YET   ((float)camera.camera_hardware_.video_resolution_x_ / (float)camera.camera_hardware_.resolution_x_);
        crop_offset_x = crop_offset_adjustment_x + (x_scale * (camera.camera_hardware_.resolution_x_ - (ball_x + watching_crop_width / 2.0)));

        float y_scale = crop_offset_scale_adjustment_y; // NOT IMPLEMENTED YET  ((float)camera.camera_hardware_.video_resolution_y_ / (float)camera.camera_hardware_.resolution_y_);
        crop_offset_y = crop_offset_adjustment_y + (y_scale * (camera.camera_hardware_.resolution_y_ - (ball_y + watching_crop_height / 2.0)));

        // If we're trying to get club images, then skew the cropping so that the ball ends up near the
        // bottom-right such that the the golf ball "watch" ROI will eventually also be 
        // all the way at the bottom right (to give more room so see the club)
        if (GolfSimClubData::kGatherClubData) {
            crop_offset_x += (0.5 * watching_crop_width - 0.5 * largest_inscribed_square_side_length_of_ball);
            crop_offset_y += (0.5 * watching_crop_height - 0.5 * largest_inscribed_square_side_length_of_ball);
        }

        // Check for and correct if the resulting crop window would be outside the full resolution image
        // If we need to correct something, preserve the crop width and correct the offset.
        // NOTE - Camera resolutions are 1 greater than the greatest pixel position
        if ((((camera.camera_hardware_.resolution_x_ - 1) - crop_offset_x) + watching_crop_width) >= camera.camera_hardware_.resolution_x_) {
            crop_offset_x = (camera.camera_hardware_.video_resolution_x_ - crop_offset_x) - 1;
        }

        if ((((camera.camera_hardware_.resolution_y_ - 1) - crop_offset_y) + watching_crop_height) >= camera.camera_hardware_.resolution_y_) {
            crop_offset_y = (camera.camera_hardware_.video_resolution_y_ - crop_offset_y) - 1;
        }

        cv::Vec2i watching_crop_size = cv::Vec2i((uint)watching_crop_width, (uint)watching_crop_height);
        cv::Vec2i watching_crop_offset = cv::Vec2i((uint)crop_offset_x, (uint)crop_offset_y);

        // Check to see if this resolution is the same as we currently have
        if (LibCameraInterface::camera_crop_configuration_ == LibCameraInterface::kCropped &&
            LibCameraInterface::current_watch_resolution_ == watching_crop_size &&
            LibCameraInterface::current_watch_offset_ == watching_crop_offset
            ) {
            // Don't reset the crop if we don't need to.  It takes time, especially the kernel-based cropping command lines
            return true;
        }

        if (!SendCameraCroppingCommand(camera, watching_crop_size, watching_crop_offset)) {
            GS_LOG_TRACE_MSG(error, "Failed to SendCameraCroppingCommand.");
            return false;
        }


        // TBD - Note - this entire thing should work on x,y vectors to the extent possible.

        // Determine what the resulting frame rate is in the resulting camera mode  (and confirm the resolution)
        // The camera would have been stopped after we took the first picture, so need re-start for this call
        cv::Vec2i cropped_resolution;
        uint cropped_frame_rate_fps;
        if (!RetrieveCameraInfo(camera.camera_hardware_.camera_number_, cropped_resolution, cropped_frame_rate_fps, true)) {
            return false;
        }

        if (!ConfigureLibCameraOptions(app, watching_crop_size, cropped_frame_rate_fps)) {
            GS_LOG_TRACE_MSG(error, "Failed to ConfigureLibCameraOptions.");
            return false;
        }

        // For the post processing, we also need to know what portion of the cropped window
        // is of interest in terms of determining ball movement.
        // Unlike the cropping window, Offsets here are from the top-left corner of the cropped window
        // Assume the ball is perfectly round, so the roi is square.  We don't want to watch for movement
        // anywhere but within the ball.
        // NOTE - We have to convert from the center of the ROI to the top-left

        float roi_offset_x = 0.0;
        float roi_offset_y = 0.0;

        float roi_size_x = 0.0;
        float roi_size_y = 0.0;

        float size_difference_x = largest_inscribed_square_side_length_of_ball - watching_crop_width;
        float size_difference_y = largest_inscribed_square_side_length_of_ball - watching_crop_height;

        if (size_difference_x >= 0.0) {
            // The cropped area is already fully inside the ball (assuming we're not dealing with club strike data
            // so just have the ROI match the cropping area
            roi_size_x = watching_crop_width;
            roi_offset_x = 0.0;
        }
        else {
            // The cropping area is larger than a square inscribed in the ball circle, so we want to focus the
            // ROI on just the area of that square.
            roi_size_x = largest_inscribed_square_side_length_of_ball * x_scale;
            // Essentially center the ROI within the image, assuming that the ball is centered in the cropping area
            // (which will likely not be the case if we are widening the cropping area for club strike data)
            roi_offset_x = -0.5 * (roi_size_x - watching_crop_width);
        }

        if (size_difference_y >= 0.0) {
            // The cropped area is already fully inside the ball (assuming we're not dealing with club strike data
            // so just have the ROI match the cropping area
            roi_size_y = watching_crop_height;
            roi_offset_y = 0.0;
        }
        else {
            // The cropping area is larger than a square inscribed in the ball circle, so we want to focus the
            // ROI on just the area of that square.
            roi_size_y = largest_inscribed_square_side_length_of_ball * y_scale;
            // Essentially center the ROI within the image, assuming that the ball is centered in the cropping area
            // (which will likely not be the case if we are widening the cropping area for club strike data)
            roi_offset_y = -0.5 * (roi_size_y - watching_crop_height);
        }

        roi_offset_x = std::max(roi_offset_x, 0.0f);
        roi_offset_y = std::max(roi_offset_y, 0.0f);

        if (camera.camera_hardware_.video_resolution_x_ < 0 || camera.camera_hardware_.video_resolution_y_ < 0) {
            GS_LOG_TRACE_MSG(error, "camera.camera_hardware_.video_resolution_x_ or _y_ have not been set.  Exiting.");
            return false;
        }

        roi_offset_x = std::min(roi_offset_x, (float)camera.camera_hardware_.video_resolution_x_);
        roi_offset_y = std::min(roi_offset_y, (float)camera.camera_hardware_.video_resolution_y_);

        cv::Vec2i roi_offset = cv::Vec2i((int)roi_offset_x, (int)roi_offset_y);
        cv::Vec2i roi_size = cv::Vec2i((uint)roi_size_x, (uint)roi_size_y);

        if (!ConfigurePostProcessing(roi_size, roi_offset)) {
            GS_LOG_TRACE_MSG(error, "Failed to ConfigurePostProcessing.");
            return false;
        }


        // Save the current cropping setup in hopes that we might be able to
        // avoid another media-ctl call next time if we are going to use the same
        // values next time.
        LibCameraInterface::current_watch_resolution_ = watching_crop_size;
        LibCameraInterface::current_watch_offset_ = watching_crop_offset;

        // Signal that the cropping setup has changed so that we know to change it 
        // back to full-screen later when we're watching for the ball to first appear..
        LibCameraInterface::camera_crop_configuration_ = LibCameraInterface::kCropped;

        return true;
    }


bool DiscoverCameraLocation(const GsCameraNumber camera_number, int& media_number, int& device_number) {

    // The camera location won't change during the course of a single exceution, so 
    // no need to figure this out more than once - re-use the earlier values if we can
    if (camera_location_found_) {
        media_number = previously_found_media_number_;
        device_number = previously_found_device_number_;

        return true;
    }

    // Otherwise, go out and search all of the possible places to search for the camera
    const std::string pitrac_root = std::getenv("PITRAC_ROOT");

    if (pitrac_root.empty()) {
        GS_LOG_TRACE_MSG(error, "DiscoverCameraLocation - could not get PITRAC_ROOT environment variable");
        return false;
    }

    const std::string kOutputFileName = "/tmp/pi_cam_location.txt";

    std::string s;

    s += "#!/bin/bash\n";
    s += "rm -f /tmp/discover_media.txt /tmp/discover_device.txt /tmp/discover_result.txt " + kOutputFileName + "\n";
    s += "for ((m = 0; m <= 5; ++m))\n";
    s += "    do\n";
    s += "        rm -f /tmp/discover_result.txt\n";
    s += "        media-ctl -d \"/dev/media$m\" --print-dot | grep imx > /tmp/discover_media.txt\n";
    s += "        awk -F\"imx296 \" '{print $2}' < /tmp/discover_media.txt | cut -d- -f1 > /tmp/discover_device.txt\n";
    s += "        echo -n -e \"$m \" > /tmp/discover_result.txt\n";
    s += "        cat /tmp/discover_device.txt >> /tmp/discover_result.txt\n";

    s += "       if  grep imx /tmp/discover_media.txt > /dev/null;  then  cat /tmp/discover_result.txt >> " + kOutputFileName + ";  fi\n";
    s += "            done\n";

    s += "            rm -f /tmp/discover_media.txt /tmp/discover_device.txt /tmp/discover_result.txt\n";

    const std::string script_name = "/tmp/pi_cam_location.sh";

    // Write the script out to file to run.  
    // Otherwise, system() would try to run the script as a sh script,
    // not a bash script
    std::ofstream script_file(script_name); // Open file for reading

    if (!script_file.is_open()) {
        GS_LOG_TRACE_MSG(error, "DiscoverCameraLocation - failed to open script file " + script_name);
        return false;
    }

    // Write the script to the file
    script_file << s << std::endl; 
    script_file.close();

    // At least currently, we need to make the script file executable before calling it
    std::string script_command = "chmod 777 " + script_name;
    system(script_command.c_str());

    script_command = script_name;

    int cmdResult = system(script_command.c_str());

    if (cmdResult != 0) {
        GS_LOG_TRACE_MSG(error, "system(DiscoverCameraLocation) failed.  Return value was: " + std::to_string(cmdResult));
        return false;
    }
    
    // Read and parse the output results
    std::ifstream file(kOutputFileName); 

    if (!file.is_open()) {
        GS_LOG_TRACE_MSG(error, "DiscoverCameraLocation - failed to open output file " + kOutputFileName);
        return false;
    }

    std::string line;

    std::stringstream buffer;
    buffer << file.rdbuf();

    line = buffer.str();

    // Read only one line
    if (line.empty()) {
        GS_LOG_TRACE_MSG(error, "system(DiscoverCameraLocation) failed.");
        return false;
    }
        
    file.close();

    // The format of the output file should be
    // <media number> <space> <device number>
    try {

        // Using a single pi requires both cameras to be connected to that Pi.
        // If we are not receiving two sets of camera data, then something is wrong.
        int new_line_position = line.find('\n');

        if (new_line_position == (int)string::npos) {

            // There is only one line of discovered information

            if (GolfSimOptions::GetCommandLineOptions().run_single_pi_) {
                GS_LOG_TRACE_MSG(error, "No expected new line found in camera location output.  Missing camera when running in single-pi mode.");
                return false;
            }
            else {
                // We have only a single line of information.  Do not need to do anything else
            }
        }
        else {
            // Assume (TBD - Confirm with Pi people) that the camera on camera unit 0
            // (the port nearest the LAN port) will correspond to the first line of the
            // returned media-ctl output

            if (camera_number == GsCameraNumber::kGsCamera1) {
                // Gee the information from the first line
                std::string first_line_str = line.substr(0, new_line_position);
                line = first_line_str;
            }
            else {
                // Gee the information from the second line
                std::string first_line_str = line.substr(new_line_position + 1);
                line = first_line_str;
            }
        }

        // Parse out the media and device numbers from what should be the first line of the media-ctl location report

        int last_space_position = line.rfind(' ');

        std::string device_number_str;

        if (last_space_position != (int)string::npos) {
            device_number_str = line.substr(last_space_position + 1);
        }
        else {
            GS_LOG_TRACE_MSG(error, "No space found");
            return false;
        }

        int first_space_position = line.find(' ');

        std::string media_number_str;

        if (first_space_position != (int)string::npos) {
            media_number_str = line.substr(0, first_space_position);
        }
        else {
            GS_LOG_TRACE_MSG(error, "No space found");
            return false;
        }

        if (media_number_str.empty() || device_number_str.empty()) {
            GS_LOG_TRACE_MSG(error, "Failed to parse media and device number strings");
            return false;
        }

        media_number = std::stoi(media_number_str);
        device_number = std::stoi(device_number_str);
    }
    catch (std::exception const& e)
    {
        GS_LOG_MSG(error, "ERROR: *** " + std::string(e.what()) + " ***");
        return false;
    }

    // Signal that we won't need to do this again during this run.
    camera_location_found_ = true;
    previously_found_media_number_ = media_number;
    previously_found_device_number_ = device_number;

    return true;
}


bool SendCameraCroppingCommand(const GolfSimCamera& camera, cv::Vec2i& cropping_window_size, cv::Vec2i& cropping_window_offset) {

    std::string mediaCtlCmd = GetCmdLineForMediaCtlCropping(camera, cropping_window_size, cropping_window_offset);
    int cmdResult = system(mediaCtlCmd.c_str());

    if (cmdResult != 0) {
        GS_LOG_TRACE_MSG(error, "system(mediaCtlCmd) failed.");
        return false;
    }
    return true;
}


bool ConfigurePostProcessing(const cv::Vec2i& roi_size, const cv::Vec2i& roi_offset ) {

    float kDifferenceM = 0.;
    float kDifferenceC = 0.;
    float kRegionThreshold = 0.;
    float kMaxRegionThreshold = 0.;
    uint kFramePeriod = 0;
    uint kHSkip = 0;
    uint kVSkip = 0;


    GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kDifferenceM", kDifferenceM);
    GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kDifferenceC", kDifferenceC);
    GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kRegionThreshold", kRegionThreshold);
    GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kMaxRegionThreshold", kMaxRegionThreshold);
    GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kFramePeriod", kFramePeriod);
    GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kHSkip", kHSkip);
    GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kVSkip", kVSkip);

    GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kCroppedImagePixelOffsetLeft", kCroppedImagePixelOffsetLeft);
    GolfSimConfiguration::SetConstant("gs_config.motion_detect_stage.kCroppedImagePixelOffsetUp", kCroppedImagePixelOffsetUp);

    // These values will be used within the motion-detect post-processing

    MotionDetectStage::incoming_configuration.use_incoming_configuration = true;  // Don't use .json file values -- use the following

    
    MotionDetectStage::incoming_configuration.roi_x = roi_offset[0];
    MotionDetectStage::incoming_configuration.roi_y = roi_offset[1];

    MotionDetectStage::incoming_configuration.roi_width = roi_size[0];
    MotionDetectStage::incoming_configuration.roi_height = roi_size[1];

    MotionDetectStage::incoming_configuration.difference_m = kDifferenceM;
    MotionDetectStage::incoming_configuration.difference_c = kDifferenceC;
    MotionDetectStage::incoming_configuration.region_threshold = kRegionThreshold;
    MotionDetectStage::incoming_configuration.max_region_threshold = kMaxRegionThreshold;
    MotionDetectStage::incoming_configuration.frame_period = kFramePeriod;
    MotionDetectStage::incoming_configuration.hskip = kHSkip; // TBD - don't hard code the skip factor
    MotionDetectStage::incoming_configuration.vskip = kVSkip;
    MotionDetectStage::incoming_configuration.verbose = 2;
    MotionDetectStage::incoming_configuration.showroi = true;

    return true;
}


bool ConfigureLibCameraOptions(RPiCamEncoder& app, const cv::Vec2i& cropping_window_size, uint cropped_frame_rate_fps) {

    VideoOptions* options = app.GetOptions();

    char dummy_arguments[] = "DummyExecutableName";
    char* argv[] = { dummy_arguments, NULL };

    if (!options->Parse(1, argv))
    {
        GS_LOG_TRACE_MSG(trace, "failed to parse dummy command line.");
        return false;
    }

    SetLibCameraLoggingOff();

    options->no_raw = true;  // See https://forums.raspberrypi.com/viewtopic.php?t=369927 - cameras won't work unless this is set.

    std::string shutter_speed_string;
    // Generally need to crank up gain due to short exposure time at high FPS.
    float camera_gain = 0.0;

    if (GolfSimClubData::kGatherClubData) {
        camera_gain = GolfSimClubData::kClubImageCameraGain;
        shutter_speed_string = std::to_string((int)(GolfSimClubData::kClubImageShutterSpeedMultiplier * (1. / cropped_frame_rate_fps * 1000000.))) + "us";   // TBD - should be 1,000,000 for uS setting
    }
    else {
        camera_gain = LibCameraInterface::kCamera1HighFPSGain;
        shutter_speed_string = std::to_string((int)(1. / cropped_frame_rate_fps * 1000000.)) + "us";   // TBD - should be 1,000,000 for uS setting
    }

    options->gain = camera_gain;
    options->shutter.set(shutter_speed_string);   // TBD - should be 1,000,000 for uS setting

    options->timeout.set("0ms");

    const CameraHardware::CameraModel  camera_model = GolfSimCamera::kSystemSlot1CameraType;
    if (camera_model != CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
        options->denoise = "cdn_off";
    }
    else {
        options->denoise = "auto";
    }

        GS_LOG_TRACE_MSG(trace, "Camera denoise option set to: " + options->denoise);
    options->framerate = cropped_frame_rate_fps;
    options->nopreview = true;
    options->lores_width = 0;
    options->lores_height = 0;
    options->viewfinder_width = 0;
    options->viewfinder_height = 0;
    options->info_text = "";
    options->level = "4.2";

    // On the Pi5, there's no hardware H.264 encoding, so let's try to turn it off entirely
    // TBD - See video_options.cpp to consider other options like libav
    options->codec = "yuv420";    // was h.264, but that no longer works on Pi5

    if (GolfSimConfiguration::GetPiModel() == GolfSimConfiguration::PiModel::kRPi5) {
        options->tuning_file = "/usr/share/libcamera/ipa/rpi/pisp/imx296.json";
    }
    else {
        options->tuning_file = "/usr/share/libcamera/ipa/rpi/vc4/imx296.json";
    }
    setenv("LIBCAMERA_RPI_TUNING_FILE", options->tuning_file.c_str(), 1);

    // TBD - We are switching away from having the post_process_file trigger the
    // dynamic loading of the motion_detection module.  Instead, hop[efully for speed,
    // we will use a statically-bound motion_detection module.  See ball_watcher.cpp
    // options->post_process_file = LibCameraInterface::kCameraMotionDetectSettings;

    if (cropping_window_size[0] > 0 && cropping_window_size[1] > 0) {
        options->width = cropping_window_size[0];
        options->height = cropping_window_size[1];
    }

    if (options->verbose >= 2)
        options->Print();

    return true;
}


// For example, to set the GS cam back to its default, use  "(0, 0)/1456x1088"
// 98x88 can deliver 572 FPS on the GS cam.
std::string GetCmdLineForMediaCtlCropping(const GolfSimCamera &camera, cv::Vec2i croppedHW, cv::Vec2i crop_offset_xY) {

    std::string s;

    int media_number = -1;
    int device_number = -1;

    if (!DiscoverCameraLocation(camera.camera_hardware_.camera_number_, media_number, device_number)) {
        GS_LOG_MSG(error, "Could not DiscoverCameraLocation");
        return "";
    }

    // The format will be different for mono cameras amd cp;pr
    std::string format = (camera.camera_hardware_.camera_is_mono()) ? "Y10_1X10" : "SBGGR10_1X10";

    s += "#!/bin/sh\n";
    s += "if  media-ctl -d \"/dev/media" + std::to_string(media_number) + "\" --set-v4l2 \"'imx296 " + std::to_string(device_number) + "-001a':0 [fmt:" + format + "/" + std::to_string(croppedHW[0]) + "x" + std::to_string(croppedHW[1]) + " crop:(" + std::to_string(crop_offset_xY[0]) + "," + std::to_string(crop_offset_xY[1]) + ")/" + 
        std::to_string(croppedHW[0]) + "x" + std::to_string(croppedHW[1]) + "]\" > /dev/null;  then  echo -e \"/dev/media" + std::to_string(media_number) + "\" > /dev/null; break;  fi\n";

    return s;
}


bool RetrieveCameraInfo(const GsCameraNumber camera_number, cv::Vec2i& resolution, uint& frameRate, bool restartCamera) {

    LibcameraJpegApp app;

    if (restartCamera) {

        try
        {
            StillOptions* options = app.GetOptions();
            
            char dummy_arguments[] = "DummyExecutableName";
            char* argv[] = { dummy_arguments, NULL };

            if (options->Parse(1, argv))
            {
                if (options->verbose >= 2)
                    options->Print();

                SetLibCameraLoggingOff();

                options->no_raw = true;  // See https://forums.raspberrypi.com/viewtopic.php?t=369927

                // Set the camera number (0 or 1, likely) when we have more than one camera
                options->camera = (camera_number == GsCameraNumber::kGsCamera1 || !GolfSimOptions::GetCommandLineOptions().run_single_pi_) ? 0 : 1;

                // Get the camera open for a moment so that we can read its settings
                app.OpenCamera();
                // GS_LOG_TRACE_MSG(trace, "About to ConfigureViewfinder");
                app.ConfigureViewfinder();
                // GS_LOG_TRACE_MSG(trace, "About to StartCamera");
                app.StartCamera();
                // GS_LOG_TRACE_MSG(trace, "About to StopCamera");
                app.StopCamera();
            }
        }
        catch (std::exception const& e)
        {
            GS_LOG_MSG(error, "ERROR: *** " + std::string(e.what()) + " ***");
            return false;
        }
    }

    std::vector<std::shared_ptr<libcamera::Camera>> cameras = app.GetCameras();

    if (cameras.size() == 0)
    {
        GS_LOG_MSG(error, "Could not getCameras.");
        return false;
    }

    int camera_slot_number = (camera_number == GsCameraNumber::kGsCamera1 || !GolfSimOptions::GetCommandLineOptions().run_single_pi_) ? 0 : 1;

    auto const& cam = cameras[camera_slot_number];

    std::unique_ptr<libcamera::CameraConfiguration> config = cam->generateConfiguration({ libcamera::StreamRole::Raw });
    if (!config)
    {
        GS_LOG_MSG(error, "Could not get cam config.");
        return false;
    }

    // Parse out the frames-per-second value (FPS)
    auto fd_ctrl = cam->controls().find(&controls::FrameDurationLimits);
    auto crop_ctrl = cam->properties().get(properties::ScalerCropMaximum);
    libcamera::Rectangle cropRect = crop_ctrl.value();
    double fps = fd_ctrl == cam->controls().end() ? NAN : (1e6 / fd_ctrl->second.min().get<int64_t>());
    std::cout << std::fixed << std::setprecision(2) << "["
        << fps << " fps - " << crop_ctrl->toString() << " crop" << "]\n";

    // Return results
    resolution = cv::Vec2i(cropRect.width, cropRect.height);
    frameRate = fps;

    return true;
}



cv::Mat LibCameraInterface::undistort_camera_image(const cv::Mat& img, const GolfSimCamera& camera) {

    if (!camera.camera_hardware_.use_calibration_matrix_) {
        GS_LOG_MSG(trace, "undistort_camera_image ignoring camera with no undistortion matrix. Returning original image.");
        return img;
    }

    GsCameraNumber camera_number = camera.camera_hardware_.camera_number_;
    CameraHardware::CameraModel camera_model = camera.camera_hardware_.camera_model_;

    // Get the calibration values from the camera

    cv::Mat cameracalibrationMatrix_ = camera.camera_hardware_.calibrationMatrix_;
    cv::Mat cameraDistortionVector_ = camera.camera_hardware_.cameraDistortionVector_;

    cv::Mat unDistortedBall1Img;
    cv::Mat m_undistMap1, m_undistMap2;

    if (camera.camera_hardware_.camera_is_mono()) {
        cv::initUndistortRectifyMap(cameracalibrationMatrix_, cameraDistortionVector_, cv::Mat(), cameracalibrationMatrix_, cv::Size(img.cols, img.rows), CV_8UC1, m_undistMap1, m_undistMap2);
    }
    else {
        cv::initUndistortRectifyMap(cameracalibrationMatrix_, cameraDistortionVector_, cv::Mat(), cameracalibrationMatrix_, cv::Size(img.cols, img.rows), CV_32FC1, m_undistMap1, m_undistMap2);
    }

    cv::remap(img, unDistortedBall1Img, m_undistMap1, m_undistMap2, cv::INTER_LINEAR);

    return unDistortedBall1Img;
}


bool ConfigCameraForFullScreenWatching(const GolfSimCamera& c) {

    if (lci::camera_crop_configuration_ == lci::kFullScreen) {
        // This takes time, so no need to do it repeatedly if not necessary
        // the flag will be reset if/when a cropped mode is setup
        GS_LOG_MSG(trace, "ConfigCameraForFullScreenWatching already configured.");
        return true;
    }

    uint width = c.camera_hardware_.resolution_x_;
    uint height = c.camera_hardware_.resolution_y_;

    if (width <= 0 || height <= 0) {
        GS_LOG_MSG(error, "ConfigCameraForFullScreenWatching called with camera that has no resolution set.");
        return false;
    }

    // Ensure no cropping and full resolution on the camera
    std::string mediaCtlCmd = GetCmdLineForMediaCtlCropping(c, cv::Vec2i(width, height), cv::Vec2i(0, 0));
    int cmdResult = system(mediaCtlCmd.c_str());

    if (cmdResult != 0) {
        GS_LOG_MSG(error, "system(mediaCtlCmd) failed.");
        return false;
    }

    LibCameraInterface::camera_crop_configuration_ = LibCameraInterface::kFullScreen;

    return true;
}



bool SetLibcameraTuningFileEnvVariable(const GolfSimCamera& camera) {

    GolfSimConfiguration::PiModel pi_model = GolfSimConfiguration::GetPiModel();

    std::string tuning_file;

    if (camera.camera_hardware_.camera_is_mono()) {
        // If this is a mono camera, than we must use the "mono" tuning file, regardless of whether
        // the camera is camera 1 or camera 2

        if (pi_model == GolfSimConfiguration::PiModel::kRPi5) {
            tuning_file = "/usr/share/libcamera/ipa/rpi/pisp/imx296_mono.json";
        }
        else {
            tuning_file = "/usr/share/libcamera/ipa/rpi/vc4/imx296_mono.json";
        }
    }
    else if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera1) {

        if (pi_model == GolfSimConfiguration::PiModel::kRPi5) {
            tuning_file = "/usr/share/libcamera/ipa/rpi/pisp/imx296.json";
        }
        else {
            tuning_file = "/usr/share/libcamera/ipa/rpi/vc4/imx296.json";
        }
    }
    else {
        // Use the infrared (noir) tuning file
        if (pi_model == GolfSimConfiguration::PiModel::kRPi5) {
            tuning_file = "/usr/share/libcamera/ipa/rpi/pisp/imx296_noir.json";
        }
        else {
            tuning_file = "/usr/share/libcamera/ipa/rpi/vc4/imx296_noir.json";
        }
    }

    setenv("LIBCAMERA_RPI_TUNING_FILE", tuning_file.c_str(), 1);

    return true;
}


LibcameraJpegApp* ConfigureForLibcameraStill(const GolfSimCamera& camera) {

    const GsCameraNumber camera_number = camera.camera_hardware_.camera_number_;
    int hardware_camera_index = camera_number;

    // Check first if we are already setup and can skip this

    LibcameraJpegApp* app = lci::libcamera_app_[hardware_camera_index];

    if (app != nullptr || lci::libcamera_configuration_[hardware_camera_index] == lci::CameraConfiguration::kStillPicture) {
        return lci::libcamera_app_[camera_number];
    }

    if (app != nullptr) {
        // The camera is configured, but just not for the right purposes
        // Deconfigure and then re-configure

        if (!DeConfigureForLibcameraStill(camera_number)) {
            GS_LOG_TRACE_MSG(error, "failed to DeConfigureForLibcameraStill.");
            return nullptr;
        }
    }

    // At this point, we know that we actually have to (re)configure the camera

    app = new LibcameraJpegApp;

    if (app == nullptr) {
        GS_LOG_TRACE_MSG(error, "failed to create a new LibcameraJpegApp.");
        return nullptr;
    }

    // Make sure we save the new app for later
    lci::libcamera_app_[hardware_camera_index] = app;

    try
    {
        StillOptions* options = app->GetOptions();

        char dummy_arguments[] = "DummyExecutableName";
        char* argv[] = { dummy_arguments, NULL };

        if (!options->Parse(1, argv))
        {
            GS_LOG_TRACE_MSG(error, "failed to parse dummy command line.");
            return nullptr;
        }

        SetLibCameraLoggingOff();

        double camera_gain = 1.0;
        double camera_contrast = 1.0;
        long still_shutter_time_uS = 10000;

        if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera1) {
            camera_gain = LibCameraInterface::kCamera1Gain;
            camera_contrast = LibCameraInterface::kCamera1Contrast;
            still_shutter_time_uS = LibCameraInterface::kCamera1StillShutterTimeuS;
        }
        else {
            // We're working with camera 2, which doesn't normally take still pictures.  
            // BUT, we might be doing a calibration shot, 
            // and if so, we'll want to adjust the gain & contrast to a lower value because of the
            // brighter (non-strobed) environment

            if (!GolfSimOptions::GetCommandLineOptions().lm_comparison_mode_) {
                camera_gain = LibCameraInterface::kCamera2Gain;
            }
            else {
                camera_gain = LibCameraInterface::kCamera2ComparisonGain;
            }

            camera_contrast = LibCameraInterface::kCamera2Contrast;

            // TBD - This code seems backward.  But everything is working right now, 
            // so let's not change until we can really test it all.
            if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2Calibrate ||
                GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2OnePulseOnly ||
                GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2BallLocation ||
                GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2AutoCalibrate) {

                camera_gain = LibCameraInterface::kCamera2CalibrateOrLocationGain;
                still_shutter_time_uS = LibCameraInterface::kCamera2StillShutterTimeuS;
            }
            else {
                still_shutter_time_uS = 6 * LibCameraInterface::kCamera2StillShutterTimeuS;
            }
        }

        // Assume camera 1 will be at slot 0 in all cases.  Camera 2 will be at slot 1
        // only in a single Pi system.
        options->camera = (camera_number == GsCameraNumber::kGsCamera1 || !GolfSimOptions::GetCommandLineOptions().run_single_pi_) ? 0 : 1;

        // Shouldn't need gain to take a "normal" picture.   Default will be 1.0
        // from the command line options.
        options->gain = camera_gain;
        options->contrast = camera_contrast;
        options->timeout.set("5s");
        const CameraHardware::CameraModel  camera_model = GolfSimCamera::kSystemSlot1CameraType;
        if (camera_model != CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
            options->denoise = "cdn_off";
        }
        else {
            options->denoise = "auto";
        }

        GS_LOG_TRACE_MSG(trace, "Camera denoise option set to: " + options->denoise);
        options->immediate = true;  // TBD - Trying this for now.  May have to work on white balance too
        options->awb = "indoor"; // TBD - Trying this for now.  May have to work on white balance too
        options->nopreview = true;
        options->viewfinder_width = 0;
        options->viewfinder_height = 0;
        options->shutter.set(std::to_string(still_shutter_time_uS) + "us");
        if (GolfSimOptions::GetCommandLineOptions().use_non_IR_camera_) {
            options->shutter.set("12000us"); // uS
        }
        options->info_text = "";

        if (!SetLibcameraTuningFileEnvVariable(camera) ) {
            GS_LOG_TRACE_MSG(error, "failed to SetLibcameraTuningFileEnvVariable");
            return nullptr;
        }

        if (options->verbose >= 2)
            options->Print();

        app->OpenCamera();
        // The RGB flag still works for grayscale mono images
        uint flags = RPiCamApp::FLAG_STILL_RGB;
        app->ConfigureStill(flags);

    }
    catch (std::exception const& e)
    {
        GS_LOG_MSG(error, "ERROR in ConfigureForLibcameraStill: *** " + std::string(e.what()) + " ***");
        return nullptr;
    }

    // Note the type of configuration we've done
    lci::libcamera_configuration_[hardware_camera_index] = lci::CameraConfiguration::kStillPicture;

    return app;
}



bool DeConfigureForLibcameraStill(const GsCameraNumber camera_number) {

    if (lci::libcamera_app_[camera_number] == nullptr) {
        GS_LOG_TRACE_MSG(error, "DeConfigureForLibcameraStill called, but camera_app was null.  Doing nothing.");
        return false;
    }

    if (lci::libcamera_configuration_[camera_number] != lci::CameraConfiguration::kStillPicture) {
        GS_LOG_TRACE_MSG(warning, "DeConfigureForLibcameraStill called, but camera_app was in the wrong configurations (configure was mis-matched). Camera_number was: " + std::to_string((int)lci::libcamera_configuration_[camera_number]) + " Ignoring.");
    }

    try
    {
        lci::libcamera_app_[camera_number]->StopCamera();
        lci::libcamera_app_[camera_number]->Teardown();  // TBD - Need?
        delete lci::libcamera_app_[camera_number];
        lci::libcamera_app_[camera_number] = nullptr;
    }
    catch (std::exception const& e)
    {
        GS_LOG_MSG(error, "ERROR in DeConfigureForLibcameraStill: *** " + std::string(e.what()) + " ***");
        return false;
    }

    lci::libcamera_configuration_[camera_number] = lci::CameraConfiguration::kNotConfigured;

    return true;
}



// Actually from libcamera_jpeg code, not libcamera_still
bool TakeLibcameraStill(const GolfSimCamera &camera, cv::Mat& img) {

    LibcameraJpegApp *app = ConfigureForLibcameraStill(camera);

    if (app == nullptr) {
        GS_LOG_TRACE_MSG(error, "failed to ConfigureForLibcameraStill.");
        return false;
    }

    try
    {
        still_image_event_loop(*app, img);
    }
    catch (std::exception const& e)
    {
        GS_LOG_MSG(error, "ERROR: *** " + std::string(e.what()) + " ***");
        return false;
    }

    if (!DeConfigureForLibcameraStill(GolfSimOptions::GetCommandLineOptions().GetCameraNumber())) {
        GS_LOG_TRACE_MSG(error, "failed to DeConfigureForLibcameraStill.");
        return false;
    }

    return true;
}



// TBD - This really seems like it should exist in the gs_camera module?
bool TakeRawPicture(const GolfSimCamera& camera, cv::Mat& img) {

    const GsCameraNumber camera_number = camera.camera_hardware_.camera_number_;

    const CameraHardware::CameraModel  camera_model = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1CameraType : GolfSimCamera::kSystemSlot2CameraType;

    // Ensure we have full resolution
    ConfigCameraForFullScreenWatching(camera);

    cv::Mat initialImg;
    if (!TakeLibcameraStill(camera, initialImg)) {
        GS_LOG_MSG(error, "Failed to take still picture.");
        return false;
    }

    if (initialImg.empty()) {
        return false;
    }

    img = golf_sim::LibCameraInterface::undistort_camera_image(initialImg, camera);

    return true;
}

// Enhanced ball detection using YOLO when configured
bool CheckForBallEnhanced(GolfBall& ball, cv::Mat& img) {
    bool use_yolo = (golf_sim::BallImageProc::kBallPlacementDetectionMethod == "experimental");
    
    GsCameraNumber camera_number = GolfSimOptions::GetCommandLineOptions().GetCameraNumber();
    const CameraHardware::CameraModel camera_model = (camera_number == GsCameraNumber::kGsCamera1) ? 
        GolfSimCamera::kSystemSlot1CameraType : GolfSimCamera::kSystemSlot2CameraType;
    const CameraHardware::LensType camera_lens_type = (camera_number == GsCameraNumber::kGsCamera1) ? 
        GolfSimCamera::kSystemSlot1LensType : GolfSimCamera::kSystemSlot2LensType;
    
    GolfSimCamera camera;
    camera.camera_hardware_.init_camera_parameters(camera_number, camera_model, camera_lens_type);
    
    if (!TakeRawPicture(camera, img)) {
        GS_LOG_MSG(error, "Failed to TakeRawPicture.");
        return false;
    }
    
    cv::Vec2i search_center = camera.GetExpectedBallCenter();
    
    if (use_yolo) {
        if (!golf_sim::BallImageProc::PreloadYOLOModel()) {
            GS_LOG_MSG(warning, "YOLO model not available, using legacy detection");
        } else {
            std::vector<GsCircle> detected_circles;
            bool detected = golf_sim::BallImageProc::DetectBallsONNX(img, 
                                                          golf_sim::BallImageProc::BallSearchMode::kFindPlacedBall,
                                                          detected_circles);
            
            if (detected && !detected_circles.empty()) {
                GsCircle best_circle;
                float best_distance = FLT_MAX;
                
                for (const auto& circle : detected_circles) {
                    float dx = circle[0] - search_center[0];
                    float dy = circle[1] - search_center[1];
                    float distance = sqrt(dx*dx + dy*dy);
                    
                    if (distance < best_distance) {
                        best_distance = distance;
                        best_circle = circle;
                    }
                }
                
                if (best_distance < 200) {
                    ball.ball_circle_ = best_circle;
                    ball.measured_radius_pixels_ = best_circle[2];
                    ball.search_area_center_ = search_center;
                    ball.search_area_radius_ = 200;

                    return true;
                }
            }
        }
    }

    bool expectBall = false;
    return camera.GetCalibratedBall(camera, img, ball, search_center, expectBall);
}

// TBD - This really seems like it should exist in the gs_camera module?
bool CheckForBall(GolfBall& ball, cv::Mat& img) {
    return CheckForBallEnhanced(ball, img);
}

bool CheckForBallLegacy(GolfBall& ball, cv::Mat& img) {

	GsCameraNumber camera_number = GolfSimOptions::GetCommandLineOptions().GetCameraNumber();

    // Figure out where the ball is
    // TBD - This repeats the camera initialization that we just did
    const CameraHardware::CameraModel  camera_model = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1CameraType : GolfSimCamera::kSystemSlot2CameraType;
    const CameraHardware::LensType camera_lens_type = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1LensType : GolfSimCamera::kSystemSlot2LensType;

    GolfSimCamera camera;
    camera.camera_hardware_.init_camera_parameters(camera_number, camera_model, camera_lens_type);
    camera.camera_hardware_.firstCannedImageFileName = std::string("/mnt/VerdantShare/dev/GolfSim/LM/Images/") + "FirstWaitingImage";
    camera.camera_hardware_.firstCannedImage = img;

    // If we are checking to see if a ball
    if (!TakeRawPicture(camera, img)) {
        GS_LOG_MSG(error, "Failed to TakeRawPicture.");
        return false;
    }

    cv::Vec2i search_area_center = camera.GetExpectedBallCenter();

    bool expectBall = false;
    bool success = camera.GetCalibratedBall(camera, img, ball, search_area_center, expectBall);

    if (!success) {
        return false;
    }

    return true;
}


// The following code is only relevant to the camera 2 system
bool WaitForCam2Trigger(cv::Mat& return_image) {
    LibcameraJpegApp app;

    cv::Mat raw_image;

    // Create a camera just to set the resolution and for un-distort operation
    const CameraHardware::CameraModel  camera_model = GolfSimCamera::kSystemSlot2CameraType;
    const CameraHardware::LensType camera_lens_type = GolfSimCamera::kSystemSlot2LensType;
    GolfSimCamera c;
    c.camera_hardware_.init_camera_parameters(GsCameraNumber::kGsCamera2, camera_model, camera_lens_type);

    try
    {
        StillOptions* options = app.GetOptions();

        char dummy_arguments[] = "DummyExecutableName";
        char* argv[] = { dummy_arguments, NULL };

        if (!options->Parse(1, argv))
        {
            return -1;
        }

        SetLibCameraLoggingOff();

        // On a two-Pi system, each Pi has just one camera, and that camera will be in slot 0
        // On a single-pi system the one Pi 5 has both cameras.  And Camera 2 will be in slot 1
        // because Camera 1 is in slot 0.
        if (GolfSimOptions::GetCommandLineOptions().run_single_pi_) {
            options->camera = 1;
        }
        else {
            options->camera = 0;
        }

        if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2Calibrate ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2BallLocation ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2AutoCalibrate) {

            options->gain = LibCameraInterface::kCamera2CalibrateOrLocationGain;
        }
        else if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
            options->gain = LibCameraInterface::kCamera2PuttingGain;
            options->contrast = LibCameraInterface::kCamera2PuttingContrast;
        }
        else {
            if (!GolfSimOptions::GetCommandLineOptions().lm_comparison_mode_) {
                options->gain = LibCameraInterface::kCamera2Gain;
            }
            else {
                options->gain = LibCameraInterface::kCamera2ComparisonGain;
            }

            options->contrast = LibCameraInterface::kCamera2Contrast;
        }

        options->immediate = true;
        options->timeout.set("0ms");  // Wait forever for external trigger
        const CameraHardware::CameraModel  camera_model = GolfSimCamera::kSystemSlot1CameraType;
        if (camera_model != CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
            options->denoise = "cdn_off";
        }
        else {
            options->denoise = "auto";
        }

        GS_LOG_TRACE_MSG(trace, "Camera denoise option set to: " + options->denoise);
        options->nopreview = true;
        // TBD - Currently, we are using the viewfinder stream to take the picture.  Should be corrected.
        options->viewfinder_width = c.camera_hardware_.resolution_x_;
        options->viewfinder_height = c.camera_hardware_.resolution_y_;
        options->width = c.camera_hardware_.resolution_x_;
        options->height = c.camera_hardware_.resolution_y_;
        options->shutter.set("11111us"); // Not actually used for external triggering.  Just needs to be set to something
        options->info_text = "";

        if (!SetLibcameraTuningFileEnvVariable(c)) {
            GS_LOG_TRACE_MSG(error, "failed to SetLibcameraTuningFileEnvVariable");
            return false;
        }

        if (options->verbose >= 2)
            options->Print();

        // This will block until the loop ends
        ball_flight_camera_event_loop(app, raw_image);
    }
    catch (std::exception const& e)
    {
        GS_LOG_MSG(error, "ERROR: *** " + std::string(e.what()) + " ***");
        return false;
    }

    // GS_LOG_TRACE_MSG(trace, "Tearing down initial camera.");
    app.StopCamera();  // TBD - Need?
    app.Teardown();  // TBD - Need?

    // LoggingTools::LogImage("", raw_image, std::vector < cv::Point >{}, true, "InitialRawImageCam2.png");

    // Save the image in memory after un-distorting it for the local camera/lens

    return_image = golf_sim::LibCameraInterface::undistort_camera_image(raw_image, c);

    if (GolfSimOptions::GetCommandLineOptions().camera_still_mode_ ) {

        std::string output_fname = GolfSimOptions::GetCommandLineOptions().output_filename_;
        if (output_fname.empty()) {
            output_fname = LoggingTools::kDefaultSaveFileName;
        }

        LoggingTools::LogImage("", return_image, std::vector < cv::Point >{}, true, output_fname);
    }

    return true;
}


bool PerformCameraSystemStartup() {

    SetLibCameraLoggingOff();

    // Setup the Pi Camera to be internally or externally triggered as appropriate

    SystemMode mode = GolfSimOptions::GetCommandLineOptions().system_mode_;

    switch (mode) {

        case SystemMode::kCamera1:
        case SystemMode::kCamera1TestStandalone:
        case SystemMode::kTestSpin: {

            if (!GolfSimOptions::GetCommandLineOptions().run_single_pi_) {
                std::string trigger_mode_command = "sudo $PITRAC_ROOT/ImageProcessing/CameraTools/setCameraTriggerInternal.sh";

                int command_result = system(trigger_mode_command.c_str());

                if (command_result != 0) {
                    GS_LOG_TRACE_MSG(trace, "system(trigger_mode_command) failed.");
                    return false;
                }
            }
            else {
                /****
                const CameraHardware::CameraModel  camera_model = GolfSimCamera::kSystemSlot1CameraType;
                if (camera_model == CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
                    std::string trigger_mode_command = "$PITRAC_ROOT/ImageProcessing/CameraTools/imx296_trigger 6 0";

                    GS_LOG_TRACE_MSG(trace, "Camera 1 trigger_mode_command = " + trigger_mode_command);
                    int command_result = system(trigger_mode_command.c_str());

                    if (command_result != 0) {
                        GS_LOG_TRACE_MSG(trace, "system(trigger_mode_command) failed.");
                        return false;
                    }
                }
                else {
                    GS_LOG_TRACE_MSG(trace, "Running in single-pi mode, so not setting camera triggering (internal or external) programmatically.  Instead, please see the following discussion on how to setup the boot/firmware.config.txt dtoverlays for triggering:  https://forums.raspberrypi.com/viewtopic.php?p=2315464#p2315464.");
                }
                    ****/
            }
        }
        break;

        case SystemMode::kCamera2:
        case SystemMode::kRunCam2ProcessForPi1Processing:
        case SystemMode::kCamera2TestStandalone: {

            if (!GolfSimOptions::GetCommandLineOptions().run_single_pi_) {
                std::string trigger_mode_command = "sudo $PITRAC_ROOT/ImageProcessing/CameraTools/setCameraTriggerExternal.sh";

                int command_result = system(trigger_mode_command.c_str());

                if (command_result != 0) {
                    GS_LOG_TRACE_MSG(trace, "system(trigger_mode_command) failed.");
                    return false;
                }
            }
            else {
                const CameraHardware::CameraModel  camera_model = GolfSimCamera::kSystemSlot2CameraType;

                if (camera_model == CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
                    std::string trigger_mode_command = "$PITRAC_ROOT/ImageProcessing/CameraTools/imx296_trigger 4 1";

                    int command_result = system(trigger_mode_command.c_str());

                    if (command_result != 0) {
                        GS_LOG_TRACE_MSG(trace, "system(trigger_mode_command) failed.");
                        return false;
                    }
                }
            }

            // Create a camera just for purposes of setting the tuning file variable
            GolfSimCamera camera;
            camera.camera_hardware_.init_camera_parameters(GsCameraNumber::kGsCamera2, GolfSimCamera::kSystemSlot2CameraType, GolfSimCamera::kSystemSlot2LensType);

            if (!SetLibcameraTuningFileEnvVariable(camera)) {
                GS_LOG_TRACE_MSG(error, "failed to SetLibcameraTuningFileEnvVariable");
                return false;
            }
        }
        break;

        case SystemMode::kTest:
        default:
            break;
    }

    return true;
}

}

#endif // #ifdef __unix__  // Ignore in Windows environment
// Unix-only libcamera-related code

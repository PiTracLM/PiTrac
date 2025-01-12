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

    // The system will start in a full-screen watching mode, but ensure 
    // we set it up once just in case
    LibCameraInterface::CropConfiguration LibCameraInterface::camera_crop_configuration_ = kCropUnknown;
    cv::Vec2i LibCameraInterface::current_watch_resolution_;
    cv::Vec2i LibCameraInterface::current_watch_offset_;

    LibCameraInterface::CameraConfiguration LibCameraInterface::libcamera_configuration_[] = {LibCameraInterface::CameraConfiguration::kNotConfigured, LibCameraInterface::CameraConfiguration::kNotConfigured};

    LibcameraJpegApp* LibCameraInterface::libcamera_app_[] = {nullptr, nullptr};


    void SetLibCameraLoggingOff() {
        GS_LOG_TRACE_MSG(trace, "SetLibCameraLoggingOff");
        libcamera::logSetTarget(libcamera::LoggingTargetNone);
	/* TBD - Not working, so avoid the extra log message for now
        libcamera::logSetLevel("*", "ERROR");
        libcamera::logSetLevel("", "ERROR");
	*/
        RPiCamApp::verbosity = 0;
    }

    bool WatchForHitAndTrigger(const GolfBall& ball, cv::Mat& image, bool& motion_detected) {

        GS_LOG_TRACE_MSG(trace, "WatchForHitAndTrigger");

        CameraHardware::CameraModel  cameraModel = CameraHardware::PiGSCam6mmWideLens;

        // TBD - refactor this to get rid of the dummy camera necessity
        GolfSimCamera c;
        c.camera_.init_camera_parameters(GsCameraNumber::kGsCamera1, cameraModel);

        if (!WatchForBallMovement(c, ball, motion_detected)) {
            GS_LOG_MSG(error, "Failed to WatchForBallMovement.");
            return false;
        }

        // We have access to the set of frames before and after the hit, so process
        // club data here

        if (!GolfSimClubData::ProcessClubStrikeData(RecentFrames) ) {
            GS_LOG_MSG(warning, "Failed to GolfSimClubData::ProcessClubStrikeData(RecentFrames().");
            // TBD - Ignore for now
            // return false;
        }

        return true;
    }

    bool LibCameraInterface::SendCamera2PreImage(const cv::Mat& raw_image) {
        // We must undistort here, because we are going to immediately send the pre-image and the receiver
        // may not know what camera (and what distortion matrix) is in use.
        CameraHardware::CameraModel  cameraModel = CameraHardware::PiGSCam6mmWideLens;
        cv::Mat return_image = undistort_camera_image(raw_image, GsCameraNumber::kGsCamera2, cameraModel);

        // Send the image back to the cam1 system
        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kCamera2ReturnPreImage);
        ipc_message.SetImageMat(return_image);
        GolfSimIpcSystem::SendIpcMessage(ipc_message);

        // Save the image for later analysis
        LoggingTools::LogImage("", return_image, std::vector < cv::Point >{}, true, "log_cam2_last_pre_image.png");

        return true;
    }


    bool WatchForBallMovement(GolfSimCamera& camera, const GolfBall& ball, bool & motion_detected) {

        GS_LOG_TRACE_MSG(trace, "WatchForBallMovement");

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

            GS_LOG_TRACE_MSG(trace, frame_information);
            GS_LOG_TRACE_MSG(trace, "Average framerate = " + std::to_string(average_frame_rate) + "\n");
            GS_LOG_TRACE_MSG(trace, "Slowest framerate = " + std::to_string(slowest_frame_rate) + "\n");
            GS_LOG_TRACE_MSG(trace, "Fastest framerate = " + std::to_string(fastest_frame_rate) + "\n");
        }

        return true;
    }



    bool ConfigCameraForCropping(GolfBall ball, GolfSimCamera& camera, RPiCamEncoder& app) {

        GS_LOG_TRACE_MSG(trace, "ConfigCameraForCropping");
        GS_LOG_TRACE_MSG(trace, "   ball: " + ball.Format());

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
            GS_LOG_TRACE_MSG(trace, "Setting initial crop width/height for club data to: " + std::to_string(watching_crop_width) + "/" + std::to_string(watching_crop_height) + ".");
        }
        else {
            // If we're not trying to gather club-strike image data, use the largest
            // cropping area that will still allow for the maximum FPS
            watching_crop_width = LibCameraInterface::kMaxWatchingCropWidth;
            watching_crop_height = LibCameraInterface::kMaxWatchingCropHeight;
        }

        // Starting with Pi 5, the crop height and width have to be divisible by 2. 
        // Enforce that here
        watching_crop_width += ((int)watching_crop_width % 2);
        watching_crop_height += ((int)watching_crop_height % 2);

        // Ensure the ball is not so small that the inscribed watching area ( for high FPS )
        // is larger than the ball and could pick up unrelated movement outside of the ball
        uint largest_inscribed_square_side_length_of_ball = (double)(CvUtils::CircleRadius(ball.ball_circle_)) * sqrt(2);
        GS_LOG_TRACE_MSG(trace, "largest_inscribed_square_side_length_of_ball is: " + std::to_string(largest_inscribed_square_side_length_of_ball));

        // If we are not gathering club data, then the cropping window is a fixed size.  And if that size
        // is too large, reduce it to the size of the ball.
        if (!GolfSimClubData::kGatherClubData) {
            if (largest_inscribed_square_side_length_of_ball > watching_crop_width) {
                GS_LOG_TRACE_MSG(trace, "Increasing cropping window width because largest ball square side = " + std::to_string(largest_inscribed_square_side_length_of_ball));
                watching_crop_width = largest_inscribed_square_side_length_of_ball;
            }
            if (largest_inscribed_square_side_length_of_ball > watching_crop_width) {
                GS_LOG_TRACE_MSG(trace, "Increasing cropping window length because largest ball square side = " + std::to_string(largest_inscribed_square_side_length_of_ball));
                watching_crop_width = largest_inscribed_square_side_length_of_ball;
            }
        }

        GS_LOG_TRACE_MSG(trace, "Final crop width/height is: " + std::to_string(watching_crop_width) + "/" + std::to_string(watching_crop_height) + ".");


        // Now determine the cropping window's offset within the full camera resolution image
        // This offset will be based on the position of the ball
        // The cropOffset is where, within the full-resolution image, the top-left corner of the 
        // cropping window is.

        float ball_x = CvUtils::CircleX(ball.ball_circle_);
        float ball_y = CvUtils::CircleY(ball.ball_circle_);


        // Assume first is that the ball will be centered in the cropping window, then tweak
        // it next if we're in club strike mode. Club strike imaging may require an offset.
        // NOTE - the crop offset is from the bottom right!  Not the top-left.
        float crop_offset_x = camera.camera_.resolution_x_ - (ball_x + watching_crop_width / 2.0);
        float crop_offset_y = camera.camera_.resolution_y_ - (ball_y + watching_crop_height / 2.0);

        // If we're trying to get club images, then skew the image so that the golf ball "watch" ROI is
        // all the way at the bottom right (to give more room so see the club)
        if (GolfSimClubData::kGatherClubData) {
            crop_offset_x += (0.5 * watching_crop_width - 0.5 * largest_inscribed_square_side_length_of_ball);
            crop_offset_y += (0.5 * watching_crop_height - 0.5 * largest_inscribed_square_side_length_of_ball);
        }

        cv::Vec2i watching_crop_size = cv::Vec2i((uint)watching_crop_width, (uint)watching_crop_height);
        cv::Vec2i watching_crop_offset = cv::Vec2i((uint)crop_offset_x, (uint)crop_offset_y);

        // Check to see if this resolution is the same as we currently have
        if (LibCameraInterface::camera_crop_configuration_ == LibCameraInterface::kCropped &&
            LibCameraInterface::current_watch_resolution_ == watching_crop_size &&
            LibCameraInterface::current_watch_offset_ == watching_crop_offset
            ) {
            GS_LOG_TRACE_MSG(trace, "Skipping cropping setup because already cropped.");
            // Don't reset the crop if we don't need to.  It takes time, especially the kernel-based cropping command lines
            return true;
        }


        // Check for and correct if the resulting crop window would be outside the full resolution image
        // If we need to correct something, preserve the crop width and correct the offset.
        // NOTE - Camera resolutions are 1 greater than the greatest pixel position
        if ((((camera.camera_.resolution_x_ - 1) - crop_offset_x) + watching_crop_width) >= camera.camera_.resolution_x_) {
            crop_offset_x = (camera.camera_.resolution_x_ - crop_offset_x) - 1;
        }

        if ((((camera.camera_.resolution_y_ - 1) - crop_offset_y) + watching_crop_height) >= camera.camera_.resolution_y_) {
            crop_offset_y = (camera.camera_.resolution_y_ - crop_offset_y) - 1;
        }

        GS_LOG_TRACE_MSG(trace, "Final (adjusted) crop offset x/y is: " + std::to_string(crop_offset_x) + "/" + std::to_string(crop_offset_y) + ".");

        if (!SendCameraCroppingCommand(watching_crop_size, watching_crop_offset)) {
            GS_LOG_TRACE_MSG(error, "Failed to SendCameraCroppingCommand.");
            return false;
        }


        // Determine what the resulting frame rate is in the resulting camera mode  (and confirm the resolution)
        // The camera would have been stopped after we took the first picture, so need re-start for this call
        cv::Vec2i cropped_resolution;
        uint cropped_frame_rate_fps;
        if (!RetrieveCameraInfo(cropped_resolution, cropped_frame_rate_fps, true)) {
            GS_LOG_TRACE_MSG(trace, "Failed to RetrieveCameraInfo.");
            return false;
        }

        GS_LOG_TRACE_MSG(info, "Camera FPS = " + std::to_string(cropped_frame_rate_fps) + ".");

        if (!ConfigureLibCameraOptions(app, watching_crop_size, cropped_frame_rate_fps)) {
            GS_LOG_TRACE_MSG(error, "Failed to ConfigureLibCameraOptions.");
            return false;
        }

        // For the post processing, we also need to know what portion of the cropped window
        // is of interest in terms of determining ball movement.
        // Offsets are from the top-left corner of the cropped window
        // NOTE - We have to convert from the center of the ROI to the top-left
        float roi_offset_x = (ball_x - (camera.camera_.resolution_x_ - crop_offset_x)) - largest_inscribed_square_side_length_of_ball / 2.0 + watching_crop_width;
        float roi_offset_y = (ball_y - (camera.camera_.resolution_y_ - crop_offset_y)) - largest_inscribed_square_side_length_of_ball / 2.0 + watching_crop_height;

        GS_LOG_TRACE_MSG(trace, "Final roi x/y offset is: " + std::to_string(roi_offset_x) + "/" + std::to_string(roi_offset_y) + ".");

        cv::Vec2i roi_offset = cv::Vec2i((uint)roi_offset_x, (uint)roi_offset_y);

        // Assume the ball is perfectly round, so the roi is square.  We don't want to watch for movement
        // anywhere but within the ball.
        float roi_size_x = largest_inscribed_square_side_length_of_ball;
        float roi_size_y = largest_inscribed_square_side_length_of_ball;

        // Clamp the offset if necessary
        roi_offset_x = std::clamp(roi_offset_x, 0.0f, watching_crop_width - roi_size_x);
        roi_offset_y = std::clamp(roi_offset_y, 0.0f, watching_crop_height - roi_size_y);

        GS_LOG_TRACE_MSG(trace, "Final roi width/height is: " + std::to_string(roi_size_x) + "/" + std::to_string(roi_size_y) + ".");

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


bool SendCameraCroppingCommand(cv::Vec2i& cropping_window_size, cv::Vec2i& cropping_window_offset) {

    GS_LOG_TRACE_MSG(trace, "SendCameraCroppingCommand.");
    GS_LOG_TRACE_MSG(trace, "   cropping_window_size: (width, height) = " + std::to_string(cropping_window_size[0]) + ", " + std::to_string(cropping_window_size[1]) + ".");
    GS_LOG_TRACE_MSG(trace, "   cropping_window_offset: (X, Y) = " + std::to_string(cropping_window_offset[0]) + ", " + std::to_string(cropping_window_offset[1]) + ".");

    std::string mediaCtlCmd = GetCmdLineForMediaCtlCropping(cropping_window_size, cropping_window_offset);
    GS_LOG_TRACE_MSG(trace, "mediaCtlCmd = " + mediaCtlCmd);
    int cmdResult = system(mediaCtlCmd.c_str());

    if (cmdResult != 0) {
        GS_LOG_TRACE_MSG(trace, "system(mediaCtlCmd) failed.");
        return false;
    }
    return true;
}


bool ConfigurePostProcessing(const cv::Vec2i& roi_size, const cv::Vec2i& roi_offset ) {

    GS_LOG_TRACE_MSG(trace, "ConfigurePostProcessing.");
    GS_LOG_TRACE_MSG(trace, "   roi_size: (width, height) = " + std::to_string(roi_size[0]) + ", " + std::to_string(roi_size[1]) + ".");
    GS_LOG_TRACE_MSG(trace, "   roi_offset: (X, Y) = " + std::to_string(roi_offset[0]) + ", " + std::to_string(roi_offset[1]) + ".");

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

    GS_LOG_TRACE_MSG(trace, "ConfigureLibCameraOptions.  cropping_window_size: (width, height) = " + std::to_string(cropping_window_size[0]) + ", " + std::to_string(cropping_window_size[1]) + ".");

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

    GS_LOG_TRACE_MSG(trace, "Camera gain is: " + std::to_string(camera_gain));
    GS_LOG_TRACE_MSG(trace, "Shutter speed string is: " + shutter_speed_string);

    options->gain = camera_gain;
    options->shutter.set(shutter_speed_string);   // TBD - should be 1,000,000 for uS setting

    options->timeout.set("0ms");
    options->denoise = "cdn_off";
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
        GS_LOG_TRACE_MSG(trace, "Detected PiModel::kRPi5 and camera 1.");
        options->tuning_file = "/usr/share/libcamera/ipa/rpi/pisp/imx296.json";
    }
    else {
        GS_LOG_TRACE_MSG(trace, "Detected PiModel::kRPi4 and camera 1.");
        options->tuning_file = "/usr/share/libcamera/ipa/rpi/vc4/imx296.json";
    }
    setenv("LIBCAMERA_RPI_TUNING_FILE", options->tuning_file.c_str(), 1);
    options->post_process_file = LibCameraInterface::kCameraMotionDetectSettings;

    if (!GolfSimClubData::kGatherClubData) {
    	GS_LOG_TRACE_MSG(trace, "ball_watcher_event_loop will use post-process file: " + options->post_process_file);
    }

    if (cropping_window_size[0] > 0 && cropping_window_size[1] > 0) {
        options->width = cropping_window_size[0];
        options->height = cropping_window_size[1];
    }

    if (options->verbose >= 2)
        options->Print();

    return true;
}


// For example, to set the GS cam back to its default, use  "(0, 0)/1456x1088"
// 128x96 can deliver 532 FPS on the GS cam.
std::string GetCmdLineForMediaCtlCropping(cv::Vec2i croppedHW, cv::Vec2i crop_offset_xY) {

    std::string s;

    int device_number = (GolfSimConfiguration::GetPiModel() == GolfSimConfiguration::PiModel::kRPi5) ? 6 : 10;

    s += "#!/bin/sh\n";
    for (int m = 0; m <= 5; m++) {
        s += "if  media-ctl -d \"/dev/media" + std::to_string(m) + "\" --set-v4l2 \"'imx296 " + std::to_string(device_number) + "-001a':0 [fmt:SBGGR10_1X10/" + std::to_string(croppedHW[0]) + "x" + std::to_string(croppedHW[1]) + " crop:(" + std::to_string(crop_offset_xY[0]) + "," + std::to_string(crop_offset_xY[1]) + ")/" + std::to_string(croppedHW[0]) + "x" + std::to_string(croppedHW[1]) + "]\" > /dev/null;  then  echo -e \"/dev/media" + std::to_string(m) + "\" > /dev/null; break;  fi\n";
    }

    return s;
}


bool RetrieveCameraInfo(cv::Vec2i& resolution, uint& frameRate, bool restartCamera) {

    GS_LOG_TRACE_MSG(trace, "RetrieveCameraInfo.");

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

    GS_LOG_TRACE_MSG(trace, "Getting cameras.");
    std::vector<std::shared_ptr<libcamera::Camera>> cameras = app.GetCameras();

    if (cameras.size() == 0)
    {
        GS_LOG_MSG(error, "Could not getCameras.");
        return false;
    }

    auto const& cam = cameras[0];

    std::unique_ptr<libcamera::CameraConfiguration> config = cam->generateConfiguration({ libcamera::StreamRole::Raw });
    if (!config)
    {
        GS_LOG_MSG(error, "Could not get cam config.");
        return false;
    }

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



cv::Mat LibCameraInterface::undistort_camera_image(const cv::Mat& img, GsCameraNumber camera_number, CameraHardware::CameraModel cameraModel) {
    // Get a camera object just to be able to get the calibration values
    GolfSimCamera c;
    c.camera_.resolution_x_override_ = img.cols;
    c.camera_.resolution_y_override_ = img.rows;
    c.camera_.init_camera_parameters(camera_number, cameraModel);
    cv::Mat cameracalibrationMatrix = c.camera_.calibrationMatrix;
    cv::Mat cameraDistortionVector = c.camera_.cameraDistortionVector;

    cv::Mat unDistortedBall1Img;
    cv::Mat m_undistMap1, m_undistMap2;
    // TBD - is the size rows, cols?  or cols, rows?
    cv::initUndistortRectifyMap(cameracalibrationMatrix, cameraDistortionVector, cv::Mat(), cameracalibrationMatrix, cv::Size(img.cols, img.rows), CV_32FC1, m_undistMap1, m_undistMap2);
    cv::remap(img, unDistortedBall1Img, m_undistMap1, m_undistMap2, cv::INTER_LINEAR);

    return unDistortedBall1Img;
}


bool ConfigCameraForFullScreenWatching(const GolfSimCamera& c) {

    if (lci::camera_crop_configuration_ == lci::kFullScreen) {
        // This takes time, so no need to do it repeatedly if not necessary
        // the flag will be reset if/when a cropped mode is setup
        return true;
    }

    uint width = c.camera_.resolution_x_;
    uint height = c.camera_.resolution_y_;

    if (width <= 0 || height <= 0) {
        GS_LOG_MSG(error, "ConfigCameraForFullScreenWatching called with camera that has no resolution set.");
        return false;
    }

    // Ensure no cropping and full resolution on the camera 
    std::string mediaCtlCmd = GetCmdLineForMediaCtlCropping(cv::Vec2i(width, height), cv::Vec2i(0, 0));
    GS_LOG_TRACE_MSG(trace, "mediaCtlCmd = " + mediaCtlCmd);
    int cmdResult = system(mediaCtlCmd.c_str());

    if (cmdResult != 0) {
        GS_LOG_MSG(error, "system(mediaCtlCmd) failed.");
        return false;
    }

    LibCameraInterface::camera_crop_configuration_ = LibCameraInterface::kFullScreen;

    return true;
}



LibcameraJpegApp* ConfigureForLibcameraStill(GsCameraNumber camera_number) {

    GS_LOG_TRACE_MSG(trace, "ConfigureForLibcameraStill called for camera " + std::to_string((int)camera_number));
    // Check first if we are already setup and can skip this

    LibcameraJpegApp* app = lci::libcamera_app_[camera_number];

    if (app != nullptr || lci::libcamera_configuration_[camera_number] == lci::CameraConfiguration::kStillPicture) {
        GS_LOG_TRACE_MSG(trace, "ConfigureForLibcameraStill - already configured.");
        return lci::libcamera_app_[camera_number];
    }

    if (app != nullptr) {
        // The camera is configured, but just not for the right purposes
        // Deconfigure and then re-configure
        GS_LOG_TRACE_MSG(trace, "ConfigureForLibcameraStill - re-configuring.");

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
    lci::libcamera_app_[camera_number] = app;

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

            if (GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera2Calibrate &&
                GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera2BallLocation) {

                camera_gain = LibCameraInterface::kCamera2CalibrateOrLocationGain;
                still_shutter_time_uS = LibCameraInterface::kCamera2StillShutterTimeuS;
            }
            else {
                GS_LOG_TRACE_MSG(trace, "In SystemMode::kCamera2Calibrate.  Using DEFAULT gain/contrast for Camera2.");
                GS_LOG_TRACE_MSG(trace, "Setting longer still_shutter_time_uS for Camera2.");
                still_shutter_time_uS = 6 * LibCameraInterface::kCamera2StillShutterTimeuS;
            }
        }
        // Shouldn't need gain to take a "normal" picture.   Default will be 1.0
        // from the command line options.
        options->gain = camera_gain;
        GS_LOG_TRACE_MSG(trace, "Camera Gain set to: " + std::to_string(options->gain));
        options->contrast = camera_contrast;
        GS_LOG_TRACE_MSG(trace, "Camera Contrast set to: " + std::to_string(options->contrast));
        options->timeout.set("5s");
        options->denoise = "cdn_off";
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
        if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera1) {

            if (GolfSimConfiguration::GetPiModel() == GolfSimConfiguration::PiModel::kRPi5) {
                GS_LOG_TRACE_MSG(trace, "Detected PiModel::kRPi5 and camera 1.");
                options->tuning_file = "/usr/share/libcamera/ipa/rpi/pisp/imx296.json";
            }
            else {
                GS_LOG_TRACE_MSG(trace, "Detected PiModel::kRPi4 and camera 1.");
                options->tuning_file = "/usr/share/libcamera/ipa/rpi/vc4/imx296.json";
            }
        }
        else {
            // Use the infrared (noir) tuning file
            if (GolfSimConfiguration::GetPiModel() == GolfSimConfiguration::PiModel::kRPi5) {
                GS_LOG_TRACE_MSG(trace, "Detected PiModel::kRPi5 and camera 2.");
                options->tuning_file = "/usr/share/libcamera/ipa/rpi/pisp/imx296_noir.json";
            }
            else {
                GS_LOG_TRACE_MSG(trace, "Detected PiModel::kRPi4 and camera 2.");
                options->tuning_file = "/usr/share/libcamera/ipa/rpi/vc4/imx296_noir.json";
            }
        }
        setenv("LIBCAMERA_RPI_TUNING_FILE", options->tuning_file.c_str(), 1);

        if (options->verbose >= 2)
            options->Print();

        app->OpenCamera();
        uint flags = RPiCamApp::FLAG_STILL_RGB;
        app->ConfigureStill(flags);

    }
    catch (std::exception const& e)
    {
        GS_LOG_MSG(error, "ERROR in ConfigureForLibcameraStill: *** " + std::string(e.what()) + " ***");
        return nullptr;
    }

    // Note the type of configuration we've done
    lci::libcamera_configuration_[camera_number] = lci::CameraConfiguration::kStillPicture;

    return app;
}



bool DeConfigureForLibcameraStill(GsCameraNumber camera_number) {

    GS_LOG_TRACE_MSG(trace, "DeConfigureForLibcameraStill called for camera " + std::to_string((int)camera_number));

    if (lci::libcamera_app_[camera_number] == nullptr) {
        GS_LOG_TRACE_MSG(error, "DeConfigureForLibcameraStill called, but camera_app was null.  Doing nothing.");
        return false;
    }

    if (lci::libcamera_configuration_[camera_number] != lci::CameraConfiguration::kStillPicture) {
        GS_LOG_TRACE_MSG(warning, "DeConfigureForLibcameraStill called, but camera_app was in the wrong configurations (configure was mis-matched). Camera_number was: " + std::to_string((int)lci::libcamera_configuration_[camera_number]) + " Ignoring.");
    }

    try
    {
        GS_LOG_TRACE_MSG(trace, "Tearing down initial camera.");
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
bool TakeLibcameraStill(cv::Mat& img) {

    LibcameraJpegApp *app = ConfigureForLibcameraStill(GolfSimOptions::GetCommandLineOptions().GetCameraNumber());

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
bool CheckForBall(GolfBall& ball, cv::Mat& img) {
    GS_LOG_TRACE_MSG(trace, "CheckForBall called.");

    CameraHardware::CameraModel  cameraModel = CameraHardware::PiGSCam6mmWideLens;

    // TBD - refactor this to get rid of the dummy camera necessity
    GolfSimCamera camera;
    camera.camera_.init_camera_parameters(GolfSimOptions::GetCommandLineOptions().GetCameraNumber(), cameraModel);

    // Ensure we have full resolution
    ConfigCameraForFullScreenWatching(camera);

    cv::Mat initialImg;
    if (!TakeLibcameraStill(initialImg)) {
        GS_LOG_MSG(error, "Failed to take still picture.");
        return false;
    }

    if (initialImg.empty()) {
        return false;
    }

    img = golf_sim::LibCameraInterface::undistort_camera_image(initialImg, GolfSimOptions::GetCommandLineOptions().GetCameraNumber(), cameraModel);

    // LoggingTools::DebugShowImage("First (non-processed) image", initialImg);

    // Figure out where the ball is

    camera.camera_.firstCannedImageFileName = std::string("/mnt/VerdantShare/dev/GolfSim/LM/Images/") + "FirstWaitingImage";
    camera.camera_.firstCannedImage = img;

    cv::Vec2i search_area_center = camera.GetExpectedBallCenter();

    bool expectBall = false;
    bool success = camera.GetCalibratedBall(camera, img, ball, search_area_center, expectBall);

    if (!success) {
        GS_LOG_TRACE_MSG(trace, "Failed to GetCalibratedBall.");
        return false;
    }

    GS_LOG_TRACE_MSG(trace, "kCalibrated BALL -------> " + ball.Format());

    return true;
}


// The following code is only relevant to the camera 2 system
bool WaitForCam2Trigger(cv::Mat& return_image) {
    LibcameraJpegApp app;

    cv::Mat raw_image;

    // Create a camera just to set the resolution and for un-distort operation
    CameraHardware::CameraModel  cameraModel = CameraHardware::PiGSCam6mmWideLens;
    GolfSimCamera c;
    c.camera_.init_camera_parameters(GsCameraNumber::kGsCamera2, cameraModel);

    try
    {
        StillOptions* options = app.GetOptions();

        char dummy_arguments[] = "DummyExecutableName";
        char* argv[] = { dummy_arguments, NULL };

        if (!options->Parse(1, argv))
        {
            GS_LOG_TRACE_MSG(trace, "failed to parse dummy command line.");;
            return -1;
        }

        SetLibCameraLoggingOff();

        if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
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

        GS_LOG_TRACE_MSG(trace, "Camera2 Gain set to: " + std::to_string(options->gain));

        options->immediate = true;
        options->timeout.set("0ms");  // Wait forever for external trigger
        options->denoise = "cdn_off";
        options->nopreview = true;
        // TBD - Currently, we are using the viewfinder stream to take the picture.  Should be corrected.
        options->viewfinder_width = c.camera_.resolution_x_;
        options->viewfinder_height = c.camera_.resolution_y_;
        options->width = c.camera_.resolution_x_;
        options->height = c.camera_.resolution_y_;
        options->shutter.set("11111us"); // Not actually used for external triggering.  Just needs to be set to something
        options->info_text = "";


        if (GolfSimConfiguration::GetPiModel() == GolfSimConfiguration::PiModel::kRPi5) {
            GS_LOG_TRACE_MSG(trace, "Detected PiModel::kRPi5 and camera 2.");
            options->tuning_file = "/usr/share/libcamera/ipa/rpi/pisp/imx296_noir.json";
        }
        else{
            GS_LOG_TRACE_MSG(trace, "Detected PiModel::kRPi4 and camera 2.");
            options->tuning_file = "/usr/share/libcamera/ipa/rpi/vc4/imx296_noir.json";
        }
        
        setenv("LIBCAMERA_RPI_TUNING_FILE", options->tuning_file.c_str(), 1);

        if (options->verbose >= 2)
            options->Print();

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

    // Save the image in memory after un-distorting it for the local camera/lens

    return_image = golf_sim::LibCameraInterface::undistort_camera_image(raw_image, GsCameraNumber::kGsCamera2, cameraModel);

    /* TBD - Assuming we have a decent un-distort matrix, this should not be necessary
    if (GolfSimCamera::kLogIntermediateExposureImagesToFile) {
        LoggingTools::LogImage("", raw_image, std::vector < cv::Point >{}, true, "log_cam2_PRE-undistorted_img.png");
        LoggingTools::LogImage("", return_image, std::vector < cv::Point >{}, true, "log_cam2_POST-undistorted_img.png");
    }
    */

    if (GolfSimOptions::GetCommandLineOptions().camera_still_mode_ ) {

        std::string output_fname = GolfSimOptions::GetCommandLineOptions().output_filename_;
        if (output_fname.empty()) {
            output_fname = LoggingTools::kDefaultSaveFileName;
            GS_LOG_TRACE_MSG(trace, "No output output_filename specified.  Will save picture as: " + output_fname);
        }

        LoggingTools::LogImage("", return_image, std::vector < cv::Point >{}, true, output_fname);
    }

    return true;
}


bool PerformCameraSystemStartup() {

    SetLibCameraLoggingOff();

    // Setup the Pi Camera to be internally or externally triggered as appropriate

    GS_LOG_TRACE_MSG(trace, "PerformCameraSystemStartup: System Mode: " + std::to_string(GolfSimOptions::GetCommandLineOptions().system_mode_));

    switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {

        case SystemMode::kCamera1:
        case SystemMode::kCamera1TestStandalone:
        case SystemMode::kTestSpin: {

            std::string trigger_mode_command = "sudo $PITRAC_ROOT/ImageProcessing/CameraTools/setCameraTriggerInternal.sh";

            GS_LOG_TRACE_MSG(trace, "trigger_mode_command = " + trigger_mode_command);
            int command_result = system(trigger_mode_command.c_str());

            if (command_result != 0) {
                GS_LOG_TRACE_MSG(trace, "system(trigger_mode_command) failed.");
                return false;
            }
        }
        break;

        case SystemMode::kCamera2:
        case SystemMode::kCamera2TestStandalone: {

            std::string trigger_mode_command = "sudo $PITRAC_ROOT/ImageProcessing/CameraTools/setCameraTriggerExternal.sh";

            GS_LOG_TRACE_MSG(trace, "trigger_mode_command = " + trigger_mode_command);
            int command_result = system(trigger_mode_command.c_str());

            if (command_result != 0) {
                GS_LOG_TRACE_MSG(trace, "system(trigger_mode_command) failed.");
                return false;
            }

            // Make sure we are using the NOIR settings
            std::string tuning_file;

            if (GolfSimConfiguration::GetPiModel() == GolfSimConfiguration::PiModel::kRPi5) {
                GS_LOG_TRACE_MSG(trace, "Detected PiModel::kRPi5 and camera 2.");
                tuning_file = "/usr/share/libcamera/ipa/rpi/pisp/imx296_noir.json";
            }
            else {
                GS_LOG_TRACE_MSG(trace, "Detected PiModel::kRPi4 and camera 2.");
                tuning_file = "/usr/share/libcamera/ipa/rpi/vc4/imx296_noir.json";
            }
            setenv("LIBCAMERA_RPI_TUNING_FILE", tuning_file.c_str(), 1);

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

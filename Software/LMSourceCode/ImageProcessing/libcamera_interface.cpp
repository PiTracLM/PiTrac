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

#include "image/image.hpp"

#include <boost/circular_buffer.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "gs_camera.h"
#include "camera_hardware.h"
#include "gs_options.h"
#include "gs_config.h"
#include "logging_tools.h"

#include <libcamera/logging.h>
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

    LibCameraInterface::CameraConfiguration LibCameraInterface::libcamera_configuration_[] = {LibCameraInterface::CameraConfiguration::kNotConfigured, LibCameraInterface::CameraConfiguration::kNotConfigured};

    LibcameraJpegApp* LibCameraInterface::libcamera_app_[] = {nullptr, nullptr};

    void SetLibCameraLoggingOff() {
        GS_LOG_TRACE_MSG(trace, "SetLibCameraLoggingOff");
        libcamera::logSetTarget(libcamera::LoggingTargetNone);
        libcamera::logSetLevel("*", "ERROR");
        libcamera::logSetLevel("", "ERROR");
        RPiCamApp::verbosity = 0;
    }

    bool WatchForHitAndTrigger(const GolfBall& ball, cv::Mat& image, bool& motion_detected) {

        CameraHardware::CameraModel  cameraModel = CameraHardware::PiGSCam6mmWideLens;

        // TBD - refactor this to get rid of the dummy camera necessity
        GolfSimCamera c;
        c.camera_.init_camera_parameters(GsCameraNumber::kGsCamera1, cameraModel);

        if (!WatchForBallMovement(c, ball, motion_detected)) {
            GS_LOG_MSG(error, "Failed to WatchForBallMovement.");
            return false;
        }

        return true;
    }

    bool LibCameraInterface::SendCamera2PreImage(const cv::Mat& raw_image) {
        // We must undistort here, because we are going to immediately send the pre-image
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


    bool WatchForBallMovement(GolfSimCamera& c, const GolfBall& ball, bool & motion_detected) {

        GS_LOG_TRACE_MSG(trace, "WatchForBallMovement");

        // Setup the camera to watch at a high FPS
        cv::Vec2i watchResolution;
        
        if (!ConfigCameraForCropping(ball, c, watchResolution)) {
            GS_LOG_MSG(error, "Failed to ConfigCameraForCropping.");
            return false;
        }
        

        // Determine what the resulting frame rate is in the resulting camera mode  (and confirm the resolution)
        // The camera was stopped after we took the first picture, so re-start for this call
        cv::Vec2i croppedResolution;
        uint croppedFrameRate;
        if (!RetrieveCameraInfo(croppedResolution, croppedFrameRate, true)) {
            GS_LOG_TRACE_MSG(trace, "Failed to RetrieveCameraInfo.");
            return false;
        }

        GS_LOG_TRACE_MSG(trace, "Camera resolution is ( " + std::to_string(croppedResolution[0]) + ", " + std::to_string(croppedResolution[1]) + " ). FPS = " + std::to_string(croppedFrameRate) + ".");

        // Prepare the camera to watch the small ROI at a high frame rate
        motion_detected = false;

        try
        {
            RPiCamEncoder app;
            VideoOptions* options = app.GetOptions();

            char dummy_arguments[] = "DummyExecutableName";
            char* argv[] = { dummy_arguments, NULL };

            if (!options->Parse(1, argv))
            {
                GS_LOG_TRACE_MSG(trace, "failed to parse dummy command line.");;
                return false;
            }

            SetLibCameraLoggingOff();

            // Need to crank gain due to short exposure time at high FPS.
            options->no_raw = true;  // See https://forums.raspberrypi.com/viewtopic.php?t=369927
            options->gain = 15.0;
            options->timeout.set("0ms"); 
            options->denoise = "cdn_off";
            options->framerate = croppedFrameRate;
            options->nopreview = true;
            options->lores_width = 0;
            options->lores_height = 0;
            options->viewfinder_width = 0;
            options->viewfinder_height = 0;
            options->shutter.set(std::to_string((int)(1. / croppedFrameRate * 1000000.)) + "us");   // TBD - should be 1,000,000 for uS setting
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
            GS_LOG_TRACE_MSG(trace, "ball_watcher_event_loop will use post-process file: " + options->post_process_file);

            if (croppedResolution[0] > 0 && croppedResolution[1] > 0) {
                options->width = croppedResolution[0];
                options->height = croppedResolution[1];
            }

            if (options->verbose >= 2)
                options->Print();

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
            cv::Mat& mostRecentFrame = it.mat;

            frame_information += "Frame " + std::to_string(frameIndex) + ": Framerate = " + std::to_string(it.frameRate) + "\n";
            average_frame_rate += it.frameRate;

            if (it.frameRate < slowest_frame_rate) {
                slowest_frame_rate = it.frameRate;
            }

            if (it.frameRate > fastest_frame_rate) {
                fastest_frame_rate = it.frameRate;
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




// For example, to set the GS cam back to its default, use  "(0, 0)/1456x1088"
// 128x96 can deliver 532 FPS on the GS cam.
std::string GetCmdLineForMediaCtlCropping(cv::Vec2i croppedHW, cv::Vec2i cropOffsetXY) {

    std::string s;

    int device_number = (GolfSimConfiguration::GetPiModel() == GolfSimConfiguration::PiModel::kRPi5) ? 6 : 10;

    s += "#!/bin/sh\n";
    for (int m = 0; m <= 5; m++) {
        s += "if  media-ctl -d \"/dev/media" + std::to_string(m) + "\" --set-v4l2 \"'imx296 " + std::to_string(device_number) + "-001a':0 [fmt:SBGGR10_1X10/" + std::to_string(croppedHW[0]) + "x" + std::to_string(croppedHW[1]) + " crop:(" + std::to_string(cropOffsetXY[0]) + "," + std::to_string(cropOffsetXY[1]) + ")/" + std::to_string(croppedHW[0]) + "x" + std::to_string(croppedHW[1]) + "]\" > /dev/null;  then  echo -e \"/dev/media" + std::to_string(m) + "\" > /dev/null; break;  fi\n";
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


bool ConfigCameraForCropping(GolfBall ball1, GolfSimCamera& c, cv::Vec2i& watchResolution) {

    if (LibCameraInterface::camera_crop_configuration_ == LibCameraInterface::kCropped  &&
        LibCameraInterface::current_watch_resolution_ == watchResolution) {
        // Don't reset the crop if we don't need to
        return true;
    }

    uint largestInscribedSquareSideLength = (double)(CvUtils::CircleRadius(ball1.ball_circle_)) * sqrt(2);

    float watchingCropWidth = LibCameraInterface::kMaxWatchingCropWidth;
    float watchingCropHeight = LibCameraInterface::kMaxWatchingCropHeight;

    // Ensure the ball is not so big that the inscribed watching area is larger than what we want for high FPS
    if (watchingCropWidth > largestInscribedSquareSideLength) {
        GS_LOG_TRACE_MSG(trace, "Reducing cropping window because largest ball square side = " + std::to_string(largestInscribedSquareSideLength));
        watchingCropHeight *= (largestInscribedSquareSideLength / watchingCropWidth);
        watchingCropWidth = largestInscribedSquareSideLength;
    }
    
    // Starting with Pi 5, the crop height and width have to be divisible by 2
    watchingCropWidth += ((int)watchingCropWidth % 2);
    watchingCropHeight += ((int)watchingCropHeight % 2);

    float ballRadius = CvUtils::CircleRadius(ball1.ball_circle_);
    float ballX = CvUtils::CircleX(ball1.ball_circle_);
    float ballY = CvUtils::CircleY(ball1.ball_circle_);

    float cropOffsetX = c.camera_.resolution_x_ - ballX - watchingCropWidth / 2.0;
    float cropOffsetY = c.camera_.resolution_y_ - ballY - watchingCropHeight / 2.0;



    std::string mediaCtlCmd = GetCmdLineForMediaCtlCropping(cv::Vec2i((uint)watchingCropWidth, (uint)watchingCropHeight), cv::Vec2i((uint)cropOffsetX, (uint)cropOffsetY));
    GS_LOG_TRACE_MSG(trace, "mediaCtlCmd = " + mediaCtlCmd);
    int cmdResult = system(mediaCtlCmd.c_str());

    if (cmdResult != 0) {
        GS_LOG_TRACE_MSG(trace, "system(mediaCtlCmd) failed.");
        return false;
    }

    // Setup return information so that the camera can be set to a matching resolution
    watchResolution[0] = watchingCropWidth;
    watchResolution[1] = watchingCropHeight;

    // Save the curr\ent cropping setup in hopes that we might be able to
    // avoid another media-ctl call next time
    LibCameraInterface::current_watch_resolution_ = watchResolution;
    // Signal that the cropping setup has changed so that we know to change it back later.
    LibCameraInterface::camera_crop_configuration_ = LibCameraInterface::kCropped;

    return true;
}

// TBD - This really seems like it should exist in the gs_camera module?
bool CheckForBall(GolfBall& ball, cv::Mat& img) {
    GS_LOG_TRACE_MSG(trace, "CheckForBall called.");

    CameraHardware::CameraModel  cameraModel = CameraHardware::PiGSCam6mmWideLens;

    // TBD - refactor this to get rid of the dummy camera necessity
    GolfSimCamera c;
    c.camera_.init_camera_parameters(GolfSimOptions::GetCommandLineOptions().GetCameraNumber(), cameraModel);

    // Ensure we have full resolution
    ConfigCameraForFullScreenWatching(c);

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

    c.camera_.firstCannedImageFileName = std::string("/mnt/VerdantShare/dev/GolfSim/LM/Images/") + "FirstWaitingImage";
    c.camera_.firstCannedImage = img;

    cv::Vec2i search_area_center = c.GetExpectedBallCenter();

    bool expectBall = false;
    bool success = c.GetCalibratedBall(c, img, ball, search_area_center, expectBall);

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

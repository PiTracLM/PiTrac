/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Attempts to hide some details about the specific camera hardware in use.
// Coding for any new cameras should start here

#pragma once

#include <string>
#include "logging_tools.h"
#include "cv_utils.h"
#include "gs_globals.h"
#include "gs_options.h"

namespace golf_sim {

    class CameraHardware {

    public:

        // If set to >0, init_camera_parameters will use these values instead of camera-model-specific values
        // That way, if you want to use an image with X,Y resolution different than the current camera, you
        // can sort of make-believe that a camera with just your X,Y resolution took the picture.
        // TBD - Should not be static
        static int resolution_x_override_;
        static int resolution_y_override_;

        int CAMERA_NUM_PICTURES_TO_TAKE = 2;

        enum CameraModel {
            PiCam13 = 1,
            PiCam2 = 2,
            PiHQCam6mmWideLens = 3,
            PiGSCam6mmWideLens = 4,
            kUnknown = 100
        };

        // An initial set of states to simulate a camera repeatedly taking pictures until
        // (at some point in time), the object of interest in the image changes.
        // Was used to test the camera 1 movement processing.
        enum TestVideoState {
            ImagesLoaded,
            TakingInitialStaticFrames,
            FirstMovementFrame,
            BallGoneFrames
        };

        GsCameraNumber camera_number_ = GsCameraNumber::kGsCamera1;

        CameraModel cameraModel = CameraModel::PiHQCam6mmWideLens;
        float focal_length_ = 0;        // In millimeters
        float horizontalFoV = 0;        // In degrees
        float verticalFoV = 0;          // In degrees
        float sensor_width_ = 0;        // The physical size of the camera sensor, inclusive of all the pixels.  In mm
        float sensor_height_ = 0;       // In mm

        cv::Mat calibrationMatrix;
        cv::Mat cameraDistortionVector;

        // These SHOULD depend on camera model - TBD?
        // -1 if not set via init_camera_parameters or otherwise overridden
        int resolution_x_ = -1;
        int resolution_y_ = -1;

        cv::Vec2d camera_angles_;

        // if set, the camera will use this image (file) as if (it were the image that the
        // camera took on the Pi, regardless of operating system.  First will be used first, then
        // if another picture is needed, the second will be used.
        std::string firstCannedImageFileName;
        std::string secondCannedImageFileName;

        cv::Mat firstCannedImage;
        cv::Mat secondCannedImage;

        // true; if the camera is all ready to go to take a picture.
        bool cameraReady = false;

        CameraHardware();
        ~CameraHardware();

        // Used when using test images instead of live photos
        // Pre-loads the test images to allow for faster simulated returns of them
        void load_test_images();

        void init_camera();
        void deinit_camera();

        void init_camera_parameters(const GsCameraNumber camera_number, const CameraModel model);

        bool prepareToTakePhoto();
        cv::Mat take_photo();

        bool prepareToTakeVideo();
        cv::Mat getNextFrame();

        // TBD - Probably should be private, but the higher-level golf_sim_camara needs to check this sometimes
        bool cameraInitialized = false;

    private:

        // Counts the number of static images that have been sent so far if this camera is
        // being emulated by software to take the place of a real camera.
        int staticImagesSent = 0;

        TestVideoState testVideoState = TestVideoState::ImagesLoaded;
        int currentStaticImageIndex = 0;
    };

}

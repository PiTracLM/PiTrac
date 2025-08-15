
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


// Prepare for conditional compilation in the Windows environment, where, for example,
// there is no PiCamera
#ifdef __unix__   
#include <libcamera/camera.h>
#include <unistd.h>
// using namespace boost::detail::win32;
#elif defined(_WIN32) || defined(WIN32) 
//#include <windows.h>
// TBD
#endif

#include <sstream>
#include "gs_options.h"
#include "gs_config.h"
#include "camera_hardware.h"


namespace golf_sim {

    // When running in Windows (instead of the Pi), the following image will
    // be used to simulate the PiCameras taking a real picture
    //kTestPhotoDefault = "./FakePiCameraPhotoOfGolfBall-Clr-Yellow-Flat.png"
    //kTestPhotoDefault = "./FakePiCameraPhotoOfGolfBall-Clr-White-Flat.png"   // Having problems with this one
    const std::string kTestPhotoDefault = "./Images/FakePiCameraPhotoOfGolfBall-Clr-White-2-feet-HiRes_01.png";


    int CameraHardware::resolution_x_override_ = -1;
    int CameraHardware::resolution_y_override_ = -1;

    const int kStationaryBallIndex_00 = 0;
    const int kStationaryBallIndex_01 = 1;
    const int kPreHitCloseBallIndex_00 = 2;
    const int kPostHitBallGoneIndex_00 = 3;

    const int kMaxTestImageIndex = 4;

#ifdef __unix__
	const std::string kBaseTestDir = "/mnt/VerdantShare/dev/GolfSim/LM/Images/";
#else
	const std::string kBaseTestDir = "D:\\GolfSim\\C++Code\\GolfSim\\Images\\";
#endif

    /* The following are groups of test images taken at different resolutions
    const std::string kStationaryBallFileName_00 = kBaseTestDir + "move_test_ball_present_1024w_00.png";
    const std::string kStationaryBallFileName_01 = kBaseTestDir + "move_test_ball_present_1024w_01.png";
    const std::string kPreHitCloseBallFileName_00 = kBaseTestDir + "move_test_ball_and_club_present_1024w_00.png";
    const std::string kPostHitBallGoneFileName_00 = kBaseTestDir + "move_test_no_ball_present_1024w_00.png";

    const std::string kStationaryBallFileName_00 = kBaseTestDir + "move_test_ball_present_2592w_00.png";
    const std::string kStationaryBallFileName_01 = kBaseTestDir + "move_test_ball_present_2592w_01.png";
    const std::string kPreHitCloseBallFileName_00 = kBaseTestDir + "move_test_ball_and_club_present_2592w_00.png";
    const std::string kPostHitBallGoneFileName_00 = kBaseTestDir + "move_test_no_ball_present_2592w_00.png";
    */

    // Currently-selected resolution from above is here:
    const std::string kStationaryBallFileName_00 = kBaseTestDir + "move_test_ball_present_2592w_00.png";
    const std::string kStationaryBallFileName_01 = kBaseTestDir + "move_test_ball_present_2592w_01.png";
    const std::string kPreHitCloseBallFileName_00 = kBaseTestDir + "move_test_ball_and_club_present_2592w_00.png";
    const std::string kPostHitBallGoneFileName_00 = kBaseTestDir + "move_test_no_ball_present_2592w_00.png";



    const int kNumStationaryImages = 2;

    const int kNumStaticImagesToSend = 14;

    cv::Mat TestHitSequence[kMaxTestImageIndex];


    CameraHardware::CameraHardware() {
    }

    CameraHardware::~CameraHardware() {
    }

    CameraHardware::CameraModel CameraHardware::string_to_camera_model(const std::string& model_enum_value_string) {

        std::map<std::string, int> camera_table =
        { { "1", CameraModel::PiCam13 },
            { "2", CameraModel::PiCam2 },
            { "3", CameraModel::PiHQ },
            { "4", CameraModel::PiGS },
            { "5", CameraModel::InnoMakerIMX296GS_Mono },
            { "100", CameraModel::kCameraUnknown },
        };
        if (camera_table.count(model_enum_value_string) == 0)
            throw std::runtime_error("Invalid camera model_enum_value_string: " + model_enum_value_string + ".  Expected an integer. Check environment variables.");
        
        CameraHardware::CameraModel camera_model = (CameraHardware::CameraModel)camera_table[model_enum_value_string];

        return camera_model;
    }

    CameraHardware::LensType CameraHardware::string_to_lens_type(const std::string& lens_enum_value_string) {

        std::map<std::string, int> lens_table =
        { { "1", LensType::Lens_6mm },
            { "2", LensType::Lens_3_6mm_M12 },
            { "100", LensType::kLensUnknown },
        };
        if (lens_table.count(lens_enum_value_string) == 0)
            throw std::runtime_error("Invalid camera lens_enum_value_string: " + lens_enum_value_string + ".  Expected an integer. Check environment variables.");

        CameraHardware::LensType lens_type = (CameraHardware::LensType)lens_table[lens_enum_value_string];

        return lens_type;
    }
    
    bool CameraHardware::camera_is_mono() const {
        return (camera_model_ == CameraModel::InnoMakerIMX296GS_Mono);
    }


    void CameraHardware::load_test_images() {

        // There are a couple of stationary pictures to simulate slight vibrations
        TestHitSequence[kStationaryBallIndex_00] = cv::imread(kStationaryBallFileName_00, cv::IMREAD_COLOR);
        TestHitSequence[kStationaryBallIndex_01] = cv::imread(kStationaryBallFileName_01, cv::IMREAD_COLOR);
        
        // This picture is of the club just about to hit the ball, in close proximity 
        TestHitSequence[kPreHitCloseBallIndex_00] = cv::imread(kPreHitCloseBallFileName_00, cv::IMREAD_COLOR);

        TestHitSequence[kPostHitBallGoneIndex_00] = cv::imread(kPostHitBallGoneFileName_00, cv::IMREAD_COLOR);

        // Use whatever (simulated) resolution we find in the image files we just read
        resolution_x_override_ = TestHitSequence[kStationaryBallIndex_00].cols;
        resolution_y_override_ = TestHitSequence[kStationaryBallIndex_00].rows;
    }

    cv::Mat CameraHardware::getNextFrame() {
        cv::Mat img;

        // Basically a state machine based on how far in the simulate sequence of images we are
        switch (testVideoState) {
            case ImagesLoaded:
                // We are just getting started.  Send first static image and start counter
                currentStaticImageIndex = 0;
                staticImagesSent = 0;
                testVideoState = TakingInitialStaticFrames;
                img = TestHitSequence[kStationaryBallIndex_00];
                break;

            case TakingInitialStaticFrames :
                staticImagesSent++;

                // Check if we are done sending the sequence of static images and are ready to move on to a club strike
                if (staticImagesSent > kNumStaticImagesToSend) {
                    testVideoState = FirstMovementFrame;
                }

                // Roll through the static images that we have (each will be slightly different)
                currentStaticImageIndex++;
                currentStaticImageIndex = currentStaticImageIndex % kNumStationaryImages;
                img = TestHitSequence[kStationaryBallIndex_00 + currentStaticImageIndex];
                break;

            case FirstMovementFrame:
                testVideoState = BallGoneFrames;
                // Send the simulated image of a club about to strike the ball
                img = TestHitSequence[kPreHitCloseBallIndex_00];
                break;
                
            case BallGoneFrames:
                // We stay in this state
                testVideoState = BallGoneFrames;
                // Send the simulated image of just an empty place where the ball used to be
                // This simulates the post-hit condition.
                img = TestHitSequence[kPostHitBallGoneIndex_00];
                break;

            default:
                GS_LOG_MSG(error, "CameraHardware::getNextFrame() called with testVideoState=" + std::to_string(testVideoState));
                return cv::Mat{};
        }

        // Make sure the image we got is the dimensions that we are expecting
        if (img.rows != resolution_y_ || img.cols != resolution_x_) {
            GS_LOG_MSG(error, "Returned photo does not match camera resolution!");
        }

        return img;
    }

    // TBD - Need to change signature to accept the desired resolution.  Different resolutions can result in
    // very different calibration matrices.
    void CameraHardware::init_camera_parameters(const GsCameraNumber camera_number, const CameraModel model, const LensType lens_type, const bool use_default_focal_length) {

        GS_LOG_TRACE_MSG(trace, "init_camera_parameters called with camera number = " + std::to_string(camera_number) + " and model = " + std::to_string(model) + ", and lens_type = " + std::to_string(lens_type) );

        int sizes[3] = { 3, 3 };
        
        camera_number_ = camera_number;
        camera_model_ = model;
        lens_type_ = lens_type;

        if (lens_type == Lens_6mm) {
            focal_length_ = 6.0f;
            horizontalFoV_ = 50.0f;
            verticalFoV_ = 50.0f;
            expected_ball_radius_pixels_at_40cm_ = 87;
        }

        if (lens_type == Lens_3_6mm_M12) {
            focal_length_ = 3.6f;
            horizontalFoV_ = 70.0f;
            verticalFoV_ = 70.0f;
            expected_ball_radius_pixels_at_40cm_ = 45;
        }

        // This section deals with the common characteristics of some of the cameras
        if (model == PiGS || 
            model == InnoMakerIMX296GS_Mono) {

        GS_LOG_TRACE_MSG(trace, "Initializing with a PiGS or InnoMakerIMX296GS_Mono camera." );
            // Sensor pixel width is 3.45uM square?  No - 6.33mm diagonal.  It appears that
            // the actual width is the full resolution (1456)  * 3.4uM = 4.95mm,
            // Not simply the diagonal sensor width

            sensor_width_ = (float)5.077365371;   // 4.45;   // (1456.0 * 3.4) / 1000;   // = 4.95; //  In mm 6.3 / sqrt(2.0);  // TBD - Confirm math from diagonal measurement
            sensor_height_ = (float)3.789078635;    // 4.45; //(1088.0 * 3.4) / 1000;   //  In mm 6.3 / sqrt(2.0);
            
            if (resolution_x_override_ > 0 && resolution_y_override_ > 0) {
                resolution_x_ = resolution_x_override_;
                resolution_y_ = resolution_y_override_;
            }
            else {
                //  Defaults
                resolution_x_ = 1456;
                resolution_y_ = 1088;
            }

            // For some reason, the effective resolution when taking video (and in particular, when cropping)
            // appears to be less in the y direction than when taking a still image.
            video_resolution_x_ = resolution_x_;
            video_resolution_y_ = 1080;

            GS_LOG_TRACE_MSG(trace, "Video resolution (x,y) is: " + std::to_string(video_resolution_x_) + "/" + std::to_string(video_resolution_y_) + ".");

            // Attempt to get the expected ball radius from the .json file
            std::string ball_radius_pixels_at_40cm_name = "kExpectedBallRadiusPixelsAt40cmCamera" + std::string(camera_number == GsCameraNumber::kGsCamera1 ? "1" : "2");

            int expected_ball_radius_pixels_at_40cm_from_config_file = -1;

            GolfSimConfiguration::SetConstant("gs_config.cameras." + ball_radius_pixels_at_40cm_name, expected_ball_radius_pixels_at_40cm_from_config_file);

            // The default will be used unless the .json file has a value, in which case that value will be used.

            if (expected_ball_radius_pixels_at_40cm_from_config_file < 1) {
                GS_LOG_TRACE_MSG(trace, ball_radius_pixels_at_40cm_name + " not set in .json config file.  Using default instead of : " + std::to_string(expected_ball_radius_pixels_at_40cm_)); 
            }
            else {
                expected_ball_radius_pixels_at_40cm_ = expected_ball_radius_pixels_at_40cm_from_config_file;
                GS_LOG_TRACE_MSG(info, "Over-riding default " + ball_radius_pixels_at_40cm_name + " using value from.json config file of : " + std::to_string(expected_ball_radius_pixels_at_40cm_));
            }

            cv::Mat camera_calibration_matrix_values = cv::Mat(3, 3, CV_64F);
            camera_calibration_matrix_values = cv::Mat::zeros(camera_calibration_matrix_values.rows, 
                                                               camera_calibration_matrix_values.cols, 
                                                               camera_calibration_matrix_values.type());
            cv::Mat camera_distortion_values = cv::Mat(1, 5, CV_64F);
            camera_distortion_values = cv::Mat::zeros(camera_distortion_values.rows,
                camera_distortion_values.cols,
                camera_distortion_values.type());

            std::string calibration_element_name = "kCamera" + std::to_string((int)camera_number_) + "CalibrationMatrix";
            std::string distortion_element_name = "kCamera" + std::to_string((int)camera_number_) + "DistortionVector";

            if (GolfSimConfiguration::PropertyExists("gs_config.cameras." + calibration_element_name) ) {
                GolfSimConfiguration::SetConstant("gs_config.cameras." + calibration_element_name, camera_calibration_matrix_values);
                GolfSimConfiguration::SetConstant("gs_config.cameras." + distortion_element_name, camera_distortion_values);
            }
            
            bool calibration_information_is_valid = (camera_calibration_matrix_values.at<double>(0,0) != 0.0) &&
                                                    (camera_distortion_values.at<double>(0, 0) != 0.0);

            if (!calibration_information_is_valid) {
                // We don't have calibration parameters for this resolution
                GS_LOG_MSG(trace, "No calibration parameters for resolution (width = " + std::to_string(resolution_x_) + ") are available.  Using identity (no-transform) parameters");

                calibrationMatrix_ = (cv::Mat_<float>(3, 3) <<
                    1, 0, 0,
                    0, 1, 0,
                    0, 0, 1);

                cameraDistortionVector_ = (cv::Mat_<float>(1, 5) <<
                    1, 1, 1, 1, 1);

                use_calibration_matrix_ = false;
            }
            else {
                GS_LOG_TRACE_MSG(trace, calibration_element_name + " = ");
                std::stringstream ss1; ;
                ss1 << camera_calibration_matrix_values << std::endl;
                GS_LOG_TRACE_MSG(trace, ss1.str());

                GS_LOG_TRACE_MSG(trace, distortion_element_name + " = ");
                std::stringstream ss2; ;
                ss2 << camera_distortion_values << std::endl;
                GS_LOG_TRACE_MSG(trace, ss2.str());

                calibrationMatrix_ = camera_calibration_matrix_values;
                cameraDistortionVector_ = camera_distortion_values;

                use_calibration_matrix_ = false;
            }
        }
        else if (model == PiHQ) {

            // TBD - This camera is no longer supported - REMOVE
            focal_length_ = 6.25f;
            horizontalFoV_ = 63.0f;  // TBD - Not certain of FoV yet
            verticalFoV_ = 50.0f;
            sensor_width_ = 6.287f;
            sensor_height_ = 4.712f;

            if (resolution_x_override_ > 0 && resolution_y_override_ > 0) {
                resolution_x_ = resolution_x_override_;
                resolution_y_ = resolution_y_override_;
            }
            else {
                //  Defaults
                resolution_x_ = 4056;
                resolution_y_ = 3040;
            }

            video_resolution_x_ = resolution_x_;
            video_resolution_y_ = resolution_y_;

            // These are defaults from measurements from a real camera, but may differ from
            // camera instance to instance.
            if (resolution_x_ == 4056) {
                calibrationMatrix_ = (cv::Mat_<float>(3, 3) <<
                    3942.884592, 0.000000, 1992.630087,
                    0.000000, 3929.331993, 1656.927712,
                    0.000000, 0.000000, 1.000000);

                cameraDistortionVector_ = (cv::Mat_<float>(1, 5) <<
                    -0.505410, 0.293051, -0.008886, 0.002192, -0.126480);
                use_calibration_matrix_ = true;
            }
            else {
                // We don't have calibration parameters for this resolution
                LoggingTools::Warning("No calibration parameters for resolution (width = " + std::to_string(resolution_x_) + ") are available.  Using identify parameters");

                calibrationMatrix_ = (cv::Mat_<float>(3, 3) <<
                    1, 0, 0,
                    0, 1, 0,
                    0, 0, 1);

                cameraDistortionVector_ = (cv::Mat_<float>(1, 5) <<
                    1, 1, 1, 1, 1);

                use_calibration_matrix_ = false;
            }
        }
        else if (model == PiCam2) {
            // TBD - This camera is no longer supported - REMOVE
            focal_length_ = 3.04f;
            horizontalFoV_ = 62.2f;
            verticalFoV_ = 48.8f;
            sensor_width_ = 3.68f;
            sensor_height_ = 2.76f;

            // Other possible resolutions for this camera:
            //            resolution_x_ = 1024;
            //            resolution_y_ = 768;
            //            resolution_x_ = 2592;
            //            resolution_y_ = 1944;
            //           resolution_x_ = 1024;
            //           resolution_y_ = 768;

            if (resolution_x_override_ > 0 && resolution_y_override_ > 0) {
                resolution_x_ = resolution_x_override_;
                resolution_y_ = resolution_y_override_;
            }
            else {
                //  Defaults
                resolution_x_ = 3280;
                resolution_y_ = 2464;
            }

            video_resolution_x_ = resolution_x_;
            video_resolution_y_ = resolution_y_;

            if (resolution_x_ == 3280) {
                calibrationMatrix_ = (cv::Mat_<float>(3, 3) <<
                    2716.386350, 0.000000, 1766.508245,
                    0.000000, 2712.451173, 1323.332502,
                    0.000000, 0.000000, 1.000000);

                cameraDistortionVector_ = (cv::Mat_<float>(1, 5) <<
                    0.180546, -0.486020,   0.015867,  0.020743,  0.242820);
                use_calibration_matrix_ = true;
            }
            else if (resolution_x_ == 2592) {
                calibrationMatrix_ = (cv::Mat_<float>(3, 3) <<
                    2031.299942, 0.000000, 1228.929011,
                    0.000000, 2034.953849, 937.969291,
                    0.000000, 0.000000, 1.000000);

                cameraDistortionVector_ = (cv::Mat_<float>(1, 5) <<
                    0.159431, -0.181717, 0.004414, -0.004092, -0.427269);

                    use_calibration_matrix_ = true;
            }
            else {
                // We don't have calibration parameters for this resolution
                LoggingTools::Warning("No calibration parameters for resolution (width = " + std::to_string(resolution_x_) + ") are available.  Using identify parameters");

                calibrationMatrix_ = (cv::Mat_<float>(3, 3) <<
                    1, 0, 0,
                    0, 1, 0,
                    0, 0, 1);

                cameraDistortionVector_ = (cv::Mat_<float>(1, 5) <<
                    1, 1, 1, 1, 1);

                use_calibration_matrix_ = false;
            }
         }
        else if (model == PiCam13) {
            focal_length_ = 3.6f;
            horizontalFoV_ = 53.5f;
            verticalFoV_ = 41.41f;
            // TBD - Other params for other potential resolutions

            //            resolution_x_ = 1024;
            //            resolution_y_ = 768;
            resolution_x_ = 2592;
            resolution_y_ = 1944;
            video_resolution_x_ = resolution_x_;
            video_resolution_y_ = resolution_y_;

            //            resolution_x_ = 1024;
            //            resolution_y_ = 768;
        }
        else {
            // Currently, these are the same as the PiCam1.3
            focal_length_ = 3.6f;
            resolution_x_ = 1024;
            resolution_y_ = 768;
            video_resolution_x_ = resolution_x_;
            video_resolution_y_ = resolution_y_;
        }


        // Customize any parameters that have been set in the JSON config file

        std::string tag;

        if (!use_default_focal_length) {
            tag = "gs_config.cameras.kCamera" + std::to_string(camera_number_) + "FocalLength";
            if (GolfSimConfiguration::PropertyExists(tag)) {
                GolfSimConfiguration::SetConstant(tag, focal_length_);
                GS_LOG_TRACE_MSG(trace, "Setting focal length (from JSON file) = " + std::to_string(focal_length_));
            }
        }

        tag = "gs_config.cameras.kCamera" + std::to_string(camera_number_) + "Angles";
        GolfSimConfiguration::SetConstant(tag, camera_angles_);

        cameraInitialized = true;
    }


    // Must be called before taking a picture
    // TBD - Deprecated
    bool CameraHardware::prepareToTakeVideo() {
        GS_LOG_TRACE_MSG(trace, "prepareToTakeVideo called with resolution(X,Y) = (" + std::to_string(resolution_x_) + "," + std::to_string(resolution_y_) + ")");

        staticImagesSent = 0;
        testVideoState = TestVideoState::ImagesLoaded;
        currentStaticImageIndex = 0;

#if defined(_WIN32) || defined(WIN32)
        //        boost::detail::win32::sleep(2);
        load_test_images();

        testVideoState = ImagesLoaded;

        cameraReady = true;
        return true;
#endif

#ifdef __unix__   

        // We are on the Pi, so take the photo for reals
        // TBD - Do as much of the settings as we can in the constructor instead of separately!
/**** TBD
        camera = PiCamera();
        camera.resolution = (resolution_x_, resolution_y_);
        camera.shutter_speed = 3000;
        camera.framerate = 90;// 800
        #camera.start_preview();

        // Set up the stream here so there is less to do later
        streams = [BytesIO() for i in range(CAMERA_NUM_PICTURES_TO_TAKE)];


        // Camera warm-up time - Do we really need this?
        GS_LOG_TRACE_MSG(trace, "warming up the camera");
        sleep(2);

        GS_LOG_TRACE_MSG(trace, "camera ready");
        cameraReady = true;
	***/
        return true;
#endif
    }

    // Must be called before taking a picture
    bool CameraHardware::prepareToTakePhoto() {
        GS_LOG_TRACE_MSG(trace, "prepareToTakePhoto called with resolution(X,Y) = (" + std::to_string(resolution_x_) + "," + std::to_string(resolution_y_) + ")");

        staticImagesSent = 0;
        testVideoState = TestVideoState::ImagesLoaded;
        currentStaticImageIndex = 0;

#if defined(_WIN32) || defined(WIN32)
        //        boost::detail::win32::sleep(2);
        load_test_images();

        testVideoState = ImagesLoaded;

        cameraReady = true;
        return true;
#endif

#ifdef __unix__   

        // We are on the Pi, so take the photo for reals
        // TBD - Do as much of the settings as we can in the constructor instead of separately!
/**** TBD
        camera = PiCamera();
        camera.resolution = (resolution_x_, resolution_y_);
        camera.shutter_speed = 3000;
        camera.framerate = 90;// 800
        #camera.start_preview();

        // Set up the stream here so there is less to do later
        streams = [BytesIO() for i in range(CAMERA_NUM_PICTURES_TO_TAKE)];


        // Camera warm-up time - Do we really need this?
        GS_LOG_TRACE_MSG(trace, "warming up the camera");
        sleep(2);

        GS_LOG_TRACE_MSG(trace, "camera ready");
        cameraReady = true;
**/
        return true;
#endif
    }

    void CameraHardware::init_camera() {
        GS_LOG_TRACE_MSG(trace, "init_camera");

#if defined(_WIN32) || defined(WIN32)
//        boost::detail::win32::sleep(.5);
        cameraReady = false;
#endif


#ifdef __unix__   

        //camera.stop_preview()
/***
        camera.close();
        cameraReady = false;
*/
#endif
    }

// Placeholder for later
    void CameraHardware::deinit_camera() {
        GS_LOG_TRACE_MSG(trace, "deinit_camera");

#if defined(_WIN32) || defined(WIN32)
        //        boost::detail::win32::sleep(.5);
        cameraReady = false;
#endif


#ifdef __unix__   

/***
        //camera.stop_preview()
        camera.close();
***/
        cameraReady = false;
#endif
    }



    cv::Mat CameraHardware::take_photo() {

        static int cannedPhotoIndex = 0;

        GS_LOG_TRACE_MSG(trace, "takePhoto called with resolution(X,Y) = (" + std::to_string(resolution_x_) + "," + std::to_string(resolution_y_) + ")");

        cv::Mat img;

        if (!cameraReady) {
            GS_LOG_MSG(error, "take_photo called prior to calling prepareToTakePhoto");
            return img;
        }

        // If we are not running on the Pi hardware, just return a canned
        // photo.  The default photo will have been taken 2 feet away.
#if defined(_WIN32) || defined(WIN32)

        if (firstCannedImageFileName.empty()) {
            LoggingTools::Warning("firstCannedImageFileName not set when take_photo called on Windows");
            //img = cv2.imread(kTestPhotoDefault, cv2.IMREAD_COLOR);
            img = cv::imread(kTestPhotoDefault, cv::IMREAD_COLOR);
        }
        else {
            if (cannedPhotoIndex == 0) {
                //img = cv2.imread(firstCannedImageFileName, cv2.IMREAD_COLOR)
                if (!firstCannedImage.empty()) {
                    img = firstCannedImage;
                }
                else {
                    img = cv::imread(firstCannedImageFileName, cv::IMREAD_COLOR);
                }
                cannedPhotoIndex++;
            }
            else {
                if (!secondCannedImage.empty()) {
                    img = secondCannedImage;
                }
                else {
                    img = cv::imread(secondCannedImageFileName, cv::IMREAD_COLOR);
                }
                cannedPhotoIndex = 0;
            }
        }

        // TBD - Need to check expected resolution versus resolution of the fake image
        if (img.empty()) {
            GS_LOG_MSG(error, "Could not open fake PiCamera image file { " + kTestPhotoDefault);
        }

        // Convert if (we will use openCV to get the image (which is overkill?) im_pil = Image.fromarray(img)
        return img;
#endif

#ifdef __unix__
/***
        // GS_LOG_TRACE_MSG(trace, "takePhoto {  calling capure()")
        #camera.capture(streams[0], format = imageFormat, use_video_port = false;)

            // TBD - Try taking a few pix
            camera.capture_sequence(streams, format = imageFormat, use_video_port = false;)
            GS_LOG_TRACE_MSG(trace, "takePhoto {  returned from capture()")
            cameraReady = false;

        images = [];
        for i in range(len(streams)) {
            streams[i].seek(0);
            images.append(Image.open(streams[i]));

            return images;
***/
        return img;
#endif
    }
}

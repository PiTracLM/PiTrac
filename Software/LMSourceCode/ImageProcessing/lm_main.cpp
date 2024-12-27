/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// ImageProcessing.cpp { This file contains the main test function. Program execution begins and ends there.
//

#include <boost/timer/timer.hpp>

#include <opencv2/calib3d/calib3d.hpp>

#include <iostream>
#include <cstring>
#include <filesystem>

#include "gs_globals.h"
#include "golf_ball.h"
#include "gs_options.h"
#include "gs_gspro_results.h"
#include "gs_ui_system.h"
#include "gs_config.h"
#include "gs_results.h"

#include "logging_tools.h"
#include "colorsys.h"
#include "ball_image_proc.h"
#include "cv_utils.h"
#include "gs_camera.h"
#include "gs_config.h"
#include "pulse_strobe.h"


#include "gs_gspro_results.h"
#include "gs_gspro_test_server.h"
#include "gs_gspro_response.h"
#include "gs_sim_interface.h"
#include "gs_e6_interface.h"

#include "gs_fsm.h"
#include "gs_ipc_system.h"
#include "libcamera_interface.h"


namespace fs = std::filesystem;

using namespace golf_sim;

const double kLocationTolerancePercent = 10;


// The result files we create will be prefixed with this
static constexpr std::string_view TEST_IMAGE_PREFIX = "TEST_RESULT_GetBall_";

#ifdef __unix__
const std::string kBaseTestDir = "/mnt/VerdantShare/dev/GolfSim/LM/Images/";
#else
const std::string kBaseTestDir = "V:\\Images\\";  // "D:\\GolfSim\\LM\\Images\\";
#endif


BallImageProc *get_image_processor() {
    BallImageProc  *ip = new BallImageProc;
    // TBD - Setup as necessary
    return ip;
}


void test_image(std::string_view subdir, std::string_view filename) {
    BOOST_LOG_FUNCTION();

    fs::path dir(subdir);
    fs::path file(filename);
    fs::path fullPath = dir / file;
    std::string fname = fullPath.generic_string();

    GolfBall ball;

    boost::to_lower(fname);
    size_t i = fname.find("clr-");

    std::string color;
    if (i > 0) {
        color = fname.substr(i);
    }

    if (!color.empty()) {

        boost::to_lower(color);

        if (color.find("white") > 0) {
            ball.ball_color_ = GolfBall::BallColor::kWhite;
        }
        else if (color.find("orange") > 0) {
            ball.ball_color_ = GolfBall::BallColor::kOrange;
        }
        else if (color.find("yellow") > 0) {
            ball.ball_color_ = GolfBall::BallColor::kYellow;
        }
        else if (color.find("green") > 0) {
            ball.ball_color_ = GolfBall::BallColor::kOpticGreen;
        }
        else {
            // For this part of our test suite, if there is no specified color, white(not kUnknown) is our best bet
            ball.ball_color_ = GolfBall::BallColor::kWhite;
        }
    }

    BallImageProc *ip = get_image_processor();

    cv::Mat img = cv::imread(fname, cv::IMREAD_COLOR);
    ip->image_name_ = fname;

    cv::Rect nullROI;
    std::vector<GolfBall> return_balls;
    bool result = ip->GetBall(img, ball, return_balls, nullROI, BallImageProc::BallSearchMode::kFindPlacedBall );

    if (!result || return_balls.empty()) {
        GS_LOG_MSG(error, "GetBall() failed to get a ball.");
        return;
    }

    GolfBall result_ball = return_balls.front();

    // If there were any debugging windows, get rid of them now
    cv::destroyAllWindows();

    // Create a single output file with the result overlaid on the original image, along with
    // whatever other debug images that we want.

    /******  THIS FUNCTION SHOULD BE DELETED - TBD 
    cv::Mat outputImage;

    auto ncFinalArray = nc::NdArray<nc::uint8>(ip->final_result_image_.data, ip->final_result_image_.rows, ip->final_result_image_.cols);
    auto ncCandidatesArray = nc::NdArray<nc::uint8>(ip->candidates_image_.data, ip->candidates_image_.rows, ip->candidates_image_.cols);
    auto ncOutputImage = nc::hstack({ ncFinalArray, ncCandidatesArray });

    outputImage = CvUtils::ConvertNDArrayToMat(ncOutputImage);

    fs::path outputDir(subdir);
    fs::path outputFile(std::string(TEST_IMAGE_PREFIX).append(filename));
    fs::path outputFullPath = outputDir / outputFile;
    std::string outputFileName = outputFullPath.generic_string();

    cv::imwrite(outputFileName, outputImage);

    LoggingTools::ShowImage(fname, outputImage);
    ****/
}

void test_certain_images() {

    test_image("./Images/", "FakePiCameraPhotoOfGolfBall-Clr-Green-Flat.png");
    test_image("./Images/", "WedgeNextToOrangeBall-Clr-Orange.png");
    test_image("./Images/", "FirstPiV1CamBall-Clr-Yellow.jpeg");
    test_image("./Images/", "AboutToBeHitLoRes-Clr-White.jpg");
    test_image("./Images/", "JustHitByIronToRight-Clr-White.png");
    test_image("./Images/", "JustHitSlightBlurClub-Clr-White.png");
    test_image("./Images/", "WedgeNextToOrangeBall-Clr-Orange.jpg");
    test_image("./Images/", "HitClubGoneWithFlyingTee-Clr-White.png");
    test_image("./Images/", "IMG_7713-Clr-Yellow.jpg");
}



void WalkDirectoryTree(const fs::path& pathToScan, int level = 0) {
    for (const auto& entry : fs::directory_iterator(pathToScan)) {
        const auto filenameStr = entry.path().filename().string();
        if (entry.is_directory() && (filenameStr.find("IGNORE") != std::string::npos) ) {
            // Recurse to any sub-directories
            WalkDirectoryTree(entry, level + 1);
        }
        else if (entry.is_regular_file()) {
            if (((filenameStr.find(".png") > 0) &&
                (filenameStr.find(".jpg") > 0) &&
                (filenameStr.find(".jpeg") > 0)) &&
                ((filenameStr.find("IGNORE") == std::string::npos) &&
                    (filenameStr.find("TEST_IMAGE_PREFIX") == std::string::npos))
                ) {
                test_image(pathToScan.generic_string(), filenameStr);
            }
        }
        else
            std::cout << "(ignoring)" << filenameStr << "\n";
    }
}

void test_all_test_files() {
//    std::string rootdir = "D:/GolfSim/C++Code/GolfSim/ImageProcessing/Images";
    std::string rootdir = "D:/GolfSim/TestPictures";


    WalkDirectoryTree(rootdir);
}

void test_calibrated_location(std::string twoFootImgName, std::string threeFootImgName, std::string fourFootImgName) {
    
    cv::Mat img = cv::imread(twoFootImgName);
    LoggingTools::ShowImage(twoFootImgName, img);

    // Test of the GetCalibratedBall function
    GolfSimCamera c;
    GolfBall b;

    std::vector<cv::Vec3d> camera_positions_from_origin = std::vector<cv::Vec3d>({ GolfSimCamera::kCamera1PositionsFromOriginMeters, GolfSimCamera::kCamera2PositionsFromOriginMeters });
    bool success = c.GetCalibratedBall(c, img, b);

    // Now, test the calibration by seeing if the same ball can be found using the calibrated ball
    GS_LOG_TRACE_MSG(trace, "GET BALL LOCATION AGAIN FOR 2 FEET");
    GolfBall newBall;
    success = c.GetCurrentBallLocation(c, img, b, newBall);

    if (!threeFootImgName.empty()) {
        // This original HSV masking range from the calibration is not working well.Try a lower H ?
        // b.ball_hsv_range_ = [(0, 0, 175), (175, 102, 255)]
        GS_LOG_TRACE_MSG(trace, "GET BALL LOCATION FOR 3 FEET");
        img = cv::imread(threeFootImgName);

        // TBD - Consider the fact that the ball should be no larger than the radius of the last found
        // ball, and should have that ball"s average color
        success = c.GetCurrentBallLocation(c, img, b, newBall);
        // TBD _ FIX b.average_color_, b.median_color_, b.std_color_ = cu.GetBallColorRgb(c.img, newBall.ball_circle_)
    }

    if (!fourFootImgName.empty()) {
        GS_LOG_TRACE_MSG(trace, "GET BALL LOCATION FOR 4 FEET");
        img = cv::imread(fourFootImgName);
        success = c.GetCurrentBallLocation(c, img, b, newBall);
    }
}



bool testProjection() {

    cv::Mat ball1Img, ball2Img;
    GolfBall ball1, ball2;


    const std::string kBaseTestDir = "D:\\GolfSim\\C++Code\\GolfSim\\ImageProcessing\\";

    const std::string k0_DegreeBallFileName_00 = kBaseTestDir + "test_ball_masked_0_deg_dulled.png";
    const std::string k45_DegreeBallFileName_00 = kBaseTestDir + "test_ball_masked_45_deg_dulled.png";

    ball1Img = cv::imread(k0_DegreeBallFileName_00, cv::IMREAD_COLOR);
    ball2Img = cv::imread(k45_DegreeBallFileName_00, cv::IMREAD_COLOR);

    /*
    gfilters = create_gaborfilter()
    image_g = apply_filter(myimage, gfilters)
    
    min_interval = 120
    max_interval = 250
    image_edge_g = cv2.Canny(image_g, min_interval, max_interval)

    // Create a simple matrix of 3D points
    // cv::Mat<> test3DPoints{ {0,0,1} }

    std::vector<cv::Point3i> test2DPoints{ {0,0,1}, {1,0,0} };
    */
    return true;
}

void showVisualization() {
    /* TBD - probably too much work to get Viz working 
    ///Createawindow 
    viz::Viz3dmyWindow("VizDemo"); 
    ///Starteventloop 
    myWindow.spin();

    ///Eventloopisoverwhenpressedq,Q,e,E cout<<"Firsteventloopisover"<<endl; 
    ///Accesswindowviaitsname 
    viz::Viz3dsameWindow=viz::getWindowByName("VizDemo"); 
    /// ///Starteventloop 
    sameWindow.spin(); 
    /// ///Eventloopisoverwhenpressedq,Q,e,E
    cout<<"Secondeventloopisover"<<endl;
    /// ///Eventloopisoverwhenpressedq,Q,e,E
    /// ///Starteventlooponcefor1millisecond
    sameWindow.spinOnce(1,true);

    while (!sameWindow.wasStopped()) {
        ///Interactwithwindow

        ///Eventloopfor1millisecond
        sameWindow.spinOnce(1,true);
    }
    */
}

cv::Mat undistort_image(const cv::Mat& img, CameraHardware::CameraModel cameraModel) {
    // Get a camera object just to be able to get the calibration values
    GolfSimCamera c;
    c.camera_.resolution_x_override_ = img.cols;
    c.camera_.resolution_y_override_ = img.rows;
    c.camera_.init_camera_parameters(GsCameraNumber::kGsCamera1, cameraModel);
    cv::Mat cameracalibrationMatrix = c.camera_.calibrationMatrix;
    cv::Mat cameraDistortionVector = c.camera_.cameraDistortionVector;

    cv::Mat unDistortedBall1Img;
    cv::Mat m_undistMap1, m_undistMap2;
    // TBD - is the size rows, cols?  or cols, rows?
    cv::initUndistortRectifyMap(cameracalibrationMatrix, cameraDistortionVector, cv::Mat(), cameracalibrationMatrix, cv::Size(img.cols, img.rows), CV_32FC1, m_undistMap1, m_undistMap2);
    cv::remap(img, unDistortedBall1Img, m_undistMap1, m_undistMap2, cv::INTER_LINEAR);

    return unDistortedBall1Img;
}

bool read_test_images(const std::string& img1BaseFileName, const std::string& img2BaseFileName, cv::Mat& ball1Img, cv::Mat& ball2Img, cv::Mat& ball1ImgColor, cv::Mat& ball2ImgColor, 
                                CameraHardware::CameraModel cameraModel, bool undistort = true) {

    std::string img1FileName = kBaseTestDir + img1BaseFileName;
    std::string img2FileName = kBaseTestDir + img2BaseFileName;

    GS_LOG_TRACE_MSG(trace, "Raw Image1: " + img1FileName);
    GS_LOG_TRACE_MSG(trace, "Raw Image2: " + img2FileName);

    ball1Img = cv::imread(img1FileName, cv::IMREAD_COLOR);
    ball2Img = cv::imread(img2FileName, cv::IMREAD_COLOR);

    if (ball1Img.empty() || ball2Img.empty()) {
        return false;
    }

    // Use whatever (simulated) resolution we find in the image files we just read
    CameraHardware::resolution_x_override_ = ball1Img.cols;
    CameraHardware::resolution_y_override_ = ball1Img.rows;

    LoggingTools::DebugShowImage("Original1: " + img1FileName, ball1Img);
    LoggingTools::DebugShowImage("Original2: " + img2FileName, ball2Img);

    cv::Mat unDistortedBall1Img;
    cv::Mat unDistortedBall2Img;

    // TBD - Need to get a better distortion matrix for GS camera
    /*
    if (cameraModel == CameraHardware::PiGSCam6mmWideLens) {
        LoggingTools::Warning("No distortion matrix for PiGSCam6mmWideLens.");
        undistort = false;
    }
    */

    if (undistort) {
        unDistortedBall1Img = undistort_image(ball1Img, cameraModel);
        unDistortedBall2Img = undistort_image(ball2Img, cameraModel);

        // Show the center point to help aim the camera
        LoggingTools::DebugShowImage("Undistorted " + img1FileName, unDistortedBall1Img, std::vector<cv::Point>({ cv::Point(ball1Img.cols / 2, ball1Img.rows / 2) }));
        LoggingTools::DebugShowImage("Undistorted " + img2FileName, unDistortedBall2Img, std::vector<cv::Point>({ cv::Point(ball1Img.cols / 2, ball1Img.rows / 2) }));
    }
    else {
        unDistortedBall1Img = ball1Img;
        unDistortedBall2Img = ball2Img;
    }

    ball1ImgColor = unDistortedBall1Img;
    ball2ImgColor = unDistortedBall2Img;

    cv::Mat ball1ImgGray;
    cv::cvtColor(unDistortedBall1Img, ball1ImgGray, cv::COLOR_BGR2GRAY);
    cv::Mat ball2ImgGray;
    cv::cvtColor(unDistortedBall2Img, ball2ImgGray, cv::COLOR_BGR2GRAY);

    ball1Img = ball1ImgGray;
    ball2Img = ball2ImgGray;

    return true;
}


bool absResultsPass(const cv::Vec2d& expected, const cv::Vec2d& result, const cv::Vec2d& absTolerances) {
    bool pass = true;

    if (std::abs(expected[0] - result[0]) > absTolerances[0]) {
        pass = false;
    }

    if (std::abs(expected[1] - result[1]) > absTolerances[1]) {
        pass = false;
    }

    return pass;
}


bool absResultsPass(const cv::Vec3d& expected, const cv::Vec3d& result, const cv::Vec3d& absTolerances) {
    bool pass = true;

    if (std::abs(expected[0] - result[0]) > absTolerances[0]) {
        pass = false;
    }

    if (std::abs(expected[1] - result[1]) > absTolerances[1]) {
        pass = false;
    }

    if (std::abs(expected[2] - result[2]) > absTolerances[2]) {
        pass = false;
    }

    return pass;
}



struct LocationAndSpinTestScenario {
    int testIndex = 0;
    std::string img1;
    std::string img2;
    CameraHardware::CameraModel cameraModel;
    std::vector<cv::Vec3d> camera_positions_from_origin;  // In meters.  Size=2, one vector for each image.  If same, then only one camera position was used.
    cv::Vec2i calibrationBallCenter;        // The x,y coordinates of where the first ball's picture should concentrate as an ROI
    cv::Vec3d expectedposition_deltas_ball_perspective_;
    cv::Vec2d expectedXYBallAngleDegrees;
    cv::Vec3d expectedXYz_rotation_degreesationDegrees;
};


const cv::Vec3d  kRotationAngleToleranceAbs = { 10, 10, 5 };
const cv::Vec3d  kDeltaLocationBallToleranceAbs = { 1, 1, 1 };
const cv::Vec2d  kLaunchAngleToleranceAbs = { 10, 10 };



void convertInchesToMeters(const cv::Vec3d& expectedPositionsInches, cv::Vec3d& expectedPositionsMeters) {
    expectedPositionsMeters[0] = CvUtils::InchesToMeters(expectedPositionsInches[0]);
    expectedPositionsMeters[1] = CvUtils::InchesToMeters(expectedPositionsInches[1]);
    expectedPositionsMeters[2] = CvUtils::InchesToMeters(expectedPositionsInches[2]);
}



bool testBallPosition() {

    LocationAndSpinTestScenario tests[] {


        { 25, "test_3280w_BS_camoff04x_8.5y_20z_ball00z_02y_00degx_spin00z_v1.png", "test_3280w_camoff04x_8.5y_20z_ball10z_03y_15degx_spin30z_v1.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0.1016, 0.2159, 0.508), cv::Vec3d(0.1016, 0.2159, 0.508)}), cv::Vec2i(1100, 1000), cv::Vec3d(3, 2, 10), cv::Vec2d(45, 7), cv::Vec3d(0, 0, 30)
        },

        { 20, "test_pos_4056w_cam04offx_14y_17z_Ball_0inz_00degx_00iny_00Zsp_00.png", "test_pos_4056w_cam04offx_14y_17z_Ball_10inz_10degx_00iny_30Zsp_00.png",
            CameraHardware::CameraModel::PiHQCam6mmWideLens, std::vector<cv::Vec3d>({cv::Vec3d(0.1016, 0.3556, 0.4318), cv::Vec3d(.1016, 0.3556, 0.4318)}), cv::Vec2i(1100, 2000), cv::Vec3d(6.5, -2, -1.5), cv::Vec2d(13, 0), cv::Vec3d(4, -2, 30)
        },

        { 30, "IRtest02.1-filter.png", "IRtest02.2-filter.png",
            CameraHardware::CameraModel::PiHQCam6mmWideLens, std::vector<cv::Vec3d>({cv::Vec3d(.0914, 0.0953, 0), cv::Vec3d(.0914, 0.0953, 0)}), cv::Vec2i(1400, 800), cv::Vec3d(6.5, -2, -1.5), cv::Vec2d(13, 0), cv::Vec3d(4, -2, 30)
        },

        { 21, "test_pos_4056w_cam04offx_14y_17z_Ball_0inz_00degx_00iny_00Zsp_00.png", "test_pos_4056w_cam04offx_3.5y_19x_Ball15inz_15degx_00iny_40Zsp_00.png",
            CameraHardware::CameraModel::PiHQCam6mmWideLens, std::vector<cv::Vec3d>({cv::Vec3d(.0914, 0.0953, 0), cv::Vec3d(.0914, 0.0953, 0)}), cv::Vec2i(1100, 2000), cv::Vec3d(6.5, -2, -1.5), cv::Vec2d(13, 0), cv::Vec3d(4, -2, 30)
        },


        /* These next two are too unfocused to work well and will likely fail */
        { 1, "test_pos_2592w_BASE6off_22Dist_00inz_00degx_00iny_00.png", "test_pos_2592w_6off_22Dist_15inz_20degx_3iny_00.png",
                CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(.0914, 0, 0), cv::Vec3d(.0914, 0, 0)}), cv::Vec2i(700, 1000), cv::Vec3d(0, 0, 0), cv::Vec2d(-20, 0), cv::Vec3d(0, 0, 0)
        },
        { 2, "test_pos_BS_3280w_6off_22Dist_00inz_00degx_0iny_00_00s.png", "test_pos_3280w_6off_22Dist_15inz_10degx_3iny_00_90s.png",
                CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(.0914, 0, 0), cv::Vec3d(.0914, 0, 0)}), cv::Vec2i(800, 1200), cv::Vec3d(0, 0, 0), cv::Vec2d(0, 0), cv::Vec3d(0, 0, 0)
        },

        { 22, "tt.png", "t.png",
            CameraHardware::CameraModel::PiHQCam6mmWideLens, std::vector<cv::Vec3d>({cv::Vec3d(.0914, 0.0953, 0), cv::Vec3d(.0914, 0.0953, 0)}), cv::Vec2i(1100, 1000), cv::Vec3d(6.5, -2, -1.5), cv::Vec2d(13, 0), cv::Vec3d(4, -2, 30)
        },
        { 0, "test_pos_BS_3280w_03off_20Dist_00inz_00degx_01iny_00sp_Y00_blur.png", "test_pos_3280w_03off_20Dist_06inz_15degx_01iny_30sp_Y00.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(.0914, 0, 0), cv::Vec3d(.0914, 0, 0)}), cv::Vec2i(1100, 1400), cv::Vec3d(6.5, -2, -1.5), cv::Vec2d(13, 0), cv::Vec3d(4, -2, 30)
        },
        { 4, "test_ball_spin_strong_landmarks_00d_3280w_dark_00.png", "test_ball_spin_strong_landmarks_20d_3280w_dark_00.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(.0914, 0, 0), cv::Vec3d(.0914, 0, 0)}), cv::Vec2i(1400, 1100), cv::Vec3d(0, 0, 0), cv::Vec2d(0, 0), cv::Vec3d(0, 0, 32)
        },

        /* These are better-focused and should work */
        { 11, "test_pos_BS_3280w_0off_18Dist_00inz_00degx_01iny_00sp_04.png", "test_pos_3280w_13off_18Dist_15inz_15degx_01iny_00sp_04.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0, 0, 0), cv::Vec3d(.33, 0, 0)}), cv::Vec2i(1400, 1100), cv::Vec3d(0.0925, -0.025, 0.381), cv::Vec2d(-15, 3), cv::Vec3d(0, 0, 0)
        },
        { 15, "test_pos_BS_3280w_03off_20Dist_00inz_00degx_01iny_00sp_Y00.png", "test_pos_3280w_03off_20Dist_06inz_10degx_01iny_30sp_Y00.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0.0762, 0, 0), cv::Vec3d(0.0762, 0, 0)}), cv::Vec2i(1300, 1100), cv::Vec3d(0.030, -0.038, 0.152), cv::Vec2d(-10, 4), cv::Vec3d(0, 0, 30)
        },
        { 5, "test_pos_BS_3280w_03off_20Dist_00inz_00degx_01iny_00sp_Y00.png", "test_pos_3280w_03off_20Dist_06inz_10degx_01iny_00sp_Y00.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(.0914, 0, 0), cv::Vec3d(.0914, 0, 0)}), cv::Vec2i(1200, 1100), cv::Vec3d(0.0, 0.025, 0.152), cv::Vec2d(15, 9.462), cv::Vec3d(0, 0, 0)
        },
        { 3, "test_pos_2592w_BASE6off_22Dist_00inz_00degx_00iny_00.png", "test_pos_2592w_6off_22Dist_10inz_30degx_.75iny_00.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(.0914, 0, 0), cv::Vec3d(.0914, 0, 0)}), cv::Vec2i(800, 1000), cv::Vec3d(0, 0, 0), cv::Vec2d(0, 0), cv::Vec3d(0, 0, 0)
        },
        /* Clean pictures against a bllck back drop.  This should be easy */
        { 7, "test_pos_BS_3280w_0off_18Dist_00inz_00degx_01iny_00sp_04.png", "test_pos_3280w_13off_18Dist_15inz_15degx_01iny_30z20ysp_04.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0, 0, 0), cv::Vec3d(.33, 0, 0)}), cv::Vec2i(1400, 1100), cv::Vec3d(0.0925, -0.025, 0.381), cv::Vec2d(-15, 3), cv::Vec3d(0, 0, 26)
        },
        /* Some tests that use only one camera */
        { 13, "test_pos_BS_3280w_03off_20Dist_00inz_00degx_01iny_00sp_Y00.png", "test_pos_3280w_03off_20Dist_06inz_15degx_01.25iny_15sp_Y00.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0.0762, 0, 0), cv::Vec3d(0.0762, 0, 0)}), cv::Vec2i(1300, 1100), cv::Vec3d(0.038, -0.038, 0.152), cv::Vec2d(-15, 4), cv::Vec3d(5, 0, 15)
        },
        /*  Shadow hits the ball and creates a line - seems to screw up the spin analysis*/
        { 12, "test_pos_BS_3280w_03off_20Dist_00inz_00degx_01iny_00sp_Y00.png", "test_pos_3280w_03off_20Dist_06inz_15degx_01.25iny_15sp_Y00_shdw.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0.0762, 0, 0), cv::Vec3d(0.0762, 0, 0)}), cv::Vec2i(1300, 1100), cv::Vec3d(0.038, -0.038, 0.152), cv::Vec2d(-15, 4), cv::Vec3d(0, 0, 15)
        },
        { 14, "test_pos_BS_3280w_03off_20Dist_00inz_00degx_01iny_00sp_Y00.png", "test_pos_3280w_03off_20Dist_06inz_15degx_01iny_30sp_Y00.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0.0762, 0, 0), cv::Vec3d(0.0762, 0, 0)}), cv::Vec2i(1300, 1100), cv::Vec3d(0.038, -0.038, 0.152), cv::Vec2d(-15, 4), cv::Vec3d(5, 0, 30)
        },
        { 16, "test_pos_BS_3280w_03off_20Dist_00inz_00degx_01iny_00sp_Y00.png", "test_pos_3280w_03off_20Dist_06inz_00degx_01iny_30sp_Y00.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0.0762, 0, 0), cv::Vec3d(0.0762, 0, 0)}), cv::Vec2i(1300, 1100), cv::Vec3d(0.0, -0.038, 0.152), cv::Vec2d(0, 4), cv::Vec3d(0, 0, 30)
        },



        /* Same ball picture - should return zeros for everything */
        { 8, "test_pos_BS_3280w_0off_18Dist_00inz_00degx_01iny_00sp_04.png", "test_pos_BS_3280w_0off_18Dist_00inz_00degx_01iny_00sp_04.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0, 0, 0), cv::Vec3d(0, 0, 0)}), cv::Vec2i(1400, 1100), cv::Vec3d(0.0, 0.0, 0.0), cv::Vec2d(0, 0), cv::Vec3d(0, 0, 0)
        },
        /* Clean pictures against a black back drop.  This should be easy */
        { 9, "test_pos_BS_3280w_0off_18Dist_00inz_00degx_01iny_00sp_04.png", "test_pos_3280w_13off_18Dist_15inz_15degx_01iny_30z00ysp_04.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0, 0, 0), cv::Vec3d(.33, 0, 0)}), cv::Vec2i(1400, 1100), cv::Vec3d(0.0925, -0.025, 0.381), cv::Vec2d(-15, 3), cv::Vec3d(0, 0, 30)
        },
        { 10, "test_pos_BS_3280w_0off_18Dist_00inz_00degx_01iny_00sp_04.png", "test_pos_3280w_13off_18Dist_15inz_15degx_01iny_30z00ysp_04_F.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0, 0, 0), cv::Vec3d(.33, 0, 0)}), cv::Vec2i(1400, 1100), cv::Vec3d(0.0925, -0.025, 0.381), cv::Vec2d(-15, 3), cv::Vec3d(0, 0, 30)
        },
        { 6, "test_ball_spin_strong_landmarks_00d_2592w_bright_00.png", "test_ball_spin_strong_landmarks_45d_2592w_bright_00.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({ cv::Vec3d(.0914, 0, 0), cv::Vec3d(.0914, 0, 0) }), cv::Vec2i(1200, 1000), cv::Vec3d(0.0, 0.0, 0.0), cv::Vec2d(0.0, 0.0), cv::Vec3d(0, 0, 45)
        },
    };

    /*  TBD - this lower resolution is not working for spin detection right now - FIX! 
    // ALREADY ABOVE const std::string k0_DegreeBallFileName_00 = "test_pos_2592w_BASE6off_22Dist_00inz_00degx_00iny_00.png";
    // const std::string kUnknown_DegreeBallFileName_00 = "test_pos_2592w_6off_22Dist_15inz_20degx_3iny_00.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_pos_2592w_6off_22Dist_10inz_30degx_.75iny_00.png";

    const std::string k0_DegreeBallFileName_00 = "test_pos_BS_3280w_6off_22Dist_00inz_00degx_0iny_00_00s.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_pos_3280w_6off_22Dist_15inz_10degx_3iny_00_90s.png";

    // Last trying this...
    const std::string k0_DegreeBallFileName_00 = "test_pos_BS_3280w_6off_20Dist_00inz_00degx_1iny_00sp_00.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_pos_BS_3280w_6off_20Dist_10inz_20degx_3iny_30sp_00.png";

    const std::string k0_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_00d_2592w_bright_00.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_45d_2592w_bright_00.png";

    const std::string k0_DegreeBallFileName_00 = "test_pos_BS_3280w_0off_18Dist_00inz_00degx_01iny_00sp_03.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_pos_BS_3280w_13off_18Dist_15inz_00degx_03iny_30sp_03.png";
    // The (simulated) camera and ball position will depend on the test images we use, above.
    cv::Vec3d camera_positions_from_origin{ 0.33 ,0.0, 0.0 };  // In meters
    cv::Vec2i calibrationBallCenter{ 1500, 1200 };

    const std::string k0_DegreeBallFileName_00 = "test_pos_BS_3280w_0off_18Dist_00inz_00degx_01iny_00sp_04.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_pos_BS_3280w_13off_18Dist_15inz_15degx_01iny_30z20ysp_04.png";

    const std::string k0_DegreeBallFileName_00 = "test_pos_BS_3280w_0off_20Dist_00inz_00degx_00iny_00sp_Y00.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_pos_BS_3280w_13off_20Dist_00inz_05degx_01iny_45sp_Y00.png";
    cv::Vec3d camera_positions_from_origin{ 0.33 ,0.0, 0.0 };  // In meters
    cv::Vec2i calibrationBallCenter{ 1500, 1200 };

    const std::string k0_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_00d_2592w_bright_00.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_45d_2592w_bright_00.png";
    cv::Vec3d camera_positions_from_origin{ 0.0 ,0.0, 0.0 };  // In meters
    cv::Vec2i calibrationBallCenter{ 1200, 1000 };

    // ABOVE const std::string k0_DegreeBallFileName_00 = "test_pos_BS_3280w_03off_20Dist_00inz_00degx_01iny_00sp_Y00_blur.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_pos_3280w_03off_20Dist_06inz_15degx_01iny_30sp_Y00.png";
    // The (simulated) camera and ball position will depend on the test images we use, above.
    cv::Vec3d camera_positions_from_origin{ 0.0 ,0.0, 0.0 };  // In meters
    cv::Vec2i calibrationBallCenter{ 1300, 1200 };

    */

    boost::timer::cpu_timer timer1;

    bool allTestsPassed = true;
    int numTotalTests = 0;
    int numTestsFailed = 0;

    for (auto& t : tests) {

        numTotalTests++;

        const std::string k0_DegreeBallFileName_00 = t.img1;
        const std::string kUnknown_DegreeBallFileName_00 = t.img2;
        // The (simulated) camera and ball position will depend on the test images we use, above.
        std::vector<cv::Vec3d> camera_positions_from_origin = t.camera_positions_from_origin;  // In meters
        cv::Vec2i calibrationBallCenter = t.calibrationBallCenter;


        cv::Mat ball1ImgGray;
        cv::Mat ball2ImgGray;
        cv::Mat ball1ImgColor;
        cv::Mat ball2ImgColor;
        if (!read_test_images(k0_DegreeBallFileName_00, kUnknown_DegreeBallFileName_00, ball1ImgGray, ball2ImgGray, ball1ImgColor, ball2ImgColor, t.cameraModel)) {
            GS_LOG_TRACE_MSG(trace, "Failed to read valid images for Test No. " + std::to_string(t.testIndex));
            numTestsFailed++;
            continue;
        }


        GolfSimCamera c;
        c.camera_.resolution_x_ = ball1ImgColor.cols;
        c.camera_.resolution_y_ = ball1ImgColor.rows;
        c.camera_.resolution_x_override_ = ball1ImgColor.cols;
        c.camera_.resolution_y_override_ = ball1ImgColor.rows;

        // Just for development on a non-Raspberry-Pi machine
        c.camera_.firstCannedImageFileName = kBaseTestDir + k0_DegreeBallFileName_00;
        c.camera_.secondCannedImageFileName = kBaseTestDir + kUnknown_DegreeBallFileName_00;
        c.camera_.firstCannedImage = ball1ImgColor;
        c.camera_.secondCannedImage = ball2ImgColor;
        c.camera_.init_camera_parameters(GsCameraNumber::kGsCamera1, t.cameraModel);

        long timeDelayuS = 7000;
        GolfBall result_ball;

        GS_LOG_TRACE_MSG(trace, "Starting Test No. " + std::to_string(t.testIndex) + ".");

        if (!c.analyzeShotImages(c, ball1ImgColor, ball2ImgColor,
            timeDelayuS,
            camera_positions_from_origin,
            result_ball,
            calibrationBallCenter)) {
            GS_LOG_TRACE_MSG(trace, "Failed Test No. " + std::to_string(t.testIndex));
            continue;
        }

        double ball_velocity_meters_per_second_ = result_ball.velocity_;

        result_ball.PrintBallFlightResults();

        bool testPassed = true;

        if (!absResultsPass(t.expectedXYz_rotation_degreesationDegrees, result_ball.ball_rotation_angles_camera_ortho_perspective_, kRotationAngleToleranceAbs)) {
            GS_LOG_TRACE_MSG(trace, "Test No. " + std::to_string(t.testIndex) + " - Failed ball rotation measurement.");
            GS_LOG_TRACE_MSG(trace, "    Expected X,Y,Z rotation angles (in degrees) are: " + std::to_string((t.expectedXYz_rotation_degreesationDegrees[0])) + ", " +
                std::to_string((t.expectedXYz_rotation_degreesationDegrees[1])) + ", " + std::to_string((t.expectedXYz_rotation_degreesationDegrees[2])));
            testPassed = false;
        }

        cv::Vec3d expectedPositionsMeters;
        convertInchesToMeters(t.expectedposition_deltas_ball_perspective_, expectedPositionsMeters);

        if (!absResultsPass(expectedPositionsMeters, result_ball.position_deltas_ball_perspective_, kDeltaLocationBallToleranceAbs)) {
            GS_LOG_TRACE_MSG(trace, "Test No. " + std::to_string(t.testIndex) + " - Failed ball delta location measurement.");
            GS_LOG_TRACE_MSG(trace, "    Expected X,Y,Z deltas (ball perspective in inches) are: " + std::to_string(CvUtils::MetersToInches(t.expectedposition_deltas_ball_perspective_[0])) + ", " +
                std::to_string(CvUtils::MetersToInches(t.expectedposition_deltas_ball_perspective_[1])) + ", " + std::to_string(CvUtils::MetersToInches(t.expectedposition_deltas_ball_perspective_[2])));
            testPassed = false;
        }

        if (!absResultsPass(t.expectedXYBallAngleDegrees, result_ball.angles_ball_perspective_, kLaunchAngleToleranceAbs)) {
            GS_LOG_TRACE_MSG(trace, "Test No. " + std::to_string(t.testIndex) + " - Failed ball launch angle measurement.");
            GS_LOG_TRACE_MSG(trace, "    Expected X,Y launch angles (ball perspective) (in degrees) are: " + std::to_string(t.expectedXYBallAngleDegrees[0]) + ", " +
                std::to_string(t.expectedXYBallAngleDegrees[1]));
            testPassed = false;
        }

        if (!testPassed) {
            numTestsFailed++;
        }

    }

    GS_LOG_TRACE_MSG(trace, "Final Test Statistics:\nTotal Tests: " + std::to_string(numTotalTests) + ".\nTests Failed: " + std::to_string(numTestsFailed) + ".");

    timer1.stop();
    boost::timer::cpu_times times = timer1.elapsed();
    std::cout << "analyzeShotImages timing: ";
    std::cout << std::fixed << std::setprecision(8)
        << times.wall / 1.0e9 << "s wall, "
        << times.user / 1.0e9 << "s user + "
        << times.system / 1.0e9 << "s system.\n";


    return true;
}


bool testSpinDetection() {

    /*
    const std::string k0_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_00d_3280w_bright_00.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_14d_3280w_bright_00.png";

    const std::string k0_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_00d_2592w_bright_01.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_08d_2592w_bright_01.png";

    const std::string k0_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_00d_2592w_bright_00.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_45d_2592w_bright_00.png";

    const std::string k0_DegreeBallFileName_00 = k"test_ball_spin_strong_landmarks_00d_2592w_bright_01.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_35d_2592w_bright_01.png";

    const std::string k0_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_00d-x_3280w_bright_02.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_15d-x_3280w_bright_02.png";

    const std::string k0_DegreeBallFileName_00 = "test_ball_spin_st-landmarks_rand_1st_3280w_bright_04.png";
    const std::string kUnknown_DegreeBallFileName_00 = k"test_ball_spin_st-landmarks_rand_2nd_3280w_bright_04.png";

    const std::string k0_DegreeBallFileName_00 = "test_pos_2592w_BASE6off_22Dist_00inz_00degx_00iny_00.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_pos_2592w_6off_22Dist_10inz_30degx_.75iny_00.png";

    const std::string k0_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_00d_2592w_bright_00.png";
    const std::string kUnknown_DegreeBallFileName_00 = "test_ball_spin_strong_landmarks_45d_2592w_bright_00.png";
   */

    const std::string k0_DegreeBallFileName_00 = "strobed_spin_test_0z_ctr_02.png";
    const std::string kUnknown_DegreeBallFileName_00 = "strobed_spin_test_30z_-30x_ctr_02.png";
    // const std::string kUnknown_DegreeBallFileName_00 = "loc_test_20_degree_right_strobed.png";
    // NEEDS TO BE CORRECTED FOR EACH TEST -  Assume only 1 camera for now, so all deltas are 0
    // In meters
    std::vector<cv::Vec3d> camera_positions_from_origin = std::vector<cv::Vec3d>({ GolfSimCamera::kCamera1PositionsFromOriginMeters, GolfSimCamera::kCamera2PositionsFromOriginMeters });


    cv::Mat ball1ImgGray;
    cv::Mat ball2ImgGray;
    cv::Mat ball1ImgColor;
    cv::Mat ball2ImgColor;

    CameraHardware::CameraModel  cameraModel = CameraHardware::PiGSCam6mmWideLens;

    if (!read_test_images(k0_DegreeBallFileName_00, kUnknown_DegreeBallFileName_00, ball1ImgGray, ball2ImgGray, ball1ImgColor, ball2ImgColor, cameraModel, false /*No Undistort*/)) {
        GS_LOG_TRACE_MSG(trace, "Failed to read valid images.");
        return false;
    }

    // Get the ball data.  We will calibrate based on the first ball and then get the second one
    // using that calibrated data from the first ball.
    
    GolfSimCamera c;
    c.camera_.init_camera_parameters(GsCameraNumber::kGsCamera1, cameraModel);

    GolfBall ball1, ball2;

    c.camera_.firstCannedImageFileName = kBaseTestDir + k0_DegreeBallFileName_00;
    c.camera_.firstCannedImage = ball1ImgColor;

    cv::Vec2i expectedBallCenter = cv::Vec2i(1456 / 3, 1088 / 2);

    if (GolfSimOptions::GetCommandLineOptions().search_center_x_ > 0) {
        expectedBallCenter[0] = GolfSimOptions::GetCommandLineOptions().search_center_x_;
    }

    if (GolfSimOptions::GetCommandLineOptions().search_center_y_ > 0) {
        expectedBallCenter[1] = GolfSimOptions::GetCommandLineOptions().search_center_y_;
    }


    bool success = c.GetCalibratedBall(c, ball1ImgColor, ball1, expectedBallCenter);

    if (!success) {
        GS_LOG_TRACE_MSG(trace, "Failed to GetCalibratedBall.");
        return false;
    }
    c.camera_.firstCannedImageFileName = kBaseTestDir + k0_DegreeBallFileName_00;
    c.camera_.secondCannedImageFileName = kBaseTestDir + kUnknown_DegreeBallFileName_00;
    c.camera_.firstCannedImage = ball1ImgColor;
    c.camera_.secondCannedImage = ball2ImgColor;

    success = c.GetCurrentBallLocation(c, ball2ImgColor, ball1, ball2);

    if (!success) {
        GS_LOG_TRACE_MSG(trace, "Could not find 2nd ball");
        return false;
    }

    boost::timer::cpu_timer timer1;

    cv::Vec3d rotationResults = BallImageProc::GetBallRotation(ball1ImgGray, ball1, ball2ImgGray, ball2);

    timer1.stop();
    boost::timer::cpu_times times = timer1.elapsed();
    std::cout << "BallImageProc::GetBallRotation: ";
    std::cout << std::fixed << std::setprecision(8)
        << times.wall / 1.0e9 << "s wall, "
        << times.user / 1.0e9 << "s user + "
        << times.system / 1.0e9 << "s system.\n";

    // TBD - Now implemented in the GetBallRotation() function.  See how the original image would look if rotated as the GetBallRotation function calculated

    std::string s = "Ball Rotation (degrees):  X: " + std::to_string(rotationResults[0]) + "\tY: " + std::to_string(rotationResults[1]) + "\tZ: " + std::to_string(rotationResults[2]) + "\n";

    return true;
}



bool TestSpin() {

    GolfBall ball1;
    GolfBall ball2;
    cv::Mat img1;
    cv::Mat img2;

    GS_LOG_MSG(info, "TestSpin is in process.");
    GS_LOG_TRACE_MSG(trace, "Please position the ball for a first image and hit any key.");

    int keyPressed = cv::waitKey(0) & 0xFF;

#ifdef __unix__   

    // Find the first ball.  This will cause a pause to view the image
    // that will allow the user to reposition the ball for a second time
    if (!CheckForBall(ball1, img1)) {
        GS_LOG_TRACE_MSG(trace, "Failed to CheckForBall.");
        return false;
    }

    GS_LOG_TRACE_MSG(trace, "Position the ball for a second image and hit any key.");

    if (!CheckForBall(ball2, img2)) {
        GS_LOG_TRACE_MSG(trace, "Failed to CheckForBall.");
        return false;
    }

    LoggingTools::LogImage("test_spin_img_ball1", img1, std::vector < cv::Point >{}, true);
    LoggingTools::LogImage("test_spin_img_ball2", img2, std::vector < cv::Point >{}, true);

    cv::Mat grayImage1;
    cv::Mat grayImage2;
    cv::cvtColor(img1, grayImage1, cv::COLOR_BGR2GRAY);
    cv::cvtColor(img2, grayImage2, cv::COLOR_BGR2GRAY);

    cv::Vec3d rotationResults = BallImageProc::GetBallRotation(grayImage1, ball1, grayImage2, ball2);
#endif

    return true;
}

bool testAnalyzeStrobedBalls() {

    // Have to call this here because we are not starting the FSM, but need (simulated) pulse informatiom
    PulseStrobe::InitGPIOSystem();

    std::string kTwoImageTestTeedBallImage;
    std::string kTwoImageTestStrobedImage;
    std::string kTwoImageTestPreImage;

    GolfSimConfiguration::SetConstant("gs_config.testing.kTwoImageTestTeedBallImage", kTwoImageTestTeedBallImage);
    GolfSimConfiguration::SetConstant("gs_config.testing.kTwoImageTestStrobedImage", kTwoImageTestStrobedImage);
    GolfSimConfiguration::SetConstant("gs_config.testing.kTwoImageTestPreImage", kTwoImageTestPreImage);

    const std::string kTestCamImageFileName_00 = kTwoImageTestTeedBallImage;
   
    const std::string kTestCam2StrobedmageFileName = kTwoImageTestStrobedImage;

    cv::Mat ball1ImgGray;
    cv::Mat ball2ImgGray;
    cv::Mat ball1ImgColor;
    cv::Mat ball2ImgColor;

    CameraHardware::CameraModel  cameraModel = CameraHardware::PiGSCam6mmWideLens;

    if (!read_test_images(kTestCamImageFileName_00, kTestCam2StrobedmageFileName, ball1ImgGray, ball2ImgGray, ball1ImgColor, ball2ImgColor, cameraModel,false /*No Undistort*/)) {
        GS_LOG_TRACE_MSG(trace, "Failed to read valid images.");
        return false;
    }

    cv::Mat camera2_pre_image_color;
    /****  NO LONGER USED
    // Also read the test  pre-image
    std::string preImageFileName = kBaseTestDir + kTwoImageTestPreImage;

    GS_LOG_TRACE_MSG(trace, "Pre Image1: " + preImageFileName);

    camera2_pre_image_color = cv::imread(preImageFileName, cv::IMREAD_COLOR);

    if (camera2_pre_image_color.empty()) {
        return false;
    }
    ***/

    GolfBall result_ball;
    cv::Vec3d rotation_results;
    cv::Mat exposures_image;
    std::vector<GolfBall> exposure_balls;

    if (!GolfSimCamera::ProcessReceivedCam2Image(ball1ImgColor, 
                                                 ball2ImgColor, 
                                                 camera2_pre_image_color, 
                                                 result_ball, 
                                                 rotation_results, 
                                                 exposures_image, 
                                                 exposure_balls)) {
        GS_LOG_MSG(error, "Failed ProcessReceivedCam2Image.");
        return false;
    }

#ifdef __unix__  // Ignore in Windows environment
    GsUISystem::SaveWebserverImage("kCameraXBallLocation_", ball1ImgColor, exposure_balls);
#endif
    GsGSProResults results(result_ball);
    GS_LOG_TRACE_MSG(trace, "Results are: " + results.Format());

    PulseStrobe::DeinitGPIOSystem();

    return true;
}




bool test_strobed_balls_detection() {

    const std::string kCam1BallOnTee = "test_strobe_spin_0_0_0.png";
    const std::string kCam2BallInFlight = "test_strobe_spin_0_0_45.png";


    cv::Mat ball1ImgGray;
    cv::Mat ball2ImgGray;
    cv::Mat ball1ImgColor;
    cv::Mat ball2ImgColor;
    if (!read_test_images(kCam1BallOnTee, kCam2BallInFlight, ball1ImgGray, ball2ImgGray, ball1ImgColor, ball2ImgColor, CameraHardware::PiGSCam6mmWideLens)) {
        GS_LOG_MSG(error, "Failed to read valid images.");
        return false;
    }

    BallImageProc *ip = get_image_processor();

    ip->image_name_ = kBaseTestDir + kCam1BallOnTee;

    GolfSimCamera c;
    //get camera operational and make sure working correctly
    c.camera_.cameraModel = CameraHardware::CameraModel::PiGSCam6mmWideLens;

    GolfBall ball1, ball2;

    c.camera_.firstCannedImageFileName = kBaseTestDir + kCam1BallOnTee;
    c.camera_.firstCannedImage = ball1ImgColor;
    c.camera_.secondCannedImageFileName = kBaseTestDir + kCam2BallInFlight;
    c.camera_.secondCannedImage = ball2ImgColor;
    c.camera_.init_camera_parameters(GsCameraNumber::kGsCamera1, CameraHardware::CameraModel::PiGSCam6mmWideLens);

    std::vector<cv::Vec3d> camera_positions_from_origin = std::vector<cv::Vec3d>({ GolfSimCamera::kCamera1PositionsFromOriginMeters, GolfSimCamera::kCamera2PositionsFromOriginMeters });

    cv::Vec2i expectedBallCenter = cv::Vec2i(1456 / 2, 1088 / 2);

    if (GolfSimOptions::GetCommandLineOptions().search_center_x_ > 0) {
        expectedBallCenter[0] = GolfSimOptions::GetCommandLineOptions().search_center_x_;
    }

    if (GolfSimOptions::GetCommandLineOptions().search_center_y_ > 0) {
        expectedBallCenter[1] = GolfSimOptions::GetCommandLineOptions().search_center_y_;
    }

    bool success = c.GetCalibratedBall(c, ball1ImgColor, ball1, expectedBallCenter );

    if (!success) {
        GS_LOG_MSG(error, "Failed to determine first ball.");
        return false;
    }

    success = c.GetCurrentBallLocation(c, ball2ImgColor, ball1, ball2);

    if (!success) {
        GS_LOG_MSG(error, "Failed to determine second ball.");
        return false;
    }


    boost::timer::cpu_timer timer1;

    cv::Vec3d rotationResults = BallImageProc::GetBallRotation(ball1ImgGray, ball1, ball2ImgGray, ball2);

    timer1.stop();
    boost::timer::cpu_times times = timer1.elapsed();
    std::cout << "BallImageProc::GetBallRotation: ";
    std::cout << std::fixed << std::setprecision(8)
        << times.wall / 1.0e9 << "s wall, "
        << times.user / 1.0e9 << "s user + "
        << times.system / 1.0e9 << "s system.\n";

    // TBD - Now implemented in the GetBallRotation() function.  See how the original image would look if rotated as the GetBallRotation function calculated

    std::string s = "Ball Rotation (degrees):  X: " + std::to_string(rotationResults[0]) + "\tY: " + std::to_string(rotationResults[1]) + "\tZ: " + std::to_string(rotationResults[2]) + "\n";


    return true;
}


bool test_hit_trigger() {

    GolfBall ball;

    //get camera operational and make sure working correctly
    GolfSimCamera c;
    c.camera_.cameraModel = CameraHardware::CameraModel::PiCam2;

    cv::Mat ball1ImgColor;
    cv::Mat ball2ImgColor;

    const std::string kStationaryBallFileName_00 = kBaseTestDir + "move_test_ball_present_2592w_00.png";
    const std::string kStationaryBallFileName_01 = kBaseTestDir + "move_test_ball_present_2592w_01.png";
    const std::string kPreHitCloseBallFileName_00 = kBaseTestDir + "move_test_ball_and_club_present_2592w_00.png";
    const std::string kPostHitBallGoneFileName_00 = kBaseTestDir + "move_test_no_ball_present_2592w_00.png";

    ball1ImgColor = cv::imread(kStationaryBallFileName_00, cv::IMREAD_COLOR);
    ball2ImgColor = cv::imread(kStationaryBallFileName_01, cv::IMREAD_COLOR);

    cv::Mat ball1Img;
    cv::Mat ball2Img;
    ball1Img = undistort_image(ball1ImgColor, c.camera_.cameraModel);
    ball2Img = undistort_image(ball2ImgColor, c.camera_.cameraModel);

    c.camera_.resolution_x_ = ball1Img.cols;
    c.camera_.resolution_y_ = ball1Img.rows;
    c.camera_.resolution_x_override_ = ball1Img.cols;
    c.camera_.resolution_y_override_ = ball1Img.rows;
    c.camera_.firstCannedImageFileName = kBaseTestDir + kStationaryBallFileName_00;
    c.camera_.secondCannedImageFileName = kBaseTestDir + kStationaryBallFileName_01;
    c.camera_.firstCannedImage = ball1Img;
    c.camera_.secondCannedImage = ball2Img;
    c.camera_.init_camera_parameters(GsCameraNumber::kGsCamera1, c.camera_.cameraModel);


    if (!c.prepareToTakePhoto()) {
        GS_LOG_MSG(error, "Cannot prepare camera for photos");
        return false;
    }


    std::vector<cv::Vec3d> camera_positions_from_origin = std::vector<cv::Vec3d>({ GolfSimCamera::kCamera1PositionsFromOriginMeters, GolfSimCamera::kCamera2PositionsFromOriginMeters });
    cv::Vec2i expectedBallCenter = cv::Vec2i(1300, 1000);

    GS_LOG_TRACE_MSG(trace, "Looking for ball on tee");
    bool success = false;
    bool timedOut = false;
    cv::Mat img;

    while (!success && !timedOut)
    {
        img = c.camera_.take_photo();

        if (img.empty()) {
            GS_LOG_MSG(error, "Could not take picture!");
            return false;
        }

        success = c.GetCalibratedBall(c, img, ball, expectedBallCenter);
        GS_LOG_TRACE_MSG(trace, ".");
    }

    if (!success) {
        LoggingTools::Warning("Could not find the first ball to calibrate!");
        return false;
    }

    GS_LOG_TRACE_MSG(trace, "Found ball" + ball.Format());

    cv::Mat result_image;

    //    ball.ball_circle_ = GsCircle{ (float)ball.x(), (float)ball.y(), (float)ball.measured_radius_pixels_ };

        // TBD - override the camera hardware based on the image we find
    if (BallImageProc::WaitForBallMovement(c, result_image, ball, 200)) {
        GS_LOG_TRACE_MSG(trace, "wait_for_movement returned True");
        LoggingTools::DebugShowImage("First image with movement", result_image);
    }
    else {
        GS_LOG_TRACE_MSG(trace, "wait_for_movement returned False");
    }

    return true;
}

void WaitForSimArmed() {

    // Wait until the system is armed.
    while (true) {
        if (GsSimInterface::GetAllSystemsArmed())
            break;

        GS_LOG_TRACE_MSG(info, "Waiting for interface armed...");
#ifdef __unix__   
        sleep(1);
#endif
    }
}

bool WaitAndSendShotToSim(int shot_number, GsGSProResults& test_result) {
    try {
        GS_LOG_TRACE_MSG(trace, "Sending test shot " + std::to_string(shot_number));

        if (!GsSimInterface::SendResultsToGolfSims(test_result)) {
            GS_LOG_MSG(error, "Failed to SendResultsToGolfSim (the Golf Simulator Interface).");
            return false;
        }

        GS_LOG_TRACE_MSG(trace, "Sent test shot " + std::to_string(shot_number));
    }
    catch (std::exception& e)
    {
        GS_LOG_MSG(error, "Failed TestGSProServer - Error was: " + std::string(e.what()));
        return false;
    }

    return true;
}

bool TestExternalSimMessage() {

    if (!GsSimInterface::InitializeSims()) {
        GS_LOG_MSG(error, "Failed to Initialize the Golf Simulator Interface.");
        return false;
    }

#ifdef __unix__   
    // Give the system time to connect, exchange any handshaking, etc.
    sleep(15);
#endif
    GolfBall ball;
    ball.velocity_ = 123.6;
    GsGSProResults test_result(ball);
    test_result.speed_mph_ = 99;
    test_result.vla_deg_ = 23.4F;
    test_result.hla_deg_ = 1.23;
    test_result.back_spin_rpm_ = 3456.0;
    test_result.side_spin_rpm_ = -5.678;

#ifdef __unix__   

    // If we are interfacing with a TruGolf/E6 system, then we need to make sure that it is armed before
    // sending shot information.  For GSPro, the arming is not important.

    if (GsE6Interface::InterfaceIsPresent()) {
        GS_LOG_TRACE_MSG(trace, "Sleeping for a while in order have user setup E6 simulator to send 'Arm' message.");
        sleep(15);
    }
    else {
        // We don't need to wait for an arm in the GSPro system
        sleep(5);
    }
#endif
    int shot_number = 1;
    WaitForSimArmed();

    if (!WaitAndSendShotToSim(shot_number, test_result)) {
        GS_LOG_MSG(error, "Failed to WaitAndSendShotToSim (the Golf Simulator Interface).");
    }

    test_result.speed_mph_ = 55;
    test_result.vla_deg_ = 12.3;

    shot_number++;

    WaitForSimArmed();

    if (!WaitAndSendShotToSim(shot_number, test_result)) {
        GS_LOG_MSG(error, "Failed to WaitAndSendShotToSim (the Golf Simulator Interface).");

    }
    return true;

    GS_LOG_TRACE_MSG(trace, "De-initializing GSPro interface.");
    GsSimInterface::DeInitializeSims();

    return true;
}

bool TestBallDeltaCalculations() {
    // Setup a couple of test balls in specific locations.  Each ball needs the same information it would have if
    // the GolfSimCamera::ComputeXyzDistanceFromOrthoCamPerspective function had been called on it
    GolfBall ball1, ball2;

    ball1.quality_ranking = 0;
    ball1.ball_circle_[0] = 934.5;
    ball1.set_x(ball1.ball_circle_[0]);
    ball1.ball_circle_[1] = 424;
    ball1.set_y(ball1.ball_circle_[1]);
    ball1.ball_circle_[2] = 50.41;
    ball1.measured_radius_pixels_ = ball1.ball_circle_[2];
    ball1.distance_to_z_plane_from_lens_ = 0.761;
    ball1.distances_ortho_camera_perspective_[0] = -0.514;
    ball1.distances_ortho_camera_perspective_[1] = -0.284;
    ball1.distances_ortho_camera_perspective_[2] = 0.485;
    ball1.angles_camera_ortho_perspective_[0] = -46.687;
    ball1.angles_camera_ortho_perspective_[1] = -30.357;

    ball2.quality_ranking = 1;
    ball2.ball_circle_[0] = 741.5;
    ball2.set_x(ball2.ball_circle_[0]);
    ball2.ball_circle_[1] = 501;
    ball2.set_y(ball2.ball_circle_[1]);
    ball2.ball_circle_[2] = 93.5;
    ball2.measured_radius_pixels_ = ball2.ball_circle_[2];
    ball2.distance_to_z_plane_from_lens_ = 0.411;
    ball2.distances_ortho_camera_perspective_[0] = 0.003;
    ball2.distances_ortho_camera_perspective_[1] = 0.084;
    ball2.distances_ortho_camera_perspective_[2] = 0.402;
    ball2.angles_camera_ortho_perspective_[0] = 2.578;
    ball2.angles_camera_ortho_perspective_[1] = 11.262;


    // Test the position and angle delta functions
    GS_LOG_TRACE_MSG(trace, "GolfSimCamera::ComputeBallDeltas - ball1 is:\n" + ball1.Format());
    GS_LOG_TRACE_MSG(trace, "GolfSimCamera::ComputeBallDeltas - ball2 is:\n" + ball2.Format());

    // At this point, we know the distances and angles of each ball relative to the camera
    // Next, find the delta differences in distance and angles as between the two balls
    // The remaining code is pretty much just the ComputeBallDeltas code
    GolfSimCamera c;

    if (!c.ComputeXyzDeltaDistances(ball1, ball2, ball2.position_deltas_ball_perspective_, ball2.distance_deltas_camera_perspective_)) {
        GS_LOG_MSG(error, "Could not calculate ComputeXyzDeltaDistances");
        return false;
    }


    // If the images were taken by different cameras at some distance from each other, we will account for that here
    // For example, if the second camera is to the right of the first (looking at the ball), then that right-direction
    // distance on the X axis should be added to the distance delta in the X-axis of the ball.

    ball2.distance_deltas_camera_perspective_ += GolfSimCamera::kCamera2OffsetFromCamera1OriginMeters;
    ball2.position_deltas_ball_perspective_[0] += GolfSimCamera::kCamera2OffsetFromCamera1OriginMeters[2];
    ball2.position_deltas_ball_perspective_[1] += GolfSimCamera::kCamera2OffsetFromCamera1OriginMeters[1];
    ball2.position_deltas_ball_perspective_[2] += GolfSimCamera::kCamera2OffsetFromCamera1OriginMeters[0];

    if (!c.getXYDeltaAnglesBallPerspective(ball2.position_deltas_ball_perspective_, ball2.angles_ball_perspective_)) {
        GS_LOG_MSG(error, "Could not calculate getXYDeltaAnglesBallPerspective");
        return false;
    }


    GS_LOG_TRACE_MSG(trace, "Calculated X,Y angles (ball perspective) (in degrees) are: " + std::to_string(ball2.angles_ball_perspective_[0]) + ", " +
        std::to_string(ball2.angles_ball_perspective_[1]));

    GS_LOG_TRACE_MSG(trace, "Calculated DELTA X,Y, Z distances (ball perspective) are: " + std::to_string(ball2.position_deltas_ball_perspective_[0]) + ", " +
        std::to_string(ball2.position_deltas_ball_perspective_[1]) + ", " + std::to_string(ball2.position_deltas_ball_perspective_[2]));

    GS_LOG_TRACE_MSG(trace, "Calculated currentDistance is: " + std::to_string(ball2.distance_to_z_plane_from_lens_) + " meters = " +
        std::to_string(12 * CvUtils::MetersToFeet(ball2.distance_to_z_plane_from_lens_)) + " inches from the lens.");

    return true;
}


bool TestGSProServer() {
    try
    {
        int kGSProConnectPort;
        GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectPort", kGSProConnectPort);

        boost::asio::io_context io_context;
        GsGSProTestServer server(io_context, kGSProConnectPort);
        GS_LOG_TRACE_MSG(trace, "About to call io_context.run()");
        io_context.run();
    }
    catch (std::exception& e)
    {
        GS_LOG_MSG(error, "Failed TestGSProServer - Error was: " + std::string(e.what()));
        return false;
    }

    return true;
}

void test_gspro_communication() {

    GolfBall ball;
    ball.rotation_speeds_RPM_[2] = 5000.;
    ball.rotation_speeds_RPM_[0] = 100.;
    GsGSProResults results(ball);
    std::string json = results.Format();
    GS_LOG_MSG(debug, json);
}

void test_function(int argc, char* argv[])
{
    GS_LOG_TRACE_MSG(trace, "Test called");

    // Start of testing
    GS_LOG_TRACE_MSG(trace, "Running on " + GolfSimPlatform);  //  platform.platform());
    GS_LOG_TRACE_MSG(trace, "OpenCV Version " + std::string(CV_VERSION));

    bool kStartInPuttingMode = false;
    GolfSimConfiguration::SetConstant("gs_config.modes.kStartInPuttingMode", kStartInPuttingMode);


    // test_strobed_balls_detection();
    // testBallPosition();
    // test_gspro_communication();

#ifdef __unix__   

    if (GolfSimOptions::GetCommandLineOptions().shutdown_) {

        GS_LOG_TRACE_MSG(trace, "Running in global shutdown mode.");
        if (!PerformSystemStartupTasks()) {
            GS_LOG_MSG(error, "Failed to PerformSystemStartupTasks.");
            return;
        }

        // Give the IPC threads time to start
        sleep(2);

        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kShutdown);
        GolfSimIpcSystem::SendIpcMessage(ipc_message);

        // Give the IPC thread time to send the message
        sleep(1);

        PerformSystemShutdownTasks();

        return;
    }

 

    if (GolfSimOptions::GetCommandLineOptions().send_test_results_) {

        GS_LOG_TRACE_MSG(trace, "Running in send_test_results mode.");
        if (!PerformSystemStartupTasks()) {
            GS_LOG_MSG(error, "Failed to PerformSystemStartupTasks.");
            return;
        }

        // Give the IPC threads time to start
        sleep(2);

        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kResults);

        // Send as many test shots as we have to whatever golf sim we are connected to
        std::vector<GsResults> shots;

        int kInterShotInjectionPauseSeconds = 0;
        if (!GolfSimConfiguration::ReadShotInjectionData(shots, kInterShotInjectionPauseSeconds)) {
            GS_LOG_MSG(error, "Failed to ReadShotInjectionData.");
            return;
        }
        else {
            GS_LOG_MSG(info, "About to inject " + std::to_string(shots.size()) + " shots.");
        }

        GsIPCResult& ipc_results = ipc_message.GetResultsForModification();

        for (GsResults& result : shots) {
            GS_LOG_MSG(info, "********   READY FOR SHOT NO. " + std::to_string(result.shot_number_) + " ********");
            
            GS_LOG_MSG(info, "********   PLEASE RE-ARM THE SIMULATOR TO ACCEPT ANOTHER SHOT  ********");

            sleep(kInterShotInjectionPauseSeconds);
            // Get the result to the golf simulator ASAP
            if (!GsSimInterface::SendResultsToGolfSims(result)) {
                GS_LOG_MSG(error, "Could not SendResultsToGolfSim. Continuing");
            }


        }

        /**** DEPRECATED 
        // Also send one IPC result to test the GUI
        ipc_results.result_type_ = GsIPCResultType::kBallPlacedAndReadyForHit;
        ipc_results.speed_mpers_ = 50.25 + rand() % 40;
        ipc_results.carry_meters_ = 100 + rand() % 150;
        ipc_results.launch_angle_deg_ = 12.34;
        // Tests edge case where a float can be interpreted by MSGPACK as an integer if there is no fractional part
        ipc_results.side_angle_deg_ = 3.0;
        ipc_results.back_spin_rpm_ = 4567;
        ipc_results.club_type_ = GolfSimClubs::GsClubType::kDriver;
        ipc_results.side_spin_rpm_ = 89;
        ipc_results.message_ = "Test message";

        ipc_results.log_messages_.push_back("[2024-01-22 11:19:36.321980] (0x000053c0) [debug] [bool __cdecl golf_sim::GolfSimCamera::GetCalibratedBall(const class cv::Mat &,const class cv::Vec<double,3> &,class golf_sim::GolfBall &,const class cv::Vec<int,2> &)(D:\\GolfSim\\LM\\ImageProcessing\\gs_camera.cpp:118)<-bool __cdecl golf_sim::GolfSimCamera::GetCalibratedBall(const class cv::Mat &,const class cv::Vec<double,3> &,class golf_sim::GolfBall &,const class cv::Vec<int,2> &)(D:\\GolfSim\\LM\\ImageProcessing\\gs_camera.cpp:86)...] Looking for a ball with min/max radius (pixels) of: 56, 85");
        ipc_results.log_messages_.push_back("[2024 - 01 - 22 11:19 : 36.497181](0x000053c0)[trace][void __cdecl golf_sim::LoggingTools::ShowImage(class std::basic_string<char, struct std::char_traits<char>, class std::allocator<char> >, const class cv::Mat&, const class std::vector<class cv::Point_<int>, class std::allocator<class cv::Point_<int> > > &)(D:\\GolfSim\\LM\\ImageProcessing\\logging_tools.cpp:270) < -bool __cdecl golf_sim::GolfSimCamera::GetCalibratedBall(const class cv::Mat&, const class cv::Vec<double, 3> &, class golf_sim::GolfBall&, const class cv::Vec<int, 2> &)(D:\\GolfSim\\LM\\ImageProcessing\\gs_camera.cpp:118)...] ShowImage(AreaMaskImage Photo, (sizeX, sizeY) = (1456, 1088)");

        GS_LOG_TRACE_MSG(trace, "Sending a test IPC Results Message:\n" + ipc_results.Format());
        GolfSimIpcSystem::SendIpcMessage(ipc_message);

        // Give the IPC thread time to send the message
        sleep(1);
        */

        PerformSystemShutdownTasks();

        return;
    }


    // In this mode, we just try to send the shutter and strobe pulses asap
    // Only do so if this is the camera1 system, of course
    // if in cam2_still_mode on the camera2 system, the only difference in
    // operation will be the number of strobe pulses is cut to 1
    if (GolfSimOptions::GetCommandLineOptions().camera_still_mode_) {

        std::string save_file_name = GolfSimOptions::GetCommandLineOptions().output_filename_;
        if (save_file_name.empty()) {
            GS_LOG_TRACE_MSG(trace, "No output output_filename specified.  Will save picture as: " + std::string(LoggingTools::kDefaultSaveFileName));
            save_file_name = LoggingTools::kDefaultSaveFileName;
        }


        if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1) {
            GS_LOG_TRACE_MSG(trace, "Running in cam_still_mode on camera1 system.  Will take one  picture.");

            // Just call the get-the-ball function to trigger an image capture.
            // It will be saved as an artifact

            GolfBall ball;
            cv::Mat img;
            // In addition to checking for the ball, this method will send an IPC results
            // message if we are in calibration mode.

            if (!CheckForBall(ball, img)) {
                GS_LOG_TRACE_MSG(trace, "Failed to CheckForBall");
            }

            LoggingTools::LogImage("", img, std::vector < cv::Point >{}, true, save_file_name);

        }

        if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2) {

            // TBD - Not completed yet
            GS_LOG_TRACE_MSG(trace, "Running in pulse cam2 still mode on camera1 system.  Will take one strobed picture in camera2 system.");

            // Will need the GPIO system for the camera trigger and stobe
            // The camera2 system will also have to be setup.
            if (!PulseStrobe::InitGPIOSystem()) {
                GS_LOG_MSG(error, "Failed to InitGPIOSystem.");
                return;
            }

            if (!PulseStrobe::SendCameraPrimingPulses(true)) {
                GS_LOG_MSG(error, "FAILED to PulseStrobe::SendCameraPrimingPulses");
            }

            // Give the camera2 system a moment
            sleep(1);

            PulseStrobe::SendExternalTrigger();

            // At this point, the camera2 sytem should take a picture and return it

        }

        return;
    }

    if (GolfSimOptions::GetCommandLineOptions().perform_pulse_test_) {

        GS_LOG_TRACE_MSG(trace, "Running in pulse test mode.");
        if (!PulseStrobe::InitGPIOSystem()) {
            GS_LOG_MSG(error, "Failed to InitGPIOSystem.");
            return;
        }

        PulseStrobe::SendCameraPrimingPulses(true /* use_high_speed */);

        while (true) {
            PulseStrobe::SendExternalTrigger();
            sleep(3);
        }
        return;
    }

    switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {

        case SystemMode::kCamera1:
        case SystemMode::kCamera1TestStandalone:
        {
            GS_LOG_MSG(info, "Running in kCamera1 or kCamera1TestStandalone mode.");
            state::InitializingCamera1System camera1_state;
            RunGolfSimFsm(camera1_state);
            break;
        }
        
        case SystemMode::kCamera2:
        case SystemMode::kCamera2TestStandalone:
        {
            GS_LOG_MSG(info, "Running in kCamera2 or kCamera2TestStandalone mode.");

            state::InitializingCamera2System camera2_state;
            RunGolfSimFsm(camera2_state);
            break;
        }
        
        case SystemMode::kTestSpin:
        {
                GS_LOG_MSG(info, "Running in kTestSpin mode.");

                TestSpin();
                break;
        }

        case SystemMode::kTest:
        {
            GS_LOG_MSG(info, "Running in mode:  SystemMode::kTest.");

            if (!PerformSystemStartupTasks()) {
                GS_LOG_MSG(error, "Failed to PerformSystemStartupTasks.");
                return;
            }

            std::string address;
            GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress", address);

            if (kStartInPuttingMode) {
                GS_LOG_MSG(info, "Starting in Putting Mode.");
                GolfSimClubs::SetCurrentClubType(GolfSimClubs::GsClubType::kPutter);
            }
            else {
                GolfSimClubs::SetCurrentClubType(GolfSimClubs::GsClubType::kDriver);
            }

            testAnalyzeStrobedBalls();
            // test_hit_trigger();
            break;
        }

        case SystemMode::kCamera1Calibrate:
        case SystemMode::kCamera2Calibrate:
        {
            GS_LOG_MSG(info, "Running in kCamera1Calibrate or kCamera2Calibrate mode.");

            // We will want to send a calibration message to any monitor UIs
            if (!GolfSimIpcSystem::InitializeIPCSystem()) {
                GS_LOG_MSG(info, "Failed to InitializeIPCSystem.");
                return;
            }

            GolfBall ball;
            cv::Mat img;
            // In addition to checking for the ball, this method will send an IPC results
            // message if we are in calibration mode.

            GS_LOG_MSG(info, "Calibration Results (Distance of kCamera (1 OR 2) CalibrationDistanceToBall):");
            double average_focal_length = 0.0;
            const int number_attempts = 20;
            int number_samples = 0;

            for (int i = 0; i < number_attempts; i++) {
                if (!CheckForBall(ball, img)) {
                    GS_LOG_TRACE_MSG(trace, "Failed to CheckForBall - skipping"); 
                    continue;
                }

                number_samples++;
                GS_LOG_TRACE_MSG(trace, "Performing focal length calibration");

                average_focal_length += ball.calibrated_focal_length_;
                std::string calibration_results_message = "Focal Length = " + std::to_string(ball.calibrated_focal_length_) + ".";
                GS_LOG_MSG(info, calibration_results_message);
#ifdef __unix__ 
                GsUISystem::SendIPCStatusMessage(GsIPCResultType::kCalibrationResults, calibration_results_message);
#endif
            }

            average_focal_length /= number_samples;
            GS_LOG_MSG(info, "====>  Average Focal Length = " + std::to_string(average_focal_length) + ".Set this value into the gs_config.json file.");

            GolfSimIpcSystem::ShutdownIPCSystem();
        }
        break;

        case SystemMode::kTestExternalSimMessage:
        {
            if (!TestExternalSimMessage()) {
                GS_LOG_MSG(info, "Failed to TestExternalSimMessage.");
                return;
            }
        }
        break;

        case SystemMode::kTestGSProServer:
        {
            if (!TestGSProServer()) {
                GS_LOG_MSG(info, "Failed to TestGSProSever.");
                return;
            }
        }
        break;

        case SystemMode::kCamera1BallLocation:
        case SystemMode::kCamera2BallLocation:
        {
            GS_LOG_MSG(info, "Running in kCamera1BallLocation or kCamera2BallLocation mode.");

            // We will want to send a test rest results message to any monitor UIs
            if (!GolfSimIpcSystem::InitializeIPCSystem()) {
                GS_LOG_MSG(info, "Failed to InitializeIPCSystem.");
                return;
            }

            PerformCameraSystemStartup();

            GolfBall ball;
            cv::Mat img;
            std::vector<cv::Vec3d> camera_positions_from_origin = std::vector<cv::Vec3d>({ GolfSimCamera::kCamera1PositionsFromOriginMeters, GolfSimCamera::kCamera2PositionsFromOriginMeters });

            CameraHardware::CameraModel  cameraModel = CameraHardware::PiGSCam6mmWideLens;
            GolfSimCamera c;
            c.camera_.init_camera_parameters(GolfSimOptions::GetCommandLineOptions().GetCameraNumber(), cameraModel);


            int i = 0;

            while (GolfSimGlobals::golf_sim_running_) {
                // In addition to checking for the ball, this method will send an IPC results
                // message if we are in calibration mode.
                bool status = CheckForBall(ball, img);

                std::vector<GolfBall> balls({ ball });
                std::vector<GolfBall> empty_balls;

                if (status) {

                    GS_LOG_MSG(info, "Found Ball - (X, Y, Z) (in cm): " + std::to_string(ball.distances_ortho_camera_perspective_[0]) + ", " +
                        std::to_string(ball.distances_ortho_camera_perspective_[1]) + ", " +
                        std::to_string(ball.distances_ortho_camera_perspective_[2]) + ". Radius: " + std::to_string(ball.measured_radius_pixels_) + "\n\n");


                    GolfSimCamera::ShowAndLogBalls("kCameraXBallLocation_" + std::to_string(i), img, balls, true);
                    GolfSimCamera::ShowAndLogBalls("kCameraXLocationImage_" + std::to_string(i), img, empty_balls, true);

                    cv::Mat gray_image;
                    cv::cvtColor(img, gray_image, cv::COLOR_BGR2GRAY);

                    bool choose_largest_final_ball = false;
                    
                    /***  NOT USING THIS NOW
                    if (!BallImageProc::DetermineBestCircle(gray_image, ball, choose_largest_final_ball, final_circle)) {
                        GS_LOG_MSG(error, "Failed to DetermineBestCircle.");
                    }
                    else
                    {
                        // A ball was found - show it to the user

                        GS_LOG_TRACE_MSG(trace, "Best Circle xiRadius was: " + std::to_string(final_circle[2]) + " pixels.\n\n");
                    }
                    */

                    // TBD - Send Test results IPC message
                    i++;
                }

                GolfSimCamera::ShowAndLogBalls("LastFailedBallImage_" + std::to_string(i), img, empty_balls, true);

            }

            GolfSimIpcSystem::ShutdownIPCSystem();
        }
        break;

        default:
            break;
    }

#else
    // TBD - REMOVE -Just for testing
    std::string address;
    GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress", address);

    if (kStartInPuttingMode) {
        GS_LOG_MSG(info, "Starting in Putting Mode.");
        GolfSimClubs::SetCurrentClubType(GolfSimClubs::GsClubType::kPutter);
    }
    else {
        GolfSimClubs::SetCurrentClubType(GolfSimClubs::GsClubType::kDriver);
    }



    testAnalyzeStrobedBalls();
    // testSpinDetection();
#endif

    return;

    /*

    //TBD - Temporary to get ball location for action trigger test
    // First call will ensure we have the OpenCV library loaded by doing a dummy test
    for (int i = 0; i < 10; i++) {
        test_hit_trigger();
    }


    test_calibrated_location("D:\\GolfSim\\C++Code\\GolfSim\\ImageProcessing\\move_test_ball_present_1024w_00.png",
        "./Images/FakePiCamGolfBall-Perfect-Clr-Green-3-feet-HiRes_01.png",
        "./Images/FakePiCamGolfBall-Perfect-Clr-Green-4-feet-HiRes_01.png");



    test_calibrated_location("D:\\GolfSim\\C++Code\\GolfSim\\ImageProcessing\\move_test_ball_present_00.png",
        "./Images/FakePiCamGolfBall-Perfect-Clr-Green-3-feet-HiRes_01.png",
        "./Images/FakePiCamGolfBall-Perfect-Clr-Green-4-feet-HiRes_01.png");
    test_calibrated_location("./Images/FakePiCamGolfBall-Perfect-Clr-Green-2-feet-HiRes_01.png",
        "./Images/FakePiCamGolfBall-Perfect-Clr-Green-3-feet-HiRes_01.png",
        "./Images/FakePiCamGolfBall-Perfect-Clr-Green-4-feet-HiRes_01.png");

    test_calibrated_location("./Images/FakePiCamGolfBall-Perfect-Clr-White-2-feet-HiRes_01.png",
        "./Images/FakePiCamGolfBall-Perfect-Clr-White-3-feet-HiRes_01.png",
        "./Images/FakePiCamGolfBall-Perfect-Clr-White-4-feet-HiRes_01.png");

    test_certain_images();

    test_all_test_files();
    */
}



int main(int argc, char *argv[])
{
    try {
        if (!GolfSimOptions::GetCommandLineOptions().Parse(argc, argv))
        {
            GS_LOG_MSG(error, "Could not GetCommandLineOptions.  Exiting.");
            return 0;
        }

        LoggingTools::InitLogging();

        GS_LOG_MSG(info, "Golf Sim Launch Monitor Started");

        GolfSimOptions::GetCommandLineOptions().Print();

        std::string config_file_name = "golf_sim_config.json";

        if (!GolfSimOptions::GetCommandLineOptions().config_file_.empty()) {
            config_file_name = GolfSimOptions::GetCommandLineOptions().config_file_;
        }

        if (!GolfSimConfiguration::Initialize(config_file_name)) {
            GS_LOG_MSG(error, "Could not initialize configuration module using config file: " + config_file_name + ".  Exiting.");
            return 0;
        }

        LoggingTools::logging_tool_wait_for_keypress_ = GolfSimOptions::GetCommandLineOptions().wait_for_key_on_images_;
#ifdef __unix__
        GolfSimConfiguration::SetConstant("gs_config.logging.kLinuxBaseImageLoggingDir", LoggingTools::kBaseImageLoggingDir);
#else
        GolfSimConfiguration::SetConstant("gs_config.logging.kPCBaseImageLoggingDir", LoggingTools::kBaseImageLoggingDir);
#endif
        // TBD - If the configuration file forgot to add a "/" at the end of the logging directory, we should add it here ourselves


        // TBD - consider if there is a better place for this?
        GolfSimGlobals::golf_sim_running_ = true;

        test_function(argc, argv);
    }
    catch (std::exception const& e)
    {
        GS_LOG_MSG(error, "Exception occurred. ERROR: *** " + std::string(e.what()) + " ***");
        return false;
    }

    GS_LOG_TRACE_MSG(trace, "Finished test_function.");
    
    // GS_LOG_TRACE_MSG(trace, "Waiting for any keypress to end program.");
    // cv::waitKey(0);

    GS_LOG_TRACE_MSG(trace, "Tests Complete");
}

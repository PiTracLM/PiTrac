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
#include <signal.h>
#include <atomic>

#include "gs_globals.h"
#include "golf_ball.h"
#include "gs_options.h"
#include "gs_gspro_results.h"
#include "gs_ui_system.h"
#include "gs_config.h"
#include "gs_results.h"
#include "gs_camera.h"

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
#include "gs_automated_testing.h"

#include "gs_fsm.h"
#include "gs_ipc_system.h"
#include "libcamera_interface.h"


namespace fs = std::filesystem;

using namespace golf_sim;

const double kLocationTolerancePercent = 10;


// The result files we create will be prefixed with this
static constexpr std::string_view TEST_IMAGE_PREFIX = "TEST_RESULT_GetBall_";

std::string kBaseTestDir = "Will be set from the .json configuration file";


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

    std::vector<cv::Vec3d> camera_positions_from_origin = std::vector<cv::Vec3d>({ GolfSimCamera::kCamera1PositionsFromExpectedBallMeters, GolfSimCamera::kCamera2PositionsFromExpectedBallMeters });
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


    std::string kBaseTestDir = "D:\\GolfSim\\C++Code\\GolfSim\\ImageProcessing\\";


    // We prefer the command-line setting even if there's one in the .json config file
    if (!GolfSimOptions::GetCommandLineOptions().base_image_logging_dir_.empty()) {
        kBaseTestDir = GolfSimOptions::GetCommandLineOptions().base_image_logging_dir_;
    }
    else {
        // Attempt to get the image logging directory from the .json config file
#ifdef __unix__
        GolfSimConfiguration::SetConstant("gs_config.logging.kLinuxBaseImageLoggingDir", kBaseTestDir);
#else
        GolfSimConfiguration::SetConstant("gs_config.logging.kPCBaseImageLoggingDir", kBaseTestDir);
#endif
    }


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
    std::vector<cv::Vec3d> camera_positions_from_origin = std::vector<cv::Vec3d>({ GolfSimCamera::kCamera1PositionsFromExpectedBallMeters, GolfSimCamera::kCamera2PositionsFromExpectedBallMeters });


    cv::Mat ball1ImgGray;
    cv::Mat ball2ImgGray;
    cv::Mat ball1ImgColor;
    cv::Mat ball2ImgColor;

    CameraHardware::CameraModel  camera_2_model = GolfSimCamera::kSystemSlot1CameraType;
    CameraHardware::LensType  camera_2_lens_type = GolfSimCamera::kSystemSlot1LensType;

    if (!GsAutomatedTesting::ReadTestImages(k0_DegreeBallFileName_00, kUnknown_DegreeBallFileName_00, ball1ImgGray, ball2ImgGray, ball1ImgColor, ball2ImgColor, camera_2_model, false /*No Undistort*/)) {
        GS_LOG_TRACE_MSG(trace, "Failed to read valid images.");
        return false;
    }

    // Get the ball data.  We will calibrate based on the first ball and then get the second one
    // using that calibrated data from the first ball.

    GolfSimCamera c;
    c.camera_hardware_.init_camera_parameters(GsCameraNumber::kGsCamera1, camera_2_model, camera_2_lens_type);

    GolfBall ball1, ball2;

    c.camera_hardware_.firstCannedImageFileName = kBaseTestDir + k0_DegreeBallFileName_00;
    c.camera_hardware_.firstCannedImage = ball1ImgColor;

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
    c.camera_hardware_.firstCannedImageFileName = kBaseTestDir + k0_DegreeBallFileName_00;
    c.camera_hardware_.secondCannedImageFileName = kBaseTestDir + kUnknown_DegreeBallFileName_00;
    c.camera_hardware_.firstCannedImage = ball1ImgColor;
    c.camera_hardware_.secondCannedImage = ball2ImgColor;

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

    // Have to call this here because we are not starting the FSM, but need (simulated) pulse information
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

    const GsCameraNumber camera_number = GolfSimOptions::GetCommandLineOptions().GetCameraNumber();
    const CameraHardware::CameraModel camera_model = GolfSimCamera::kSystemSlot1CameraType;

    if (!GsAutomatedTesting::ReadTestImages(kTestCamImageFileName_00, kTestCam2StrobedmageFileName, ball1ImgGray, ball2ImgGray, ball1ImgColor, ball2ImgColor, camera_model, false /*No Undistort*/)) {
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

    const CameraHardware::CameraModel camera_model = GolfSimCamera::kSystemSlot1CameraType;

    if (!GsAutomatedTesting::ReadTestImages(kCam1BallOnTee, kCam2BallInFlight, ball1ImgGray, ball2ImgGray, ball1ImgColor, ball2ImgColor, camera_model)) {
        GS_LOG_MSG(error, "Failed to read valid images.");
        return false;
    }

    BallImageProc *ip = get_image_processor();

    ip->image_name_ = kBaseTestDir + kCam1BallOnTee;

    GolfSimCamera c;
    //get camera operational and make sure working correctly
    c.camera_hardware_.camera_model_ = camera_model;

    GolfBall ball1, ball2;

    c.camera_hardware_.firstCannedImageFileName = kBaseTestDir + kCam1BallOnTee;
    c.camera_hardware_.firstCannedImage = ball1ImgColor;
    c.camera_hardware_.secondCannedImageFileName = kBaseTestDir + kCam2BallInFlight;
    c.camera_hardware_.secondCannedImage = ball2ImgColor;
    CameraHardware::CameraModel  camera_1_model = GolfSimCamera::kSystemSlot1CameraType;
    CameraHardware::LensType  camera_2_lens_type = GolfSimCamera::kSystemSlot1LensType;

    c.camera_hardware_.init_camera_parameters(GsCameraNumber::kGsCamera1, camera_1_model, camera_2_lens_type);

    std::vector<cv::Vec3d> camera_positions_from_origin = std::vector<cv::Vec3d>({ GolfSimCamera::kCamera1PositionsFromExpectedBallMeters, GolfSimCamera::kCamera2PositionsFromExpectedBallMeters });

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
    c.camera_hardware_.camera_model_ = CameraHardware::CameraModel::PiCam2;

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
    ball1Img = GsAutomatedTesting::UndistortImage(ball1ImgColor, c.camera_hardware_.camera_model_, c.camera_hardware_.lens_type_);
    ball2Img = GsAutomatedTesting::UndistortImage(ball2ImgColor, c.camera_hardware_.camera_model_, c.camera_hardware_.lens_type_);

    c.camera_hardware_.resolution_x_ = ball1Img.cols;
    c.camera_hardware_.resolution_y_ = ball1Img.rows;
    c.camera_hardware_.resolution_x_override_ = ball1Img.cols;
    c.camera_hardware_.resolution_y_override_ = ball1Img.rows;
    c.camera_hardware_.firstCannedImageFileName = kBaseTestDir + kStationaryBallFileName_00;
    c.camera_hardware_.secondCannedImageFileName = kBaseTestDir + kStationaryBallFileName_01;
    c.camera_hardware_.firstCannedImage = ball1Img;
    c.camera_hardware_.secondCannedImage = ball2Img;
    c.camera_hardware_.init_camera_parameters(GsCameraNumber::kGsCamera1, c.camera_hardware_.camera_model_, c.camera_hardware_.lens_type_);


    if (!c.prepareToTakePhoto()) {
        GS_LOG_MSG(error, "Cannot prepare camera for photos");
        return false;
    }


    std::vector<cv::Vec3d> camera_positions_from_origin = std::vector<cv::Vec3d>({ GolfSimCamera::kCamera1PositionsFromExpectedBallMeters, GolfSimCamera::kCamera2PositionsFromExpectedBallMeters });
    cv::Vec2i expectedBallCenter = cv::Vec2i(1300, 1000);

    GS_LOG_TRACE_MSG(trace, "Looking for ball on tee");
    bool success = false;
    bool timedOut = false;
    cv::Mat img;

    while (!success && !timedOut)
    {
        img = c.camera_hardware_.take_photo();

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
    test_result.hla_deg_ = 1.23F;
    test_result.back_spin_rpm_ = 3456;
    test_result.side_spin_rpm_ = -567;

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
    GsSimInterface::IncrementShotCounter();

    WaitForSimArmed();

    if (!WaitAndSendShotToSim(GsSimInterface::GetShotCounter(), test_result)) {
        GS_LOG_MSG(error, "Failed to WaitAndSendShotToSim (the Golf Simulator Interface).");
    }

    test_result.speed_mph_ = 55;
    test_result.vla_deg_ = 12.3F;

    GsSimInterface::IncrementShotCounter();

    WaitForSimArmed();

    if (!WaitAndSendShotToSim(GsSimInterface::GetShotCounter(), test_result)) {
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
    ball1.ball_circle_[2] = 50;
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

static std::atomic<bool> g_shutdown_requested(false);

void signal_handler(int sig) {
    GS_LOG_MSG(info, "Received signal " + std::to_string(sig) + ", initiating clean shutdown...");
    g_shutdown_requested = true;
    
    try {
        GS_LOG_MSG(info, "Cleaning up GPIO system...");
        PulseStrobe::DeinitGPIOSystem();
        GS_LOG_MSG(info, "GPIO system deinitialized successfully");
    } catch (const std::exception& e) {
        GS_LOG_MSG(error, "Error during GPIO cleanup: " + std::string(e.what()));
    } catch (...) {
        GS_LOG_MSG(error, "Unknown error during GPIO cleanup");
    }
    
    try {
        GS_LOG_MSG(info, "Shutting down IPC system...");
        GolfSimIpcSystem::ShutdownIPCSystem();
        GS_LOG_MSG(info, "IPC system shutdown successfully");
    } catch (const std::exception& e) {
        GS_LOG_MSG(error, "Error during IPC cleanup: " + std::string(e.what()));
    } catch (...) {
        GS_LOG_MSG(error, "Unknown error during IPC cleanup");
    }
    
    GS_LOG_MSG(info, "PiTrac shutdown complete");
    exit(0);
}

void setup_signal_handlers() {
#ifdef __unix__
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    
    GS_LOG_MSG(info, "Signal handlers installed for clean shutdown");
#endif
}

// In Linux, this function will start running the actual LM code.
// In Windows, this function will run whatever tests are currently
// commented in to run.
void run_main(int argc, char* argv[])
{
    GS_LOG_TRACE_MSG(trace, "run_main called");

    // Start of testing
    GS_LOG_TRACE_MSG(trace, "Running on " + GolfSimPlatform);  //  platform.platform());
    GS_LOG_TRACE_MSG(trace, "OpenCV Version " + std::string(CV_VERSION));

    bool kStartInPuttingMode = false;
    GolfSimConfiguration::SetConstant("gs_config.modes.kStartInPuttingMode", kStartInPuttingMode);


#ifdef __unix__
    // What the program will do depends on its mode


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
            GS_LOG_MSG(error, "Failed to kInterShotInjectionPauseSeconds.");
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

            if (!GolfSimOptions::GetCommandLineOptions().skip_wait_armed_) {
                while(!GsSimInterface::GetAllSystemsArmed()) {
                    sleep(2);
                    GS_LOG_MSG(info, "            Waiting for Simulator to Arm.");
                }
            } else {
                GS_LOG_MSG(info, "            Skipping wait for simulator armed (hardware-less testing mode).");
            }

            GsSimInterface::IncrementShotCounter();

            // Get the result to the golf simulator ASAP
            if (!GsSimInterface::SendResultsToGolfSims(result)) {
                GS_LOG_MSG(error, "Could not SendResultsToGolfSim. Continuing");
            }

            // Also send to Python webserver via IPC for monitoring
            GolfBall test_ball;
            test_ball.velocity_ = result.speed_mph_ * 0.44704;  // Convert mph to m/s
            test_ball.angles_ball_perspective_[0] = result.hla_deg_;
            test_ball.angles_ball_perspective_[1] = result.vla_deg_;
            test_ball.rotation_speeds_RPM_[0] = result.side_spin_rpm_;
            test_ball.rotation_speeds_RPM_[2] = result.back_spin_rpm_;

            std::string message = "Test shot #" + std::to_string(result.shot_number_);
            GsUISystem::SendIPCHitMessage(test_ball, message);
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


    // In this mode, we just take a single picture and save it.
    // Only do so locally if this is the camera1 system, of course
    // If in cam2_still_mode on the camera2 system, send the shutter
    // and strobe pulses asap, though the difference in
    // operation will be the number of strobe pulses is cut to 1
    // NOTE - when running in still mode for camera2, the Pi2/Camera2 process
    // must separately be up and running to take the picture and return it
    // to this process.
    if (GolfSimOptions::GetCommandLineOptions().camera_still_mode_) {

        GS_LOG_TRACE_MSG(trace, "Running in camera_still_mode.");

        // We will still need IPC and cameras and such to communicate in and between the systems as usual
        if (!PerformSystemStartupTasks()) {
            GS_LOG_MSG(error, "Failed to PerformSystemStartupTasks.");
            return;
        }

        GsCameraNumber camera_number = (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1 ||
                                        GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1BallLocation) ? GsCameraNumber::kGsCamera1 : GsCameraNumber::kGsCamera2;

        if (camera_number == GsCameraNumber::kGsCamera1) {
            GS_LOG_TRACE_MSG(trace, "Running in cam_still_mode on camera1 system.  Will take one picture.");
        }
        else {
            GS_LOG_TRACE_MSG(trace, "Running in  cam2 still mode on camera1 system.  Will take one strobed picture in camera2 system.");
        }
        cv::Mat image;

        GolfSimCamera camera;
        const CameraHardware::CameraModel camera_model = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1CameraType : GolfSimCamera::kSystemSlot2CameraType;
        CameraHardware::LensType camera_lens_type = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1LensType : GolfSimCamera::kSystemSlot2LensType;
        camera.camera_hardware_.init_camera_parameters(camera_number, camera_model, camera_lens_type);

        if (!GolfSimCamera::TakeStillPicture(camera, image)) {
            GS_LOG_MSG(error, "FAILED to PulseStrobe::SendCameraPrimingPulses");
        }

        std::string save_file_name = GolfSimOptions::GetCommandLineOptions().output_filename_;

        if (save_file_name.empty()) {
            GS_LOG_TRACE_MSG(trace, "No output output_filename specified.  Will save picture as: " + std::string(LoggingTools::kDefaultSaveFileName));
            save_file_name = LoggingTools::kDefaultSaveFileName;
        }

        // Save the image
        LoggingTools::LogImage("", image, std::vector < cv::Point >{}, true, save_file_name);

        PerformSystemShutdownTasks();

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
        case SystemMode::kRunCam2ProcessForPi1Processing:
        {
            GS_LOG_MSG(info, "Running in kCamera2 or kCamera2TestStandalone or kRunCam2ProcessForPi1Processing mode.");

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

        case SystemMode::kCamera1AutoCalibrate:
        case SystemMode::kCamera2AutoCalibrate:
        {
            GS_LOG_MSG(info, "Running in kCamera1AutoCalibrate or kCamera2AutoCalibrate mode.");

            // We will still need IPC and cameras and such to communicate in and between the systems as usual
            if (!PerformSystemStartupTasks()) {
                GS_LOG_MSG(error, "Failed to PerformSystemStartupTasks.");
                return;
            }

            GsCameraNumber camera_number = (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1AutoCalibrate ?
                                        GsCameraNumber::kGsCamera1 : GsCameraNumber::kGsCamera2);

            // We will want to send a calibration message to any monitor UIs
            if (!GolfSimCamera::AutoCalibrateCamera(camera_number)) {
                GS_LOG_MSG(info, "Failed to AutoCalibrateCamera.");
                return;
            }
            break;

            // Shutdown the camera2 system (if running) so the user doesn't have to do it
            GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kShutdown);
            GolfSimIpcSystem::SendIpcMessage(ipc_message);

            // Give the IPC thread time to send the message
            sleep(2);

            return;
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

        case SystemMode::kAutomatedTesting:
        {
            if (!GsAutomatedTesting::TestBallPosition()) {
                GS_LOG_MSG(info, "Failed to TestBallPosition.");
                return;
            }
        }
        break;

        case SystemMode::kCamera1BallLocation:
        case SystemMode::kCamera2BallLocation:
        {
            GS_LOG_MSG(info, "Running in kCamera1BallLocation or kCamera2BallLocation mode.");


            // We will still need IPC and cameras and such to communicate in and between the systems as usual
            if (!PerformSystemStartupTasks()) {
                GS_LOG_MSG(error, "Failed to PerformSystemStartupTasks.");
                return;
            }

            GsCameraNumber camera_number = (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1 ||
                                            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1BallLocation) ? GsCameraNumber::kGsCamera1 : GsCameraNumber::kGsCamera2;

            if (camera_number == GsCameraNumber::kGsCamera1) {
                GS_LOG_TRACE_MSG(trace, "Running in cam_still_mode on camera1 system.  Will take one picture.");
            }
            else {
                GS_LOG_TRACE_MSG(trace, "Running in  cam2 still mode on camera1 system.  Will take one strobed picture in camera2 system.");
            }


            GolfBall ball;
            cv::Mat img;
            std::vector<cv::Vec3d> camera_positions_from_origin = std::vector<cv::Vec3d>({ GolfSimCamera::kCamera1PositionsFromExpectedBallMeters, GolfSimCamera::kCamera2PositionsFromExpectedBallMeters });

            GolfSimCamera camera;
            const CameraHardware::CameraModel camera_model = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1CameraType : GolfSimCamera::kSystemSlot2CameraType;
            CameraHardware::LensType camera_lens_type = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1LensType : GolfSimCamera::kSystemSlot2LensType;
            camera.camera_hardware_.init_camera_parameters(camera_number, camera_model, camera_lens_type);

            int i = 0;

            while (GolfSimGlobals::golf_sim_running_) {


                if (!GolfSimCamera::TakeStillPicture(camera, img)) {
                    GS_LOG_MSG(error, "FAILED to PulseStrobe::SendCameraPrimingPulses");
                }


                cv::Vec2i search_area_center = camera.GetExpectedBallCenter();

                bool expectBall = false;
                bool success = camera.GetCalibratedBall(camera, img, ball, search_area_center, expectBall);

                std::vector<GolfBall> balls({ ball });
                std::vector<GolfBall> empty_balls;

                if (success) {

                    GS_LOG_MSG(info, "Found Ball - (X, Y, Z) (in cm): " + std::to_string(ball.distances_ortho_camera_perspective_[0]) + ", " +
                        std::to_string(ball.distances_ortho_camera_perspective_[1]) + ", " +
                        std::to_string(ball.distances_ortho_camera_perspective_[2]) + ". Radius: " + std::to_string(ball.measured_radius_pixels_) + "\n\n");


                    GolfSimCamera::ShowAndLogBalls("kCameraXBallLocation_" + std::to_string(i), img, balls, true);
                    GolfSimCamera::ShowAndLogBalls("kCameraXLocationImage_" + std::to_string(i), img, empty_balls, true);

                    cv::Mat gray_image;
                    cv::cvtColor(img, gray_image, cv::COLOR_BGR2GRAY);

                    i++;
                }
                else {
                    GS_LOG_MSG(warning, "Unable to find ball.  Check to ensure it is near the kCamera1 of kCamera2PositionsFromExpectedBallMeters and is adequately lit.");
                }

                GolfSimCamera::ShowAndLogBalls("LastAttemptedBallImage_" + std::to_string(i), img, empty_balls, true);

            }

            PerformSystemShutdownTasks();
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


    switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {

        case SystemMode::kAutomatedTesting:
        {
            GS_LOG_MSG(info, "Running in (Windows) kAutomatedTesting mode.");

            if (!GsAutomatedTesting::TestFinalShotResultData()) {
                GS_LOG_MSG(error, "Failed TestBallPosition().");
                return;
            }
            break;
        }

        case SystemMode::kCamera1AutoCalibrate:
        case SystemMode::kCamera2AutoCalibrate:
        {
            GS_LOG_MSG(info, "Running in kCamera1AutoCalibrate or kCamera2AutoCalibrate mode.");

            GsCameraNumber camera_number = (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1AutoCalibrate ?
                GsCameraNumber::kGsCamera1 : GsCameraNumber::kGsCamera2);

            // We will want to send a calibration message to any monitor UIs
            if (!GolfSimCamera::AutoCalibrateCamera(camera_number)) {
                GS_LOG_MSG(info, "Failed to AutoCalibrateCamera.");
                return;
            }

            break;
        }

        case SystemMode::kCamera1BallLocation:
        case SystemMode::kCamera2BallLocation:
        {
            GS_LOG_MSG(info, "Running in kCamera1BallLocation or kCamera2BallLocation mode.");

            // TBD - In Windows, this function is mostly just for debugging purposes
            // Now see if the ball location math will come up with a ball in the same place as the
            // calibration rig should have held that ball.
            GsCameraNumber camera_number = (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1BallLocation ?
                GsCameraNumber::kGsCamera1 : GsCameraNumber::kGsCamera2);

            GolfSimCamera camera;
            CameraHardware::CameraModel camera_model = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1CameraType : GolfSimCamera::kSystemSlot2CameraType;
            CameraHardware::LensType camera_lens_type = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1LensType : GolfSimCamera::kSystemSlot2LensType;
            camera.camera_hardware_.init_camera_parameters(camera_number, camera_model, camera_lens_type);

            cv::Mat color_image;

            if (!GolfSimCamera::TakeStillPicture(camera, color_image)) {
                GS_LOG_MSG(error, "FAILED to TakeStillPicture");
                return;
            }

            GolfBall ball;

            camera_number = GolfSimOptions::GetCommandLineOptions().GetCameraNumber();
            camera_model = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1CameraType : GolfSimCamera::kSystemSlot2CameraType;
            camera_lens_type = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1LensType : GolfSimCamera::kSystemSlot2LensType;

            camera.camera_hardware_.init_camera_parameters(camera_number, camera_model, camera_lens_type);
            camera.camera_hardware_.firstCannedImageFileName = std::string("/mnt/VerdantShare/dev/GolfSim/LM/Images/") + "FirstWaitingImage";
            camera.camera_hardware_.firstCannedImage = color_image;

            cv::Vec2i search_area_center = camera.GetExpectedBallCenter();

            bool expectBall = false;
            bool success = camera.GetCalibratedBall(camera, color_image, ball, search_area_center, expectBall);

            if (!success) {
                GS_LOG_TRACE_MSG(trace, "Failed to GetCalibratedBall.");
                return;
            }

            GS_LOG_TRACE_MSG(info, "(Simulated) kCamera1BallLocation Sanity Check Returned a ball = " + ball.Format());

            break;
        }

        default:
        {
            // Comment in whichever tests you want to run in Windows here
            testAnalyzeStrobedBalls();
            // test_strobed_balls_detection();
            // TestBallPosition();
            // test_gspro_communication();
            // testSpinDetection();
            break;
        }
    }

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
        setup_signal_handlers();
        
        if (!GolfSimOptions::GetCommandLineOptions().Parse(argc, argv))
        {
            GS_LOG_MSG(error, "Could not GetCommandLineOptions.  Exiting.");
            return 0;
        }

	// If we're just showing the help information, that's already been
	// done when we called parse.  So just exit.
	if (GolfSimOptions::GetCommandLineOptions().help_)
	{
		return 1;
	}

        LoggingTools::InitLogging();

        GS_LOG_MSG(info, "Golf Sim Launch Monitor Started");

        GolfSimOptions::GetCommandLineOptions().Print();

        std::string config_file_name = "golf_sim_config.json";

        if (!GolfSimOptions::GetCommandLineOptions().config_file_.empty()) {
            config_file_name = GolfSimOptions::GetCommandLineOptions().config_file_;
        }

        GS_LOG_MSG(info, "Loading configuration from: " + config_file_name);

        if (!GolfSimConfiguration::Initialize(config_file_name)) {
            GS_LOG_MSG(error, "Could not initialize configuration module using config file: " + config_file_name + ".  Exiting.");
            return 0;
        }

        LoggingTools::logging_tool_wait_for_keypress_ = GolfSimOptions::GetCommandLineOptions().wait_for_key_on_images_;

        // We prefer the command-line setting even if there's one in the .json config file
        if (!GolfSimOptions::GetCommandLineOptions().base_image_logging_dir_.empty()) {
            kBaseTestDir = GolfSimOptions::GetCommandLineOptions().base_image_logging_dir_;
        }
        else {
            // Attempt to get the image logging directory from the .json config file
#ifdef __unix__
            GolfSimConfiguration::SetConstant("gs_config.logging.kLinuxBaseImageLoggingDir", kBaseTestDir);
#else
            GolfSimConfiguration::SetConstant("gs_config.logging.kPCBaseImageLoggingDir", kBaseTestDir);
#endif
        }
        // If the configuration file forgot to add a "/" at the end of the logging directory, we should add it here ourselves
        if (kBaseTestDir.back() != '/') {
            kBaseTestDir += '/';
        }


        // TBD - consider if there is a better place for this?
        GolfSimGlobals::golf_sim_running_ = true;

        run_main(argc, argv);
        
        GS_LOG_MSG(info, "PiTrac Launch Monitor shutting down normally...");
        
        try {
            golf_sim::PulseStrobe::DeinitGPIOSystem();
            GS_LOG_MSG(info, "GPIO system cleaned up successfully");
        } catch (const std::exception& e) {
            GS_LOG_MSG(error, "Error during GPIO cleanup: " + std::string(e.what()));
        }
        
        try {
            golf_sim::GolfSimIpcSystem::ShutdownIPCSystem();
            GS_LOG_MSG(info, "IPC system cleaned up successfully");
        } catch (const std::exception& e) {
            GS_LOG_MSG(error, "Error during IPC cleanup: " + std::string(e.what()));
        }
    }
    catch (std::exception const& e)
    {
        GS_LOG_MSG(error, "Exception occurred. ERROR: *** " + std::string(e.what()) + " ***");
        
        try {
            golf_sim::PulseStrobe::DeinitGPIOSystem();
        } catch (...) {
            GS_LOG_MSG(error, "Failed to cleanup GPIO on exception");
        }
        
        try {
            golf_sim::GolfSimIpcSystem::ShutdownIPCSystem();
        } catch (...) {
            GS_LOG_MSG(error, "Failed to cleanup IPC on exception");
        }
        
        return -1;
    }

    GS_LOG_TRACE_MSG(trace, "Finished run_main.");

    // GS_LOG_TRACE_MSG(trace, "Waiting for any keypress to end program.");
    // cv::waitKey(0);

    GS_LOG_TRACE_MSG(trace, "Tests Complete");
    return 0;
}

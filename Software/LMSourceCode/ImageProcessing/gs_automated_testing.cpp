/*****************************************************************//**
 * \file   gs_automated_testing.cpp
 * \brief  Handles automated testing of PiTrac.
 *
 * \author PiTrac
 * \date   February 2025
 *********************************************************************/

 /* SPDX-License-Identifier: GPL-2.0-only */
 /*
  * Copyright (C) 2022-2025, Verdant Consultants, LLC.
  */



#include <fstream>

#include <boost/timer/timer.hpp>
#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <opencv2/calib3d/calib3d.hpp>

#include "gs_config.h"
#include "pulse_strobe.h"

#include "gs_automated_testing.h"


namespace fs = boost::filesystem;


namespace golf_sim {



    const cv::Vec3d  kRotationAngleToleranceAbs = { 10, 10, 5 };
    const cv::Vec3d  kDeltaLocationBallToleranceAbs = { 1, 1, 1 };
    const cv::Vec2d  kLaunchAngleToleranceAbs = { 10, 10 };

    std::string kAutomatedBaseTestDir = "Will be set from the .json configuration file";

/**
 * Tests to see if the absolute differences between the expected and result vectors are within
 * the tolerances.
 *
 * \param expected Expected values
 * \param result The values to compare to the expected values
 * \param abs_tolerances The tolerances to compare the absolute differences with
 * \return True if the absolute differences between each of the elements of the results
 * and the expected values are <= each of the respective tolerances.
 */
bool GsAutomatedTesting::AbsResultsPass(const cv::Vec2d& expected, const cv::Vec2d& result, const cv::Vec2d& abs_tolerances) {
    bool pass = true;

    if (std::abs(expected[0] - result[0]) > abs_tolerances[0]) {
        pass = false;
    }

    if (std::abs(expected[1] - result[1]) > abs_tolerances[1]) {
        pass = false;
    }

    return pass;
}


/**
 * Tests to see if the absolute differences between the expected and result vectors are within
 * the tolerances.
 *
 * \param expected Expected values
 * \param result The values to compare to the expected values
 * \param abs_tolerances The tolerances to compare the absolute differences with
 * \return True if the absolute differences between each of the elements of the results
 * and the expected values are <= each of the respective tolerances.
 */
bool GsAutomatedTesting::AbsResultsPass(const cv::Vec3d& expected, const cv::Vec3d& result, const cv::Vec3d& abs_tolerances) {
    bool pass = true;

    if (std::abs(expected[0] - result[0]) > abs_tolerances[0]) {
        pass = false;
    }

    if (std::abs(expected[1] - result[1]) > abs_tolerances[1]) {
        pass = false;
    }

    if (std::abs(expected[2] - result[2]) > abs_tolerances[2]) {
        pass = false;
    }

    return pass;
}

bool GsAutomatedTesting::AbsResultsPass(const float expected, const float result, const float abs_tolerances) {
    return !(std::abs(expected - result) > abs_tolerances);
}

bool GsAutomatedTesting::AbsResultsPass(const int expected, const int result, const int abs_tolerances) {
    return !(std::abs(expected - result) > abs_tolerances);
}



/**
 * Converts the vector of inches to a vector of meters.
 *
 * \param expectedPositionsInches
 * \param expectedPositionsMeters
 */
void GsAutomatedTesting::ConvertInchesToMeters(const cv::Vec3d& expectedPositionsInches, cv::Vec3d& expectedPositionsMeters) {
    expectedPositionsMeters[0] = CvUtils::InchesToMeters(expectedPositionsInches[0]);
    expectedPositionsMeters[1] = CvUtils::InchesToMeters(expectedPositionsInches[1]);
    expectedPositionsMeters[2] = CvUtils::InchesToMeters(expectedPositionsInches[2]);
}


bool GsAutomatedTesting::ReadExpectedResults(const std::string& expected_results_filename, std::vector<FinalResultsTestScenario>& shots) {

    std::ifstream file(expected_results_filename);
    std::string line;

    if (!file.is_open()) {
        GS_LOG_MSG(error, "ReadExpectedResults - could not open file: " + expected_results_filename);
        return false;
    }


    // Skip the first line, which we assume are column headings
    if (!std::getline(file, line)) {
        GS_LOG_MSG(error, "ReadExpectedResults - file " + expected_results_filename + " was empty.");
        return false;
    }

    int test_index = 0;
    
    while (std::getline(file, line)) {

        boost::tokenizer<boost::escaped_list_separator<char>> tok(line, boost::escaped_list_separator<char>('\\', ',', '"'));
        std::vector<std::string> fields;

        for (const auto& t : tok) {
            fields.push_back(t);
        }

        FinalResultsTestScenario shot;

        shot.test_index = test_index++;
        shot.shot_number = std::stoi(fields[0]);
        shot.expected_results.speed_mph_ = std::stof(fields[1]);
        shot.expected_results.vla_deg_ = std::stof(fields[2]);
        shot.expected_results.hla_deg_ = std::stof(fields[3]);
        shot.expected_results.back_spin_rpm_ = std::stoi(fields[4]);
        shot.expected_results.side_spin_rpm_ = std::stoi(fields[5]);
        if (fields[6].empty() || fields[6] == "FALSE" || fields[6] == "0") {
            shot.ignore_shot = false;
        }
        else {
            shot.ignore_shot = true;
        }

        shots.push_back(shot);

        GS_LOG_MSG(trace, "ReadExpectedResults - Shot: " + shot.expected_results.Format());
    }

    file.close();

    return true;
}



std::vector<std::string> GsAutomatedTesting::get_files_by_wildcard(const std::string& dir_path, const std::string& wildcard_pattern) {

    std::vector<std::string> matched_files;
    boost::regex pattern(wildcard_pattern);

    if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
        std::cerr << "Invalid directory path." << std::endl;
        return matched_files;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(dir_path)) {
        if (fs::is_regular_file(entry.status())) {
            std::string filename = entry.path().filename().string();
            if (boost::regex_match(filename, pattern)) {
                matched_files.push_back(entry.path().string());
            }
        }
    }

    return matched_files;
}


bool GsAutomatedTesting::TestFinalShotResultData() {

    std::string kWebServerLastTeedBallImageFilenamePrefix;
    std::string kWebServerCamera2ImageFilenamePrefix;

    GolfSimConfiguration::SetConstant("gs_config.user_interface.kWebServerLastTeedBallImage", kWebServerLastTeedBallImageFilenamePrefix);
    GolfSimConfiguration::SetConstant("gs_config.user_interface.kWebServerCamera2Image", kWebServerCamera2ImageFilenamePrefix);

    std::string kAutomatedTestSuiteDirectory;
    std::string kAutomatedTestExpectedResultsCSV;
    GsResults tolerances;

    GolfSimConfiguration::SetConstant("gs_config.testing.kAutomatedTestSuiteDirectory", kAutomatedTestSuiteDirectory);
    GolfSimConfiguration::SetConstant("gs_config.testing.kAutomatedTestExpectedResultsCSV", kAutomatedTestExpectedResultsCSV);
    GolfSimConfiguration::SetConstant("gs_config.testing.kAutomatedTestToleranceBallSpeedMPH", tolerances.speed_mph_);
    GolfSimConfiguration::SetConstant("gs_config.testing.kAutomatedTestToleranceHLA", tolerances.hla_deg_);
    GolfSimConfiguration::SetConstant("gs_config.testing.kAutomatedTestToleranceVLA", tolerances.vla_deg_);
    GolfSimConfiguration::SetConstant("gs_config.testing.kAutomatedTestToleranceBackSpin", tolerances.back_spin_rpm_);
    GolfSimConfiguration::SetConstant("gs_config.testing.kAutomatedTestToleranceSideSpin", tolerances.side_spin_rpm_);

    // Create absolute file path(s)
    kAutomatedTestExpectedResultsCSV = kAutomatedTestSuiteDirectory + kAutomatedTestExpectedResultsCSV;


    std::vector<FinalResultsTestScenario> tests;

    try {
        if (!ReadExpectedResults(kAutomatedTestExpectedResultsCSV, tests)) {
            GS_LOG_MSG(error, "Could not ReadExpectedResults().");
            return false;
        }
    }
    catch (std::exception& ex) {
        GS_LOG_TRACE_MSG(error, "Exception! - " + std::string(ex.what()) + ".  Exiting.");
        return false;
    }

    // TBD - Centralize in a single place -- this is also in the LoggingTools implementation
    const std::string kLogImagePrefix = "gs_log_img__";

    // For each expected result, find the corresponding image files

    for (FinalResultsTestScenario& r : tests) {

        // Perform a wild-card search to find the right file.  It basically just needs to have a particular prefix and the shot number
        std::string wildcarded_teed_ball_filename = kLogImagePrefix + kWebServerLastTeedBallImageFilenamePrefix + "_Shot_" + std::to_string(r.shot_number) + "_(.*)png";

        std::vector<std::string> teed_ball_filenames = get_files_by_wildcard(kAutomatedTestSuiteDirectory, wildcarded_teed_ball_filename);

        std::string wildcarded_strobed_ball_filename = kLogImagePrefix + kWebServerCamera2ImageFilenamePrefix + "_Shot_" + std::to_string(r.shot_number) + "_(.*)png";

        std::vector<std::string> wildcarded_strobed_ball_filenames = get_files_by_wildcard(kAutomatedTestSuiteDirectory, wildcarded_strobed_ball_filename);

        if (teed_ball_filenames.size() != 1 || wildcarded_strobed_ball_filenames.size() != 1) {
            GS_LOG_MSG(error, "Could not resolve iamge filenames.");
            return false;
        }

        std::string teed_ball_filename = teed_ball_filenames[0];
        std::string strobed_ball_filename = wildcarded_strobed_ball_filenames[0];

        r.teed_ball_filename = teed_ball_filename;
        r.strobed_ball_filename = strobed_ball_filename;
    }

    // The pulses must be setup so that we can determine, e.g., pulse-ratios for distance and time measurements

    if (!PulseStrobe::InitGPIOSystem(nullptr /* Signal handler not needed here */)) {
        GS_LOG_MSG(error, "Failed to InitGPIOSystem.");
        return false;
    }

    // Now that we have the images and expected results, perform the actual testing.

    std::string kAutomatedTestResultsCSV;
    GolfSimConfiguration::SetConstant("gs_config.testing.kAutomatedTestResultsCSV", kAutomatedTestResultsCSV);


    std::ofstream testing_results_csv_file(kAutomatedTestSuiteDirectory + kAutomatedTestResultsCSV);
    GS_LOG_TRACE_MSG(trace, "Writing CSV result data to: " + kAutomatedTestResultsCSV);
    testing_results_csv_file << "Shot ID,,Comparison (PiTrac value minus Uneekor),,,,,,System Data,,,,,,,,,,,,,,,,,,," << std::endl;
    testing_results_csv_file << "Ball, PiTrac Shot, Speed ? (mph), VLA ? , HLA ? °, Back Spin ? (rpm), Side Spin ? (rpm), , Uneekor Speed, PiTrac Speed, , Uneekor VLA°, PiTrac VLA°, , Uneekor HLA°, PiTrac HLA°, , Uneekor Back Spin, PiTrac Back Spin, , Uneekor Side Spin, PiTrac Side Spin, , Ball ID Picture, Spin Ball 1, Spin Ball 2, Test Result Ball, Notes" << std::endl;
    

    boost::timer::cpu_timer timer1;

    bool allTestsPassed = true;
    int numTotalTests = 0;
    int numTestsFailed = 0;

    CameraHardware::CameraModel  camera_model = CameraHardware::PiGS;

    for (auto& test : tests) {

        numTotalTests++;

        GS_LOG_TRACE_MSG(info, "Starting Test No. " + std::to_string(test.test_index) + ".");

        if (test.ignore_shot) {
            GS_LOG_TRACE_MSG(info, "Ignoring Test No. " + std::to_string(test.test_index) + ".");

            // Just leave a blank line with the shot number
            testing_results_csv_file << ",";
            testing_results_csv_file << test.shot_number << "," << std::endl;
            continue;
        }

        const std::string teed_ball_image_filename = test.teed_ball_filename;
        const std::string strobed_balls_image_filename = test.strobed_ball_filename;
        // The (simulated) camera and ball position will depend on the test images we use, above.

        // NOTE - These tests are expected to be run using the same .json configuration file
        // with which the original images were captured.

        cv::Mat teed_ball_ImgGray;
        cv::Mat strobed_balls_ImgGray;
        cv::Mat teed_ball_ImgColor;
        cv::Mat strobed_balls_ImgColor;

        if (!GsAutomatedTesting::ReadTestImages(teed_ball_image_filename, strobed_balls_image_filename, 
                                teed_ball_ImgGray, strobed_balls_ImgGray, teed_ball_ImgColor, strobed_balls_ImgColor, camera_model, false /* No undistort)*/, true /* do_not_alter_filenames */)) {
            GS_LOG_TRACE_MSG(warning, "Failed to read valid images for Test No. " + std::to_string(test.test_index));
            numTestsFailed++;
            continue;
        }

        // Run the test using whatever current .json configuration we have

        GolfBall result_ball;
        cv::Vec3d rotation_results;
        cv::Mat exposures_image;
        cv::Mat dummy_pre_image;
        std::vector<GolfBall> exposure_balls;

        if (!GolfSimCamera::ProcessReceivedCam2Image(teed_ball_ImgColor,
                                                     strobed_balls_ImgColor,
                                                     dummy_pre_image,
                                                     result_ball,
                                                     rotation_results,
                                                     exposures_image,
                                                     exposure_balls)) {
            GS_LOG_TRACE_MSG(warning, "Failed to ProcessReceivedCam2Image() for Test No. " + std::to_string(test.test_index));
            numTestsFailed++;
            continue;
        }

        result_ball.PrintBallFlightResults();

        // Compare the results to the expected results
        bool test_passed = true;

        if (!AbsResultsPass((float)CvUtils::MetersPerSecondToMPH((float)result_ball.velocity_), test.expected_results.speed_mph_, (float)tolerances.speed_mph_)) {
            GS_LOG_TRACE_MSG(info, "Test No. " + std::to_string(test.test_index) + " - Failed ball shot speed measurement.");
            test_passed = false;
        }

        if (!AbsResultsPass( (float)result_ball.angles_ball_perspective_[0], test.expected_results.hla_deg_, (float)tolerances.hla_deg_)) {
            GS_LOG_TRACE_MSG(info, "Test No. " + std::to_string(test.test_index) + " - Failed ball HLA measurement.");
            test_passed = false;
        }

        if (!AbsResultsPass((float)result_ball.angles_ball_perspective_[1], test.expected_results.vla_deg_, (float)tolerances.vla_deg_)) {
            GS_LOG_TRACE_MSG(info, "Test No. " + std::to_string(test.test_index) + " - Failed ball VLA measurement.");
            test_passed = false;
        }

        if (!AbsResultsPass( (int)result_ball.rotation_speeds_RPM_[2], test.expected_results.back_spin_rpm_, tolerances.back_spin_rpm_)) {
            GS_LOG_TRACE_MSG(info, "Test No. " + std::to_string(test.test_index) + " - Failed ball back spin measurement.");
            test_passed = false;
        }

        if (!AbsResultsPass((int)result_ball.rotation_speeds_RPM_[0], test.expected_results.side_spin_rpm_, tolerances.side_spin_rpm_)) {
            GS_LOG_TRACE_MSG(info, "Test No. " + std::to_string(test.test_index) + " - Failed ball side spin measurement.");
            test_passed = false;
        }

        if (!test_passed) {
            numTestsFailed++;
        }


        // Save the results (both the actual numbers and the differences) in the csv file
        testing_results_csv_file << ",";
        testing_results_csv_file << test.shot_number << ",";

        float speed_mph_delta = (float)CvUtils::MetersPerSecondToMPH((float)result_ball.velocity_) - test.expected_results.speed_mph_;
        float hla_deg_delta = (float)(result_ball.angles_ball_perspective_[0] - test.expected_results.hla_deg_);
        float vla_deg_delta = (float)(result_ball.angles_ball_perspective_[1] - test.expected_results.vla_deg_);
        int back_spin_rpm_delta = (int)(result_ball.rotation_speeds_RPM_[2] - test.expected_results.back_spin_rpm_);
        int side_spin_rpm_delta = (int)(result_ball.rotation_speeds_RPM_[0] - test.expected_results.side_spin_rpm_);

        testing_results_csv_file << speed_mph_delta << ",";
        testing_results_csv_file << hla_deg_delta << ",";
        testing_results_csv_file << vla_deg_delta << ",";
        testing_results_csv_file << back_spin_rpm_delta << ",";
        testing_results_csv_file << side_spin_rpm_delta << ", ,";

        testing_results_csv_file << test.expected_results.speed_mph_ << "," << CvUtils::MetersPerSecondToMPH((float)result_ball.velocity_) << ", ,";
        testing_results_csv_file << test.expected_results.vla_deg_ << "," << result_ball.angles_ball_perspective_[1] << ", ,";
        testing_results_csv_file << test.expected_results.hla_deg_ << "," << result_ball.angles_ball_perspective_[0] << ", ,";
        testing_results_csv_file << test.expected_results.back_spin_rpm_ << "," << result_ball.rotation_speeds_RPM_[2] << ", ,";
        testing_results_csv_file << test.expected_results.side_spin_rpm_ << "," << result_ball.rotation_speeds_RPM_[0] << ",";
        testing_results_csv_file << (test_passed? "PASS" : "FAIL") << std::endl;
        testing_results_csv_file << " , , , ," << std::endl;
    }

    testing_results_csv_file.close();


    GS_LOG_TRACE_MSG(trace, "Final Test Statistics:\nTotal Tests: " + std::to_string(numTotalTests) + ".\nTests Failed: " + std::to_string(numTestsFailed) + ".");

    timer1.stop();
    boost::timer::cpu_times times = timer1.elapsed();
    std::cout << "TestFinalShotResultData timing: ";
    std::cout << std::fixed << std::setprecision(8)
        << times.wall / 1.0e9 << "s wall, "
        << times.user / 1.0e9 << "s user + "
        << times.system / 1.0e9 << "s system.\n";


    return true;
}



cv::Mat GsAutomatedTesting::UndistortImage(const cv::Mat& img, CameraHardware::CameraModel camera_model, CameraHardware::LensType lens_type) {
    // Get a camera object just to be able to get the calibration values
    GolfSimCamera c;
    c.camera_hardware_.resolution_x_override_ = img.cols;
    c.camera_hardware_.resolution_y_override_ = img.rows;
    c.camera_hardware_.init_camera_parameters(GsCameraNumber::kGsCamera1, camera_model, lens_type);
    cv::Mat cameracalibrationMatrix_ = c.camera_hardware_.calibrationMatrix_;
    cv::Mat cameraDistortionVector_ = c.camera_hardware_.cameraDistortionVector_;

    cv::Mat unDistortedBall1Img;
    cv::Mat m_undistMap1, m_undistMap2;
    // TBD - is the size rows, cols?  or cols, rows?
    cv::initUndistortRectifyMap(cameracalibrationMatrix_, cameraDistortionVector_, cv::Mat(), cameracalibrationMatrix_, cv::Size(img.cols, img.rows), CV_32FC1, m_undistMap1, m_undistMap2);
    cv::remap(img, unDistortedBall1Img, m_undistMap1, m_undistMap2, cv::INTER_LINEAR);

    return unDistortedBall1Img;
}


bool GsAutomatedTesting::ReadTestImages(const std::string& img_1_base_filename, const std::string& img_2_base_filename, cv::Mat& ball1Img, cv::Mat& ball2Img, cv::Mat& ball1ImgColor, cv::Mat& ball2ImgColor,
    CameraHardware::CameraModel camera_model, bool undistort, bool do_not_alter_filenames) {

    std::string kBaseTestDir;

    if (!do_not_alter_filenames) {
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

        // If a base directory for test images exists, use it to override the kBaseTestDir
        std::string separate_base_test_dir;
        GolfSimConfiguration::SetConstant("gs_config.testing.kBaseTestImageDir", separate_base_test_dir);
        if (!separate_base_test_dir.empty()) {
            kBaseTestDir = separate_base_test_dir;
        }
    }

    std::string img1FileName = kBaseTestDir + img_1_base_filename;
    std::string img2FileName = kBaseTestDir + img_2_base_filename;

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

    if (undistort) {
        // TBD - Parametersize the lens_type later
        unDistortedBall1Img = GsAutomatedTesting::UndistortImage(ball1Img, camera_model, CameraHardware::LensType::Lens_6mm);
        unDistortedBall2Img = GsAutomatedTesting::UndistortImage(ball2Img, camera_model, CameraHardware::LensType::Lens_6mm);

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


// This function is deprecated.  Leaving it here for now just in case.
// It originally tested the exact ball-position calculations against canned data.
// But at this point in time (February 2025), it probably makes more sense to just
// do regression testing against the final outputs (speed, HLA, etc.).
bool GsAutomatedTesting::TestBallPosition() {

    std::vector<LocationAndSpinTestScenario> old_tests;

#ifdef NOT_THIS_NOW
    LocationAndSpinTestScenario tests[]{

        { 1, "test_3280w_BS_camoff04x_8.5y_20z_ball00z_02y_00degx_spin00z_v1.png", "test_3280w_camoff04x_8.5y_20z_ball10z_03y_15degx_spin30z_v1.png",
            CameraHardware::CameraModel::PiCam2, std::vector<cv::Vec3d>({cv::Vec3d(0.1016, 0.2159, 0.508), cv::Vec3d(0.1016, 0.2159, 0.508)}), cv::Vec2i(1100, 1000), cv::Vec3d(3, 2, 10), cv::Vec2d(45, 7), cv::Vec3d(0, 0, 30)
        },

        { 2, "test_pos_4056w_cam04offx_14y_17z_Ball_0inz_00degx_00iny_00Zsp_00.png", "test_pos_4056w_cam04offx_14y_17z_Ball_10inz_10degx_00iny_30Zsp_00.png",
            CameraHardware::CameraModel::PiHQCam6mmWideLens, std::vector<cv::Vec3d>({cv::Vec3d(0.1016, 0.3556, 0.4318), cv::Vec3d(.1016, 0.3556, 0.4318)}), cv::Vec2i(1100, 2000), cv::Vec3d(6.5, -2, -1.5), cv::Vec2d(13, 0), cv::Vec3d(4, -2, 30)
        },

        { 3, "IRtest02.1-filter.png", "IRtest02.2-filter.png",
            CameraHardware::CameraModel::PiHQCam6mmWideLens, std::vector<cv::Vec3d>({cv::Vec3d(.0914, 0.0953, 0), cv::Vec3d(.0914, 0.0953, 0)}), cv::Vec2i(1400, 800), cv::Vec3d(6.5, -2, -1.5), cv::Vec2d(13, 0), cv::Vec3d(4, -2, 30)
        },
    };

#endif

    boost::timer::cpu_timer timer1;

    bool allTestsPassed = true;
    int numTotalTests = 0;
    int numTestsFailed = 0;

    for (auto& t : old_tests) {

        numTotalTests++;

        const std::string teed_ball_image_filename = t.img1;
        const std::string strobed_balls_image_filename = t.img2;
        // The (simulated) camera and ball position will depend on the test images we use, above.
        std::vector<cv::Vec3d> camera_positions_from_origin = t.camera_positions_from_origin;  // In meters
        cv::Vec2i calibration_ball_center = t.calibration_ball_center;


        cv::Mat ball1ImgGray;
        cv::Mat ball2ImgGray;
        cv::Mat ball1ImgColor;
        cv::Mat ball2ImgColor;
        if (!GsAutomatedTesting::ReadTestImages(teed_ball_image_filename, strobed_balls_image_filename, ball1ImgGray, ball2ImgGray, ball1ImgColor, ball2ImgColor, t.camera_model)) {
            GS_LOG_TRACE_MSG(trace, "Failed to read valid images for Test No. " + std::to_string(t.test_index));
            numTestsFailed++;
            continue;
        }


        GolfSimCamera c;
        c.camera_hardware_.resolution_x_ = ball1ImgColor.cols;
        c.camera_hardware_.resolution_y_ = ball1ImgColor.rows;
        c.camera_hardware_.resolution_x_override_ = ball1ImgColor.cols;
        c.camera_hardware_.resolution_y_override_ = ball1ImgColor.rows;

        // Just for development on a non-Raspberry-Pi machine
        c.camera_hardware_.firstCannedImageFileName = kAutomatedBaseTestDir + teed_ball_image_filename;
        c.camera_hardware_.secondCannedImageFileName = kAutomatedBaseTestDir + strobed_balls_image_filename;
        c.camera_hardware_.firstCannedImage = ball1ImgColor;
        c.camera_hardware_.secondCannedImage = ball2ImgColor;
        // TBD - Add the lens type to the tests
        c.camera_hardware_.init_camera_parameters(GsCameraNumber::kGsCamera1, t.camera_model, CameraHardware::LensType::Lens_6mm);

        long timeDelayuS = 7000;
        GolfBall result_ball;

        GS_LOG_TRACE_MSG(trace, "Starting Test No. " + std::to_string(t.test_index) + ".");

        if (!c.analyzeShotImages(c, ball1ImgColor, ball2ImgColor,
            timeDelayuS,
            camera_positions_from_origin,
            result_ball,
            calibration_ball_center)) {
            GS_LOG_TRACE_MSG(trace, "Failed Test No. " + std::to_string(t.test_index));
            continue;
        }

        double ball_velocity_meters_per_second_ = result_ball.velocity_;

        result_ball.PrintBallFlightResults();

        bool test_passed = true;

        if (!AbsResultsPass(t.expected_xyz_rotation_degrees, result_ball.ball_rotation_angles_camera_ortho_perspective_, kRotationAngleToleranceAbs)) {
            GS_LOG_TRACE_MSG(trace, "Test No. " + std::to_string(t.test_index) + " - Failed ball rotation measurement.");
            GS_LOG_TRACE_MSG(trace, "    Expected X,Y,Z rotation angles (in degrees) are: " + std::to_string((t.expected_xyz_rotation_degrees[0])) + ", " +
                std::to_string((t.expected_xyz_rotation_degrees[1])) + ", " + std::to_string((t.expected_xyz_rotation_degrees[2])));
            test_passed = false;
        }

        cv::Vec3d expectedPositionsMeters;
        ConvertInchesToMeters(t.expected_position_deltas_ball_perspective_, expectedPositionsMeters);

        if (!AbsResultsPass(expectedPositionsMeters, result_ball.position_deltas_ball_perspective_, kDeltaLocationBallToleranceAbs)) {
            GS_LOG_TRACE_MSG(trace, "Test No. " + std::to_string(t.test_index) + " - Failed ball delta location measurement.");
            GS_LOG_TRACE_MSG(trace, "    Expected X,Y,Z deltas (ball perspective in inches) are: " + std::to_string(CvUtils::MetersToInches(t.expected_position_deltas_ball_perspective_[0])) + ", " +
                std::to_string(CvUtils::MetersToInches(t.expected_position_deltas_ball_perspective_[1])) + ", " + std::to_string(CvUtils::MetersToInches(t.expected_position_deltas_ball_perspective_[2])));
            test_passed = false;
        }

        if (!AbsResultsPass(t.expected_xy_ball_angle_degrees, result_ball.angles_ball_perspective_, kLaunchAngleToleranceAbs)) {
            GS_LOG_TRACE_MSG(trace, "Test No. " + std::to_string(t.test_index) + " - Failed ball launch angle measurement.");
            GS_LOG_TRACE_MSG(trace, "    Expected X,Y launch angles (ball perspective) (in degrees) are: " + std::to_string(t.expected_xy_ball_angle_degrees[0]) + ", " +
                std::to_string(t.expected_xy_ball_angle_degrees[1]));
            test_passed = false;
        }

        if (!test_passed) {
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





}
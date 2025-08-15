/*****************************************************************//**
 * \file   gs_automated_testing.h
 * \brief  Handles automated testing of PiTrac.
 * 
 * \author PiTrac
 * \date   February 2025
 *********************************************************************/

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

 
#pragma once


#include <iostream>
#include <filesystem>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include "gs_camera.h"
#include "gs_results.h"

namespace golf_sim {

    class GsAutomatedTesting {

    public:

        struct FinalResultsTestScenario {
            int test_index = 0;
            int shot_number = 0;
            std::string teed_ball_filename;
            std::string strobed_ball_filename;
            GsResults expected_results;
            bool ignore_shot = false;
        };

        // Deprecated
        struct LocationAndSpinTestScenario {
            int test_index = 0;
            int shot_number = 0;
            std::string img1;
            std::string img2;
            CameraHardware::CameraModel camera_model;
            std::vector<cv::Vec3d> camera_positions_from_origin;  // In meters.  Size=2, one vector for each image.  If same, then only one camera position was used.
            cv::Vec2i calibration_ball_center;        // The x,y coordinates of where the first ball's picture should concentrate as an ROI
            cv::Vec3d expected_position_deltas_ball_perspective_;
            cv::Vec2d expected_xy_ball_angle_degrees;
            cv::Vec3d expected_xyz_rotation_degrees;
        };

        static std::string kAutomatedTestDir;


    public:

        static bool TestFinalShotResultData();

        static void ConvertInchesToMeters(const cv::Vec3d& expectedPositionsInches, cv::Vec3d& expectedPositionsMeters);

        static bool ReadTestImages(const std::string& img_1_base_filename, 
                                    const std::string& img_2_base_filename, 
                                    cv::Mat& ball1Img, 
                                    cv::Mat& ball2Img, 
                                    cv::Mat& ball1ImgColor, 
                                    cv::Mat& ball2ImgColor,
                                    CameraHardware::CameraModel camera_model, 
                                    bool undistort = true, 
                                    bool do_not_alter_filenames = false);

        static cv::Mat UndistortImage(const cv::Mat& img, CameraHardware::CameraModel camera_model, CameraHardware::LensType lens_type);

        static bool ReadExpectedResults(const std::string& expected_results_filename, std::vector<FinalResultsTestScenario>& shots);

        static std::vector<std::string> get_files_by_wildcard(const std::string& dir_path, const std::string& wildcard_pattern);

        static bool AbsResultsPass(const cv::Vec2d& expected, const cv::Vec2d& result, const cv::Vec2d& abs_tolerances);
        static bool AbsResultsPass(const cv::Vec3d& expected, const cv::Vec3d& result, const cv::Vec3d& abs_tolerances);
        static bool AbsResultsPass(const float expected, const float result, const float abs_tolerances);
        static bool AbsResultsPass(const int expected, const int result, const int abs_tolerances);

        // Deprecated
        static bool TestBallPosition();
    };
}

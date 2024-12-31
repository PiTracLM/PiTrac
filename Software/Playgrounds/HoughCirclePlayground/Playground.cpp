/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// This code allows the user to play with the HoughCircle transform parameters to
// help determine which parameters work best.  The HoughCircle process is pretty
// touch, so sometimes it's easier to move some sliders around to figure out what
// works best instead of trying different parameters in a .json file and running the
// applicaiton again and again.

#include <iostream>
#include <cstring>
#include <filesystem>

#include "cv_utils.h"
#include "gs_globals.h"
#include "logging_tools.h"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <math.h>



namespace fs = std::filesystem;

namespace gs = golf_sim;



// Deprecated
int hsv_lower_h = 14; //16;
int hsv_upper_h = 48;
int hsv_lower_s = 26;
int hsv_upper_s = 255;
int hsv_lower_v = 114;
int hsv_upper_v = 255;

int kStrobedEnvironmentCannyLower = 35; //  41 for external;
int kStrobedEnvironmentCannyUpper = 81;  // 44 for external 98;
int kStrobedEnvironmentPreCannyBlurSizeInt = 11;
int kStrobedEnvironmentPreHoughBlurSizeInt = 16;

int kStrobedEnvironmentHoughDpParam1Int = 8;  //  Will be divided by 10 if using HOUGH_GRADIENT_ALT.   Either 1.7 or *1.8* tremendously helps the accuracy! 1.9 can work ok for external.  1.3 is good for stationary ball (cam1)
int kStrobedEnvironmentBallCurrentParam1Int = 130;
int kStrobedEnvironmentBallMinParam2Int = 28; // 60 for external
int kStrobedEnvironmentBallMaxParam2Int = 140;
int kStrobedEnvironmentBallStartingParam2Int = 65;
int kStrobedEnvironmentBallParam2IncrementInt = 4;
int kStrobedEnvironmentMinimumSearchRadiusInt = 48; // 59; //  60;  // gets bad fast as this widens.  I'd try 56 and 85 for now
int kStrobedEnvironmentMaximumSearchRadiusInt = 120;  // 148;  // 80;

// The following are specific to an experiment designed to remove Uneekor strobe artifacts
int kStrobedEnvironmentHoughLineIntersections = 58;
int kStrobedEnvironmentMinimumHoughLineLength = 23;
int kStrobedEnvironmentMaximumHoughLineGap = 7;
int kStrobedEnvironmentLinesAngleLower = 190;  // 180 is a horizontal line
int kStrobedEnvironmentLinesAngleUpper = 290;  // 280 is a vertical line


int kStrobedEnvironmentBottomIgnoreHeight = 0; //  80;

int min_hough_circle_distance = 8;

// TBD - Not used?
int kStrobedEnvironmentMinHoughReturnCirclesInt = 3;
int kStrobedEnvironmentMaxHoughReturnCirclesInt = 1.2;


cv::Mat src_f;
cv::Mat dest;
int binary_threshold = 2;   // *10.  Nominal: 3
int white_percent = 0;   // This will be an output

bool process_window_ready = false;


    // The result files we create will be prefixed with this
    static std::string processWindowName = "Process window";



    void sizeWindow(std::string windowName, cv::Mat img) {

        static int xIncrement = 0;
        static int yIncrement = 0;

        cv::Rect r = cv::getWindowImageRect(windowName);

        int windowWidth = r.width;
        int windowHeight = r.height;

        // int largestDim = (int)std::max(windowWidth, windowHeight);
        int largestDim = (int)std::max(gs::CvUtils::CvWidth(img), gs::CvUtils::CvHeight(img));

        largestDim = (int)std::max(largestDim, 900);

        int xDim = 0;
        int yDim = 0;

        if (windowWidth > windowHeight)
        {
            xDim = largestDim;
            yDim = (int)((float)largestDim * ((float)windowHeight / (float)windowWidth));
        }
        else
        {
            yDim = largestDim;
            xDim = (int)((float)largestDim * ((float)windowWidth / (float)windowHeight));
        }

        cv::resizeWindow(windowName, xDim, yDim);
        // cv::moveWindow(windowName, 1200+xIncrement, 20+yIncrement);
    }


    // Returns a mask with 1 bits wherever the corresponding pixel is OUTSIDE the upper/lower HSV range
    cv::Mat GetColorMaskImage(const cv::Mat& hsvImage, const gs::GsColorTriplet input_lowerHsv, const gs::GsColorTriplet input_upperHsv) {

        gs::GsColorTriplet lowerHsv = input_lowerHsv;
        gs::GsColorTriplet upperHsv = input_upperHsv;

        const int kColorMaskWideningAmount = 0;

        for (int i = 0; i < 3; i++) {
            lowerHsv[i] -= kColorMaskWideningAmount;   // (int)std::round(((double)lowerHsv[i] * kColorMaskWideningRatio));
            upperHsv[i] += kColorMaskWideningAmount;   //(int)std::round(((double)upperHsv[i] * kColorMaskWideningRatio));
        }


        // Ensure we didn't go too big on the S or V upper bound (which is 255)
        upperHsv[1] = std::min((int)upperHsv[1], 255);
        upperHsv[2] = std::min((int)upperHsv[2], 255);

        // Because we are creating a binary mask, it should be CV_8U or CV_8S (TBD - I think?)
        // cv::Mat color_mask_image(hsvImage.rows, hsvImage.cols, CV_8S, cv::Scalar(0));
        cv::Mat color_mask_image = cv::Mat::zeros(hsvImage.size(), CV_8U);
        //        CvUtils::SetMatSize(hsvImage, color_mask_image);
        // color_mask_image = hsvImage.clone();

        // We will need TWO masks if the hue range crosses over the 180 - degreee "loop" point for reddist colors
        // TBD - should we convert the ranges to scalars?
        if ((lowerHsv[0] >= 0) && (upperHsv[0] <= (float)gs::CvUtils::kOpenCvHueMax)) {
            cv::inRange(hsvImage, cv::Scalar(lowerHsv), cv::Scalar(upperHsv), color_mask_image);
            bitwise_not(color_mask_image, color_mask_image);
        }
        else {
            // 'First' and 'Second' refer to the Hsv triplets that will be used for he first and second masks
            cv::Vec3f firstLowerHsv;
            cv::Vec3f secondLowerHsv;
            cv::Vec3f firstUpperHsv;
            cv::Vec3f secondUpperHsv;

            cv::Vec3f leftMostLowerHsv;
            cv::Vec3f leftMostUpperHsv;
            cv::Vec3f rightMostLowerHsv;
            cv::Vec3f rightMostUpperHsv;

            // Check the hue range - does it loop around 180 degrees?
            if (lowerHsv[0] < 0) {
                // the lower hue is below 0
                leftMostLowerHsv = cv::Vec3f(0.f, (float)lowerHsv[1], (float)lowerHsv[2]);
                leftMostUpperHsv = cv::Vec3f((float)upperHsv[0], (float)upperHsv[1], (float)upperHsv[2]);
                rightMostLowerHsv = cv::Vec3f((float)gs::CvUtils::kOpenCvHueMax + (float)lowerHsv[0], (float)lowerHsv[1], (float)lowerHsv[2]);
                rightMostUpperHsv = cv::Vec3f((float)gs::CvUtils::kOpenCvHueMax, (float)upperHsv[1], (float)upperHsv[2]);
            }
            else {
                // the upper hue is over 180 degrees
                leftMostLowerHsv = cv::Vec3f(0.f, (float)lowerHsv[1], (float)lowerHsv[2]);
                leftMostUpperHsv = cv::Vec3f((float)upperHsv[0] - 180.f, (float)upperHsv[1], (float)upperHsv[2]);
                rightMostLowerHsv = cv::Vec3f((float)lowerHsv[0], (float)lowerHsv[1], (float)lowerHsv[2]);
                rightMostUpperHsv = cv::Vec3f((float)gs::CvUtils::kOpenCvHueMax, (float)upperHsv[1], (float)upperHsv[2]);
            }

            //GS_LOG_MSG(trace, "leftMost Lower/Upper HSV{ " + LoggingTools::FormatVec3f(leftMostLowerHsv) + ", " + LoggingTools::FormatVec3f(leftMostUpperHsv) + ".");
            //GS_LOG_MSG(trace, "righttMost Lower/Upper HSV{ " + LoggingTools::FormatVec3f(rightMostLowerHsv) + ", " + LoggingTools::FormatVec3f(rightMostUpperHsv) + ".");

            cv::Mat firstColorMaskImage;
            cv::inRange(hsvImage, leftMostLowerHsv, leftMostUpperHsv, firstColorMaskImage);

            cv::Mat secondColorMaskImage;
            cv::inRange(hsvImage, rightMostLowerHsv, rightMostUpperHsv, secondColorMaskImage);

            //LoggingTools::DebugShowImage(image_name_ + "  firstColorMaskImage", firstColorMaskImage);
            //LoggingTools::DebugShowImage(image_name_ + "  secondColorMaskImage", secondColorMaskImage);

            cv::Mat intermediate_color_mask;
            cv::bitwise_or(firstColorMaskImage, secondColorMaskImage, intermediate_color_mask);
            bitwise_not(intermediate_color_mask, color_mask_image);
        }

        //LoggingTools::DebugShowImage("BallImagProc::GetColorMaskImage returning color_mask_image", color_mask_image);

        return color_mask_image;
    }

    
    void DrawFilterLines(const std::vector<cv::Vec4i>& lines, 
                        cv::Mat& image, 
                        const cv::Scalar& color, 
                        const int thickness = 1) {
        for (size_t i = 0; i < lines.size(); i++)
        {
            cv::Point pt1 = cv::Point(lines[i][0], lines[i][1]);
            cv::Point pt2 = cv::Point(lines[i][2], lines[i][3]);

            double angle = atan2(pt1.y - pt2.y, pt1.x - pt2.x);
            if (angle < 0.0) {
                angle += 2 * CV_PI;
            }

            angle = gs::CvUtils::RadiansToDegrees(angle);

            std::cout << "angle = " << std::to_string(angle) << std::endl;
            // std::cout << "rho, theta = " << std::to_string(rho) << ", " << std::to_string(gs::CvUtils::RadiansToDegrees(theta)) << "." << std::endl;

            bool is_high_priority_angle = (angle > kStrobedEnvironmentLinesAngleLower) && (angle < kStrobedEnvironmentLinesAngleUpper);

            double line_length = sqrt((pow((double)(pt1.x - pt2.x), 2.) + pow((double)(pt1.y - pt2.y), 2.)));

            // Ignore this line if it's not in the most-relevant angle range unless
            // it's a long line.
            if (false && !is_high_priority_angle  /* && line_length < kStrobedEnvironmentMinimumHoughLineLength */) {
                continue;
            }

            // cv::line(image, pt1, pt2, color, thickness, cv::LINE_AA);
            cv::line(image, pt1, pt2, color, thickness);

        }
    }

    void Process(int, void*)
    {
        if (!process_window_ready) {
            return;
        }

        int h = src_f.rows;
        int w = src_f.cols;

        // Filtering out long lines (usually of the golf shaft)

        cv::Mat src_f_gray;
        cv::Mat cannyOutput;

        cv::cvtColor(src_f, src_f_gray, cv::COLOR_BGR2GRAY);

        cv::Scalar black_color{ 0,0,0 };
        cv::Scalar white_color{ 255,255,255 };
        cv::Scalar red_color{ 0 ,0, 255 };

        // NOTE - the following code should mirror the code in the main LM BallImageProc::GetBall function,
        // though this code will only account for one main mode of operation, typically the kStrobedBall mode.

        // The size for the blur must be odd
        if (kStrobedEnvironmentPreCannyBlurSizeInt > 0) {
            if (kStrobedEnvironmentPreCannyBlurSizeInt % 2 != 1) {
                kStrobedEnvironmentPreCannyBlurSizeInt++;
            }
            cv::GaussianBlur(src_f_gray, src_f_gray, cv::Size(kStrobedEnvironmentPreCannyBlurSizeInt, kStrobedEnvironmentPreCannyBlurSizeInt), 0);
        }
 
        // Change the "< 0" to "< 2" or similar to try this sharpening technique.
        // Usually this is harmful, so currently disabled.
        for (int i = 0; i < 0; i++) {
            cv::erode(src_f_gray, src_f_gray, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
            cv::dilate(src_f_gray, src_f_gray, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
        }
        
        // Get a good picture of the edges of the balls.  Will probably have way too many shaft lines
        cv::Mat cannyOutput_for_balls = src_f_gray.clone();
        cv::Canny(src_f_gray, cannyOutput_for_balls, kStrobedEnvironmentCannyLower, kStrobedEnvironmentCannyUpper);

        cv::imshow("Initial cannyOutput", cannyOutput_for_balls);
        sizeWindow("Initial cannyOutput", cannyOutput_for_balls);

        if (kStrobedEnvironmentPreHoughBlurSizeInt > 0) {
            if (kStrobedEnvironmentPreHoughBlurSizeInt % 2 != 1) {
                kStrobedEnvironmentPreHoughBlurSizeInt++;
            }
            cv::GaussianBlur(cannyOutput_for_balls, cannyOutput_for_balls, cv::Size(kStrobedEnvironmentPreHoughBlurSizeInt, kStrobedEnvironmentPreHoughBlurSizeInt), 0);
        }

        // cannyOutput_for_balls = cannyOutput_for_balls * -1 + 255;
        if (kStrobedEnvironmentBottomIgnoreHeight > 0) {
            cv::Rect floor_blackout_area{ 0, h - kStrobedEnvironmentBottomIgnoreHeight, w, h };
            cv::rectangle(cannyOutput_for_balls, floor_blackout_area.tl(), floor_blackout_area.br(), black_color, cv::FILLED);
        }


        cv::imshow("Final PreHough Image", cannyOutput_for_balls);
        sizeWindow("Final PreHough Image", cannyOutput_for_balls);


        double kStrobedEnvironmentBallCurrentParam1 = (double)kStrobedEnvironmentBallCurrentParam1Int;
        double kStrobedEnvironmentBallMinParam2 = (double)kStrobedEnvironmentBallMinParam2Int;
        double kStrobedEnvironmentBallMaxParam2 = (double)kStrobedEnvironmentBallMaxParam2Int;
        double kStrobedEnvironmentBallStartingParam2 = (double)kStrobedEnvironmentBallStartingParam2Int;
        double kStrobedEnvironmentBallParam2Increment = (double)kStrobedEnvironmentBallParam2IncrementInt;
        double kStrobedEnvironmentMinHoughReturnCircles = (double)kStrobedEnvironmentMinHoughReturnCirclesInt;
        double kStrobedEnvironmentMaxHoughReturnCircles = (double)kStrobedEnvironmentMaxHoughReturnCirclesInt;
        double kStrobedEnvironmentPreHoughBlurSize = (double)kStrobedEnvironmentPreHoughBlurSizeInt;
        double kStrobedEnvironmentHoughDpParam1 = (double)kStrobedEnvironmentHoughDpParam1Int / 10.0;
        double kStrobedEnvironmentMinimumSearchRadius = (double)kStrobedEnvironmentMinimumSearchRadiusInt;
        double kStrobedEnvironmentMaximumSearchRadius = (double)kStrobedEnvironmentMaximumSearchRadiusInt;

        int minDistance = min_hough_circle_distance;

        cv::Mat final_search_image = cannyOutput_for_balls.clone();

        // A main switch - currently, haven't had good luck with this
        const bool kUseAltGradient = true;

        double local_param2 = 0;
        cv::HoughModes mode = cv::HOUGH_GRADIENT;

        if (kUseAltGradient) {
            local_param2 = kStrobedEnvironmentBallStartingParam2 / 100.0;
            if (local_param2 >= 1.0) {
                local_param2 = 0.9999;
            }
            mode = cv::HOUGH_GRADIENT_ALT;
        }
        else {
            local_param2 = kStrobedEnvironmentBallStartingParam2;
            mode = cv::HOUGH_GRADIENT;
        }

        GS_LOG_MSG(trace, "Executing houghCircles with mode = " + std::to_string(mode) + ", currentDP = " + std::to_string(kStrobedEnvironmentHoughDpParam1) +
            ", minDist = " + std::to_string(minDistance) + ", param1 = " + std::to_string(kStrobedEnvironmentBallCurrentParam1) +
            ", param2 = " + std::to_string(local_param2) + ", minRadius = " + std::to_string(int(kStrobedEnvironmentMinimumSearchRadius)) +
            ", maxRadius = " + std::to_string(int(kStrobedEnvironmentMaximumSearchRadius)));

        // TBD - May want to adjust min / max radius
        // NOTE - Param 1 may be sensitive as well - needs to be 100 for large pictures ?
        // TBD - Need to set minDist to rows / 8, roughly ?
        std::vector<gs::GsCircle> testCircles;
        cv::HoughCircles(final_search_image,
            testCircles,
            mode,
            kStrobedEnvironmentHoughDpParam1,
            /* minDist = */ minDistance, // Does this really matter if we are only looking for one circle ?
            /* param1 = */ kStrobedEnvironmentBallCurrentParam1,
            /* param2 = */ local_param2,
            /* minRadius = */ int(kStrobedEnvironmentMinimumSearchRadius),
            /* maxRadius = */ int(kStrobedEnvironmentMaximumSearchRadius));

        GS_LOG_MSG(trace, "Identified " + std::to_string(testCircles.size()) + " circles.");

        dest = src_f.clone();


        int MAX_CIRCLES_TO_EVALUATE = 100;
        int kMaxCirclesToEmphasize = 3;
        int i = 0;
        int largest_index = -1;
        double largest_radius = -1.0;

        for (auto& c : testCircles) {

            i += 1;

            if (i > MAX_CIRCLES_TO_EVALUATE) {
                break;
            }

            double found_radius = c[2];

            if (found_radius > largest_radius) {
                largest_radius = found_radius;
                largest_index = i - 1;
            }

            GS_LOG_MSG(trace, "Circle radius: " + std::to_string(found_radius));

            if (i <= 20) {
                gs::LoggingTools::DrawCircleOutlineAndCenter(dest, c, std::to_string(i), i, (i > kMaxCirclesToEmphasize));
            }

        }

        /*
        if (largest_index >= 0) {
            gs::LoggingTools::DrawCircleOutlineAndCenter(dest, testCircles[largest_index], std::to_string(largest_index), largest_index, true);
        }
        */

        // LoggingTools::DebugShowImage("Hough-only-identified Circles{", dest);

    cv::imshow("Result", dest);
    sizeWindow("Result", dest);

    // TBD - ONLY FOR PLACED BALL TESTING WHERE THERE IS ONE BALL
    if (false && testCircles.size() == 1) {


        int r1 = (int)std::round(testCircles[0][2] * 1.5);
        int rInc = (long)(r1 - testCircles[0][2]);
        // Don't assume the ball is well within the larger picture

        int x1 = testCircles[0][0] - r1;
        int y1 = testCircles[0][1] - r1;
        int x_width = 2 * r1;
        int y_height = 2 * r1;

        // Ensure the isolated image is entirely in the larger image
        x1 = std::max(0, x1);
        y1 = std::max(0, y1);

        if (x1 + x_width >= final_search_image.cols) {
            x1 = final_search_image.cols - x_width - 1;
        }
        if (y1 + y_height >= final_search_image.rows) {
            y1 = final_search_image.rows - y_height - 1;
        }

        cv::Rect ballRect{ x1, y1, x_width, y_height };


        cv::Point offset_sub_to_full;
        cv::Point offset_full_to_sub;
        cv::Mat ball_image = gs::CvUtils::GetSubImage(dest, ballRect, offset_sub_to_full, offset_full_to_sub);


        cv::namedWindow("ball_image", cv::WINDOW_KEEPRATIO);
        cv::imshow("ball_image", ball_image);
    }
    }



    int main(int argc, char** argv)
    {
        gs::LoggingTools::InitLogging();

        BOOST_LOG_FUNCTION();


        // const std::string kBaseTestDir = "D:/GolfSim/C++Code/UneekorComparePlayground/CircleIDPlayground/Images/";
        const std::string kBaseTestDir = "M:/Dev/PiTrac/Software/LMSourceCode/Images/";


        const std::string kTestImageFileName = kBaseTestDir + "log_cam2_last_strobed_img_232_fast.png";  // "log_cam2_last_strobed_img_17_2024-Jun-24_13.17.33.png";

        src_f = cv::imread(kTestImageFileName, cv::IMREAD_COLOR);

        cv::namedWindow(processWindowName, 1);
        cv::resizeWindow(processWindowName, 700, 400);
        
        // OpenCV trackbars have some weird limitation where they truncate the bar label to 10 characters
        // We'll just shorten ours for now
        cv::createTrackbar("CanLower", processWindowName, &kStrobedEnvironmentCannyLower, 100, Process);
        cv::createTrackbar("CanUpper", processWindowName, &kStrobedEnvironmentCannyUpper, 300, Process);

        cv::createTrackbar("B4CanBlrSz", processWindowName, &kStrobedEnvironmentPreCannyBlurSizeInt, 20, Process);
        cv::createTrackbar("B4HghBlrSz", processWindowName, &kStrobedEnvironmentPreHoughBlurSizeInt, 20, Process);

        cv::createTrackbar("CurrParam1", processWindowName, &kStrobedEnvironmentBallCurrentParam1Int, 400, Process);
        cv::createTrackbar("DpParam1", processWindowName, &kStrobedEnvironmentHoughDpParam1Int, 20, Process);
        cv::createTrackbar("HghParam2", processWindowName, &kStrobedEnvironmentBallStartingParam2Int, 200, Process);
        cv::createTrackbar("MinRadius", processWindowName, &kStrobedEnvironmentMinimumSearchRadiusInt, 100, Process);
        cv::createTrackbar("MaxRadius", processWindowName, &kStrobedEnvironmentMaximumSearchRadiusInt, 120, Process);
        cv::createTrackbar("CircDist", processWindowName, &min_hough_circle_distance, 30, Process);


        process_window_ready = true;

        Process(0, NULL);

        cv::waitKey(0);
        return 0;

        gs::LoggingTools::Debug("Tests Complete");
    }

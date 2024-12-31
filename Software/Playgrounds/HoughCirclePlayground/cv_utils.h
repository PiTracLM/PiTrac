/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <string_view>
#include <algorithm>
#include <format>

#include <NumCpp.hpp>
#include <NumCpp/Core/Types.hpp>
#include <NumCpp/NdArray.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include "gs_globals.h"
#include "logging_tools.h"
#include "colorsys.h"

namespace golf_sim {

struct CvUtils
{
    const static std::byte kOpenCvHueMax{ 180 };  // Apparently because 360 degrees is more than can fit in an 8 - bit uchar
	const static std::byte kOpenCvSatMax{ 255 };
    const static std::byte kOpenCvValMax{ 255 };

    static int CircleRadius(const GsCircle& circle);

    static cv::Vec2i CircleXY(const GsCircle& circle);
    static int CircleX(const GsCircle& circle);
    static int CircleY(const GsCircle& circle);

    static cv::Vec2i CvSize(const cv::Mat& img);

    static int CvHeight(const cv::Mat& img);

    static int CvWidth(const cv::Mat& img);
    
    static double inline DegreesToRadians(double deg) { return ((deg / 180.0) * CV_PI); };
    static double inline RadiansToDegrees(double deg) { return ((deg / CV_PI) * 180.0); };

    // Note that the rgb value will be stored in openCV format - i.e., as bgr
    static GsColorTriplet ConvertRgbToHsv(const GsColorTriplet& rgb);

    // Note that the hsv value will be stored in openCV format 
    static GsColorTriplet ConvertHsvToRgb(const GsColorTriplet& hsv);

    static float ColorDistance(const GsColorTriplet& rgb1, const GsColorTriplet& rgb2);

    // The ball color will be an average of the colors near the middle of the determined ball
    // The returned color is in RGB form
    static std::vector<GsColorTriplet> GetBallColorRgb(const cv::Mat &img, const GsCircle &circle);
    
    static cv::Mat GetAreaMaskImage(int resolutionX, int resolutionY, int expected_ball_X, int expected_ball_Y, int mask_radius, cv::Rect &mask_dimensions, bool use_square = false);

    // Must be a 2D image with 3 channels of uints
    static cv::Mat ConvertNDArrayToMat(const nc::NdArray<nc::uint8>& ndarr);
 
    static double MetersToFeet(double m);
    static double MetersToInches(double m);
    static double InchesToMeters(double i);

    // Size the result image to the size of the image_to_size
    static void SetMatSize(const cv::Mat& image_to_size, cv::Mat & result_image);

    static void BrightnessAndContrastAutoAlgo1(const cv::Mat& src, cv::Mat& dst, float clip_hist_percent = 0);

    static void BrightnessAndContrastAutoAlgo2(const cv::Mat& bgr_image, cv::Mat& dst);

    static void DrawGrayImgHistogram(const cv::Mat& img, const bool ignore_zeros = false);

    // Note - if the ball_ROI_rect rectangle is invalid, it will be corrected.
    static cv::Mat GetSubImage(const cv::Mat& full_image, cv::Rect& ball_ROI_rect, cv::Point& offset_sub_to_full, cv::Point& offset_full_to_sub);

    static bool IsUprightRect(float theta);
};

}

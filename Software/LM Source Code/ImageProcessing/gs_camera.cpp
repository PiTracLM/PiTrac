/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include <algorithm>

#include "gs_options.h"
#include "ball_image_proc.h"
#include "pulse_strobe.h"
#include "gs_ui_system.h"
#include "gs_config.h"
#include "gs_clubs.h"

#include "gs_camera.h"


namespace golf_sim {

    // Constants used by this class
    bool GolfSimCamera::kLogIntermediateExposureImagesToFile = false;
    bool GolfSimCamera::kShowDebugImagesDuringExposureSelection = true;
    bool GolfSimCamera::kLogWebserverImagesToFile = true;
    bool GolfSimCamera::kLogDiagnosticImagesToUniqueFiles = false;
    
    int GolfSimCamera::kMaximumOffTrajectoryDistance = 5;
    unsigned int GolfSimCamera::kNumberHighQualityBallsToRetain = 2;
    double GolfSimCamera::kMaxStrobedBallColorDifferenceRelaxed = 35000.;
    double GolfSimCamera::kMaxPuttingBallColorDifferenceRelaxed = 35000.;
    double GolfSimCamera::kMaxStrobedBallColorDifferenceStrict = 15000.;
    double GolfSimCamera::kBallProximityMarginPercentRelaxed = 50.;
    double GolfSimCamera::kBallProximityMarginPercentStrict = 5.;

    // These constants may be used before this class's constructor is called.
    // For that reason, they are intialized in the GsConfiguration startup
    cv::Vec3d GolfSimCamera::kCamera1PositionsFromOriginMeters;
    cv::Vec3d GolfSimCamera::kCamera2PositionsFromOriginMeters;
    cv::Vec3d GolfSimCamera::kCamera2OffsetFromCamera1OriginMeters;

    double GolfSimCamera::kColorDifferenceRgbPostMultiplierForDarker = 5.0;
    double GolfSimCamera::kColorDifferenceRgbPostMultiplierForLighter = 10.0;
    double GolfSimCamera::kColorDifferenceStdPostMultiplierForDarker = 3.0;
    double GolfSimCamera::kColorDifferenceStdPostMultiplierForLighter = 2.0;

    double GolfSimCamera::kMaxDistanceFromTrajectory = 20.;

    int GolfSimCamera::kClosestBallPairEdgeBackoffPixels = 200;

    double GolfSimCamera::kMaxIntermediateBallRadiusChangePercent = 10.0;
    double GolfSimCamera::kMaxPuttingIntermediateBallRadiusChangePercent = 10.0;
    double GolfSimCamera::kMaxOverlappedBallRadiusChangeRatio = 1.3;
    double GolfSimCamera::kMaxRadiusDifferencePercentageFromBest = 20;

    bool GolfSimCamera::kUsePreImageSubtraction = false;  // Ultimately, this concept was not as helpful as hoped for
    double GolfSimCamera::kPreImageWeightingOverall = 1.0;
    double GolfSimCamera::kPreImageWeightingBlue = 1.0;
    double GolfSimCamera::kPreImageWeightingGreen = 1.0;
    double GolfSimCamera::kPreImageWeightingRed = 1.0;

    float GolfSimCamera::kBallAreaMaskRadiusRatio = 5.0f;
    double GolfSimCamera::kCamera1CalibrationDistanceToBall = 0.5;
    double GolfSimCamera::kCamera2CalibrationDistanceToBall = 0.5;

    double GolfSimCamera::kCamera1XOffsetForTilt = 0.0;
    double GolfSimCamera::kCamera1YOffsetForTilt = 0.0;
    double GolfSimCamera::kCamera2XOffsetForTilt = 0.0;
    double GolfSimCamera::kCamera2YOffsetForTilt = 0.0;

    double GolfSimCamera::kExpectedBallPositionXcm = -50.0;
    double GolfSimCamera::kExpectedBallPositionYcm = -28.0;
    double GolfSimCamera::kExpectedBallPositionZcm = 50.0;
    double GolfSimCamera::kExpectedBallRadiusPixelsAt40cm = 50;
    float GolfSimCamera::kMaxMovedBallRadiusRatio = 1.40f;
    float GolfSimCamera::kMinMovedBallRadiusRatio = 0.50f;
    double GolfSimCamera::kMinRadiusRatio = 0.7;
    double GolfSimCamera::kMaxRadiusRatio = 1.2;

    double GolfSimCamera::kUnlikelyAngleMinimumDistancePixels = 40;
    double GolfSimCamera::kMaxQualityExposureLaunchAngle = 45.0;
    double GolfSimCamera::kMinQualityExposureLaunchAngle = -5.0;
    double GolfSimCamera::kMaxPuttingQualityExposureLaunchAngle = +10.0;
    double GolfSimCamera::kMinPuttingQualityExposureLaunchAngle = -10.0;
    double GolfSimCamera::kNumberAngleCheckExposures = 3;

    double GolfSimCamera::kStandardBallSpeedSlowdownPercentage = 0.5;
    double GolfSimCamera::kPracticeBallSpeedSlowdownPercentage = 2.0;
    double GolfSimCamera::kPuttingBallSpeedSlowdownPercentage = 5.0;
    bool GolfSimCamera::kCameraRequiresFlushPulse = true;

    double GolfSimCamera::kMaxBallsToRetain = 18;

    bool GolfSimCamera::kExternallyStrobedEnvFilterImage = true;
    int GolfSimCamera::kExternallyStrobedEnvBottomIgnoreHeight = 70;
    int GolfSimCamera::kExternallyStrobedEnvFilterHsvLowerH = 14;
    int GolfSimCamera::kExternallyStrobedEnvFilterHsvUpperH = 48;
    int GolfSimCamera::kExternallyStrobedEnvFilterHsvLowerS = 26;
    int GolfSimCamera::kExternallyStrobedEnvFilterHsvUpperS = 255;
    int GolfSimCamera::kExternallyStrobedEnvFilterHsvLowerV = 114;
    int GolfSimCamera::kExternallyStrobedEnvFilterHsvUpperV = 255;
    int GolfSimCamera::kExternallyStrobedEnvCannyLower = 156;
    int GolfSimCamera::kExternallyStrobedEnvCannyUpper = 337;
    int GolfSimCamera::kExternallyStrobedEnvPreHoughBlurSize = 13;  // 6 for External Strobe, 9 for normal
    int GolfSimCamera::kExternallyStrobedEnvPreCannyBlurSize = 3;
    
    int GolfSimCamera::kExternallyStrobedEnvHoughLineIntersections = 235;
    int GolfSimCamera::kExternallyStrobedEnvLinesAngleLower = 140;
    int GolfSimCamera::kExternallyStrobedEnvLinesAngleUpper = 180;
    int GolfSimCamera::kExternallyStrobedEnvMaximumHoughLineGap = 7;
    int GolfSimCamera::kExternallyStrobedEnvMinimumHoughLineLength = 23;

    bool GolfSimCamera::kPlacedBallUseLargestBall = true;

    BallImageProc* get_image_processor() {
        BallImageProc* ip = new BallImageProc;

        return ip;
    }


    GolfSimCamera::GolfSimCamera() {
        GS_LOG_TRACE_MSG(trace, "GolfSimCamera reading constants from JSON file.");
        // The following constants are only used internal to the GolfSimCamera class, and so can be initialized in the constructor
        GolfSimConfiguration::SetConstant("gs_config.logging.kLogIntermediateExposureImagesToFile", kLogIntermediateExposureImagesToFile);
        GolfSimConfiguration::SetConstant("gs_config.logging.kShowDebugImagesDuringExposureSelection", kShowDebugImagesDuringExposureSelection);
        GolfSimConfiguration::SetConstant("gs_config.logging.kLogWebserverImagesToFile", kLogWebserverImagesToFile);
        GolfSimConfiguration::SetConstant("gs_config.logging.kLogDiagnosticImagesToUniqueFiles", kLogDiagnosticImagesToUniqueFiles);


        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaximumOffTrajectoryDistance", kMaximumOffTrajectoryDistance);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kNumberHighQualityBallsToRetain", kNumberHighQualityBallsToRetain);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxStrobedBallColorDifferenceStrict", kMaxStrobedBallColorDifferenceStrict);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxStrobedBallColorDifferenceRelaxed", kMaxStrobedBallColorDifferenceRelaxed);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kBallProximityMarginPercentRelaxed", kBallProximityMarginPercentRelaxed);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxPuttingBallColorDifferenceRelaxed", kMaxPuttingBallColorDifferenceRelaxed);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kBallProximityMarginPercentStrict", kBallProximityMarginPercentStrict);

        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kColorDifferenceRgbPostMultiplierForDarker", kColorDifferenceRgbPostMultiplierForDarker);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kColorDifferenceRgbPostMultiplierForLighter", kColorDifferenceRgbPostMultiplierForLighter);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kColorDifferenceStdPostMultiplierForDarker", kColorDifferenceStdPostMultiplierForDarker);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kColorDifferenceStdPostMultiplierForLighter", kColorDifferenceStdPostMultiplierForLighter);

        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxDistanceFromTrajectory", kMaxDistanceFromTrajectory);

        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kClosestBallPairEdgeBackoffPixels", kClosestBallPairEdgeBackoffPixels);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxBallsToRetain", kMaxBallsToRetain);
        
        GolfSimConfiguration::SetConstant("gs_config.strobing.kStandardBallSpeedSlowdownPercentage", kStandardBallSpeedSlowdownPercentage);
        GolfSimConfiguration::SetConstant("gs_config.strobing.kPracticeBallSpeedSlowdownPercentage", kPracticeBallSpeedSlowdownPercentage);
        GolfSimConfiguration::SetConstant("gs_config.strobing.kPuttingBallSpeedSlowdownPercentage", kPuttingBallSpeedSlowdownPercentage);
        GolfSimConfiguration::SetConstant("gs_config.strobing.kCameraRequiresFlushPulse", kCameraRequiresFlushPulse);
        

        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxIntermediateBallRadiusChangePercent", kMaxIntermediateBallRadiusChangePercent);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxPuttingIntermediateBallRadiusChangePercent", kMaxPuttingIntermediateBallRadiusChangePercent);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxOverlappedBallRadiusChangeRatio", kMaxOverlappedBallRadiusChangeRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxRadiusDifferencePercentageFromBest", kMaxRadiusDifferencePercentageFromBest);
        
        GolfSimConfiguration::SetConstant("gs_config.calibration.kCamera1CalibrationDistanceToBall", kCamera1CalibrationDistanceToBall);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kCamera2CalibrationDistanceToBall", kCamera2CalibrationDistanceToBall);

        GolfSimConfiguration::SetConstant("gs_config.ball_position.kBallAreaMaskRadiusRatio", kBallAreaMaskRadiusRatio);
        
        GolfSimConfiguration::SetConstant("gs_config.cameras.kCamera1XOffsetForTilt", kCamera1XOffsetForTilt);
        GolfSimConfiguration::SetConstant("gs_config.cameras.kCamera1YOffsetForTilt", kCamera1YOffsetForTilt);
        GolfSimConfiguration::SetConstant("gs_config.cameras.kCamera2XOffsetForTilt", kCamera2XOffsetForTilt);
        GolfSimConfiguration::SetConstant("gs_config.cameras.kCamera2YOffsetForTilt", kCamera2YOffsetForTilt);

        GolfSimConfiguration::SetConstant("gs_config.ball_position.kExpectedBallPositionXcm", kExpectedBallPositionXcm);
        GolfSimConfiguration::SetConstant("gs_config.ball_position.kExpectedBallPositionYcm", kExpectedBallPositionYcm);
        GolfSimConfiguration::SetConstant("gs_config.ball_position.kExpectedBallPositionZcm", kExpectedBallPositionZcm);
        GolfSimConfiguration::SetConstant("gs_config.ball_position.kExpectedBallRadiusPixelsAt40cm", kExpectedBallRadiusPixelsAt40cm);
        GolfSimConfiguration::SetConstant("gs_config.ball_position.kMaxMovedBallRadiusRatio", kMaxMovedBallRadiusRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_position.kMinMovedBallRadiusRatio", kMinMovedBallRadiusRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_position.kMinRadiusRatio", kMinRadiusRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_position.kMaxRadiusRatio", kMaxRadiusRatio);

        
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kUnlikelyAngleMinimumDistancePixels", kUnlikelyAngleMinimumDistancePixels);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxQualityExposureLaunchAngle", kMaxQualityExposureLaunchAngle);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMinQualityExposureLaunchAngle", kMinQualityExposureLaunchAngle);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMaxPuttingQualityExposureLaunchAngle", kMaxPuttingQualityExposureLaunchAngle);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kMinPuttingQualityExposureLaunchAngle", kMinPuttingQualityExposureLaunchAngle);
        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kNumberAngleCheckExposures", kNumberAngleCheckExposures);

        GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kUsePreImageSubtraction", kUsePreImageSubtraction);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvFilterImage", kExternallyStrobedEnvFilterImage);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvBottomIgnoreHeight", kExternallyStrobedEnvBottomIgnoreHeight);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvFilterHsvLowerH", kExternallyStrobedEnvFilterHsvLowerH);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvFilterHsvUpperH", kExternallyStrobedEnvFilterHsvUpperH);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvFilterHsvLowerS", kExternallyStrobedEnvFilterHsvLowerS);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvFilterHsvUpperS", kExternallyStrobedEnvFilterHsvUpperS);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvFilterHsvLowerV", kExternallyStrobedEnvFilterHsvLowerV);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvFilterHsvUpperV", kExternallyStrobedEnvFilterHsvUpperV);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvCannyLower", kExternallyStrobedEnvCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvCannyUpper", kExternallyStrobedEnvCannyUpper);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvPreHoughBlurSize", kExternallyStrobedEnvPreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvPreCannyBlurSize", kExternallyStrobedEnvPreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvHoughLineIntersections", kExternallyStrobedEnvHoughLineIntersections);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvLinesAngleLower", kExternallyStrobedEnvLinesAngleLower);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvLinesAngleUpper", kExternallyStrobedEnvLinesAngleUpper);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMaximumHoughLineGap", kExternallyStrobedEnvMaximumHoughLineGap);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMinimumHoughLineLength", kExternallyStrobedEnvMinimumHoughLineLength);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallUseLargestBall", kPlacedBallUseLargestBall);

    }

    GolfSimCamera::~GolfSimCamera() {
    }

    int GolfSimCamera::getExpectedBallRadiusPixels(const int resolution_x_, const double distance) {

        double radius = kExpectedBallRadiusPixelsAt40cm;

        GS_LOG_TRACE_MSG(trace, "getExpectedBallRadiusPixels called with resolution: " + std::to_string(resolution_x_) + 
                        ", distance: " + std::to_string(distance) + ", and with expected radius at 40cm of: " + std::to_string(radius));

        if (std::abs(distance) < 0.0001) {
            GS_LOG_MSG(error, "getExpectedBallRadiusPixels called with 0 distance.");
            return 0;
        }

        radius *= (0.40 / distance);

        // Scale to our current resolution
        radius *= ((double)resolution_x_ / 1456.0);

        GS_LOG_TRACE_MSG(trace, "getExpectedBallRadiusPixels returning: " + std::to_string(std::round(radius)));

        return (int)std::round(radius);
    }


    std::vector<GsColorTriplet> GolfSimCamera::GetBallHSVRange(const GsColorTriplet& ball_color_RGB) {

        // Create an HSV range around the average color that will be broad enough to encompass
        // any expected HSV values of pixes in the golf ball as it moves through the frame
        //hsv = cs.rgb_to_hsv(ball_color_[2], ball_color_[1], ball_color_[0])
        GsColorTriplet hsvAvg = CvUtils::ConvertRgbToHsv(ball_color_RGB);

        //GS_LOG_TRACE_MSG(trace, "GetBallHSVRange COLORSYS computed HSV of: " + std::to_string(hsvAvg))

        // Note that the consumer of this function will need to loop around the 360 degree circle if the widened hue numbers
        // go belore zero
        float hmin = (float)hsvAvg[0] - H_MIN_CAL_COLOR_WIDENING_AMOUNT;
        float hmax = (float)hsvAvg[0] + H_MAX_CAL_COLOR_WIDENING_AMOUNT;
        float smin = (float)std::max(0, (int)hsvAvg[1] - (int)(S_MIN_CAL_COLOR_WIDENING_AMOUNT));
        float vmin = (float)std::max(0, (int)hsvAvg[2] - (int)(V_MIN_CAL_COLOR_WIDENING_AMOUNT * 0.9));

        float smax = (float)std::min((int)CvUtils::kOpenCvSatMax, (int)hsvAvg[1] + S_MAX_CAL_COLOR_WIDENING_AMOUNT);
        float vmax = (float)std::min((int)CvUtils::kOpenCvValMax, (int)hsvAvg[2] + V_MAX_CAL_COLOR_WIDENING_AMOUNT);

        GsColorTriplet hsvMin{ hmin, smin, vmin };
        GsColorTriplet hsvMax{ hmax, smax, vmax };

        std::vector<GsColorTriplet> r;
        r.push_back(hsvMin);
        r.push_back(hsvMax);

        GS_LOG_TRACE_MSG(trace, "GetBallHSVRange for average (" + LoggingTools::FormatGsColorTriplet(hsvAvg) + ") = "
            + LoggingTools::FormatGsColorTriplet(hsvMin) + " | " + LoggingTools::FormatGsColorTriplet(hsvMax));

        return r;
    }

    // Expects a single ball to be placed near the expectedBallCenter at a certain
    // distance from the camera.
    // Returns true iff the input ball was successfully calibrated
    bool GolfSimCamera::GetCalibratedBall(const GolfSimCamera& camera, 
                                          const cv::Mat& rgbImg,
                                          GolfBall& b,
                                          const cv::Vec2i& expectedBallCenter,
                                          const bool expectBall) {

        GS_LOG_TRACE_MSG(trace, "GetCalibratedBall");

        BallImageProc* ip = get_image_processor();
        ip->image_name_ = "Calibration Photo";

        if (rgbImg.empty()) {
            GS_LOG_MSG(error, "GetCalibratedBall received an empty photo.");
            return false;
        }

        if (!camera_.cameraInitialized) {
            GS_LOG_MSG(error, "Camera hardware not initialized in GetCalibratedBall!");
            return false;
        }

        // Make sure the image we got is the dimensions that we are expecting
        // TBD - NEED TO REFACTOR SO THAT RESOLUTION IS NOT COMING FROM THE CAMERA!
        if (rgbImg.rows != camera_.resolution_y_ || rgbImg.cols != camera_.resolution_x_) {
            GS_LOG_MSG(error, "Returned photo does not match camera resolution!");
            return false;
        }


        LoggingTools::DebugShowImage("Calibration Photo", rgbImg);

        // We expect the ball to be near the origin, so will assume that distance
        double expected_distance = 0;

        if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera1) {
            expected_distance = CvUtils::GetDistance(kCamera1PositionsFromOriginMeters);
        }
        else {
            expected_distance = CvUtils::GetDistance(kCamera2PositionsFromOriginMeters);
            GS_LOG_TRACE_MSG(trace, "GetCalibratedBall called for camera2 (usually used for camera1 images).");
        }

        // If we are calibrating the focal length, then override the distance to that specified in 
        // the JSON file
        if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1Calibrate ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2Calibrate ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1BallLocation ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2BallLocation ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1TestStandalone ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2TestStandalone ) {

            if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera1) {
                expected_distance = kCamera1CalibrationDistanceToBall;
            }
            else {
                expected_distance = kCamera2CalibrationDistanceToBall;
            }
            GS_LOG_TRACE_MSG(trace, "GetCalibratedBall overriding expected_distance.  Setting to: " + std::to_string(expected_distance));
        }
        
        GS_LOG_TRACE_MSG(trace, "GetCalibratedBall using expected ball distance of: " + std::to_string(expected_distance));

        // TBD - Will want to be able to vary the calibration distance later
        double expectedRadius = getExpectedBallRadiusPixels(rgbImg.cols, expected_distance);
        int min = int(expectedRadius * kMinRadiusRatio);
        int max = int(expectedRadius * kMaxRadiusRatio);

        ip->min_ball_radius_ = min;
        ip->max_ball_radius_ = max;
        GS_LOG_TRACE_MSG(trace, "Looking for a ball with min/max radius (pixels) of: " + std::to_string(min) + ", " + std::to_string(max));

        // Expect the ball in the center of the image if not otherwise specified
        int expected_ball_X = expectedBallCenter[0];
        int expected_ball_Y = expectedBallCenter[1];

        cv::Rect roi;

        if (expected_ball_X == 0 && expected_ball_Y == 0) {
            expected_ball_X = int(rgbImg.cols / 2);
            expected_ball_Y = int(rgbImg.rows / 2);

            // Limit the search area if we know where it should be
            double search_area_radius = max * 1.1;  // add a little buffer to make sure we can find things at the edge
            roi = cv::Rect{ (int)(expected_ball_X - search_area_radius), (int)(expected_ball_Y - search_area_radius),
                           (int)(2. * search_area_radius), (int)(2. * search_area_radius) };
        }

        int mask_radius = int(expectedRadius * kBallAreaMaskRadiusRatio);

        ip->area_mask_image_ = CvUtils::GetAreaMaskImage(rgbImg.cols, rgbImg.rows, expected_ball_X, expected_ball_Y, mask_radius, roi);
        LoggingTools::DebugShowImage("AreaMaskImage Photo", ip->area_mask_image_);

        // The whole point here is that we don't know the color until we calibrate, so force a 
        // very broad color range mask
        // NOTE - the entire color thing is pretty much deprecated now
        ip->ball_.ball_color_ = GolfBall::BallColor::kUnknown;

        // This is more useful when using the Hough Circle search technique.
        // Lately has been producing the wrong results
        bool useLargestFoundBall = false;

        std::vector<GolfBall> return_balls;
        bool result = ip->GetBall(rgbImg, b, return_balls, roi, BallImageProc::BallSearchMode::kFindPlacedBall, useLargestFoundBall, expectBall);

        if (!result || return_balls.empty()) {
            if (expectBall) {
                GS_LOG_MSG(error, "GetBall() failed to get a ball.");
            }

            // Pass the information about where the system searched for the ball
            // So that the caller can (potentially) indicate where it was
            // supposed to be (given that it was not found).
            b.search_area_center_[0] = expected_ball_X;
            b.search_area_center_[1] = expected_ball_Y;
            b.search_area_radius_ = mask_radius;

            return false;
        }

        GS_LOG_MSG(trace, "GetBall() returned " + std::to_string(return_balls.size()) + " ball(s).");
        ShowAndLogBalls("GetBallReturnedBalls", rgbImg, return_balls, kLogIntermediateExposureImagesToFile);


        // Assign the returned ball's information to the ball this function will return
        if (kPlacedBallUseLargestBall) {

            int largest_index = 0;
            double largest_radius = -1.0;

            for (size_t i = 0; i < return_balls.size(); i++) {

                double found_radius = return_balls[i].measured_radius_pixels_;

                if (found_radius > largest_radius) {
                    largest_radius = found_radius;
                    largest_index = (int)i;
                }
            }

            b = return_balls[largest_index];
        }
        else {
            b = return_balls.front();
        }

        std::vector<GolfBall> final_ball{ (b) };
        GS_LOG_TRACE_MSG(trace, "GetBallReturnedBalls Final Ball (calibrated):" + b.Format());
        ShowAndLogBalls("GetBallReturnedBalls Final Ball:", rgbImg, final_ball, kLogIntermediateExposureImagesToFile);




        // TBD- Put the rest of this method into a different method
        /*
        if (!SetCalibrationInformation(b, )) {
            GS_LOG_MSG(error, "Failed to SetCalibrationInformation.");
            return false;
        }
        */

        // We were able to discern a circle that the system thinks is a ball - return the ball with the information corresponding to it inside

        // Setup a ball to return with all the pertinent information
        b.measured_radius_pixels_ = b.ball_circle_[2];

        // We might also try to force the user to put the ball at a specific distance from the camera and calibrate from that
        // Note that the distance being calculated here is not the precise Z distance, but instead it is the distance
        // directly to the ball, as the crow flies.

        double distance_to_ball = -1.0;

        if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1Calibrate ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2Calibrate) {

            if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera1) {
                distance_to_ball = kCamera1CalibrationDistanceToBall;
            }
            else {
                distance_to_ball = kCamera2CalibrationDistanceToBall;
            }
            
            b.distance_to_z_plane_from_lens_ = distance_to_ball;

            // Note - since we are measuring the ball using the standard focal length of the camera hardware,
            // when we compute the length again here, it should be the same.  We'd use this if we have the ball 
            // at a known, precise distance and then get the focal length that makes the system find that distance.
            b.calibrated_focal_length_ = computeFocalDistanceFromBallData(camera, b.measured_radius_pixels_, distance_to_ball);
            GS_LOG_MSG(info, "Calibrated focal length for distance " + std::to_string(distance_to_ball) + " and Radius: " + std::to_string(b.measured_radius_pixels_) +
                        " mm is " + std::to_string(b.calibrated_focal_length_) + ".");

            return true;
        }
        else {
            distance_to_ball = ComputeDistanceToBallUsingRadius(camera, b);
            b.distance_to_z_plane_from_lens_ = distance_to_ball;
        }

        // Make sure all the related ball elements are set consistently
        b.set_circle(b.ball_circle_);

        cv::Vec2d camera_angles = camera.camera_.camera_angles_;

        // The golf ball may not be centered in the frame of the camera_.  Determine the angle at which
        // the ball sits so that it can be taken into account for, e.g., ball rotation perspectives

        if (!ComputeXyzDistanceFromOrthoCamPerspective(camera, b, b.distances_ortho_camera_perspective_)) {
            GS_LOG_MSG(error, "Could not calculate ComputeXyzDistanceFromOrthoCamPerspective");
            return false;
        }
        if (!ComputeBallXYAnglesFromCameraPerspective(b.distances_ortho_camera_perspective_, b.angles_camera_ortho_perspective_)) {
            GS_LOG_MSG(error, "Could not calculate ComputeBallXYAnglesFromCameraPerspective");
            return false;
        }

        GetBallColorInformation(rgbImg, b);

        b.distance_at_calibration_ = distance_to_ball;
        // The measured radius may change later, so save the current one now
        b.radius_at_calibration_pixels_ = (float)b.measured_radius_pixels_;

        b.calibrated = true;

        GS_LOG_TRACE_MSG(trace, "Calibrated Ball Results: " + b.Format());

        return true;
    }


    void GolfSimCamera::GetBallColorInformation(const cv::Mat& color_image, GolfBall& b) {
        std::vector<GsColorTriplet> stats = CvUtils::GetBallColorRgb(color_image, b.ball_circle_);
        b.average_color_ = stats[0];
        b.median_color_ = stats[1];
        b.std_color_ = stats[2];
        b.ball_color_ = GolfBall::BallColor::kCalibrated;

        std::vector<GsColorTriplet> hsvRange = GetBallHSVRange(b.average_color_);
        b.ball_hsv_range_.min = hsvRange[0];
        b.ball_hsv_range_.max = hsvRange[1];
    }


    bool GolfSimCamera::ComputeBallDeltas(GolfBall& ball1, GolfBall& ball2, const GolfSimCamera& first_camera, const GolfSimCamera& second_camera) {

        if (!ComputeSingleBallXYZOrthoCamPerspective(first_camera, ball1)) {
            GS_LOG_MSG(error, "ComputeBallDeltas: Could not ComputeSingleBallXYZOrthoCamPerspective for ball1");
            return false;
        }

        // The second ball is assumed always to be related to camera 2, so use kCamera2Angles
        if (!ComputeSingleBallXYZOrthoCamPerspective(second_camera, ball2)) {
            GS_LOG_MSG(error, "ComputeBallDeltas: Could not ComputeSingleBallXYZOrthoCamPerspective for ball2");
            return false;
        }

        GS_LOG_TRACE_MSG(trace, "GolfSimCamera::ComputeBallDeltas - ball1 is:\n" + ball1.Format());
        GS_LOG_TRACE_MSG(trace, "GolfSimCamera::ComputeBallDeltas - ball2 is:\n" + ball2.Format());

        // At this point, we know the distances and angles of each ball relative to the camera
        // Next, find the delta differences in distance and angles as between the two balls

        if (!ComputeXyzDeltaDistances(ball1, ball2, ball2.position_deltas_ball_perspective_, ball2.distance_deltas_camera_perspective_)) {
            GS_LOG_MSG(error, "Could not calculate ComputeXyzDeltaDistances");
            return false;
        }


        // If the images were taken by different cameras at some distance from each other, we will account for that here
        // For example, if the second camera is to the right of the first (looking at the ball), then that right-direction
        // distance on the X axis should be added to the distance delta in the X-axis of the ball.

        if (first_camera.camera_.camera_number_ != second_camera.camera_.camera_number_) {  
            // The first camera is camera_1, so add the offset to camera_2
            ball2.distance_deltas_camera_perspective_ += kCamera2OffsetFromCamera1OriginMeters;
            ball2.position_deltas_ball_perspective_[0] += kCamera2OffsetFromCamera1OriginMeters[2];
            ball2.position_deltas_ball_perspective_[1] += kCamera2OffsetFromCamera1OriginMeters[1];
            ball2.position_deltas_ball_perspective_[2] += kCamera2OffsetFromCamera1OriginMeters[0];
        }

        if (!getXYDeltaAnglesBallPerspective(ball2.position_deltas_ball_perspective_, ball2.angles_ball_perspective_)) {
            GS_LOG_MSG(error, "Could not calculate getXYDeltaAnglesBallPerspective");
            return false;
        }


        GS_LOG_TRACE_MSG(trace, "Calculated X,Y angles (ball perspective) (in degrees) are: " + std::to_string(ball2.angles_ball_perspective_[0]) + ", " +
            std::to_string(ball2.angles_ball_perspective_[1]));

        GS_LOG_TRACE_MSG(trace, "Calculated DELTA X,Y, Z distances (ball perspective) are: " + std::to_string(ball2.position_deltas_ball_perspective_[0]) + ", " +
            std::to_string(ball2.position_deltas_ball_perspective_[1]) + ", " + std::to_string(ball2.position_deltas_ball_perspective_[2]));

        GS_LOG_TRACE_MSG(trace, "Calculated currentDistance is: " + std::to_string(ball2.distance_to_z_plane_from_lens_) + " meters = " +
            std::to_string(12*CvUtils::MetersToFeet(ball2.distance_to_z_plane_from_lens_)) + " inches from the lens.");

        return true;
    }


    // TBD - Make sure we know if the 
    bool GolfSimCamera::ComputeSingleBallXYZOrthoCamPerspective(const GolfSimCamera& camera, GolfBall& ball1 ) {

        ball1.distance_to_z_plane_from_lens_ = ComputeDistanceToBallUsingRadius(camera, ball1);

        // The golf ball may not be centered in the frame of the camera.  Determine the distances and angle at which
        // the ball sits so that it can be taken into account for, e.g., ball rotation perspectives
        if (!ComputeXyzDistanceFromOrthoCamPerspective(camera, ball1, ball1.distances_ortho_camera_perspective_)) {
            GS_LOG_MSG(error, "Could not calculate ComputeXyzDistanceFromOrthoCamPerspective");
            return false;
        }

        if (!ComputeBallXYAnglesFromCameraPerspective(ball1.distances_ortho_camera_perspective_, ball1.angles_camera_ortho_perspective_)) {
            GS_LOG_MSG(error, "Could not calculate ComputeBallXYAnglesFromCameraPerspective");
            return false;
        }


        /* DEBUG
        GS_LOG_TRACE_MSG(trace, "Calculated X,Y angles (camera perspective) (in degrees) are: " + std::to_string(angles_camera_perspective[0]) + ", " +
            std::to_string(angles_camera_perspective[1]));

        GS_LOG_TRACE_MSG(trace, "Calculated X,Y angles (ball perspective) (in degrees) are: " + std::to_string(angles_ball_perspective[0]) + ", " +
            std::to_string(angles_ball_perspective[1]));

        GS_LOG_TRACE_MSG(trace, "Calculated currentDistance is: " + std::to_string(ball1.distance_to_z_plane_from_lens_) + " meters = " +
            std::to_string(12*CvUtils::MetersToFeet(ball1.distance_to_z_plane_from_lens_)) + " inches from the lens.");
        */

        return true;
    }



        // returns a new golf ball object with the ball's current information
        // Returns true iff the new ball was successfully located
        // The first, calibrated ball is used to help find the "found" ball.
        // For example, the radius (and possibly color) of the first calibrated ball
        // is used to help narrow the image processing for the "found" ball.

        bool GolfSimCamera::GetCurrentBallLocation(const GolfSimCamera& camera, 
                                                   const cv::Mat& rgbImg,
                                                   const GolfBall& calibrated_ball, 
                                                   GolfBall& foundBall) {
                
            GS_LOG_TRACE_MSG(trace, "GetCurrentBallLocation(ball).  calibrated_ball = " + calibrated_ball.Format());

            if (!calibrated_ball.calibrated ) {

                GS_LOG_MSG(error, "GetCurrentBallLocation called without a properly calibrated ball.");
                return false;
            }

            // TBD - will copy work on a ball yet ?
            foundBall = calibrated_ball;

            BallImageProc *ip = get_image_processor();

            LoggingTools::DebugShowImage("GolfSimCamera::GetCurrentBallLocation input: ", rgbImg);

            // Find where the ball currently is based on where we expected it from the prior information

            double distance_at_calibration_ = calibrated_ball.distance_at_calibration_;

            double radius_at_calibration_pixels_ = calibrated_ball.radius_at_calibration_pixels_;
            ip->min_ball_radius_ = int(radius_at_calibration_pixels_ * kMinMovedBallRadiusRatio);
            ip->max_ball_radius_ = int(radius_at_calibration_pixels_ * kMaxMovedBallRadiusRatio);

            GS_LOG_TRACE_MSG(trace, "Original radius at calibration-time distance of " + std::to_string(distance_at_calibration_) + " was: " + std::to_string(radius_at_calibration_pixels_) + ". Looking for a ball with min/max radius (pixels) of: " +
                std::to_string(ip->min_ball_radius_) + ", " + std::to_string(ip->max_ball_radius_));

            // This is more useful when using the Hough Circle search technique
            bool useLargestFoundBall = true;

            cv::Rect emptyROI; 
            cv::Mat nullAreaMaskImage;
            ip->area_mask_image_ = nullAreaMaskImage;

            std::vector<GolfBall> return_balls;
            bool result = ip->GetBall(rgbImg, foundBall, return_balls, emptyROI, BallImageProc::BallSearchMode::kFindPlacedBall, useLargestFoundBall);

            if (!result || return_balls.empty()) {
                GS_LOG_MSG(error, "GetBall() failed to get a ball.");
                return false;
            }

            // Transfer the new ball information to the output ball
            foundBall = return_balls.front();

            if (!ComputeSingleBallXYZOrthoCamPerspective(camera, foundBall)) {
                GS_LOG_MSG(error, "GolfSimCamera::GetCurrentBallLocation failed to ComputeSingleBallXYZOrthoCamPerspective.");
                return false;
            }

            return true;
        }

        cv::Vec2d GolfSimCamera::computeCameraAnglesToBallPlane(cv::Vec3d& camera_positions_from_origin) {
            cv::Vec2d angles;

            double camera_height_above_ball = camera_positions_from_origin[1];
            double zDistanceToBall = camera_positions_from_origin[2];

            if (camera_height_above_ball < 0.0 || zDistanceToBall <= 0.0) {
                LoggingTools::Warning("GolfSimCamera::computeCameraAnglesToBallPlane called, but camera_height_above_ball_ or zDistanceToBall <= 0 (and likely not set)");
            }

            angles[1] = CvUtils::RadiansToDegrees(atan(camera_height_above_ball / zDistanceToBall));

            return angles;
        }



        bool GolfSimCamera::analyzeShotImages(  const GolfSimCamera& camera, 
                                                const cv::Mat& rgbImg1,
                                                const cv::Mat& rgbImg2,
                                                long timeDelayuS,
                                                const std::vector<cv::Vec3d>& camera_positions_from_origin,
                                                GolfBall& result_ball,
                                                const cv::Vec2i& expectedBallCenter) {

            if (timeDelayuS == 0) {
                GS_LOG_MSG(error, "In analyzeShotImages, timeDelayuS was 0.");
            }

            GolfBall ball1;

            // Get the ball data.  We will calibrate based on the first ball and then get the second one
            // using that calibrated data from the first ball.

            // The Vec2i is a hack to deal with the ball in the test images not being in the center (where it would be expected)
            bool success = GetCalibratedBall(camera, rgbImg1, ball1, expectedBallCenter);

            if (!success) {
                GS_LOG_TRACE_MSG(trace, "Could not find the first ball to calibrate!");
                return false;
            }

            GS_LOG_TRACE_MSG(trace, "Ball1 (calibrated):" + ball1.Format());

            if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1Calibrate ||
                GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2Calibrate) {

                ball1.calibrated_focal_length_ = computeFocalDistanceFromBallData(camera, ball1.measured_radius_pixels_, ball1.distance_to_z_plane_from_lens_);

                return true;
            }

            // Using the first ball for reference, determine where the second one is in relation to the first
            success = GetCurrentBallLocation(camera, rgbImg2, ball1, result_ball);

            if (!success) {
                GS_LOG_TRACE_MSG(trace, "Could not find 2nd ball");
                return false;
            }

            GS_LOG_TRACE_MSG(trace, "Ball2 :" + result_ball.Format());

            // Now figure out the rotational speeds
            cv::Mat grayImg1, grayImg2;
            cv::cvtColor(rgbImg1, grayImg1, cv::COLOR_BGR2GRAY);
            cv::cvtColor(rgbImg2, grayImg2, cv::COLOR_BGR2GRAY);

            cv::Vec3d rotationResults = BallImageProc::GetBallRotation(grayImg1, ball1, grayImg2, result_ball);

            CalculateBallSpinRates(result_ball, rotationResults, timeDelayuS);

            return true;
        }


        void GolfSimCamera::CalculateBallVelocity(GolfBall& b, long time_delay_us) {

            // TBD - streamline the math
            b.velocity_ = GetTotalDistance(b.position_deltas_ball_perspective_);
            b.velocity_ /= (double)(time_delay_us);
            b.velocity_ *= (double)1000000;
        }

        void GolfSimCamera::CalculateBallSpinRates(GolfBall& b, const cv::Vec3d& rotation_results, long time_delay_us){

            b.ball_rotation_angles_camera_ortho_perspective_ = rotation_results;
            b.rotation_speeds_RPM_[0] = 60. * ((rotation_results[0] / 360.) / (double)(time_delay_us)) * 1000000.;
            b.rotation_speeds_RPM_[1] = 60. * ((rotation_results[1] / 360.) / (double)(time_delay_us)) * 1000000.;
            b.rotation_speeds_RPM_[2] = 60. * ((rotation_results[2] / 360.) / (double)(time_delay_us)) * 1000000.;
        }


        double GolfSimCamera::GetTotalDistance(cv::Vec3d& distance_deltas) {
            return sqrt(pow(distance_deltas[0], 2) + pow(distance_deltas[1], 2) + pow(distance_deltas[2], 2));
        }

        // Determine the focal length based on a distance and measured radius (in pixels)
        double GolfSimCamera::computeFocalDistanceFromBallData(const GolfSimCamera& camera, double ball_radius_pixels, double ball_distance_meters) {
            
            // Could reasonably do this with either height or width parameters for the sensors that we are using,
            // but for at least the GS camera, the "correct" divisor is the X axis, not Y.
            return ( ball_distance_meters * camera.camera_.sensor_width_ * ((2.0 * ball_radius_pixels) / camera.camera_.resolution_x_ ) / (2.0*GolfBall::kBallRadiusMeters));
        }

        double GolfSimCamera::convertXDistanceToMeters(const GolfSimCamera& camera, double zDistanceMeters, double xDistancePixels) {
            // Note that we are NOT using the calibrated_focal_length
            double halfWidthMeters = (zDistanceMeters / camera.camera_.focal_length_) * (camera.camera_.sensor_width_ / 2.0);
            double xDistanceMeters = halfWidthMeters * (xDistancePixels / (camera.camera_.resolution_x_ / 2.0));
            return (xDistanceMeters);
        }

        double GolfSimCamera::convertYDistanceToMeters(const GolfSimCamera& camera, double zDistanceMeters, double yDistancePixels) {
            // Note that we are NOT using the calibrated_focal_length
            double halfHeightMeters = (zDistanceMeters / camera.camera_.focal_length_) * (camera.camera_.sensor_height_/2.0);
            double yDistanceMeters = halfHeightMeters * (yDistancePixels / (camera.camera_.resolution_y_ / 2.0));
            return (yDistanceMeters);
        }

        double GolfSimCamera::ComputeDistanceToBallUsingRadius(const GolfSimCamera& camera, const GolfBall& ball) {
            // compute and return the distance from the object to the camera in meters
            // (based on triangle similarity, e.g., https://pyimagesearch.com/2015/01/19/find-distance-camera-objectmarker-using-python-opencv/) is

            double chosenRadiusPixels;

            double effectiveSensorWidth;
            double effectiveCameraResolution;

            if (ball.ball_ellipse_.size.width > 0 && ball.ball_ellipse_.size.height > 0) {

                // TBD - Currently Deprecated
                

                // If we have ellipse representation, then use it - it's probably more reliable than a strict circle, but
                // we need to figure out WHICH radius of the ellipse to use
                
                // TBD -- Seems to work correctly only if we use the (smaller) width.
                // We could also use the largest radius in the hope that it will be more accurate, percentage-wise
                // This should presumably be the height
                chosenRadiusPixels = std::min(ball.ball_ellipse_.size.width, ball.ball_ellipse_.size.height) / 2.0;

                // The ellipse may be at an angle.  
                // Use the effective number of pixels on the sensor at that angle as a measuring stick to determine the real-world distance
                double ellipseAngleDegrees = ball.ball_ellipse_.angle;

                // From vertical
                double ellipseAngleAtSensorCorner = CvUtils::RadiansToDegrees(atan(camera.camera_.sensor_height_ / camera.camera_.sensor_width_));


                // See notebook for details of this algorithm
                // Note - see https://namkeenman.wordpress.com/2015/12/21/opencv-determine-orientation-of-ellipserotatedrect-in-fitellipse/
                // for details of rotatedRect angles.  They are non-intuitive.  Ellipse angle is to the right from vertical
                if ((ellipseAngleDegrees > ellipseAngleAtSensorCorner && ellipseAngleDegrees < 180 - ellipseAngleAtSensorCorner) ||
                    (ellipseAngleDegrees > (180 + ellipseAngleAtSensorCorner) && ellipseAngleDegrees < (360 - ellipseAngleAtSensorCorner))
                    ) {
                    effectiveSensorWidth = std::abs(camera.camera_.sensor_width_ / cos(CvUtils::DegreesToRadians(90 - ellipseAngleDegrees)));
                    effectiveCameraResolution = std::abs(camera.camera_.resolution_x_ / cos(CvUtils::DegreesToRadians(90 - ellipseAngleDegrees)));
                }
                else {
                    effectiveSensorWidth = std::abs((double)camera.camera_.sensor_height_ / cos(CvUtils::DegreesToRadians(ellipseAngleDegrees)));  // 90-ellipse angle
                    effectiveCameraResolution = std::abs((double)camera.camera_.resolution_y_ / cos(CvUtils::DegreesToRadians(ellipseAngleDegrees)));
                }

            }
            else {
                // Use the information from the circle to determine the radiue
                chosenRadiusPixels = CvUtils::CircleRadius(ball.ball_circle_);

                effectiveSensorWidth = camera.camera_.sensor_width_;
                effectiveCameraResolution = camera.camera_.resolution_x_;
            }

            // Use similar triangles should also work
            // double ballRadiusMeters = (effectiveCameraResolution / (2.0 * chosenRadiusPixels)) * (2.0 * kBallRadiusMeters) * (camera.focal_length_ / effectiveSensorWidth);

            double distanceToBallMeters = (effectiveCameraResolution / (2.0 * chosenRadiusPixels)) * (2.0 * GolfBall::kBallRadiusMeters) * (camera.camera_.focal_length_ / effectiveSensorWidth);

            return distanceToBallMeters;

            // TBD - AlternativelyCreate an upright (non-rotated) rectangle that fits around the ellipse, which is itself a rotated rect.
            // Then, use the X and Y widths of the resulting upright rectangle to figure out how wide/high the ellipse is.  
            // Finally, use the pythagorean theory to figure out the diagonal's real-world length.

            std::vector<cv::Point> contours;    // ?? = std::vector<cv:Point>(ball.ball_ellipse_.re);

        }


        bool GolfSimCamera::ComputeXyzDeltaDistances(const GolfBall& b1, const GolfBall& b2, cv::Vec3d& position_deltas_ball_perspective, cv::Vec3d& distance_deltas_camera_perspective) {
            // Axes are as perceived by the camera -- z is close/far, x is left/right, and y is up/down

            // Distance is already in real-world meters
            distance_deltas_camera_perspective[0] = b2.distances_ortho_camera_perspective_[0] - b1.distances_ortho_camera_perspective_[0];
            distance_deltas_camera_perspective[1] = b2.distances_ortho_camera_perspective_[1] - b1.distances_ortho_camera_perspective_[1];
            distance_deltas_camera_perspective[2] = b2.distances_ortho_camera_perspective_[2] - b1.distances_ortho_camera_perspective_[2];

            // Ball X is -Camera Z, Ball Y is Camera Y, Ball Z is Camera X  (TBD - earlier, this was computed from the other two ??
            position_deltas_ball_perspective[0] = -(b2.distances_ortho_camera_perspective_[2] - b1.distances_ortho_camera_perspective_[2]);
            position_deltas_ball_perspective[1] = b2.distances_ortho_camera_perspective_[1] - b1.distances_ortho_camera_perspective_[1];
            position_deltas_ball_perspective[2] = b2.distances_ortho_camera_perspective_[0] - b1.distances_ortho_camera_perspective_[0];

            return true;
        }

        // Camera angles are measured as follows:
        //      Negative X angle is degrees to the left of the camera bore line
        //      Negative Y angle is degrees down from the camera bore line.
        // The camera bore line is assumed to be straigt out from the system hardware
        // toward (and orthogonal to) the ball plane 
        // The adjusted distances will be as follows:
        //      -X is the distance to the left of the camera lens
        //      -Y is the distance down from the camera lens
        // Units of measurement are retained (and not assumed to be any particular
        // units) through this method

        bool GolfSimCamera::AdjustXYZDistancesForCameraAngles(const cv::Vec2d& camera_angles, 
                                                              const cv::Vec3d& original_distances, 
                                                              cv::Vec3d& adjusted_distances) {
            if (original_distances[kZ_index] == 0) {
                GS_LOG_MSG(error, "GolfSimCamera::AdjustXYZDistancesForCameraAngles received original Z distance of 0");
                return false;
            }

            GS_LOG_TRACE_MSG(trace, "X,Y camera angles (degrees) = " + std::to_string(camera_angles[kX_index]) + ", " + std::to_string(camera_angles[kY_index]));
            GS_LOG_TRACE_MSG(trace, "X,Y distances (meters) = " + std::to_string(original_distances[kX_index]) + ", " + std::to_string(original_distances[kY_index]));

            double camera_X_offset_for_tilt;
            double camera_Y_offset_for_tilt;

            SystemMode mode = GolfSimOptions::GetCommandLineOptions().system_mode_;

            if (mode == SystemMode::kCamera1 ||
                mode == SystemMode::kCamera1Calibrate || 
                mode == SystemMode::kCamera1TestStandalone ||
                mode == SystemMode::kCamera1BallLocation) {
                camera_X_offset_for_tilt = kCamera1XOffsetForTilt;
                camera_Y_offset_for_tilt = kCamera1YOffsetForTilt;
            }
            else {
                camera_X_offset_for_tilt = kCamera2XOffsetForTilt;
                camera_Y_offset_for_tilt = kCamera2YOffsetForTilt;
            }


            // X adjustment.  See Salmon notebook for diagram
            /*  This initial cut was a big fail
            double hypotenuse = sqrt( pow(original_distances[kX_index],2.) + pow(original_distances[kZ_index], 2.));

            double b_angle_radians = asin(original_distances[kX_index] / original_distances[kZ_index]);
            double a_prime_angle_radians = (kPi/2.) - (CvUtils::DegreesToRadians(camera_angles[kX_index]) + b_angle_radians);

            adjusted_distances[kZ_index] = hypotenuse * sin(a_prime_angle_radians) * cos(CvUtils::DegreesToRadians(camera_angles[kY_index]));
            adjusted_distances[kX_index] = hypotenuse * cos(a_prime_angle_radians) * cos(CvUtils::DegreesToRadians(camera_angles[kY_index]));

            GS_LOG_MSG(info, "X hypotenuse = " + std::to_string(hypotenuse) + ", b_angle_radians(X) = " +
                std::to_string(b_angle_radians) + ", a_prime_angle_radians(X) = " + std::to_string(a_prime_angle_radians));

            adjusted_distances[kX_index] += camera_X_offset_for_tilt;

            GS_LOG_MSG(info, "X = " + std::to_string(adjusted_distances[kX_index]));
            GS_LOG_MSG(info, "Z = " + std::to_string(adjusted_distances[kZ_index]));

            // Y adjustment
            hypotenuse = sqrt(pow(original_distances[kY_index], 2.) + pow(original_distances[kZ_index], 2.));

            b_angle_radians = atan(original_distances[kY_index] / original_distances[kZ_index]);
            a_prime_angle_radians = (kPi / 2.) + (CvUtils::DegreesToRadians(camera_angles[kY_index]) + b_angle_radians);

            GS_LOG_MSG(info, "Y hypotenuse = " + std::to_string(hypotenuse) + ", b_angle_radians(Y) = " + 
                        std::to_string(b_angle_radians) + ", a_prime_angle_radians(Y) = " + std::to_string(a_prime_angle_radians));


            adjusted_distances[kY_index] = hypotenuse * cos(a_prime_angle_radians);

            adjusted_distances[kY_index] += camera_Y_offset_for_tilt;

            GS_LOG_MSG(info, "Y = " + std::to_string(adjusted_distances[kY_index]) );
            */
            
            // Switch the coordinate system so taht the angles here are the angles AROUND the axis, not 
            // the angle IN the axis.  The configuation has X as left/right pan and Y as up/down tilt
            double camera_angle_y_radians = CvUtils::DegreesToRadians(camera_angles[kX_index]);
            double camera_angle_x_radians = CvUtils::DegreesToRadians(camera_angles[kY_index]);

            // Perform X-Axis rotation
            adjusted_distances[kY_index] = (original_distances[kY_index] * cos(camera_angle_x_radians)) -
                (original_distances[kZ_index] * sin(camera_angle_x_radians));

            adjusted_distances[kZ_index] = (original_distances[kY_index] * sin(camera_angle_x_radians)) +
                (original_distances[kZ_index] * cos(camera_angle_x_radians));

            // Perform Y-Axis rotation - NOTE - this re-uses and ajusts the Z distance already calculated just above
            adjusted_distances[kX_index] = (original_distances[kX_index] * cos(camera_angle_y_radians)) +
                (adjusted_distances[kZ_index] * sin(camera_angle_y_radians));

            adjusted_distances[kZ_index] = (adjusted_distances[kZ_index] * cos(camera_angle_y_radians)) -
                (original_distances[kX_index] * sin(camera_angle_y_radians));

            // TBD - Not certain this is useful - we can make up for the offset of the tilted camera from the
            // center of the system in other ways
            adjusted_distances[kX_index] += camera_X_offset_for_tilt;
            adjusted_distances[kY_index] += camera_Y_offset_for_tilt;

            return true;
        }



        // The ball's direct distance to the lens, as well as the x,y coordinates from the picture center
        // origin both have to be set BEFORE calling this method.
        // Axes are as perceived by the camera -- z is close/far, x is left/right, and y is up/down
        // NOTE that the Z distance is the line-of-sight distance from the lens to the ball.
        bool GolfSimCamera::ComputeXyzDistanceFromOrthoCamPerspective(const GolfSimCamera& camera, const GolfBall& b1, cv::Vec3d& distances) {

            if (b1.distance_to_z_plane_from_lens_ <= 0.0001) {
                LoggingTools::Warning("ComputeXyzDistanceFromOrthoCamPerspective called without setting the ball line-of-sight-distance");
                return false;
            }

            // First calculate the distances as if the camera was facing straight ahead toward 
            // the ball flight plane

            double xFromCameraCenter = b1.x() - std::round(camera.camera_.resolution_x_ / 2.0);
            double yFromCameraCenter = b1.y() - std::round(camera.camera_.resolution_y_ / 2.0);

            cv::Vec3d camera_perspective_distances;

            // Direct-to-ball-PLANE distance is already in real-world meters.   
            // However, we do not have the exact direct-to-ball distance due to the lens.
            // We will figure out the Z axis distance (which will generally be a little shorter) first.
            double xDistanceFromCamCenter = convertXDistanceToMeters(camera, b1.distance_to_z_plane_from_lens_, xFromCameraCenter);
            camera_perspective_distances[0] = xDistanceFromCamCenter;  // X distance, negative means to the left of the camera
            
            double yDistanceFromCamCenter = convertYDistanceToMeters(camera, b1.distance_to_z_plane_from_lens_, yFromCameraCenter);

            camera_perspective_distances[1] = -yDistanceFromCamCenter;  // Y distance, positive is upward (smaller Y values)  // TBD - sqrt(pow(b1.distance_to_z_plane_from_lens_, 2) - pow(yDistanceFromCamCenter, 2));  // Y distance.  Positive values are above the camera center

            camera_perspective_distances[2] = b1.distance_to_z_plane_from_lens_;// FOR NON_DIRECT sqrt(pow(b1.distance_to_z_plane_from_lens_, 2) - pow(xDistanceFromCamCenter, 2));  // Z distance.  Only positive values in front of camera

            GS_LOG_TRACE_MSG(trace, "GolfSimCamera::ComputeXyzDistanceFromOrthoCamPerspective computed camera_perspective_distances of: " +
                std::to_string(camera_perspective_distances[0]) + ", " +
                std::to_string(camera_perspective_distances[1]));

            // Convert the distances from the camera into a set of angles from the camera's perspective


            // Need to first determine the new Z-axis (down the barrel of the camera) twist angle 
            // that occurs because as a result of the tilt and spin of that camera.
            // That twist needs to be applied to the X and Y distances from the center of the image that
            // the camera_perspective_distances represent.
            double adjusted_camera_angle_y = camera.camera_.camera_angles_[1];
            if (std::abs(camera.camera_.camera_angles_[1]) < 0.0001) {
                // Make sure we don't divide by 0
                adjusted_camera_angle_y = 0.0001;
            }

            // The spin axis is negative as the camera spins clockwise looking out the lens from the back
            double spin_axis_radians = CvUtils::DegreesToRadians( sin(CvUtils::DegreesToRadians(camera.camera_.camera_angles_[0])) * adjusted_camera_angle_y );
            GS_LOG_TRACE_MSG(trace, "GolfSimCamera::ComputeXyzDistanceFromOrthoCamPerspective accounting for a camera twist angle of: " +
                std::to_string(CvUtils::RadiansToDegrees(spin_axis_radians)) + " degrees.");

            // Rotate the x,y point 
            double rotated_x = camera_perspective_distances[0] * cos(spin_axis_radians) - camera_perspective_distances[1] * sin(spin_axis_radians);
            double rotated_y = camera_perspective_distances[0] * sin(spin_axis_radians) + camera_perspective_distances[1] * cos(spin_axis_radians);

            GS_LOG_TRACE_MSG(trace, "GolfSimCamera::ComputeXyzDistanceFromOrthoCamPerspective un-rotated X,Y distances of: " +
                std::to_string(rotated_x) + ", " + std::to_string(rotated_y));

            // Using the adjusted X and Y distances (lined up with the real world up/down and left/right),
            // determine the angles of the ball from the camera perspective
            // Positive X degrees is to the left looking out the camera
            // Negative Y degrees is tilting down looking out the camera
            cv::Vec2d deltaAnglesCameraPerspective;
            deltaAnglesCameraPerspective[0] = -CvUtils::RadiansToDegrees(atan(rotated_x / camera_perspective_distances[2]));
            deltaAnglesCameraPerspective[1] = CvUtils::RadiansToDegrees(atan(rotated_y / camera_perspective_distances[2]));

            // Account for the angle of the camera, which will adjust the camera perspective angles
            // to the real-world LM-perspective polar coordinates.
            // X angle here is positive in a counter-clockwise movement looking down at the LM from above
            // Y angle is negative as the azimuth angle goes down from horizontal
            cv::Vec2d deltaAnglesLMPerspective;
            deltaAnglesLMPerspective[0] = camera.camera_.camera_angles_[0] + deltaAnglesCameraPerspective[0] ;
            deltaAnglesLMPerspective[1] = camera.camera_.camera_angles_[1] + deltaAnglesCameraPerspective[1];

            GS_LOG_TRACE_MSG(trace, "GolfSimCamera::ComputeXyzDistanceFromOrthoCamPerspective computed (LM-perspective) X,Y angles of: " + 
                                        std::to_string(deltaAnglesLMPerspective[0]) + ", " +
                                        std::to_string(deltaAnglesLMPerspective[1]));

            // Second, figure out the X, Y, and Z distances from the LM's perspective
            // This is essentially a polor-coordinate system to cartesian system conversion
            // See https://math.libretexts.org/Courses/Mount_Royal_University/MATH_2200%3A_Calculus_for_Scientists_II/7%3A_Vector_Spaces/5.7%3A_Cylindrical_and_Spherical_Coordinates#:~:text=To%20convert%20a%20point%20from,y2%2Bz2).

            double phi_radians = CvUtils::DegreesToRadians( 90. + deltaAnglesLMPerspective[1] );
            double p_rho = camera_perspective_distances[2];
            double theta_radians = CvUtils::DegreesToRadians(deltaAnglesLMPerspective[0]);
            double cartesian_x = p_rho * sin(phi_radians) * cos(theta_radians);
            double cartesian_y = p_rho * sin(phi_radians) * sin(theta_radians);
            double cartesian_z = p_rho * cos(phi_radians);

            // Convert from the model cartesian system into the one used by the launch monitor
            distances[0] = -cartesian_y;
            distances[1] = -cartesian_z;
            distances[2] = cartesian_x;

            // TBD - deprecated
            /*
            if (!AdjustXYZDistancesForCameraAngles(camera_angles, camera_perspective_distances, distances)) {
                GS_LOG_MSG(error, "GolfSimCamera::ComputeXyzDistanceFromOrthoCamPerspective failed.");
                return false;
            }
            */
            return true;
        }

        // The delta angles are the angles between the two balls, in either the camera's position,
        // or, alternatively, the ball's position (i.e., looking down-range)
        bool GolfSimCamera::getXYDeltaAnglesBallPerspective(const cv::Vec3d& position_deltas_ball_perspective,
            cv::Vec2d& deltaAnglesBallPerspective) {

            // Deal with case where the ball may not have moved (or some other problem occurred).  
            // In this case, just return 0's.
            if (position_deltas_ball_perspective[2] <= 0.001) {
                GS_LOG_MSG(error, "getXYDeltaAnglesBallPerspective:  b2.distance_to_z_plane_from_lens_ was 0!");
                deltaAnglesBallPerspective = (0, 0);
                return true;   // Don't error out on this
            }

            // TBD - determine the angles from the perspective of the camera?
            // Determine from perspective of the camera x-axis, looking in the positive direction (the ball's
            // most natural perspective).

            // Negative degrees means clockwise looking down the z axis

            // X angle (up/down) = arctan(z/x) from ball perspective
            deltaAnglesBallPerspective[0] = CvUtils::RadiansToDegrees(atan(position_deltas_ball_perspective[0] / position_deltas_ball_perspective[2]));
            // Y angle (left/right) = arctan(y/xf)
            deltaAnglesBallPerspective[1] = CvUtils::RadiansToDegrees(atan((position_deltas_ball_perspective[1] / position_deltas_ball_perspective[2])));

            return true;
        }


        bool GolfSimCamera::ComputeBallXYAnglesFromCameraPerspective(const cv::Vec3d& distances_camera_perspective,
            cv::Vec2d& deltaAnglesCameraPerspective) {

            if (distances_camera_perspective[2] < 0.0001) {
                GS_LOG_MSG(error, "ComputeBallXYAnglesFromCameraPerspective:  b2.distance_to_z_plane_from_lens_ was 0!");
                return false;
            }

            /* First version is for when the distances_camera_perspective[2] was the direct-to-ball distance instead of just the z plane
            // Positive X angle is counter-clockwise looking down on the camera/ball from above
            deltaAnglesCameraPerspective[0] = -CvUtils::RadiansToDegrees(asin(distances_camera_perspective[0] / distances_camera_perspective[2]));
            double directDistance = sqrt(pow(distances_camera_perspective[2], 2) + pow(distances_camera_perspective[0], 2));
            deltaAnglesCameraPerspective[1] = -CvUtils::RadiansToDegrees(atan(-distances_camera_perspective[1] / directDistance));
            */

            // Positive X angle is counter-clockwise looking down on the camera/ball from above
            // Positive Y angle is looking up from level to the ball
            deltaAnglesCameraPerspective[0] = CvUtils::RadiansToDegrees(atan(distances_camera_perspective[0] / distances_camera_perspective[2]));
            deltaAnglesCameraPerspective[1] = -CvUtils::RadiansToDegrees(atan(-distances_camera_perspective[1] / distances_camera_perspective[2]));

            return true;
        }

        bool  GolfSimCamera::prepareToTakeVideo() {
            return camera_.prepareToTakeVideo();
        }

        bool  GolfSimCamera::prepareToTakePhoto() {
            camera_.prepareToTakePhoto();
            return true;
        }

        cv::Mat  GolfSimCamera::getNextFrame() {
            return camera_.getNextFrame();
        }

        bool GolfSimCamera::ShowAndLogBalls(const std::string& title, 
                                            const cv::Mat& img, 
                                            std::vector<GolfBall>& balls, 
                                            bool log_image_to_file,
                                            const int middle_ball_index, 
                                            const int second_ball_index ) {

            // It's expensive to clone an image, so make sure we're here to do at least something
            if (!log_image_to_file && 
                !GolfSimOptions::GetCommandLineOptions().show_images_ && 
                GolfSimOptions::GetCommandLineOptions().artifact_save_level_ != ArtifactSaveLevel::kAll) {
                return true;
            }

            cv::Mat ball_image = img.clone();

            // Outline the final candidates for this image 
            for (size_t i = 0; i < balls.size(); i++) {
                GolfBall& b = balls[i];
                GsCircle& c = b.ball_circle_;

                std::string label;

                if (i == (size_t)middle_ball_index) {
                    label = std::to_string(i) + "(Mid)";
                }
                else if (i == (size_t)second_ball_index) {
                    label = std::to_string(i) + "(2nd)";
                } else {
                    label = std::to_string(i);
                }

                LoggingTools::DrawCircleOutlineAndCenter(ball_image, c, label);
            }

            LoggingTools::DebugShowImage(title, ball_image);

            if (log_image_to_file && GolfSimOptions::GetCommandLineOptions().artifact_save_level_ == ArtifactSaveLevel::kAll) {
                LoggingTools::LogImage("", ball_image, std::vector < cv::Point >{}, true, title + ".png");
            }

            return true;
        }

        void GolfSimCamera::SortBallsByXPosition(std::vector<GolfBall>& balls) {

            if (GolfSimOptions::GetCommandLineOptions().golfer_orientation_ == GolferOrientation::kRightHanded) {
                std::sort(balls.begin(), balls.end(), [](const GolfBall& a, const GolfBall& b)
                    { return (a.x() < b.x()); });
            }
            else {
                std::sort(balls.begin(), balls.end(), [](const GolfBall& a, const GolfBall& b)
                    { return (a.x() > b.x()); });
            }

        }

        int GolfSimCamera::GetMostCenteredBallIndex(const std::vector<GolfBall>& balls, const int ball_to_ignore_index) {

            // Randomly pick one ball as the current, initial candidate
            int most_centered_ball_index = -1;
            int smallest_distance_from_center = 10 * camera_.resolution_x_;   // Ensure first ball will be closer to the center

            // Loop through the balls, but ignore the ball_to_ignore if it is set (will otherwise be -1)
            for (size_t i = 0; i < balls.size(); i++) {

                if (i == (size_t)ball_to_ignore_index) {
                    continue;
                }

                const GolfBall& b = balls[i];

                int x_distance = (int)(std::abs(CvUtils::CircleX(b.ball_circle_) - (camera_.resolution_x_ / 2.)));
                int y_distance = (int)(std::abs(CvUtils::CircleY(b.ball_circle_) - (camera_.resolution_y_ / 2.)));

                int distance_from_center = std::sqrt(x_distance * x_distance + y_distance * y_distance);


                if (distance_from_center < smallest_distance_from_center) {
                    most_centered_ball_index = i;
                    smallest_distance_from_center = distance_from_center;

                    GS_LOG_TRACE_MSG(trace, "GetMostCenteredBallIndex - Best current candidate with distance from center of " +
                        std::to_string(distance_from_center) + " was: \n" + b.Format());
                }
            }

            return most_centered_ball_index;
        }


        bool GolfSimCamera::GetBallDistancesAndRatios(const std::vector<GolfBall>& balls,
                                                            std::vector<double>& distances,
                                                            std::vector<double>& distance_ratios) {

            // First get the inter-ball distances.  There will be one less distance element
            // than the number of balls.
            // "size()-1" because we need at least two balls for a single inter-ball distance
            for (size_t ball_index = 0; ball_index < balls.size() - 1; ball_index++) {

                const GolfBall& left_ball =  balls[ball_index];
                const GolfBall& right_ball = balls[ball_index + 1];

                double distance = left_ball.PixelDistanceFromBall(right_ball);

                distances.push_back(distance);
            }

            // There will be one less distance ratio than distances in a perfect world.
            // But there may be times when one of the circles is dropped and we have a lot fewer
            // distances
            for (size_t distance_index = 0; distance_index < distances.size() - 1; distance_index++) {

                double left_distance =  distances[distance_index];
                double right_distance = distances[distance_index + 1];

                // Account for friction forces that would tend to decrease the ideal distance
                // that would have occured with a constant-velocity assumption
                right_distance = AdjustDistanceForSlowing(right_distance);

                if (left_distance <= 1.0) {
                    LoggingTools::Warning("Found invalid (<1.0) distance");
                    return false;
                }
                double distance_ratio = right_distance / left_distance;

                distance_ratios.push_back(distance_ratio);
            }

            return true;
        }

        // Look at the balls around the two identified balls to determine (if possible) which
        // of the two balls is most likely to be on the track-line (really more of an arc) that appears
        // to be defined by the rest of the balls.  The found ball may be a false positive that
        // just happened not to be filtered by other techniques.
        // If true is returned, then off_track_ball_index will point to the ball that is least likely
        // to be part of the line of flight.
        // The balls must be sorted from left to right by their x coordinate, with higher indexes
        // pointing to balls with greater-or-equal x positions than lower indexes.
        // candidate_ball_index_1 must be < candidate_ball_index_2
        bool GolfSimCamera::FindBestBallOnLineOfFlight(const std::vector<GolfBall>& balls,
                                                       const int candidate_ball_index_left,
                                                       const int candidate_ball_index_right,
                                                       double& ball_1_distance,
                                                       double& ball_2_distance,
                                                       const GolfBall& line_ball1, 
                                                       const GolfBall& line_ball2) {


            /* This was the original code that used the balls around the ball being analyzed to determine the line
            if (balls.size() <= 3) {
                // Definitely not enough balls to figure out the line of flight
                return false;
            }
            int right_point_index = -1;
            int left_point_index = -1;

            // Determine the end points of the line of flight if we can.
            if (candidate_ball_index_left == 0) {
                // Left ball is all the way to the left of the ball vector (first index)
                // We can't find a ball to the left, so maybe we can find two
                // balls to the right to define the line of flight

                if (candidate_ball_index_right < (int)balls.size() - 2) {
                    // At least two balls exist to the right, so we can use those
                    // balls as anchors on the line of flight
                    left_point_index = candidate_ball_index_right + 1;
                    right_point_index = candidate_ball_index_right + 2;
                }
            }
            else if (candidate_ball_index_right == (int)balls.size() - 1) {
                // Right ball is all the way to the right of the ball vector (last index)
                // We can't find two balls to the right, so maybe we can find two
                // balls to the left to define the line of flight

                if (candidate_ball_index_left > 1) {
                    // At least two balls exist to the left, so we can use those
                    // balls as anchors on the line of flight
                    left_point_index = candidate_ball_index_left - 2;
                    right_point_index = candidate_ball_index_left - 1;
                }
            }
            else if (candidate_ball_index_left > 0 && candidate_ball_index_right < balls.size() - 1) {
                left_point_index = candidate_ball_index_left - 1;
                right_point_index = candidate_ball_index_right + 1;
            }

            if (candidate_ball_index_left < 0 || candidate_ball_index_right < 0 ) {
                // Were not able to find two anchor balls
                return false;
            }

            // We have enought points to define a line-of-flight
            const GolfBall& ball1 = balls[candidate_ball_index_left];
            const GolfBall& ball2 = balls[candidate_ball_index_right];
            const GolfBall& left_line_ball = balls[left_point_index];
            const GolfBall& right_line_ball = balls[right_point_index];

            ball_1_distance = GetPerpendicularDistanceFromLine(ball1.x(), ball1.y(), left_line_ball.x(), left_line_ball.y(), right_line_ball.x(), right_line_ball.y());
            ball_2_distance = GetPerpendicularDistanceFromLine(ball2.x(), ball2.y(), left_line_ball.x(), left_line_ball.y(), right_line_ball.x(), right_line_ball.y());
            */

            // We have enough points to define a line-of-flight

            // The new code defines the trajectory as being between the two best balls
            const GolfBall& ball1 = balls[candidate_ball_index_left];
            const GolfBall& ball2 = balls[candidate_ball_index_right];

            ball_1_distance = GetPerpendicularDistanceFromLine(ball1.x(), ball1.y(), line_ball1.x(), line_ball1.y(), line_ball2.x(), line_ball2.y());
            ball_2_distance = GetPerpendicularDistanceFromLine(ball2.x(), ball2.y(), line_ball1.x(), line_ball1.y(), line_ball2.x(), line_ball2.y());

            return true;
        }

        // xc, yc is designates the target point and xa, ya to xb, yb designates the 
        // line segment that goes through x1, y1, and x2, y2.
        double GolfSimCamera::GetPerpendicularDistanceFromLine(double xc, double yc, double xa, double ya, double xb, double yb) {

            // See, e.g., https://math.stackexchange.com/questions/2757318/distance-between-a-point-and-a-line-defined-by-2-points
            // The following was only for a line SEGMENT: https://stackoverflow.com/questions/849211/shortest-distance-between-a-point-and-a-line-segment
            
            // If the line is vertical, then the distance is just the x difference to the line
            if (std::abs(xb - xa) < 0.0001) {
                return (std::abs(xc - xb));
            }

            // If the line is horizontal, then the distance is just the y difference to the line
            if (std::abs(yb - ya) < 0.0001) {
                return (std::abs(yc - yb));
            }

            double nA = xb - xa;
            double nB = yc - ya;
            double nC = yb - ya;
            double nD = xc - xa;

            double numerator = std::abs(nA * nB - nC * nD);

            double dA = xb - xa;
            double dB = yb - ya;

            double denominator = sqrt(dA * dA + dB * dB);

            return numerator / denominator;
        }

        void GolfSimCamera::RemoveLowScoringBalls(std::vector<GolfBall>& balls, const int max_balls_to_retain) {

            int adjusted_max_balls_to_retain = max_balls_to_retain;

            if (adjusted_max_balls_to_retain >= (int)balls.size()) {
                GS_LOG_TRACE_MSG(trace, "RemoveLowScoringBalls asked to remove more balls than were identified.  max_balls_to_retain= " + 
                                    std::to_string(max_balls_to_retain) + ", but only have " + std::to_string(balls.size()) + " balls.");
                return;
            }

            int original_number_balls = (int)balls.size();
            int ball_to_delete = original_number_balls - 1;
            for (int i = 0; i < original_number_balls - adjusted_max_balls_to_retain; i++) {
                balls.erase(balls.begin() + ball_to_delete);
                ball_to_delete--;
            }
        }


        void GolfSimCamera::RemoveUnlikelyRadiusChangeBalls(std::vector<GolfBall>& initial_balls, 
                                                            const double max_change_percent,
                                                            const double max_overlapped_ball_radius_change_ratio,
                                                            const bool preserve_high_quality_balls) {

            if (initial_balls.size() < 3) {
                GS_LOG_TRACE_MSG(trace, "GolfSimCamera::RemoveUnlikelyRadiusChangeBalls found too few (< 3) balls.  Not processing anything.");
                return;
            }

            // We should never drop the <n> best balls
            uint kNumberHighQualityBallsToRetain_ = 2;

            // Identify any balls that are outside the expected radius range by retaining them only in the initial ball vector
            for (int i = (int)initial_balls.size() - 3; i >= 0; i--) {
                double b1_radius = initial_balls[i].measured_radius_pixels_;
                double b2_radius = initial_balls[i + 1].measured_radius_pixels_;
                double b3_radius = initial_balls[i + 2].measured_radius_pixels_;

                double middle_to_right_ball_proximity_pixels = initial_balls[i + 1].PixelDistanceFromBall(initial_balls[i + 2]);
                double middle_to_left_ball_proximity_pixels = initial_balls[i].PixelDistanceFromBall(initial_balls[i + 1]);

                double  middle_to_right_distance_adjustment = (middle_to_right_ball_proximity_pixels / 150.0) / 100.0;
                double  middle_to_left_distance_adjustment = (middle_to_left_ball_proximity_pixels / 150.0) / 100.0;

                if ((b2_radius > (b1_radius * (1.0 + max_change_percent/ 100. + middle_to_left_distance_adjustment)) &&
                    b2_radius > (b3_radius * (1.0 + max_change_percent / 100. + middle_to_right_distance_adjustment)))    ||
                    (b2_radius < (b1_radius * (1.0 - max_change_percent / 100. - middle_to_left_distance_adjustment)) &&
                        b2_radius < (b3_radius * (1.0 - max_change_percent / 100. - middle_to_right_distance_adjustment))) ) {

                    if (initial_balls[i + 1].quality_ranking >= kNumberHighQualityBallsToRetain_) {
                        GS_LOG_TRACE_MSG(trace, "RemoveUnlikelyRadiusChangeBalls removing ball " + std::to_string(i + 1) + " because it was too much smaller/larger than both adjacent balls.");
                        initial_balls.erase(initial_balls.begin() + i + 1);
                    }
                    else {
                        GS_LOG_TRACE_MSG(trace, "RemoveUnlikelyRadiusChangeBalls NOT removing ball " + std::to_string(i + 1) + " because although it was larger than both adjacent balls, it was a high-quality circle.");
                    }
                    // Also, if the middle ball is already overlapping with the ball to the left,
                    // then it's likely that ALL the balls from the middle ball to the left-most
                    // are all overlapped
                    // TBDXXX - Consider getting rid of everything to the left??
                }
                else {
                    // If not, is the outer of the three balls different enough to discard
                    double left_radius_change = std::abs(b2_radius - b1_radius);
                    double right_radius_change = std::abs(b3_radius - b2_radius);

                    // Are the rightmost two balls really close when the others are not
                    if (middle_to_right_ball_proximity_pixels < b3_radius  &&
                        middle_to_right_ball_proximity_pixels < middle_to_left_ball_proximity_pixels / 2) {

                        // The right-most ball shouldn't have changed in size this much when
                        // it hasn't moved very far.  Likely it's a mis-identification
                        if (right_radius_change > max_overlapped_ball_radius_change_ratio * left_radius_change) {
                            if (initial_balls[i + 2].quality_ranking >= kNumberHighQualityBallsToRetain_ ||
                                preserve_high_quality_balls == false) {
                                GS_LOG_TRACE_MSG(trace, "RemoveUnlikelyRadiusChangeBalls removing ball " + std::to_string(i + 2) + " because it was much larger/smaller than the ball it overlaps.");
                                initial_balls.erase(initial_balls.begin() + i + 2);
                            }
                        }
                    }


                    // Are the leftmost two balls really close?
                    if (middle_to_left_ball_proximity_pixels < b1_radius &&
                        middle_to_left_ball_proximity_pixels < middle_to_right_ball_proximity_pixels / 2) {

                        // The left-most ball shouldn't have changed in size this much when
                        // it hasn't moved very far.  Likely it's a mis-identification
                        if (left_radius_change > max_overlapped_ball_radius_change_ratio * right_radius_change) {
                            if (initial_balls[i].quality_ranking > kNumberHighQualityBallsToRetain_ - 1 ||
                                preserve_high_quality_balls == false) {
                                GS_LOG_TRACE_MSG(trace, "RemoveUnlikelyRadiusChangeBalls removing ball " + std::to_string(i) + " because it was much larger/smaller than the ball it overlaps.");
                                initial_balls.erase(initial_balls.begin() + i);
                            }
                        }
                    }
                }
            }
        }


        void GolfSimCamera::RemoveOffTrajectoryBalls(std::vector<GolfBall>& initial_balls, 
                                                     const double max_distance_from_trajectory, 
                                                     const GolfBall& best_ball,
                                                     const GolfBall& second_best_ball ) {

            // Identify any balls that are far from the projected trajectory
            for (int i = (int)initial_balls.size() - 1; i >= 0; i--) {
                GolfBall& b = initial_balls[i];

                // Don't both examining the two balls we're using to draw the trajectory line
                if (b.quality_ranking == best_ball.quality_ranking ||
                    b.quality_ranking == second_best_ball.quality_ranking ) {
                    continue;
                }

                double ball_distance = GetPerpendicularDistanceFromLine(b.x(), b.y(), best_ball.x(), best_ball.y(), second_best_ball.x(), second_best_ball.y());

                if (ball_distance > max_distance_from_trajectory) {
                    // GS_LOG_TRACE_MSG(trace, "Not analyzing ball " + std::to_string(i) + " due to it having off - trajectory distance of : " + std::to_string(ball_distance));
                    initial_balls.erase(initial_balls.begin() + i);
                }
            }
        }


        void GolfSimCamera::RemoveNearbyPoorQualityBalls(std::vector<GolfBall>& initial_balls,
                                                         const double max_ball_proximity,
                                                         const int max_quality_difference) {

            // Examine each of the search balls and remove any other balls that are both
            // much worse in quality and nearby the search ball

            std::vector<GolfBall> balls_copy = initial_balls;

            for (size_t outer_index = 0; outer_index < balls_copy.size(); outer_index++) {

                GolfBall& current_ball = balls_copy[outer_index];

                for (int i = (int)initial_balls.size() - 1; i > (int)outer_index; i--) {
                    GolfBall& b = initial_balls[i];

                    double ball_distance = current_ball.PixelDistanceFromBall(b);
                    int quality_difference = b.quality_ranking - current_ball.quality_ranking;

                    if (ball_distance < max_ball_proximity && quality_difference > max_quality_difference) {
                        GS_LOG_TRACE_MSG(trace, "Not analyzing ball " + std::to_string(i) + " due to its proximity of : " 
                                    + std::to_string(ball_distance) + " and poor quality of " + std::to_string(b.quality_ranking));
                        initial_balls.erase(initial_balls.begin() + i);
                    }
                }
            }
        }

        uint GolfSimCamera::RemoveOverlappingBalls(const std::vector<GolfBall>& initial_balls, 
                                                   const double ball_proximity_margin_percent,
                                                   const bool attempt_removal_of_off_trajectory_balls,
                                                   std::vector<GolfBall>& return_balls, 
                                                   const GolfBall& best_ball, 
                                                   const GolfBall& second_best_ball,
                                                   const bool preserve_high_quality_balls) {

            uint number_removed = 0;

            // We assume that the strobe intervals either constant or increasing in time/distance.
            // Start with the ball that is most likely to be in the clear, and then go toward the tee
            // until the intervals are so short that the ball images overlap.
            // The loop iterates right-to-left in the right-hand case.
            // TBD - What about left-handed??
            for (int i = (int)initial_balls.size() - 1; i >= 0; i--) {
                const GolfBall& ball = initial_balls[i];

                if (i == 0) {
                    // We have reached the closest ball to the tee in the vector
                    // There are no more balls to compare X-position with,
                    // and the loop did not drop out on the last iteration, so there was no overlap with this ball,
                    // so retain the current ball and the one to it's left and stop the iteration
                    return_balls.push_back(ball);
                    break;
                }

                const  GolfBall& next_closer_ball = initial_balls[i - 1];
                double ball_proximity_pixels = ball.PixelDistanceFromBall(next_closer_ball);
                double proximity_limit = ((1. - ball_proximity_margin_percent / 100.) * (next_closer_ball.measured_radius_pixels_ + ball.measured_radius_pixels_));

                if (ball_proximity_pixels < proximity_limit) {

                    if (attempt_removal_of_off_trajectory_balls) {

                        // The next ball closer to the tee is too close to the ball we are examining
                        // Thus, BOTH balls (including the 'current' ball) are overlapping and may have to be 
                        // ignored for the purpose of for example, spin analysis

                        // However, if we can determine that one of the balls is clearly a false positive, such
                        // as because that ball is clearly off the line-of-flight defined by the other balls, then
                        // we can just drop the false positive and keep the other ball.
                        double ball_1_distance = -1;
                        double ball_2_distance = -1;

                        FindBestBallOnLineOfFlight(initial_balls, i - 1, i, ball_1_distance, ball_2_distance, best_ball, second_best_ball);

                        // If both balls are on the trajectory (or really close), then assume they were both valid and remove both of them.
                        // Otherwise, assume one was a mis-identification and remove only it
                        // TBD - We might even want to increase the maximum off-trajectory now that we're doing 
                        // quite a bit of trajectory checks before this?
                        int kMaximumOffTrajectoryDistance_ = 8;

                        // The ground can cause a lot of bounch up & down on the balls, so
                        // make sure we don't get rid of a good ball just because it moved a bit.
                        if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
                            kMaximumOffTrajectoryDistance_ = 23;
                        }

                        uint kNumberHighQualityBallsToRetain_ = 2;

                        if (ball_1_distance < kMaximumOffTrajectoryDistance_ && ball_2_distance < kMaximumOffTrajectoryDistance_) {
                            // This appears to be an overlap of two actual balls
                            // Effectively get rid of both the current (overlapped) ball and the one to the left
                            // by skipping to the next ball to the left (if it exists).  They are both unlikely to be
                            // useful for spin analysis (and possibly position) because of the overlap

                            // However, do not get rid of any high-ranked ball

                            if (ball.quality_ranking < kNumberHighQualityBallsToRetain_ &&
                                preserve_high_quality_balls == true) {
                                // Leave the right-most ball (index i) alone and move ahead
                                // to the next one to the left
                                return_balls.push_back(ball);
                                i--;
                                number_removed++;                                
                                continue;
                            }
                            else if (next_closer_ball.quality_ranking < kNumberHighQualityBallsToRetain_ &&
                                preserve_high_quality_balls == true) {
                                // Preserve the left-most ball by simply skipping inserting the right-most
                                // ball in the return vector.  Assume the right-most is not a 'real' ball.
                                continue;
                            }
                            else {
                                // Get rid of both balls
                                i--;
                                number_removed += 2;

                                // special case - if we got rid of both balls and there's only one more ball
                                // that we would be looking at, there's no way to compare that ball to the
                                // ones we have just removed.  But, knowing that the spacing is even closer
                                // as we move to the left, we'll just effectively remove that last ball as well
                                // since it likely overlapped with the current left-side ball.
                                if (i == 1) {
                                    break;
                                }
                                else {
                                    continue;
                                }
                            }
                        }
                        else {
                            // At least one of the balls is a misidentified ball.  Assume one ball is real.
                            // Get rid of the ball that was furthest from the assumed line of flight
                            // unless that ball is one of the highest-ranked, in which case, lose the other one.
                            // The off-track ball is the one that is furthest from the line defined by the end points determined earlier
                            if (ball_1_distance > ball_2_distance) {
                                // The next ball to the left was the ball that is furthest from the line of flight
                                // Save the current ball (to the right of the other) and effectively skip the other
                                // We think that we only removed ONE real ball, however

                                return_balls.push_back(ball);
                                i--;
                                number_removed++;
                                continue;
                            }
                            else {
                                // The next ball to the left was the ball that is closest to the line of flight
                                // Effectively skip the current ball and move on to looking at the next ball to the left
                                // In this case, we don't believe we've removed a real ball, so don't count it
                                continue;
                            }
                        }
                    }
                    else {
                        // we are not allowing for avoiding removing a ball if it is overlapped 
                        // with an off-trajectory ball.  So just get rid of both
                        i--;
                        number_removed += 2;
                        // See speciai case comment above for an explanation for potentially breaking out of the loop here
                        if (i == 1) {
                            break;
                        }
                        else {
                            continue;
                        }
                    }
                }

                // The current ball was (visually) free and clear.  So retain it on the final ball list.
                return_balls.push_back(ball);
            }

            // The balls went in furthest-from-tee first, so resort the opposite way
            SortBallsByXPosition(return_balls);

            return number_removed;
        }

        // DEPRECATED
        void GolfSimCamera::RemoveTooSmallOrBigBalls(std::vector<GolfBall>& initial_balls,
                                                     const GolfBall &expected_best_ball) {

            // Other, valid, strobed balls should have similar radii
            // TBD - What about very hooked/sliced shots?  Would their radii change more?
            const double kMinStrobedBallRadiusRation = 0.80;
            const double kMaxStrobedBallRadiusRation = 1.20;

            int min_strobed_ball_radius = int(expected_best_ball.measured_radius_pixels_ * kMinStrobedBallRadiusRation);
            int max_strobed_ball_radius = int(expected_best_ball.measured_radius_pixels_ * kMaxStrobedBallRadiusRation);

            // Identify any balls that are outside the expected radius range by retaining them only in the initial ball vector
            for (int i = (int)initial_balls.size() - 1; i >= 0 ;i--) {
                GolfBall& b = initial_balls[i];

                double radius = b.measured_radius_pixels_;

                if (radius < min_strobed_ball_radius ||
                    radius > max_strobed_ball_radius) {
                    GS_LOG_TRACE_MSG(trace, "  Not analyzing found ball due to it having radius = {" + std::to_string(radius));
                    initial_balls.erase(initial_balls.begin() + i);
                }
            }
        }

        void GolfSimCamera::DetermineSecondBall(std::vector<GolfBall>& return_balls, 
                                                const int most_centered_ball_index,
                                                int &second_ball_index) {

            GolfBall& face_ball = return_balls[most_centered_ball_index];
            
            // One question is how to determine which OTHER ball should be used to compare with the face_ball
            // Should it be the nearest or the furthest?  Closest to the center (other than the middle ball)?
            // Answer:  We will focus on the spin analysis
            // Most centered will be better to compare for spin, as the face-on angle will be the
            // most similar.  For high speed spins, the closer the ball images, the higher
            // the recoverable speed, because the ball doesn't have as much time to spin.

            second_ball_index = GetMostCenteredBallIndex(return_balls, most_centered_ball_index);
            return;
        }

        void GolfSimCamera::RemoveUnlikelyAngleLowerQualityBalls(std::vector<GolfBall>& initial_balls) {

            // Note - the balls must have been ordered in quality order before calling this method.

            GS_LOG_TRACE_MSG(trace, "GolfSimCamera::RemoveUnlikelyAngleLowerQualityBalls");

            size_t number_exposures_to_analyze = kNumberAngleCheckExposures;
            // Make sure we're not trying to check more exposures than we have
            if (number_exposures_to_analyze >= initial_balls.size() - 1) {
                number_exposures_to_analyze = initial_balls.size() - 1;
            }

            // Examine each of the search balls and remove any near-by balls that are at an unreasonable angle.
            // This process takes care of the (likely) situation when the top (position 0) quality ball is
            // near another high-quality ball (e.g., position 1), but the second ball is a mistake and is
            // at a weird angle below/above the higher-quality ball.

            std::vector<GolfBall> balls_copy = initial_balls;

            // This index should point to the highest-quality ball
            size_t outer_index = 0;

            while (outer_index < initial_balls.size() - 1) {

                GolfBall& current_ball = initial_balls[outer_index];

                for (size_t i = initial_balls.size() - 1; i > outer_index; i--) {
                    GolfBall& b = initial_balls[i];

                    double ball_angle_degrees = 0;

                    // TBD - This is only an approximation.  It might not work at very high
                    // camera in(de)clinations

                    int x_distance_pixels = std::abs(b.x() - current_ball.x());

                    if (x_distance_pixels > kUnlikelyAngleMinimumDistancePixels) {
                        // The balls are too far apart to want to check for unlikely angles
                        continue;
                    }
                    else if (x_distance_pixels < 0.001) {
                        // GS_LOG_TRACE_MSG(trace, "RemoveUnlikelyAngleLowerQualityBalls not analyzing ball " + std::to_string(i) + " because its X coordinate is too far from ball number " + std::to_string(outer_index));
                        // If the balls are right above/below each other, just pick a very big angle to avoid doing a divide by zero
                        // The large angle should ensure that the 'bad' ball is removed
                        ball_angle_degrees = 89;
                    }
                    else {
                        // Calculate angle so that it doesn't matter which ball is to the left
                        ball_angle_degrees = CvUtils::RadiansToDegrees(atan((double)(b.y() - current_ball.y()) /
                            (double)std::abs(b.x() - current_ball.x())));;
                    }

                    if (b.x() > current_ball.x()) {
                        // The ball to be compared to the outer loop ball is to the right of the outer loop
                        ball_angle_degrees = -ball_angle_degrees;
                    }
                    else {
                        // The ball to be compared to the outer loop ball is to the left of the outer loop
                        // Leave the angles alone
                    }

                    double min_angle = 0;
                    double max_angle = 0;

                    // We will generally allow much smaller angles if we're putting
                    if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
                        min_angle = kMinPuttingQualityExposureLaunchAngle;
                        max_angle = kMaxPuttingQualityExposureLaunchAngle;
                    }
                    else {
                        min_angle = kMinQualityExposureLaunchAngle;
                        max_angle = kMaxQualityExposureLaunchAngle;
                    }

                    if (ball_angle_degrees < min_angle || ball_angle_degrees > max_angle) {
                        GS_LOG_TRACE_MSG(trace, "Not analyzing ball " + std::to_string(i) + " due to its unlikely angle of "
                            + std::to_string(ball_angle_degrees) + " degrees with respect to ball number " + std::to_string(outer_index));

                        initial_balls.erase(initial_balls.begin() + i);
                    }
                }

                outer_index++;
            }
        }



        void GolfSimCamera::RemoveWrongColorBalls(const cv::Mat& rgbImg, 
                                                    std::vector<GolfBall>& initial_balls,
                                                    const GolfBall& expected_best_ball,
                                                    const double max_strobed_ball_color_difference) {

            GS_LOG_TRACE_MSG(trace, "GolfSimCamera::RemoveWrongColorBalls");

            // Get the color and std of the ball that is the most likely to be a real ball
            std::vector<GsColorTriplet> statistics = CvUtils::GetBallColorRgb(rgbImg, expected_best_ball.ball_circle_);
            GsColorTriplet expectedBallRGBAverage{ statistics[0] };
            GsColorTriplet expectedBallRGBMedian{ statistics[1] };
            GsColorTriplet expectedBallRGBStd{ statistics[2] };

            // Expect that the expected_best_ball will not be removed from the vector, as it's differences 
            // should be zero.

            for (int i = (int)initial_balls.size() - 1; i >= 0; i--) {
                GolfBall& b = initial_balls[i];

                std::vector<GsColorTriplet> statistics = CvUtils::GetBallColorRgb(rgbImg, b.ball_circle_);
                GsColorTriplet avg_RGB{ statistics[0] };
                GsColorTriplet median_RGB{ statistics[1] };
                GsColorTriplet std_RGB{ statistics[2] };

                // Save the information for later - TBD - Centralize this earlier somewhere
                b.average_color_ = avg_RGB;
                b.std_color_ = std_RGB;

                // Draw the outer circle if in debug
                GS_LOG_TRACE_MSG(trace, "\n\nExamining circle No. " + std::to_string(i) + ".  Radius " + std::to_string(b.measured_radius_pixels_) +
                    " pixels. Average RGB is{ " + LoggingTools::FormatGsColorTriplet(avg_RGB)
                    + ". Average HSV is{ " + LoggingTools::FormatGsColorTriplet(CvUtils::ConvertRgbToHsv(avg_RGB)));

                // Determine how "different" the average color is from the expected ball color
                // If we don't have an expected ball color, than we use the RGB center from the  
                // current mask
                float rgb_avg_diff = CvUtils::ColorDistance(avg_RGB, expectedBallRGBAverage);
                float rgb_median_diff = CvUtils::ColorDistance(median_RGB, expectedBallRGBMedian);   // TBD
                float rgb_std_diff = CvUtils::ColorDistance(std_RGB, expectedBallRGBStd);   // TBD

                // Even if a potential ball has a really close median color, if the STD is even a little off, we want to down - grade it
                // The following works to mix the three statistics together appropriately
                // Will also penalize balls that are found toward the tail end of the list
                //  NOMINAL - large StdDiff was throwing off? float calculated_color_difference = rgb_avg_diff + (float)(100. * pow(rgb_std_diff, 2.));
                // TBD - this needs to be optimized and should probably be different than the code in the BallImageProc class.
                //                    double calculated_color_difference = pow(rgb_avg_diff,2) + (float)(2. * pow(rgb_std_diff, 2.)) + (float)(125. * pow(i, 4.));
                // NOTE - if the flash-times are different for the ball we are using for the color, this is likely to pick the wrong thing.

                // We are primarily concerned about situations where the median RGB went UP, because that's usually what happens when a ball overlaps
                // with another ball(s).  We are going to weigh that situation more heavily
                double calculated_color_difference = 0.;
                double rgb_difference_component = 0.;
                double std_difference_component = 0.;
                std::string brightness = "darker";

                if (CvUtils::IsDarker(avg_RGB, expectedBallRGBMedian)) {
                    brightness = "darker";
                    rgb_difference_component = (double)(kColorDifferenceRgbPostMultiplierForDarker * pow(1.0 * rgb_avg_diff, 2.));
                    std_difference_component = (double)(kColorDifferenceStdPostMultiplierForDarker * pow(2.3 * rgb_std_diff, 2.));

                    calculated_color_difference = rgb_difference_component + std_difference_component;
                } 
                else {
                    brightness = "lighter";
                    rgb_difference_component = (double)(kColorDifferenceRgbPostMultiplierForLighter * pow(1.0 * rgb_avg_diff, 2.));
                    std_difference_component = (double)(kColorDifferenceStdPostMultiplierForLighter * pow(2.0 * rgb_std_diff, 2.));

                    calculated_color_difference = rgb_difference_component + std_difference_component;
                }

                GS_LOG_TRACE_MSG(trace, "Found " + brightness + " circle number " + std::to_string(i) + "(x,y) = (" + std::to_string(b.x()) + ", " + std::to_string(b.y()) + "). radius = " + std::to_string(b.measured_radius_pixels_) +
                    " rgb_avg_diff = " + std::to_string(rgb_avg_diff) +
                    " CALCDiff = " + std::to_string(calculated_color_difference) + " rgbDiff = " + std::to_string(rgb_avg_diff) +
                    " rgb_median_diff = " + std::to_string(rgb_median_diff) + " rgb_std_diff = " + std::to_string(rgb_std_diff));

                // Identify any balls that are outside the expected radius range by retaining them only in the initial ball vector
                if (calculated_color_difference > max_strobed_ball_color_difference) {
                    GS_LOG_TRACE_MSG(trace, "  Not analyzing found ball No. " + std::to_string(i) + " due to it having too different a color ( difference was " +
                        std::to_string(calculated_color_difference) + "), and the max was " + std::to_string(max_strobed_ball_color_difference) + ".\n");
                    GS_LOG_TRACE_MSG(trace, " rgb_difference_component was " + std::to_string(rgb_difference_component) + "), and std_difference_component was " + std::to_string(std_difference_component) + ".\n\n");
                    initial_balls.erase(initial_balls.begin() + i);
                }

            }

        }


        void GolfSimCamera::RemoveWrongRadiusBalls(std::vector<GolfBall>& initial_balls,
                                                   const GolfBall& expected_best_ball ) {

            GS_LOG_TRACE_MSG(trace, "GolfSimCamera::RemoveWrongRadiusBalls");

            double nominal_radius = expected_best_ball.measured_radius_pixels_;
               
            // Expect that the expected_best_ball will not be removed from the vector, as it's differences 
            // should be zero.

            for (int i = (int)initial_balls.size() - 1; i > 0; i--) {
                GolfBall& b = initial_balls[i];

                double radius_difference = std::abs(nominal_radius - b.measured_radius_pixels_);
                double max_radius_different = nominal_radius * (kMaxRadiusDifferencePercentageFromBest / 100.0);

                // Remove any balls that are too far away from the expected radius range
                if (radius_difference > max_radius_different) {
                    GS_LOG_TRACE_MSG(trace, "  Not analyzing found ball No. " + std::to_string(i) + " due to it having too different a radius from best ball ( difference was " +
                        std::to_string(radius_difference) + "), and the max was " + std::to_string(max_radius_different));
                    initial_balls.erase(initial_balls.begin() + i);
                }
            }
        }

        void GolfSimCamera::ReportBallSearchError(const int number_balls_found) {
            std::string root_cause_str;

            if (number_balls_found == 0) {
                root_cause_str = "Unable to find ANY ball exposures after ball hit.  Did ball move inadvertently?";
            }
            else if (number_balls_found == 1) {
                root_cause_str = "Unable to find at least two ball exposures after ball hit.  It's possible the ball was hit faster or slower than the system can handle.";
            }
            else {
                root_cause_str = "An error occured while processing the post-hit ball image.  Please check logs.";
            }

            LoggingTools::current_error_root_cause_ = root_cause_str;
            GS_LOG_MSG(error, root_cause_str);
        }

        bool GolfSimCamera::AnalyzeStrobedBalls( const cv::Mat& strobed_balls_color_image,
                                                 const cv::Mat& strobed_balls_gray_image,
                                                 const GolfBall& calibrated_ball,
                                                 GsBallsAndTimingVector& return_balls_and_timings,
                                                 GsBallsAndTimingVector& non_overlapping_balls_and_timing,
                                                 GolfBall& face_ball,
                                                 GolfBall& ball2,
                                                 long& time_between_ball_images_uS  ) {

            GS_LOG_TRACE_MSG(trace, "AnalyzeStrobedBalls(ball).  calibrated_ball = " + calibrated_ball.Format());

            if (!calibrated_ball.calibrated) {

                GS_LOG_MSG(error, "AnalyzeStrobedBall called without a properly calibrated ball.");
                return false;
            }

            if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
                GS_LOG_MSG(info, "In putting mode.");
            }
            else {
                GS_LOG_MSG(info, "In driving mode.");
            }


            BallImageProc* ip = get_image_processor();

            LoggingTools::DebugShowImage("GolfSimCamera::TestAnalyzeStrobedBall COLOR input: ", strobed_balls_color_image);
            LoggingTools::DebugShowImage("GolfSimCamera::TestAnalyzeStrobedBall GRAY input: ", strobed_balls_gray_image);

            // Find where the ball currently is based on where we expected it from the prior information

            double distance_at_calibration_ = calibrated_ball.distance_at_calibration_;

            // Approximate the new distance based just on the X/Z plane distance, assuming
            // the ball will be hit straight
            double expected_camera2_distance = calibrated_ball.distances_ortho_camera_perspective_[2];
            if (expected_camera2_distance < 0.0001) {

                GS_LOG_MSG(error, "AnalyzeStrobedBall: Calculated expected_camera2_distance was 0.");
                return false;
            }

            double expected_strobed_ball_radius = calibrated_ball.radius_at_calibration_pixels_ * (distance_at_calibration_ / expected_camera2_distance);

            // Setup to search for a ball that has a reasonable size relationship to the calibrated ball
// TBD - REMOVE            double radius_at_calibration_pixels_ = getExpectedBallRadiusPixels(strobed_balls_color_image.cols, expected_camera2_distance);
            ip->min_ball_radius_ = int(expected_strobed_ball_radius * kMinMovedBallRadiusRatio);
            ip->max_ball_radius_ = int(expected_strobed_ball_radius * kMaxMovedBallRadiusRatio);

            GS_LOG_TRACE_MSG(trace, "Original radius at calibration-time distance of " + std::to_string(distance_at_calibration_) + " was: " + std::to_string(calibrated_ball.radius_at_calibration_pixels_) +
                ".  Adjusted radius for camera2 is: " + std::to_string(expected_strobed_ball_radius) + ". Looking for a ball with min/max radius (pixels) of: " +
                std::to_string(ip->min_ball_radius_) + ", " + std::to_string(ip->max_ball_radius_));

            // This is more useful when using the Hough Circle search technique - TBD
            bool useLargestFoundBall = false;
            bool dontReportErrors = false;

            cv::Rect roi;
            cv::Mat nullAreaMaskImage;
            ip->area_mask_image_ = nullAreaMaskImage;

            std::vector<GolfBall> initial_balls;

            BallImageProc::BallSearchMode processing_mode = BallImageProc::BallSearchMode::kStrobed;

            if (GolfSimOptions::GetCommandLineOptions().lm_comparison_mode_) {
                processing_mode = BallImageProc::BallSearchMode::kExternalStrobe;
            }

            // If we're putting, the ball should only be in the lower one-half to one-third of the image
            if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
                processing_mode = BallImageProc::BallSearchMode::kPutting;

                roi = cv::Rect{ 0, (int)(0.5 * strobed_balls_color_image.rows),
                           strobed_balls_color_image.cols, (int)(strobed_balls_color_image.rows * 0.49) };
            }
            else {
                // Leave the ROI as it was originally constructed by default - all 0's
            }

            // Don't search on color - the colors could be quite different
            GolfBall non_const_ball = calibrated_ball;
            non_const_ball.average_color_ = GsColorTriplet(0, 0, 0);

            bool result = ip->GetBall(strobed_balls_color_image, non_const_ball, initial_balls, roi, processing_mode, useLargestFoundBall, dontReportErrors);

            int number_of_initial_balls = initial_balls.size();

            if (!result || number_of_initial_balls < 2) {
                ReportBallSearchError(initial_balls.size());

                return false;
            }

            ShowAndLogBalls("AnalyzeStrobedBall_Initial_Candidate_Balls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            // Save the best and 2nd-best balls based on the GetBall's sorting
            // Do this here after we've gotten rid of any low-quality/angle balls above
            // Balls must still be sorted by quality at this point.
            GolfBall best_ball = initial_balls[0];
            GolfBall second_best_ball = initial_balls[1];
            GolfBall expected_best_ball = best_ball;

            // Note - balls should be sorted by quality during this early phase
            // TBD - let's try the putter way for the strobed balls as well?

            double max_color_difference = (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) ? kMaxPuttingBallColorDifferenceRelaxed : kMaxStrobedBallColorDifferenceRelaxed;
            RemoveWrongColorBalls(strobed_balls_color_image, initial_balls, expected_best_ball, max_color_difference);
            ShowAndLogBalls("AnalyzeStrobedBall_After_RemoveWrongColorBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);
            LoggingTools::Trace("Initial_balls after RemoveWrongColorBalls: ", initial_balls);

            // Seems like wrong radius balls can be a better and more-ball-removing early filter than UnlikelyAngle balls
            RemoveWrongRadiusBalls(initial_balls, expected_best_ball);
            ShowAndLogBalls("AnalyzeStrobedBall_After_RemoveWrongRadiusBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            RemoveUnlikelyAngleLowerQualityBalls(initial_balls);
            ShowAndLogBalls("AnalyzeStrobedBall_After_RemoveUnlikelyAngleLowerQualityBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            // Unlikely that the best balls would have been removed.  However, it is possible.
            // Reset as necessary
            best_ball = initial_balls[0];
            second_best_ball = initial_balls[1];
            expected_best_ball = best_ball;

            // TBD - Am putting this back in because we're still getting too many balls with the new edge detector
            RemoveLowScoringBalls(initial_balls, kMaxBallsToRetain);
            ShowAndLogBalls("AnalyzeStrobedBall_After_RemoveLowScoringBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            // Must be sorted by quality
            RemoveUnlikelyAngleLowerQualityBalls(initial_balls);
            ShowAndLogBalls("AnalyzeStrobedBall_After_RemoveUnlikelyAngleLowerQualityBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            RemoveWrongRadiusBalls(initial_balls, expected_best_ball);
            ShowAndLogBalls("AnalyzeStrobedBall_After_RemoveWrongRadiusBalls (Normal Mode)", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            // Unlikely that the best balls would have been removed.  However, it is possible.
            // Reset as necessary
            best_ball = initial_balls[0];
            second_best_ball = initial_balls[1];
            expected_best_ball = best_ball;


            // Especially if we are using a loose Hough for initial identification, this could get rid of 100 or more ball candidates
            SortBallsByXPosition(initial_balls);
            RemoveOffTrajectoryBalls(initial_balls, kMaxDistanceFromTrajectory, best_ball, second_best_ball);
            ShowAndLogBalls("AnalyzeStrobedBall_After_0thRemoveOffTrajectoryBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            // A frequent problem because of the very-broad net we cast is that crappy balls end up determined
            // right next to a good one.  Get rid of 'em
            if (number_of_initial_balls > 20) {
                ShowAndLogBalls("AnalyzeStrobedBall Balls before RemoveNearbyPoorQualityBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);
                RemoveNearbyPoorQualityBalls(initial_balls, ip->min_ball_radius_, number_of_initial_balls / 2);
                ShowAndLogBalls("AnalyzeStrobedBall Balls after RemoveNearbyPoorQualityBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);
            }

            // Allow for a couple of misidentifications, but assume that the best scoring
            // balls are all at the front of the herd and that we can get rid of the ones
            // at the back of the pack, quality-wise;
            RemoveLowScoringBalls(initial_balls, kMaxBallsToRetain);

            ShowAndLogBalls("AnalyzeStrobedBall_after_RemoveLowScoringBalls", strobed_balls_color_image, initial_balls, false);

            if (initial_balls.size() == 1) {
                GS_LOG_MSG(warning, "GetBall() found only one ball after initial filtering.  Ball velocity may have been too high or very slow.");
                return false;
            }

            SortBallsByXPosition(initial_balls);
            ShowAndLogBalls("AnalyzeStrobedBall_Before_RemoveUnlikelyRadiusChangeBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            double max_intermediate_ball_radius_change_percent = 0.0;

            if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
                max_intermediate_ball_radius_change_percent = kMaxPuttingIntermediateBallRadiusChangePercent;
            }
            else {
                max_intermediate_ball_radius_change_percent = kMaxIntermediateBallRadiusChangePercent;
            }

            RemoveUnlikelyRadiusChangeBalls(initial_balls, max_intermediate_ball_radius_change_percent, kMaxOverlappedBallRadiusChangeRatio);

            SortBallsByXPosition(initial_balls);
            ShowAndLogBalls("AnalyzeStrobedBall_After_1stRemoveUnlikelyRadiusChangeBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            RemoveUnlikelyRadiusChangeBalls(initial_balls, max_intermediate_ball_radius_change_percent, kMaxOverlappedBallRadiusChangeRatio);

            SortBallsByXPosition(initial_balls);
            ShowAndLogBalls("AnalyzeStrobedBall_After_2ndRemoveUnlikelyRadiusChangeBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            RemoveUnlikelyRadiusChangeBalls(initial_balls, max_intermediate_ball_radius_change_percent, kMaxOverlappedBallRadiusChangeRatio);

            ShowAndLogBalls("AnalyzeStrobedBall_After_3rdRemoveUnlikelyRadiusChangeBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            // Identify any balls that are overlapping with other balls.  Such balls are unlikely to be useful 
            // for spin calculations.

            // After sorting, the first ball will be the one that is furthest away from the tee-off spot
            // This is necessary for the RemoveOverlappingBalls to work correctly
            SortBallsByXPosition(initial_balls);

            LoggingTools::Trace("Initial_balls sorted by ascending X value: ", initial_balls);
            ShowAndLogBalls("AnalyzeStrobedBall_After_1stRemoveOffTrajectoryBalls", strobed_balls_color_image, initial_balls, false);


            // This should be using a tighter trajectory limit, but we're trying to accomodate some slow balls right now 
            // that end up with curved trajectories.  TBD
            SortBallsByXPosition(initial_balls);
            RemoveOffTrajectoryBalls(initial_balls, kMaxDistanceFromTrajectory, best_ball, second_best_ball);

            ShowAndLogBalls("AnalyzeStrobedBall_After_1stRemoveOffTrajectoryBalls", strobed_balls_color_image, initial_balls, kLogIntermediateExposureImagesToFile);

            SortBallsByXPosition(initial_balls);


            // Because some of the overlapping balls have bright colors that will likely be removed
            // in the color-filtering phase, we retain a copy of the overlapped balls BEFORE
            // the color-filter.  That way, we can later determine a set of strictly non-overlapping balls
            // for purposes of, e.g., spin analysis.
            // Otherwise, we might remove balls that were actually overlaps and then not be able to
            // later remove any (strictly) overlapping balls for spin analysis.
            std::vector<GolfBall> possibly_overlapping_balls_before_color_filter = initial_balls;

            // Balls with a small (say 25%) overlap should still be evaluated and retained if possible
            // During this pass, we will preserve high-quality balls even if they look sketchy
            std::vector<GolfBall> first_pass_balls;
            int number_overlapping_balls_removed = RemoveOverlappingBalls(initial_balls, kBallProximityMarginPercentRelaxed, true, 
                                                            first_pass_balls, best_ball, second_best_ball);
            SortBallsByXPosition(first_pass_balls);

            ShowAndLogBalls("AnalyzeStrobedBall_After_1stRemoveOverlappingBalls", strobed_balls_color_image, first_pass_balls, kLogIntermediateExposureImagesToFile);

            // TBD -Trying these two steps twice to deal with really close balls that remain after the first pass
            // THIS time, do not preserve high-quality balls that look sketchy
            RemoveUnlikelyRadiusChangeBalls(first_pass_balls, max_intermediate_ball_radius_change_percent, kMaxOverlappedBallRadiusChangeRatio, false);
            ShowAndLogBalls("AnalyzeStrobedBall_After_3rdRemoveUnlikelyRadiusChangeBalls", strobed_balls_color_image, first_pass_balls, kLogIntermediateExposureImagesToFile);

            SortBallsByXPosition(first_pass_balls);


            std::vector<GolfBall> return_balls;
            number_overlapping_balls_removed += RemoveOverlappingBalls(first_pass_balls, kBallProximityMarginPercentRelaxed, true,
                                                    return_balls, best_ball, second_best_ball, false);


            // From here one, we're only working with the return_balls vector

            ShowAndLogBalls("AnalyzeStrobedBall Candidates after RemoveOverlappingBalls", strobed_balls_color_image, return_balls, kLogIntermediateExposureImagesToFile);
            GS_LOG_TRACE_MSG(trace, "Number of Return_balls Candidates after RemoveOverlappingBalls: " + std::to_string(return_balls.size()));

            if (return_balls.size() < 2) {
                GS_LOG_MSG(error, "GetBall() found only one ball after color filtering.  Ball velocity may have been too high or very slow.");
                return false;
            }

            // If we got rid of so many balls that fewer than two remain, then the speed of the ball 
            // (relative to the strobing) was probably too slow because they images were all overlapped.
            if (return_balls.size() < 2) {
                GS_LOG_MSG(error, "GetBall() found fewer than two balls after removing overlapping ball images.  Ball velocity may have been too slow.");
                return false;
            }

            // Determine which ball is closest to the center of the image.  That will be the best one to use for spin determination
            SortBallsByXPosition(return_balls);

            ShowAndLogBalls("AnalyzeStrobedBall_After_Size_Color_Filtered_Candidates", strobed_balls_color_image, return_balls, kLogIntermediateExposureImagesToFile);
            GS_LOG_TRACE_MSG(trace, "Return_balls after color and size filter" + std::to_string(return_balls.size()));

            int most_centered_ball_index = GetMostCenteredBallIndex(return_balls);

            GS_LOG_TRACE_MSG(trace, "The closest-to-center-screen ball index was " + std::to_string(most_centered_ball_index));

            if (most_centered_ball_index < 0 || most_centered_ball_index >= (int)return_balls.size()) {
                GS_LOG_MSG(error, "Could not determine the closest-to-center-screen ball.");
                return false;
            }

            // NOTE - At this point we cannot re-sort the ball vector or else our indexes may not make sense
            // any more.
 
            // We need a second ball as a ball to compare from one point in time to another and to
            // use to determine spin.
            int second_ball_index = -1;
            DetermineSecondBall(return_balls, most_centered_ball_index, second_ball_index);

            GS_LOG_TRACE_MSG(trace, "Second ball index was " + std::to_string(second_ball_index));

            /**** TBD - No longer doing the ball improvement, because the new edge detector does a 
            // pretty good job in the first instance?
            ***/
            // Now that we're pretty sure we've go the best set of circle estiamtes, 
            // compute the best possible circle for each ball.
            // For now, do NOT do any refinement if we are putting and the balls are close
            // to the bottom of the image - the circles are probably as good as they are
            // going to get(?)  They may be near the bottom of the image and hard to isolate

            bool perform_best_circle_fit_when_putting = true;

            if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
                if (best_ball.y() + best_ball.measured_radius_pixels_ >= 0.98 * strobed_balls_gray_image.rows) {
                    perform_best_circle_fit_when_putting = false;
                }
            }

            if (BallImageProc::kUseBestCircleRefinement && perform_best_circle_fit_when_putting) {
                for (size_t first_index = 0; first_index < return_balls.size(); first_index++) {
                    GolfBall& original_ball = return_balls[first_index];

                    GsCircle best_circle;

                    if (first_index == 0) {
                        LoggingTools::DebugShowImage("GolfSimCamera::TestAnalyzeStrobedBall GRAY pre-best-circle input: ", strobed_balls_gray_image);
                    }

                    // TBD - Still trying to figure out of the largest circle (among the top few) is the best?
                    if (!BallImageProc::DetermineBestCircle(strobed_balls_gray_image, original_ball, BallImageProc::kUseBestCircleLargestCircle, best_circle)) {
                        GS_LOG_MSG(warning, "GolfSimCamera::AnalyzeStrobedBalls - failed to DetermineBestCircle spin ball number " +
                            std::to_string(first_index) + " .Using originally - found ball.");
                        continue;
                    }

                    LoggingTools::DebugShowImage("GolfSimCamera::TestAnalyzeStrobedBall GRAY POST-best-circle input: ", strobed_balls_gray_image);

                    // Replace the ball circle information with this (hopefully) better information
                    original_ball.set_circle(best_circle);

                    // Also find this ball in the possibly_overlapping_balls_before_color_filter and update it's circle information
                    for (size_t nonoverlapping_index = 0; nonoverlapping_index < possibly_overlapping_balls_before_color_filter.size(); nonoverlapping_index++) {
                        GolfBall& nonoverlapping_ball = possibly_overlapping_balls_before_color_filter[nonoverlapping_index];

                        // Find the ball by its quality ranking, which should be unique in each set
                        if (original_ball.quality_ranking == nonoverlapping_ball.quality_ranking) {
                            nonoverlapping_ball.set_circle(best_circle);
                        }
                    }
                }
            }

            ShowAndLogBalls("AnalyzeStrobedBall - improved ball set", strobed_balls_color_image, return_balls, kLogIntermediateExposureImagesToFile);

            // Figure out how much time passed between the middle and the second ball images, as
            // well as generally between each pair of balls in the return_balls
            std::vector<double> pulse_interval_sequence;
            DetermineStrobeIntervals(number_overlapping_balls_removed, 
                                    return_balls, 
                                    most_centered_ball_index, 
                                    second_ball_index, 
                                    time_between_ball_images_uS,
                                    return_balls_and_timings);

            GS_LOG_TRACE_MSG(trace, "The time between the center-most two images is: " + std::to_string((double)time_between_ball_images_uS/1000.0) + "ms.");

            // This is a "final" image, so we want to store it
            ShowAndLogBalls("AnalyzeStrobedBall_Final_Candidate_Balls", strobed_balls_color_image, return_balls, true, most_centered_ball_index, second_ball_index);

            face_ball = return_balls[most_centered_ball_index];
            ball2 = return_balls[second_ball_index];

            // Also return a set of fairly-strictly-non-overlapping balls for use with, e.g., spin analysis
            // NOTE - if there is a negative %, it is here to make sure the balls are well-separated.  
            // Otherwise, we sometimes get some overlap with a ball that was missed.

            // TBD - Some question here whether to get rid of the clear overlaps first, and then 
            // get rid of the bad colors, or vice versa.  Let's use color as a last resort...

            SortBallsByXPosition(possibly_overlapping_balls_before_color_filter);

            ShowAndLogBalls("AnalyzeStrobedBall - possibly_overlapping_balls_before_color_filter before RemoveOverlappingBalls", strobed_balls_color_image, possibly_overlapping_balls_before_color_filter, false);
            LoggingTools::Trace("AnalyzeStrobedBall - possibly_overlapping_balls_before_color_filter before RemoveOverlappingBalls: ", possibly_overlapping_balls_before_color_filter);

            std::vector<GolfBall> non_overlapping_balls;

            // The false is to ensure ALL overlapping balls are removed.  We want to check for overlaps
            // between as many possible balls as we can, so we perform this identification on the set
            // of balls before off-color (and possibly overlapping) balls were removed.
            RemoveUnlikelyRadiusChangeBalls(possibly_overlapping_balls_before_color_filter, max_intermediate_ball_radius_change_percent, kMaxOverlappedBallRadiusChangeRatio, false);

            ShowAndLogBalls("AnalyzeStrobedBall - balls after final RemoveUnlikelyRadiusChangeBalls", strobed_balls_color_image, possibly_overlapping_balls_before_color_filter, false);

            // TBD - For some reason, we end up creating the possibly-overlapping-balls vector at a time before the poor quality balls are necessarily removed.
            // So, do so here
            RemoveUnlikelyAngleLowerQualityBalls(possibly_overlapping_balls_before_color_filter);
            ShowAndLogBalls("AnalyzeStrobedBall_After FINAL RemoveUnlikelyAngleLowerQualityBalls", strobed_balls_color_image, possibly_overlapping_balls_before_color_filter, kLogIntermediateExposureImagesToFile);


            RemoveOverlappingBalls(possibly_overlapping_balls_before_color_filter,
                kBallProximityMarginPercentStrict,
                false, /* attempt_removal_of_off_trajectory_balls*/
                non_overlapping_balls,
                best_ball,
                second_best_ball,
                false /* preserve_high_quality_balls */);

            ShowAndLogBalls("AnalyzeStrobedBall - balls after strictly-overlapping are removed", strobed_balls_color_image, non_overlapping_balls, kLogIntermediateExposureImagesToFile);

            RemoveWrongColorBalls(strobed_balls_color_image, non_overlapping_balls, expected_best_ball, kMaxStrobedBallColorDifferenceStrict);



            // Create the NON_OVERLAPPING balls_and_timing vector.
            // Will do so by taking the current possibly-overlapping vector and
            // only adding each ball to the non_overlapping_balls_and_timing
            // if the ball also exists in the non_overlapping_balls vector.
            // Basically, we're just keeping the two vectors in sync.
            // TBD - Query whether we should just use a GsBallAndTiming vector 
            // everywhere?
            // The trick is to maintain the correct interval timing when we remove
            // a ball/element
            // 
            // TBD - Clean this up - super sloppy!

            non_overlapping_balls_and_timing = return_balls_and_timings;

            for (int i = non_overlapping_balls_and_timing.size() - 1; i >= 0; i--) {
                GsBallAndTimingElement& be = non_overlapping_balls_and_timing[i];

                bool ball_is_in_non_overlapping_vector = false;

                for (GolfBall& non_overlapping_ball : non_overlapping_balls) {
                    if (be.ball.x() == non_overlapping_ball.x() && be.ball.y() == non_overlapping_ball.y()) {
                        ball_is_in_non_overlapping_vector = true;
                    }
                }

                if (!ball_is_in_non_overlapping_vector) {
                    // Need to remove this ball element from the non-overlapping ball/timing vector
                    // because it was removed from the ball-only vector.
                    // Before we do so, Add it's left-most interval time to the left-most
                    // interval time of the ball to the right (if such ball exists)
                    if (i < (int)non_overlapping_balls_and_timing.size() - 1) {
                        non_overlapping_balls_and_timing[i + 1].time_interval_before_ball_ms += be.time_interval_before_ball_ms;
                    }

                    non_overlapping_balls_and_timing.erase(non_overlapping_balls_and_timing.begin() + i);
                }
            }

            ShowAndLogBalls("AnalyzeStrobedBall - FINAL strictly-non-overlapping balls", strobed_balls_color_image, non_overlapping_balls);

            return true;
        }

        // Especially for lightweight practice balls, the ball will slow down considerably
        // as it traverses the field of view.  Thus, the right-distance will be shorter than 
        // it would have been if the ball velocity was constant.  So, we will boost the
        // right distance a bit to try to make up for this.
        // TBD - Need to have the percentage better reflect actual ball physics

        double GolfSimCamera::AdjustDistanceForSlowing(const double initial_right_distance) {

            double kBallConstantSpeedAdjustmentPercentage;

            if (GolfSimOptions::GetCommandLineOptions().practice_ball_) {
                kBallConstantSpeedAdjustmentPercentage = kPracticeBallSpeedSlowdownPercentage; // percent
            }
            else if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
                kBallConstantSpeedAdjustmentPercentage = kPuttingBallSpeedSlowdownPercentage; // percent
            }
            else {
                kBallConstantSpeedAdjustmentPercentage = kStandardBallSpeedSlowdownPercentage; // percent
            }

            double right_distance = initial_right_distance * (1.0 + kBallConstantSpeedAdjustmentPercentage / 100.0);

            return right_distance;
        }


        int GolfSimCamera::FindClosestRatioPatternMatchOffset(const std::vector<double> distance_ratios,
                                                              const std::vector<double> pulse_ratios,
                                                              double& delta_to_closest_ratio) {

            // The current offset within the pulse interval ratios at which the pattern
            // of distance ratios is most closely correlated.
            delta_to_closest_ratio = 99999.0;
            int closest_timing_interval_offset = -1;

            if (pulse_ratios.size() < distance_ratios.size()) {
                return -1;
            }

            for (int distance_pattern_offset = 0; distance_pattern_offset <= (int)(pulse_ratios.size() - distance_ratios.size()); distance_pattern_offset++) {

                double difference_in_ratios = ComputeRatioDistance(distance_ratios, pulse_ratios, distance_pattern_offset);

                GS_LOG_TRACE_MSG(trace, "difference_in_ratios for offset " + std::to_string(distance_pattern_offset) + " is: " + std::to_string(difference_in_ratios));

                // If the current offest of the distance ratios within the pulse ratio pattern
                // results in the lowest error (distance), then assume it's the best for now.
                if (difference_in_ratios < delta_to_closest_ratio) {

                    // This is the smallest difference we've seen so far, so keep it!
                    // Also, figure out if the interval is to the left or to the right of the 
                    // middle ball (depending on which ball we chose as ball 2).
                    delta_to_closest_ratio = difference_in_ratios;
                    closest_timing_interval_offset = distance_pattern_offset;

                }
            }

            return closest_timing_interval_offset;
        }


        // Returns a score of the closeness of the vector of distance_ratios within the pulse_ratios at an offset
        // of the distance_ratios from the beginning of the pulse_ratios
        double GolfSimCamera::ComputeRatioDistance(const std::vector<double> distance_ratios,
            const std::vector<double> pulse_ratios,
            int& distance_pattern_offset) {

            // TBD - Do some error checking on the inputs

            double kMaxRatioDistance = 1000.0;

            double difference_in_ratios = 0;

            // Compute the differences between each distance ratio in the pattern of distances as compared
            // to each possible offset of those ratios to the pulse interval ratios.
            for (size_t distance_ratio_index = 0; distance_ratio_index < distance_ratios.size(); distance_ratio_index++) {

                // If we got more ball distance ratios than we have collapsed (and thus down-sized) pulse
                // ratios, then just stop and return a big error number to drop this comparison from the
                // best-of list.

                if ((distance_ratio_index + distance_pattern_offset) >= pulse_ratios.size()) {
                    LoggingTools::Warning("GolfSimCamera::ComputeRatioDistance received a distance_ratio_index higher than the number of pulse ratios.");
                    return kMaxRatioDistance;
                }
                // Compute the difference as a percent of the distance ratio so that each element
                // of the pattern of ratios can contribute a meaningful amount of error/difference
                // even if the distance is small.
                double distance_ratio = distance_ratios[distance_ratio_index];
                double pulse_ratio = pulse_ratios[distance_ratio_index + distance_pattern_offset];

                // Get a difference that is >= 0 so that squaring it will not decrease the number
                // double single_ratio_difference = 100. * std::abs(distance_ratio - pulse_ratio) / std::min(distance_ratio, pulse_ratio);
                double single_ratio_difference = 100. * std::abs(distance_ratio - pulse_ratio);


                // If we get a crazy number, cap it so that we don't go out of bounds of our variable
                if (single_ratio_difference > kMaxRatioDistance) {
                    GS_LOG_TRACE_MSG(trace, "single_ratio_difference > kMaxRatioDistance!");
                    single_ratio_difference = kMaxRatioDistance;
                }

                // Square to highlight/emphasize larger errors
                single_ratio_difference = pow(single_ratio_difference, 2);
                GS_LOG_TRACE_MSG(trace, "   single_ratio_difference: " + std::to_string(single_ratio_difference));

                difference_in_ratios += single_ratio_difference;
            }

            return difference_in_ratios;
        }


        // Determine the ratios of the exposure distances and compare to the
        // ratios of the strobe pulses to find a correlation.  
        // The best correlation will determine the 
        // likely correspondence between the strobe intervals and the ball intervals.
        // time_between_ball_images_ms is the time between 
        bool GolfSimCamera::DetermineStrobeIntervals(int number_overlapping_balls_removed,
                                                    std::vector<GolfBall>& input_balls, 
                                                    int most_centered_ball_index, 
                                                    int second_ball_index, 
                                                    long & time_between_ball_images_uS,
                                                    GsBallsAndTimingVector& return_balls_and_timing) {

            GS_LOG_TRACE_MSG(trace, "GolfSimCamera::DetermineStrobeInterval");

            LoggingTools::Trace( "   Return_balls: ", input_balls);

            std::vector<double> distances;
            std::vector<double> distance_ratios;

            // Distance ratios will be adjusted to account for ball slow down 
            if (!GetBallDistancesAndRatios(input_balls, distances, distance_ratios)) {
                LoggingTools::Warning("GetBallDistancesAndRatios failed.");
                return false;
            }

            LoggingTools::Trace( "Return_balls distances: ", distances);
            LoggingTools::Trace( "--------------------Return_balls distance ratios (adjusted for slow-down): ", distance_ratios);

            // We will get the full set of pulse ratios to see if we can confirm that there are fewer 
            // distances than pulses.  TBD - Not sure useful?
            // We also need the original, actual intervals to figure out how many we want to collapse
            // further down in this function.
            std::vector<double>test_pulse_ratios;
            std::vector<float>test_pulse_intervals;
            
            bool result = GetPulseIntervalsAndRatios(test_pulse_intervals, test_pulse_ratios);

            LoggingTools::Trace( "The pulse_interval ratios were: ", test_pulse_ratios);

            // Look at each pulse interval ratio that we have and see which is closest to the ratio(s) that 
            // exist for distance deltas.
            // NOTE - Assumes near-constant ball speed such that the ratios of strobe intervals is directly
            // proportional to the distances covered during those intervals.

            // Provide some sensible default for the interval time.  Will calculate in code below.
            time_between_ball_images_uS = 0;


            // Ensure we have at least one distance ratio, which means at least 2 distances and 3 balls
            // If not, fall back to a simpler estimte
            if (distance_ratios.size() > 0) {

                // Each interval ratio we have will involve three balls (left/right/middle), and should be 
                // roughly equal to the ratio of the distance between the middle and next ball divided 
                // by the distance between the ball before the middle and the middle

                // Compute the correlation (closeness) of the ball distance ratio pattern to the pulse ratio
                // pattern.  The distance ratios will always be <= the number of pulse intervals, due to the
                // possibility of losing some highly-overlapped images early in the pattern.

                // There shouldn't be too many missed pulses, but they could occur anywhere, so make sure
                // that we consider possible collapsed pulses all the way to the end

                std::vector<float>best_pulse_intervals;
                long best_ratio_distance = 999999.;
                int best_final_offset_of_distance_ratios = -1;
                int best_pulses_to_collapse = -1;
                int best_collapse_offset = -1;


                for (int pulses_to_collapse = 0; pulses_to_collapse < std::floor(test_pulse_intervals.size() / 2); pulses_to_collapse++) {
                 
                    // Only the earlier pulses will typically be overlapped, as the pulse intervals grow as time
                    // progresses.  TBD - For now, try looking for collapsed pulses only in the first third
                    for (size_t collapse_offset = 0; (collapse_offset < (test_pulse_intervals.size() - pulses_to_collapse) - 1 );
                                                    collapse_offset++) {

                        // We don't go through any offsets other than 0 if we are not collapsing any pulse intervals.
                        if (pulses_to_collapse == 0 && collapse_offset > 0) {
                            break;
                        }

                        std::vector<double>pulse_ratios;
                        std::vector<float>pulse_intervals;

                        // We will retrieve the actual pulse interval (in uS) from the list of such intervals.
                        // Just need to figure out WHICH interval coresponds to the two balls of interest.
                        bool result = GetPulseIntervalsAndRatios(pulse_intervals, pulse_ratios, pulses_to_collapse, collapse_offset);

                        LoggingTools::Trace("The (potentially collapsed) pulse_intervals were (ignore last '0' interval): ", pulse_intervals);
                        LoggingTools::Trace("The (potentially collapsed) pulse_ratios were : ", pulse_ratios);

                        double delta_to_closest_ratio;

                        int best_local_offset_of_distance_ratios = FindClosestRatioPatternMatchOffset(distance_ratios, pulse_ratios, delta_to_closest_ratio);

                        // It shouldn't frequently be necessary to collapse pulses, because the images will widen out substantially as the ball 
                        // crosses the field of view, and the "lost" balls are likely to be far to the left, and not the balls that we are going
                        // focus on in the middle.  For that reason, we will penalize otherwise-low distances for cases where the pulses are 
                        // being collapsed.
                        double kLostPulsePenaltyPercent = 70.;

                        if (pulses_to_collapse > 0) {
                            delta_to_closest_ratio *= (1.0 + kLostPulsePenaltyPercent / 100.);
                        }

                        // If this is the closest match seen so far, save the information
                        if (best_local_offset_of_distance_ratios >= 0 && delta_to_closest_ratio < best_ratio_distance) {

                            best_ratio_distance = delta_to_closest_ratio;
                            best_final_offset_of_distance_ratios = best_local_offset_of_distance_ratios;
                            best_pulses_to_collapse = pulses_to_collapse;
                            best_collapse_offset = collapse_offset;

                            GS_LOG_TRACE_MSG(trace, "------------> Found best (so far) pulse ratio pattern match.  best_ratio_distance= " + std::to_string(best_ratio_distance)
                                + " best_final_offset_of_distance_ratios= " + std::to_string(best_ratio_distance)
                                + " best_pulses_to_collapse= " + std::to_string(best_pulses_to_collapse)
                                + " best_collapse_offset= " + std::to_string(best_collapse_offset));
                        }
                    }
                }

                if (best_collapse_offset < 0 || best_final_offset_of_distance_ratios < 0) {
                    GS_LOG_MSG(error, "Unable to find best (most corellated) ratios.");
                    return false;
                }

                // If we found a best correllation, re-create the corresponding set of ratios and intervals
                std::vector<double>pulse_ratios;
                std::vector<float>pulse_intervals;

                // We will retrieve the actual pulse interval (in uS) from the list of such intervals.
                // Just need to figure out WHICH interval coresponds to the two balls of interest.
                GetPulseIntervalsAndRatios(pulse_intervals, pulse_ratios, best_pulses_to_collapse, best_collapse_offset);

                LoggingTools::Trace( "The best set of pulse_intervals was (ignore last '0' interval): ", pulse_intervals);

                // Transfer the pulse intervals to the array of balls and associated timing
                // The first ball doesn't get a prior interval

                for (size_t i = 0; i < input_balls.size(); i++) {
                    GolfBall& b = input_balls[i];
                    GsBallAndTimingElement be;
                    be.ball = b;
                    if (i > 0) {
                        be.time_interval_before_ball_ms = 1000 * pulse_intervals[best_final_offset_of_distance_ratios + i - 1];
                    }
                    return_balls_and_timing.push_back(be);
                }
                // Sort the ball and timing vector by ball.x position, left to right
                std::sort(return_balls_and_timing.begin(), return_balls_and_timing.end(), [](const GsBallAndTimingElement& a, const GsBallAndTimingElement& b)
                    { return (a.ball.x() < b.ball.x()); });


                if (second_ball_index > most_centered_ball_index) {
                    // The correct interval is the right one, as ball2 is to the right
                    // of the middle ball.
                    time_between_ball_images_uS = 1000 * pulse_intervals[most_centered_ball_index + best_final_offset_of_distance_ratios];
                }
                else {
                    // Ball2 is to the left of the middle ball.
                    time_between_ball_images_uS = 1000 * pulse_intervals[most_centered_ball_index + best_final_offset_of_distance_ratios - 1];
                }

            }
            else {
                if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {

                    GS_LOG_MSG(warning, "DetermineStrobeInterval received only two recognized balls - will make a guess that this was the last two exposures.");

                    // We only have two valid ball images
                    // Special cases where we only have 2 balls initially, or if we ultimately only end up with two.  
                    // Having only two balls might occur when the ball was hit really fast, and we only got
                    // two strobes before the ball went out of the FoV.  In this case, we (MAYBE) can assume that the first interval 
                    // must be the first strobe delay.
                    //    We could do:   time_between_ball_images_uS = pulse_intervals[0];
                    // OR...
                    // Maybe we trust that we actually managed to identify EACH of the strobes (even though we threw some out earlier)
                    // In that case, just count the number of images up to the two balls of interest and pick that pulse length:

                    // Keep track of how many balls we got considered heavily overlapped

                    std::vector<double>pulse_ratios;
                    std::vector<float>pulse_intervals;

                    // We will retrieve the actual pulse interval (in uS) from the list of such intervals.
                    // Just need to figure out WHICH interval coresponds to the two balls of interest.
                    if (!GetPulseIntervalsAndRatios(pulse_intervals, pulse_ratios)) {
                        GS_LOG_MSG(error, "GetPulseIntervalsAndRatios failed.");
                        return false;
                    }

                    if (input_balls.size() == 2) {
                        // We didn't see ANYTHING except two balls
                        int last_ball_index = input_balls.size() - 1;
                        time_between_ball_images_uS = 1000 * pulse_intervals[pulse_intervals.size() - 2];

                        GsBallAndTimingElement be1;
                        be1.ball = input_balls[last_ball_index-1];
                        GsBallAndTimingElement be2;
                        be2.ball = input_balls[last_ball_index];
                        be2.time_interval_before_ball_ms = time_between_ball_images_uS;

                        return_balls_and_timing.push_back(be1);
                        return_balls_and_timing.push_back(be2);
                    }
                    else {
                        GS_LOG_MSG(error, "GetPulseIntervalsAndRatios failed - Input balls < 3 and not 2 (?.");
                        return false;
                    }
                }
                else {
                        GS_LOG_MSG(warning, "DetermineStrobeInterval received only two recognized balls - will make a guess that this was the first two exposures.");

                        // We only have two valid ball images
                        // Special cases where we only have 2 balls initially, or if we ultimately only end up with two.  
                        // Having only two balls might occur when the ball was hit really fast, and we only got
                        // two strobes before the ball went out of the FoV.  In this case, we (MAYBE) can assume that the first interval 
                        // must be the first strobe delay.
                        //    We could do:   time_between_ball_images_uS = pulse_intervals[0];
                        // OR...
                        // Maybe we trust that we actually managed to identify EACH of the strobes (even though we threw some out earlier)
                        // In that case, just count the number of images up to the two balls of interest and pick that pulse length:

                        // Keep track of how many balls we got considered heavily overlapped

                        std::vector<double>pulse_ratios;
                        std::vector<float>pulse_intervals;

                        // We will retrieve the actual pulse interval (in uS) from the list of such intervals.
                        // Just need to figure out WHICH interval coresponds to the two balls of interest.
                        if (!GetPulseIntervalsAndRatios(pulse_intervals, pulse_ratios)) {
                            GS_LOG_MSG(error, "GetPulseIntervalsAndRatios failed.");
                            return false;
                        }

                        if (input_balls.size() == 2) {
                            // We didn't see ANYTHING except two balls
                            time_between_ball_images_uS = 1000 * pulse_intervals[0];

                            GsBallAndTimingElement be1;
                            be1.ball = input_balls[0];
                            GsBallAndTimingElement be2;
                            be2.ball = input_balls[1];
                            be2.time_interval_before_ball_ms = time_between_ball_images_uS;

                            return_balls_and_timing.push_back(be1);
                            return_balls_and_timing.push_back(be2);
                        }
                        else {
                            GS_LOG_MSG(error, "GetPulseIntervalsAndRatios failed - Input balls < 3 and not 2 (?.");
                            return false;
                        }
                }
            }

            // Sort the ball and timing vector by ball.x position, left to right
            std::sort(return_balls_and_timing.begin(), return_balls_and_timing.end(), [](const GsBallAndTimingElement& a, const GsBallAndTimingElement& b)
                { return (a.ball.x() < b.ball.x()); });

            return true;
        }


        bool GolfSimCamera::GetPulseIntervalsAndRatios(std::vector<float>& pulse_pause_intervals, 
                                                       std::vector<double>& pulse_pause_ratios,                                                       
                                                       const int number_pulses_to_collapse,
                                                       const int collapse_offset ) {

            GS_LOG_TRACE_MSG(trace, "GolfSimCamera::GetPulseIntervalsAndRatios: number_pulses_to_collapse: " + std::to_string(number_pulses_to_collapse) + 
                                                    ", collapse_offset: " + std::to_string(collapse_offset));

            const std::vector<float>& initial_pulse_intervals_ms = PulseStrobe::GetPulseIntervals();

            if (initial_pulse_intervals_ms.size() < 3) {
                GS_LOG_MSG(error, "Strobe pulse sequence is too short to compute ratios.");

                return false;
            }

            std::vector<float> working_pulse_intervals = initial_pulse_intervals_ms;

            // "Collapsing" a pulse ratio means taking two pulses and treating them as one.  Which is 
            // how the rest of the code would see the world if two strobed ball images could not be
            // separately resolved.  In that scenario, it's as if a ball was lost, so the way to correct
            // for it is to treat things as if that strobe pulse never happened, and that the prior strobe
            // consumed the time of the lost pulse as well as the prior pulse.

            if (collapse_offset >= 0 && number_pulses_to_collapse > 0) {

                if (collapse_offset > (int)working_pulse_intervals.size() - 1 - number_pulses_to_collapse) {
                    GS_LOG_MSG(error, "GolfSimCamera::GetPulseRatios - collapse_offset of " + std::to_string(collapse_offset) + " was too large.");
                    return false;
                }

                // Collapse
                for (int i = 0; i < number_pulses_to_collapse; i++) {
                    working_pulse_intervals[collapse_offset] += working_pulse_intervals[collapse_offset + 1];

                    // The erase() will both remove the collapsed element and also move the remaining element(s)
                    // over to the left by one to fill in the gap.
                    working_pulse_intervals.erase(working_pulse_intervals.begin() + collapse_offset + 1);
                }
            }

            LoggingTools::Trace("Collapsed pulse vector is: ", working_pulse_intervals);

            pulse_pause_intervals = working_pulse_intervals;


            // Now, calculate the resulting ratios
            // 
            // The "- 2" deals with having a 0 at the end of the sequence. 
            for (size_t i = 0; i < pulse_pause_intervals.size() - 2; i++) {

                /* ORIGINAL - TBD - WRONG?
                double ratio = ((double)(pulse_intervals_ms[i + 1]) - (double)(pulse_intervals_ms[i])) /
                    ((double)(pulse_intervals_ms[i]) - (double)(pulse_intervals_ms[i - 1]));
                */
                double ratio = ((double)(pulse_pause_intervals[i + 1])) /
                    ((double)(pulse_pause_intervals[i]));

                pulse_pause_ratios.push_back(ratio);
            }

            return true;
        }


        // Returns all of the result information in the result ball
        // TBD - How about we use a result instead of a ball for this purpose??
        bool GolfSimCamera::ProcessReceivedCam2Image(const cv::Mat& ball1_mat,
            const cv::Mat& strobed_ball_mat,
            const cv::Mat& camera2_pre_image_,
            GolfBall& result_ball,
            cv::Vec3d& rotationResults,
            cv::Mat& exposures_image,
            std::vector<GolfBall>& exposure_balls) {

            GS_LOG_TRACE_MSG(trace, "ProcessReceivedCam2Image called.");

            if (ball1_mat.empty()) {
                GS_LOG_MSG(error, "ProcessReceivedCam2Image received empty ball1_mat.");
                return false;
            }

            if (strobed_ball_mat.empty()) {
                GS_LOG_MSG(error, "ProcessReceivedCam2Image received empty strobed_ball_mat.");
                return false;
            }

            cv::Mat prepared_strobed_ball_mat = strobed_ball_mat.clone();

            if (!kUsePreImageSubtraction) {
                // Do no subtraction
            }
            else {
                if (camera2_pre_image_.empty()) {
                    GS_LOG_MSG(warning, "ProcessReceivedCam2Image - not using kUsePreImageSubtraction, or received empty camera2_pre_image_.");
                }
                else
                {
                    // TBD - For test:
                    /*

                    // cv::erode(camera2_pre_image_, camera2_pre_image_, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
                    // cv::GaussianBlur(camera2_pre_image_, camera2_pre_image_, cv::Size(7, 7), 0);
                    // cv::dilate(camera2_pre_image_, camera2_pre_image_, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);

                    LoggingTools::LogImage("", bgr[0], std::vector < cv::Point >{}, true, "pre_image (blue).png");
                    LoggingTools::LogImage("", bgr[1], std::vector < cv::Point >{}, true, "pre_image (green).png");
                    LoggingTools::LogImage("", bgr[2], std::vector < cv::Point >{}, true, "pre_image (red).png");


                    */

                    // cv::GaussianBlur(camera2_pre_image_, camera2_pre_image_, cv::Size(3, 3), 0);
                    // MAY HURT HOUGH cv::erode(camera2_pre_image_, camera2_pre_image_, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 1);

                    GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kPreImageWeightingOverall", kPreImageWeightingOverall);
                    GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kPreImageWeightingBlue", kPreImageWeightingBlue);
                    GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kPreImageWeightingGreen", kPreImageWeightingGreen);
                    GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kPreImageWeightingRed", kPreImageWeightingRed);


                    std::vector<cv::Mat> bgr;

                    cv::split(camera2_pre_image_, bgr);
                    bgr[0] = bgr[0] * kPreImageWeightingOverall * kPreImageWeightingBlue;
                    bgr[1] = bgr[1] * kPreImageWeightingOverall * kPreImageWeightingGreen;
                    bgr[2] = bgr[2] * kPreImageWeightingOverall * kPreImageWeightingRed;


                    cv::Mat final_pre_image;    // = camera2_pre_image_;
                    cv::merge(bgr, final_pre_image);

                    // LoggingTools::LogImage("", final_pre_image, std::vector < cv::Point >{}, true, "scaled_pre_image.png");

                    // Subtract the pre-image from the incoming strobed image to (hopefully) end up with just
                    // the golf balls and not all the background clutter
                    cv::Mat strobed_ball_mat_copy = prepared_strobed_ball_mat.clone();
                    cv::subtract(strobed_ball_mat_copy, final_pre_image, strobed_ball_mat);
                    LoggingTools::LogImage("", prepared_strobed_ball_mat, std::vector < cv::Point >{}, true, "strobed_img_minus_pre_image.png");
                }
            }

            // TBD - Are we doing this just to allow us to use non-const images?  Refactor?
            cv::Mat ball1ImgColor;

            if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera2) {
                // Special case - if we are using this function just to get the ball location
                // for testing, then use the camera2 image if that's what we're testing
                ball1ImgColor = prepared_strobed_ball_mat;
            }
            else {
                ball1ImgColor = ball1_mat;
            }

            cv::Mat strobed_balls_color_image = prepared_strobed_ball_mat;

            cv::Mat strobed_balls_gray_image;

            cv::cvtColor(strobed_balls_color_image, strobed_balls_gray_image, cv::COLOR_BGR2GRAY);

            CameraHardware::CameraModel  cameraModel = CameraHardware::PiGSCam6mmWideLens;

            // Get the ball data.  We will calibrate based on the first ball and then get the second one
            // using that calibrated data from the first ball.

            GolfSimCamera camera_1;
            camera_1.camera_.init_camera_parameters(GsCameraNumber::kGsCamera1, cameraModel);

            // One set of positions, below describes the relationship of camera2 to itself and the z-plane of the ball.
            // That set does not contain any displacement in the X,Y plane.
            // The offsets are from the origin, which is assumed to be where the ball is placed
            // A second set of positions describes the relationship of camera2 to camera1 so that trajectories 
            // may be calculated as between the initial ball image and the second images.

            // Current positioning is 50cm back from ball, 13cm up for camera 2.
            std::vector<cv::Vec3d> camera1_to_camera2_positions_from_origin = std::vector<cv::Vec3d>({ kCamera1PositionsFromOriginMeters, kCamera2PositionsFromOriginMeters });
            std::vector<cv::Vec3d> camera2_to_camera2_positions_from_origin = std::vector<cv::Vec3d>({ kCamera2PositionsFromOriginMeters, kCamera2PositionsFromOriginMeters });


            // TBD - Get rid of this stuff
            camera_1.camera_.firstCannedImageFileName = "Dummy Ball1 Image Name";
            camera_1.camera_.secondCannedImageFileName = "Dummy Ball2 Image Name";
            camera_1.camera_.firstCannedImage = ball1ImgColor;
            camera_1.camera_.secondCannedImage = strobed_balls_color_image;

            cv::Vec2i expectedBallCenter = cv::Vec2i(1456 / 2, 1088 / 2);

            if (GolfSimOptions::GetCommandLineOptions().search_center_x_ > 0) {
                expectedBallCenter[0] = GolfSimOptions::GetCommandLineOptions().search_center_x_;
            }

            if (GolfSimOptions::GetCommandLineOptions().search_center_y_ > 0) {
                expectedBallCenter[1] = GolfSimOptions::GetCommandLineOptions().search_center_y_;
            }

            // Get the location information about the first ball from the initial, static, image
            GolfBall calibrated_ball;

            /*****************************  Get the first (teed) ball  ***************************/
            bool success = camera_1.GetCalibratedBall(camera_1, ball1_mat, calibrated_ball, expectedBallCenter);

            if (!success) {
                GS_LOG_TRACE_MSG(trace, "ProcessReceivedCam2Image - Failed to GetCalibratedBall.");
                return false;
            }

            if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1Calibrate ||
                GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2Calibrate ||
                GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1BallLocation ||
                GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2BallLocation) {

                GS_LOG_TRACE_MSG(trace, "ProcessReceivedCam2Image returning early, as we are just here to find the ball location for testing.");
                return true;
            }


            GS_LOG_TRACE_MSG(trace, "ProcessReceivedCam2Image - Calibrated Ball is:\n" + calibrated_ball.Format());

            // Next, get all the strobed balls

            GsBallsAndTimingVector return_balls_and_timing;
            GsBallsAndTimingVector non_overlapping_balls_and_timing;

            GolfBall first_strobed_ball, second_strobed_ball;

            long time_between_balls_uS;

            LoggingTools::DebugShowImage("Current Gray Image 1", strobed_balls_gray_image);

            GolfSimCamera camera_2;
            camera_2.camera_.init_camera_parameters(GsCameraNumber::kGsCamera2, cameraModel);


            success = camera_2.AnalyzeStrobedBalls(strobed_balls_color_image,
                                            strobed_balls_gray_image,
                                            calibrated_ball, 
                                            return_balls_and_timing, 
                                            non_overlapping_balls_and_timing, 
                                            first_strobed_ball, 
                                            second_strobed_ball, 
                                            time_between_balls_uS);

            if (!success || return_balls_and_timing.size() < 2) {
                GS_LOG_TRACE_MSG(trace, "ProcessReceivedCam2Image - Could not find two balls");
                ReportBallSearchError((int)return_balls_and_timing.size());
                return false;
            }

            LoggingTools::DebugShowImage("Current Gray Image 1.5", strobed_balls_gray_image);

            // Setup to return the exposures that were found to the caller
            exposures_image = strobed_balls_color_image.clone();
            for (auto& exposure_ball_and_timing : return_balls_and_timing) {
                exposure_balls.push_back(exposure_ball_and_timing.ball);
            }

            // First, determine a velocity based on the two best balls as determined by the
            // AnalyzeStrobedBall method.

            GolfBall ball1, ball2;

            // Normalize the balls such that ball1 is to the left and ball2 is to the right
            // TBD - We are moving away from using just the first and second strobed balls, 
            // so should probably remove this code.
            if (first_strobed_ball.x() > second_strobed_ball.x()) {
                ball2 = first_strobed_ball;
                ball1 = second_strobed_ball;
            }
            else {
                ball1 = first_strobed_ball;
                ball2 = second_strobed_ball;
            }

            GS_LOG_TRACE_MSG(trace, "ProcessReceivedCam2Image - ball1 is:\n" + ball1.Format());
            GS_LOG_TRACE_MSG(trace, "ProcessReceivedCam2Image - ball2 is:\n" + ball2.Format());


            // Now use those two 'best' balls to determine the position deltas for the balls so that we 
            // can, for example, compute velocity.
            // Both balls were captured by camera2
            if (!ComputeBallDeltas(ball1, ball2, camera_2, camera_2)) {
                GS_LOG_MSG(error, "ProcessReceivedCam2Image - failed to ComputeBallLocation for ball1.");
                return false;
            }

            GS_LOG_TRACE_MSG(trace, "ProcessReceivedCam2Image - ball2 (with delta information) is:\n" + ball2.Format());


            // At this point, ball2 now holds the important delta information from which things like velocity will be
            // computed.  Transfer all of that to the result ball.
            result_ball = ball2;

            // Next, calculate launch and side angles as between the initial, stationary, ball and each of
            // the strobed images.  Given the distance from the initial ball and the later in-flight
            // exposures, the average angles should be pretty accurate, even if, for example, there is
            // some noisey calculations of the radius of the strobed ball exposures.

            std::vector<GolfBall> camera1_average_ball_vector;

            for (size_t first_index = 0; first_index < return_balls_and_timing.size(); first_index++) {
                GolfBall& ball2 = return_balls_and_timing[first_index].ball;

                // Now get the locations so that the spin analysis can work
                if (!camera_1.ComputeBallDeltas(calibrated_ball, ball2, camera_1, camera_2)) {
                    GS_LOG_MSG(error, "ProcessReceivedCam2Image - failed to ComputeBallLocation between initial ball and strobed ball.");
                    return false;
                }

                GS_LOG_TRACE_MSG(trace, "ProcessReceivedCam2Image - Strobed Ball (for averaging) is:\n" + ball2.Format());


                // Get ready to average the measurements of this calculated ball with any others
                camera1_average_ball_vector.push_back(ball2);
            }


            // We want to average the launch angles, but we don't want to average any spins or distance or velocities.
            // Doesn't really make sense to average the two pairs of balls' distances, for example,
            // as those would be different by design since it's (usually) different pairs
            GolfBall camera1_averaged_ball;
            GolfBall::AverageBalls(camera1_average_ball_vector, camera1_averaged_ball);

            GS_LOG_TRACE_MSG(trace, "Averaged angles from the initial, stationary ball to each strobed ball:\n" + camera1_averaged_ball.Format());

            // Overwrite the angle information with the (hopefully) more accurate angles formed by the
            // initial ball to each of the strobed balls
            result_ball.angles_ball_perspective_ = camera1_averaged_ball.angles_ball_perspective_;
            result_ball.angles_camera_ortho_perspective_ = camera1_averaged_ball.angles_camera_ortho_perspective_;

            // At this point, we also have a more accurate idea of the launch side-angle than we could have
            // derived from just the change in the radius of the ball in the camera2 image.  So, re-calculate
            // the velocity based on that side angle in combination with the other two original angles
            result_ball.position_deltas_ball_perspective_[0] = result_ball.distances_ortho_camera_perspective_[0] * 
                                                                sin(CvUtils::DegreesToRadians(result_ball.angles_ball_perspective_[0]));

            // The velocity will be calculated from the updated position_deltas_ball_perspective_
            CalculateBallVelocity(result_ball, time_between_balls_uS);

            result_ball.time_between_ball_positions_for_velocity_uS_ = time_between_balls_uS;


            LoggingTools::DebugShowImage("Current Gray Image 2", strobed_balls_gray_image);


            // TBD - Let's see how the entire group of strobed balls BY THEMSELVES do in terms of HLA, VLA, velocity, etc.
            GolfBall average_of_strobed_ball_data;

            if (!ComputeAveragedStrobedBallData(camera_2, return_balls_and_timing, average_of_strobed_ball_data)) {
                GS_LOG_MSG(error, "ProcessReceivedCam2Image - failed to ComputeBallLocation between initial ball and strobed ball.");
                return false;
            }
            GS_LOG_TRACE_MSG(trace, "ComputeAveragedStrobedBallData returned ball=:" + average_of_strobed_ball_data.Format());


            // Send a quick IPCResult message here to allow the user to quickly
            // see the angular and velocity information before we do the (lengthy) spin measurement.
#ifdef __unix__ 
            // TBD - Note the sleep in the send will slow us down getting to the spin measurement
            // once spin is faster, we should just send the final message
            // GsUISystem::SendIPCHitMessage(result_ball);
#endif
            bool kSkipSpinCalculation = false;
            GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.kSkipSpinCalculation", kSkipSpinCalculation);

            if (kSkipSpinCalculation || GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
                // Do nothing regarding spin and just get back as quickly as possible
                GS_LOG_TRACE_MSG(trace, "Skipping spin analysis.");
            }
            else {
                if (non_overlapping_balls_and_timing.size() < 2) {
                    std::string error_str = "Could not find two non-overlapping balls to analyze for spin.";
                    GS_LOG_MSG(error, error_str);
                    LoggingTools::current_error_root_cause_ = error_str;

                    // We probably still calculated non-spin values like HLA, VLA and velocity,
                    // so return successfully and set the spin values to something we can
                    // later identify as N/A, such as the default 0, 0.

                    return true;
                }

                // Determine the spin based on the two closest balls in the strictly 
                // non-overlapping set of balls, and apply that information to the result_ball
                // that we are building up.
                if (!ProcessSpin(camera_2, strobed_balls_gray_image, non_overlapping_balls_and_timing,
                    camera2_to_camera2_positions_from_origin, result_ball, rotationResults)) {

                    // If we can't compute spin, it's a bummer, but it shouldn't be fatal
                    std::string error_str = "Unable to compute spin.";
                    GS_LOG_MSG(warning, error_str);
                    LoggingTools::current_error_root_cause_ = error_str;
                }
            }

            result_ball.PrintBallFlightResults();

            return true;
        }




        bool GolfSimCamera::ProcessSpin(GolfSimCamera &camera,
                                        const cv::Mat& strobed_balls_gray_image,
                                        const GsBallsAndTimingVector& non_overlapping_balls_and_timing, 
                                        std::vector<cv::Vec3d>& camera2_to_camera2_positions_from_origin,
                                        GolfBall& result_ball,
                                        cv::Vec3d& rotationResults) {

            GolfBall spin_ball1;
            GolfBall spin_ball2;
            double spin_timing_interval_uS = 0.0;

            // Try to find the two closest balls while avoiding any balls really close to the edge if we can.
            // Back off if necessary
            if (!FindBestTwoSpinBalls(strobed_balls_gray_image, non_overlapping_balls_and_timing, true, spin_ball1, spin_ball2, spin_timing_interval_uS)) {
                if (!FindClosestTwoBalls(strobed_balls_gray_image, non_overlapping_balls_and_timing, false, spin_ball1, spin_ball2, spin_timing_interval_uS)) {
                    GS_LOG_MSG(error, "FindClosestTwoBalls failed.");
                    return false;
                }
            }

            // Now use the two 'best' balls to determine the position deltas for the balls so that we 
            // can, for example, compute velocity
            if (!camera.ComputeBallDeltas(spin_ball1, spin_ball2, camera, camera)) {
                GS_LOG_MSG(error, "ProcessReceivedCam2Image - failed to ComputeBallDeltas for spin ball.");
                return false;
            }

            std::vector<GolfBall> finalSpinBalls;
            finalSpinBalls.push_back(spin_ball1);
            finalSpinBalls.push_back(spin_ball2);

            LoggingTools::Trace("Two closest balls (for spin analysis) are:\n" + spin_ball1.Format() + "\nand\n" + spin_ball2.Format());

            ShowAndLogBalls("ProcessReceivedCam2Image - Final Spin Balls", strobed_balls_gray_image, finalSpinBalls, kLogIntermediateExposureImagesToFile);


            // The best spin analysis will likely be between the two closest balls that are non-overlapping
            rotationResults = BallImageProc::GetBallRotation(strobed_balls_gray_image, spin_ball1, strobed_balls_gray_image, spin_ball2);

            // TBD - Find the interval between spin_ball1 and spin_ball2
            // 
            // Calculate the spin RPMs into the result ball
            camera.CalculateBallSpinRates(result_ball, rotationResults, (long)spin_timing_interval_uS);

            result_ball.time_between_angle_measures_for_rpm_uS_ = spin_timing_interval_uS;

            return true;
        }


        bool GolfSimCamera::FindClosestTwoBalls(const cv::Mat& img,
                                                const GsBallsAndTimingVector& balls,
                                                const bool use_edge_backoffs,
                                                GolfBall& ball1,
                                                GolfBall& ball2,
                                                double &timing_interval_uS) {

            int closest_ball1 = -1;
            int closest_ball2 = -1;
            double closest_distance_so_far = 100000;

            int minX = kClosestBallPairEdgeBackoffPixels;
            int minY = kClosestBallPairEdgeBackoffPixels;
            int maxX = img.cols - kClosestBallPairEdgeBackoffPixels;
            int maxY = img.rows - kClosestBallPairEdgeBackoffPixels;

            // If MOST of the balls have x's and y's that are close to a border, however
            // we can't rule those out as our spin balls.  
            // In that case, we will move the min/max values back out to help ensure
            // that we can use those balls.
            // Of course, if use_edge_backoffs == false, we're going to ignore the
            // backoffs entirely.

            std::vector<GolfBall> only_balls;
            for (const GsBallAndTimingElement& b : balls) {
                only_balls.push_back(b.ball);
            }

            GolfBall averaged_ball;
            GolfBall::AverageBalls(only_balls, averaged_ball);

            const double kEdgeGroupLowBackawayRatio = 1.5;
            const double kEdgeGroupHighBackawayRatio = 0.8;

            // A bunched-up group of balls in the x dimension is pretty unlikely, but check just in case
            if (averaged_ball.x() < minX * kEdgeGroupLowBackawayRatio) {
                minX = (int)std::round(averaged_ball.x() / 2.0);
            }
            if (averaged_ball.y() < minY * kEdgeGroupLowBackawayRatio) {
                minY = (int)std::round(averaged_ball.y() / 2.0);
            }
            // Get rid of the max limit entirely if necessary
            if (averaged_ball.x() > maxX * kEdgeGroupHighBackawayRatio) {
                maxX = img.cols;
            }
            if (averaged_ball.y() > maxY * kEdgeGroupHighBackawayRatio) {
                maxY = img.rows;
            }

            bool found_touching_pair = false;

            // We want to find the closest pair of balls.  
            // However, if we found a pair that was actually touching or overlapping a bit,
            // we do NOT want to continue to look at potential pairs further to the left, as those
            // ball are likely to be more heavily overlapped with other exposures, including
            // exposures that were potentially missed during the original filtering/identification.
            // Loop from right to left so we can stop once we find balls that are actually touching
            for (int first_index = (int)balls.size() - 1; first_index > 0 && !found_touching_pair; first_index--) {
                for (int second_index = first_index - 1; second_index >= 0 && !found_touching_pair; second_index--) {

                    const GolfBall& b1 = balls[first_index].ball;
                    const GolfBall& b2 = balls[second_index].ball;

                    if (use_edge_backoffs) {
                        // If the ball is too close to the edge, do not use it
                        if (b1.x() - b1.measured_radius_pixels_ < minX ||
                            b1.x() + b1.measured_radius_pixels_ > maxX ||
                            b1.y() - b1.measured_radius_pixels_ < minY ||
                            b1.y() + b1.measured_radius_pixels_ > maxY) {
                            continue;
                        }

                        if (b2.x() - b2.measured_radius_pixels_ < minX ||
                            b2.x() + b2.measured_radius_pixels_ > maxX ||
                            b2.y() - b2.measured_radius_pixels_ < minY ||
                            b2.y() + b2.measured_radius_pixels_ > maxY) {
                            continue;
                        }
                    }

                    double next_distance = balls[first_index].ball.PixelDistanceFromBall(balls[second_index].ball);

                    if (next_distance < closest_distance_so_far) {
                        closest_distance_so_far = next_distance;
                        closest_ball1 = first_index;
                        closest_ball2 = second_index;

                        // If the balls we just found are touching or overlapped, bail out
                        if (next_distance <= b1.measured_radius_pixels_ + b2.measured_radius_pixels_) {
                            found_touching_pair = true;
                            break;
                        }
                    }
                }
            }

            if (closest_ball1 == -1 || closest_ball2 == -1) {
                return false;
            }

            // TBDXXX - Hardcoded for testing some spin issues:
            // closest_ball1 = 2;  closest_ball2 = 1;

            // Reverse the ball order so that the ball on the left will be first.
            ball1 = balls[closest_ball2].ball;
            ball2 = balls[closest_ball1].ball;
            timing_interval_uS = balls[closest_ball1].time_interval_before_ball_ms;

            return true;
        }


        bool GolfSimCamera::ComputeAveragedStrobedBallData(const GolfSimCamera& camera, const GsBallsAndTimingVector& balls_and_timing,
                                                           GolfBall& output_averaged_ball) {

            std::vector<GolfBall> delta_balls;

            // Go through the second-to-last ball on the outer loop, as the inner
            // loop will take care of the next ball
            for (size_t i = 0; i < balls_and_timing.size() - 1; i++) {

                GolfBall ball1 = balls_and_timing[i].ball;

                // For each ball, pair it with all the other balls 
                // (shouldn't be more than 100 pairs)
                for (size_t j = i + 1; j < balls_and_timing.size(); j++) {

                    GolfBall ball2 = balls_and_timing[j].ball;

                    GS_LOG_MSG(error, "ComputeAveragedStrobedBallData comparing the following two balls (indexes are within the vector): Balls (" + std::to_string(i) + ", " + std::to_string(j) + ").");

                    // Ball2 will have the averaged information
                    if (!ComputeBallDeltas(ball1, ball2, camera, camera /* all_balls_camera2 */)) {
                        GS_LOG_MSG(error, "ComputeAveragedStrobedBallData failed.");
                        return false;
                    }


                    delta_balls.push_back(ball2);
                }
            }

            GolfBall::AverageBalls(delta_balls, output_averaged_ball);

            return true;
        }


        bool GolfSimCamera::FindBestTwoSpinBalls(const cv::Mat& img,
                                                const GsBallsAndTimingVector& balls_and_timing,
                                                const bool use_edge_backoffs,
                                                GolfBall& output_ball1,
                                                GolfBall& output_ball2,
                                                double& timing_interval_uS) {

            int closest_ball1 = -1;
            int closest_ball2 = -1;
            double closest_distance_so_far = 100000;

            int minX = kClosestBallPairEdgeBackoffPixels;
            int minY = kClosestBallPairEdgeBackoffPixels;
            int maxX = img.cols - kClosestBallPairEdgeBackoffPixels;
            int maxY = img.rows - kClosestBallPairEdgeBackoffPixels;

            // If MOST of the balls have x's and y's that are close to a border, however
            // we can't rule those out as our spin balls.  
            // In that case, we will move the min/max values back out to help ensure
            // that we can use those balls.
            // Of course, if use_edge_backoffs == false, we're going to ignore the
            // backoffs entirely.

            std::vector<GolfBall> balls;
            std::vector<GsBallPairAndSpinCandidateScoreElement> ball_pairs_and_scores;
            std::vector<GsBallPairAndSpinCandidateScoreElement> ball_pair_elements;

            // See header file for descrip[tions
            double kEdgeProximityScoreWeighting = 4;
            double kPairProximityScoreWeighting = 6;
            double kColorStdScoreWeighting = 4;
            double kMiddleProximityScoreWeighting = 2;
            double kLegProximityScoreWeighting = 2;
            double kRadiusSimilarityScoreWeighting = 7;


            for (size_t i = 0; i < balls_and_timing.size(); i++) { 
                const GolfBall& ball1 = balls_and_timing[i].ball;
                // Create one vector of just the balls to use for averaging
                balls.push_back(ball1);

                // For each ball, pair it with all the other balls 
                // (shouldn't be more than 100 pairs)
                for (size_t j = i + 1; j < balls_and_timing.size(); j++) {
                    const GolfBall& ball2 = balls_and_timing[j].ball;

                    GsBallPairAndSpinCandidateScoreElement ball_pair_element;

                    ball_pair_element.ball1 = ball1;
                    ball_pair_element.ball2 = ball2;

                    ball_pair_element.ball1_index = (int)i;
                    ball_pair_element.ball2_index = (int)j;

                    // See header file for descriptions
                    double edge_proximity_score = 0;
                    double pair_proximity_score = 0;
                    double color_std_score = 0;
                    double middle_proximity_score = 0;
                    double leg_proximity_score = 0;
                    double radius_similarity_score = 0;

                    edge_proximity_score = 10.0;
                    if (ball1.x() - ball1.measured_radius_pixels_ < minX ||
                        ball1.x() + ball1.measured_radius_pixels_ > maxX ||
                        ball1.y() - ball1.measured_radius_pixels_ < minY ||
                        ball1.y() + ball1.measured_radius_pixels_ > maxY) {
                        edge_proximity_score -= 5;
                    }

                    if (ball2.x() - ball2.measured_radius_pixels_ < minX ||
                        ball2.x() + ball2.measured_radius_pixels_ > maxX ||
                        ball2.y() - ball2.measured_radius_pixels_ < minY ||
                        ball2.y() + ball2.measured_radius_pixels_ > maxY) {
                        edge_proximity_score -= 5;
                    }

                    double pair_proximity = (double)ball1.PixelDistanceFromBall(ball2);
                    // Really close but not overlapping pairs should get about a 10 score

                    // If the balls are overlapping, give the result a low score - they will likely
                    // be too smudgy for a good spin calculation
                    if (pair_proximity < 0.95 * (ball1.measured_radius_pixels_ + ball2.measured_radius_pixels_)) {
                        pair_proximity_score = 0;
                    }
                    else {
                        pair_proximity_score = (10.0 * (ball1.measured_radius_pixels_ + ball2.measured_radius_pixels_)) / pair_proximity;
                    }

                    cv::Point screen_center = cv::Point(img.cols / 2, img.rows / 2);
                    cv::Point screen_edge = cv::Point(img.cols, img.rows);
                    cv::Point ball1_center = cv::Point(ball1.x(), ball1.y());
                    cv::Point ball2_center = cv::Point(ball2.x(), ball2.y());

                    double distance_from_screen_center_to_edge = CvUtils::GetDistance(screen_edge, screen_center);
                    double ball1_distance_from_screen_center = CvUtils::GetDistance(ball1_center, screen_center);
                    double ball2_distance_from_screen_center = CvUtils::GetDistance(ball2_center, screen_center);

                    double ball1_middle_proximity_score = 5.0 * ((distance_from_screen_center_to_edge - ball1_distance_from_screen_center) / distance_from_screen_center_to_edge);
                    double ball2_middle_proximity_score = 5.0 * ((distance_from_screen_center_to_edge - ball2_distance_from_screen_center) / distance_from_screen_center_to_edge);

                    middle_proximity_score = ball1_middle_proximity_score + ball2_middle_proximity_score;

                    // A large difference in color STD suggests that one ball has some overlap with
                    // something bright that will affect it's ability to be accurately filtered for spin
                    double std_diff = CvUtils::ColorDistance(ball1.std_color_, ball2.std_color_);

                    color_std_score = std::max(0.0, (30.0 - std_diff) / 3.0);

                    // The 13.6 and 8 just allows the too-big-of-a-radius-change limit to be relative to the
                    // number of pixels we have to work with.  Should be about 08.
                    radius_similarity_score = std::max(0.0, (img.rows / 13.6) - pow(5.0 * (ball1.measured_radius_pixels_ - ball2.measured_radius_pixels_), 2.0)) / 8.;

                    // Not implemented yet - TBD
                    if (GolfSimOptions::GetCommandLineOptions().golfer_orientation_ == GolferOrientation::kRightHanded) {
                        kLegProximityScoreWeighting = 0;
                    }
                    else {
                        kLegProximityScoreWeighting = 0;
                    }

                    // Store the results for the pair.
                    ball_pair_element.edge_proximity_score = edge_proximity_score;
                    ball_pair_element.pair_proximity_score = pair_proximity_score;
                    ball_pair_element.color_std_score = color_std_score;
                    ball_pair_element.middle_proximity_score = middle_proximity_score;
                    ball_pair_element.leg_proximity_score = leg_proximity_score;
                    ball_pair_element.radius_similarity_score = radius_similarity_score;

                    ball_pair_element.total_pair_score =
                        kEdgeProximityScoreWeighting * edge_proximity_score +
                        kPairProximityScoreWeighting * pair_proximity_score +
                        kColorStdScoreWeighting * color_std_score +
                        kMiddleProximityScoreWeighting * middle_proximity_score +
                        kLegProximityScoreWeighting * leg_proximity_score +
                        kRadiusSimilarityScoreWeighting * radius_similarity_score;

                    ball_pair_elements.push_back(ball_pair_element);
                }
                
            }


            if (ball_pair_elements.size() < 1) {
                GS_LOG_TRACE_MSG(warning, "Could not find any potential ball pairs for spin analysis");
                return false;
            }

            std::sort(ball_pair_elements.begin(), ball_pair_elements.end(), [](const GsBallPairAndSpinCandidateScoreElement& a, const GsBallPairAndSpinCandidateScoreElement& b)
                { return (a.total_pair_score > b.total_pair_score); });


            for (const GsBallPairAndSpinCandidateScoreElement& ball_pair_element : ball_pair_elements) {

                std::string spin_ball_score_text = "TOTAL: " + std::to_string(ball_pair_element.total_pair_score) +
                    ", edge_proximity_score: " + std::to_string(ball_pair_element.edge_proximity_score) +
                    ", pair_proximity_score: " + std::to_string(ball_pair_element.pair_proximity_score) +
                    ", color_std_score: " + std::to_string(ball_pair_element.color_std_score) +
                    ", middle_proximity_score: " + std::to_string(ball_pair_element.middle_proximity_score) +
                    ", leg_proximity_score: " + std::to_string(ball_pair_element.leg_proximity_score) +
                    ", radius_similarity_score: " + std::to_string(ball_pair_element.radius_similarity_score);

                GS_LOG_TRACE_MSG(trace, "Potential Spin Ball Combination of balls ( " + std::to_string(ball_pair_element.ball1_index) + ", " + std::to_string(ball_pair_element.ball2_index) + ") scored: " + spin_ball_score_text);
            }

            // TBD - REMOVE?  Still necessary?
            GolfBall averaged_ball;
            GolfBall::AverageBalls(balls, averaged_ball);

            // Find the balls with the two highest scores

            closest_ball1 = ball_pair_elements[0].ball1_index;
            closest_ball2 = ball_pair_elements[0].ball2_index;

            if (closest_ball1 == -1 || closest_ball2 == -1) {
                GS_LOG_TRACE_MSG(warning, "Could not find any potential ball pairs for spin analysis");
                return false;
            }

            // If necessary, reverse the ball order so that the ball on the left will be first.
            if (ball_pair_elements[0].ball1.x() > ball_pair_elements[0].ball2.x()) {
                closest_ball1 = ball_pair_elements[0].ball1_index;
                closest_ball2 = ball_pair_elements[0].ball2_index;
            }

            output_ball1 = balls[closest_ball1];
            output_ball2 = balls[closest_ball2];

            int index_of_ball_with_interval = std::max(closest_ball1, closest_ball2);

            timing_interval_uS = balls_and_timing[index_of_ball_with_interval].time_interval_before_ball_ms;

            return true;
        }

        cv::Vec2i GolfSimCamera::GetExpectedBallCenter() {

            unsigned int search_area_x = (unsigned int)std::round(camera_.resolution_x_ / 2.0);
            unsigned int search_area_y = (unsigned int)std::round(camera_.resolution_y_ / 2.0);

            if (GolfSimOptions::GetCommandLineOptions().search_center_x_ > 0) {
                search_area_x = GolfSimOptions::GetCommandLineOptions().search_center_x_;
            }

            if (GolfSimOptions::GetCommandLineOptions().search_center_y_ > 0) {
                search_area_y = GolfSimOptions::GetCommandLineOptions().search_center_y_;
            }

            return cv::Vec2i(search_area_x, search_area_y);
        }


        void GolfSimCamera::DrawFilterLines(const std::vector<cv::Vec4i>& lines, 
                                            cv::Mat& image, 
                                            const cv::Scalar& color, 
                                            const int thickness) {
            for (size_t i = 0; i < lines.size(); i++)
            {
                cv::Point pt1 = cv::Point(lines[i][0], lines[i][1]);
                cv::Point pt2 = cv::Point(lines[i][2], lines[i][3]);

                double angle = atan2(pt1.y - pt2.y, pt1.x - pt2.x);
                if (angle < 0.0) {
                    angle += 2 * CV_PI;
                }

                angle = CvUtils::RadiansToDegrees(angle);

                // std::cout << "angle = " << std::to_string(angle) << std::endl;
                // std::cout << "rho, theta = " << std::to_string(rho) << ", " << std::to_string(CvUtils::RadiansToDegrees(theta)) << "." << std::endl;

                bool is_high_priority_angle = (angle > kExternallyStrobedEnvLinesAngleLower) && (angle < kExternallyStrobedEnvLinesAngleUpper);

                double line_length = sqrt((pow((double)(pt1.x - pt2.x), 2.) + pow((double)(pt1.y - pt2.y), 2.)));

                // Ignore this line if it's not in the most-relevant angle range unless
                // it's a long line.
                if (!is_high_priority_angle  /* && line_length < kExternallyStrobedEnvMinimumHoughLineLength */) {
                    continue;
                }

                // cv::line(image, pt1, pt2, color, thickness, cv::LINE_AA);
                cv::line(image, pt1, pt2, color, thickness, cv::LINE_AA);

            }
        }

        bool GolfSimCamera::CleanExternalStrobeArtifacts(const cv::Mat &image, cv::Mat& output_image, std::vector<cv::Vec4i>& lines)
        {
            // Filtering out long lines (usually of the golf shaft)


            int h = image.rows;
            int w = image.cols;

            cv::Mat image_gray;
            cv::Mat cannyOutput;

            cv::cvtColor(image, image_gray, cv::COLOR_BGR2GRAY);

            cv::Scalar black_color{ 0,0,0 };
            cv::Scalar white_color{ 255,255,255 };
            cv::Scalar red_color{ 0 ,0, 255 };

            cv::GaussianBlur(image_gray, image_gray, cv::Size(kExternallyStrobedEnvPreCannyBlurSize, kExternallyStrobedEnvPreCannyBlurSize), 0);

            // Get a good picture of the edges of the balls.  Will probably have way too many shaft lines
            cv::Mat cannyOutput_for_balls;
            cv::Canny(image_gray, cannyOutput_for_balls, kExternallyStrobedEnvCannyLower, kExternallyStrobedEnvCannyUpper);


            LoggingTools::DebugShowImage("Initial cannyOutput", cannyOutput_for_balls);

            if (kExternallyStrobedEnvPreHoughBlurSize > 0) {
                if (kExternallyStrobedEnvPreHoughBlurSize % 2 != 1) {
                    kExternallyStrobedEnvPreHoughBlurSize++;
                }
                cv::GaussianBlur(cannyOutput_for_balls, cannyOutput_for_balls, cv::Size(kExternallyStrobedEnvPreHoughBlurSize, kExternallyStrobedEnvPreHoughBlurSize), 0);
            }

            LoggingTools::DebugShowImage("Post-Blur cannyOutput", cannyOutput_for_balls);

            output_image = cannyOutput_for_balls;



            // Color Filtering

            /****
            GsColorTriplet lowerHsv{ (float)kExternallyStrobedEnvFilterHsvLowerH, (float)kExternallyStrobedEnvFilterHsvLowerS, (float)kExternallyStrobedEnvFilterHsvLowerV };
            GsColorTriplet upperHsv{ (float)kExternallyStrobedEnvFilterHsvUpperH, (float)kExternallyStrobedEnvFilterHsvUpperS, (float)kExternallyStrobedEnvFilterHsvUpperV };

            cv::Mat hsvImage;
            cv::cvtColor(image, hsvImage, cv::COLOR_BGR2HSV);

            cv::Mat color_mask_image = BallImageProc::GetColorMaskImage(hsvImage, lowerHsv, upperHsv);

            cv::dilate(color_mask_image, color_mask_image, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(1, 1)), cv::Point(-1, -1), 1);


            // LoggingTools::DebugShowImage("External Strobe Color Mask- color_mask_image", color_mask_image);

            cv::bitwise_and(output_image, color_mask_image, output_image);
            // Apply the mask to the output image
            
            cv::Mat dest = output_image.clone();

            std::vector<cv::Mat> output_image_planes(3);
            cv::split(dest, output_image_planes);  // now we have the L image in lab_planes[0]

            cv::bitwise_and(output_image_planes[0], color_mask_image, output_image_planes[0]);
            cv::bitwise_and(output_image_planes[1], color_mask_image, output_image_planes[1]);
            cv::bitwise_and(output_image_planes[2], color_mask_image, output_image_planes[2]);

            cv::merge(output_image_planes, output_image);
            ***/

            if (kExternallyStrobedEnvBottomIgnoreHeight > 0) {
                cv::Rect floor_blackout_area{ 0, h - kExternallyStrobedEnvBottomIgnoreHeight, w, h };
                cv::rectangle(output_image, floor_blackout_area.tl(), floor_blackout_area.br(), black_color, cv::FILLED);
            }

            // LoggingTools::DebugShowImage("External Strobe Final Result Image", output_image);

            return true;
        }

}



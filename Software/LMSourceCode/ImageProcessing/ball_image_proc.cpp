/*****************************************************************//**
 * \file   ball_image_proc.cpp
 * \brief  Handles most of the image processing related to ball-identification 
 * 
 * \author PiTrac
 * \date   February 2025
 *********************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#include <ranges>
#include <algorithm>
#include <vector>
#include <chrono>
#include <fstream>
#include "gs_format_lib.h"

#include <boost/timer/timer.hpp>
#include <boost/math/special_functions/erf.hpp>
#include <boost/circular_buffer.hpp>
#include <opencv2/photo.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/core/cvdef.h>

#include "ball_image_proc.h"
#include "logging_tools.h"
#include "cv_utils.h"
#include "gs_config.h"
#include "gs_options.h"
#include "gs_ui_system.h"
#include "EllipseDetectorCommon.h"
#include "EllipseDetectorYaed.h"

// Edge detection
#include "ED.h"
#include "EDPF.h"
#include "EDColor.h"

namespace golf_sim {

    // Currently, equalizing the brightness of the input images appears to help the results
#define GS_USING_IMAGE_EQ
#define DONT__PERFORM_FINAL_TARGETTED_BALL_ID  // Remove DONT to perform a final, targetted refinement of the ball circle identification
#define DONT__USE_ELLIPSES_FOR_FINAL_ID    

    const int MIN_BALL_CANDIDATE_RADIUS = 10;

    // Balls with an average color that is too far from the searched-for color will not be considered
    // good candidates.The tolerance is based on a Euclidian distance. See differenceRGB in CvUtils module.
    // The tolerance is relative to the closest - in - RGB - value candidate.So if the "best" candidate ball is,
    // for example, 100 away from the expected color, than any balls with a RGB difference of greater than
    // 100 + CANDIDATE_BALL_COLOR_TOLERANCE will be excluded.
    const int CANDIDATE_BALL_COLOR_TOLERANCE = 50;

    const bool PREBLUR_IMAGE = false;
    const bool IS_COLOR_MASKING = false;   // Probably not effective on IR pictures

    // May be necessary in brighter environments - TBD
    const bool FINAL_BLUR = true;

    const int MAX_FINAL_CANDIDATE_BALLS_TO_SHOW = 4;


    // See places of use for explanation of these constants
    static const double kColorMaskWideningAmount = 35;
    static const double kEllipseColorMaskWideningAmount = 35;
    static const bool kSerializeOpsForDebug = false;

    int BallImageProc::kCoarseXRotationDegreesIncrement = 6;
    int BallImageProc::kCoarseXRotationDegreesStart = -42;
    int BallImageProc::kCoarseXRotationDegreesEnd = 42;
    int BallImageProc::kCoarseYRotationDegreesIncrement = 5;
    int BallImageProc::kCoarseYRotationDegreesStart = -30;
    int BallImageProc::kCoarseYRotationDegreesEnd = 30;
    int BallImageProc::kCoarseZRotationDegreesIncrement = 6;
    int BallImageProc::kCoarseZRotationDegreesStart = -50;
    int BallImageProc::kCoarseZRotationDegreesEnd = 60;

    double BallImageProc::kPlacedBallCannyLower;
    double BallImageProc::kPlacedBallCannyUpper;
    double BallImageProc::kPlacedBallStartingParam2 = 40;
    double BallImageProc::kPlacedBallMinParam2 = 30;
    double BallImageProc::kPlacedBallMaxParam2 = 60;
    double BallImageProc::kPlacedBallCurrentParam1 = 120.0;
    double BallImageProc::kPlacedBallParam2Increment = 4;

    int BallImageProc::kPlacedMinHoughReturnCircles = 1;
    int BallImageProc::kPlacedMaxHoughReturnCircles = 4;
    double BallImageProc::kStrobedBallsCannyLower = 50;
    double BallImageProc::kStrobedBallsCannyUpper = 110;


    int BallImageProc::kStrobedBallsMaxHoughReturnCircles = 12;
    int BallImageProc::kStrobedBallsMinHoughReturnCircles = 1;

    int BallImageProc::kStrobedBallsPreCannyBlurSize = 5;
    int BallImageProc::kStrobedBallsPreHoughBlurSize = 13;
    double BallImageProc::kStrobedBallsStartingParam2 = 40;
    double BallImageProc::kStrobedBallsMinParam2 = 30;
    double BallImageProc::kStrobedBallsMaxParam2 = 60;
    double BallImageProc::kStrobedBallsCurrentParam1 = 120.0;
    double BallImageProc::kStrobedBallsHoughDpParam1 = 1.5;
    double BallImageProc::kStrobedBallsParam2Increment = 4;

    bool  BallImageProc::kStrobedBallsUseAltHoughAlgorithm = true;
    double BallImageProc::kStrobedBallsAltCannyLower = 35;
    double BallImageProc::kStrobedBallsAltCannyUpper = 70;
    int BallImageProc::kStrobedBallsAltPreCannyBlurSize = 11;
    int BallImageProc::kStrobedBallsAltPreHoughBlurSize = 16;
    double BallImageProc::kStrobedBallsAltStartingParam2 = 0.95;
    double BallImageProc::kStrobedBallsAltMinParam2 = 0.6;
    double BallImageProc::kStrobedBallsAltMaxParam2 = 1.0;
    double BallImageProc::kStrobedBallsAltCurrentParam1 = 130.0;
    double BallImageProc::kStrobedBallsAltHoughDpParam1 = 1.5;
    double BallImageProc::kStrobedBallsAltParam2Increment = 0.05;

    bool BallImageProc::kUseCLAHEProcessing;
    int BallImageProc::kCLAHEClipLimit;
    int BallImageProc::kCLAHETilesGridSize;

    double BallImageProc::kPuttingBallStartingParam2 = 40;
    double BallImageProc::kPuttingBallMinParam2 = 30;
    double BallImageProc::kPuttingBallMaxParam2 = 60;
    double BallImageProc::kPuttingBallCurrentParam1 = 120.0;
    double BallImageProc::kPuttingBallParam2Increment = 4;
    int BallImageProc::kPuttingMaxHoughReturnCircles = 12;
    int BallImageProc::kPuttingMinHoughReturnCircles = 1;
    double BallImageProc::kPuttingHoughDpParam1 = 1.5;

    double BallImageProc::kExternallyStrobedEnvCannyLower = 35;
    double BallImageProc::kExternallyStrobedEnvCannyUpper = 80;

    double BallImageProc::kExternallyStrobedEnvCurrentParam1 = 130.0;
    double BallImageProc::kExternallyStrobedEnvMinParam2 = 28;
    double BallImageProc::kExternallyStrobedEnvMaxParam2 = 100;
    double BallImageProc::kExternallyStrobedEnvStartingParam2 = 65;
    double BallImageProc::kExternallyStrobedEnvNarrowingParam2 = 0.6;
    double BallImageProc::kExternallyStrobedEnvNarrowingDpParam = 1.1;
    double BallImageProc::kExternallyStrobedEnvParam2Increment = 4;
    int BallImageProc::kExternallyStrobedEnvMinHoughReturnCircles = 3;
    int BallImageProc::kExternallyStrobedEnvMaxHoughReturnCircles = 20;
    int BallImageProc::kExternallyStrobedEnvPreHoughBlurSize = 11;
    int BallImageProc::kExternallyStrobedEnvPreCannyBlurSize = 3;
    double BallImageProc::kExternallyStrobedEnvHoughDpParam1 = 1.0;
    int BallImageProc::kExternallyStrobedEnvNarrowingPreCannyBlurSize = 3;
    int BallImageProc::kExternallyStrobedEnvNarrowingPreHoughBlurSize = 9;
    int BallImageProc::kExternallyStrobedEnvMinimumSearchRadius = 60;
    int BallImageProc::kExternallyStrobedEnvMaximumSearchRadius = 80;

    bool BallImageProc::kUseDynamicRadiiAdjustment = true;
    int BallImageProc::kNumberRadiiToAverageForDynamicAdjustment = 3;
    double BallImageProc::kStrobedNarrowingRadiiMinRatio = 0.8;
    double BallImageProc::kStrobedNarrowingRadiiMaxRatio = 1.2;
    double BallImageProc::kStrobedNarrowingRadiiDpParam = 1.8;
    double BallImageProc::kStrobedNarrowingRadiiParam2 = 100.0;


    double BallImageProc::kPlacedNarrowingRadiiMinRatio = 0.9;
    double BallImageProc::kPlacedNarrowingRadiiMaxRatio = 1.1;
    double BallImageProc::kPlacedNarrowingStartingParam2 = 80.0;
    double BallImageProc::kPlacedNarrowingRadiiDpParam = 2.0;
    double BallImageProc::kPlacedNarrowingParam1 = 130.0;

    int BallImageProc::kPlacedPreCannyBlurSize = 5;
    int BallImageProc::kPlacedPreHoughBlurSize = 11;
    int BallImageProc::kPuttingPreHoughBlurSize = 9;


    bool BallImageProc::kLogIntermediateSpinImagesToFile = false;
    double BallImageProc::kPlacedBallHoughDpParam1 = 1.5;

    bool BallImageProc::kUseBestCircleRefinement = false;
    bool BallImageProc::kUseBestCircleLargestCircle = false;

    double BallImageProc::kBestCircleCannyLower = 55;
    double BallImageProc::kBestCircleCannyUpper = 110;
    int BallImageProc::kBestCirclePreCannyBlurSize = 5;
    int BallImageProc::kBestCirclePreHoughBlurSize = 13;
    double BallImageProc::kBestCircleParam1 = 120.;
    double BallImageProc::kBestCircleParam2 = 35.;
    double BallImageProc::kBestCircleHoughDpParam1 = 1.5;

    double BallImageProc::kExternallyStrobedBestCircleCannyLower = 55;
    double BallImageProc::kExternallyStrobedBestCircleCannyUpper = 110;
    int BallImageProc::kExternallyStrobedBestCirclePreCannyBlurSize = 5;
    int BallImageProc::kExternallyStrobedBestCirclePreHoughBlurSize = 13;
    double BallImageProc::kExternallyStrobedBestCircleParam1 = 120.;
    double BallImageProc::kExternallyStrobedBestCircleParam2 = 35.;
    double BallImageProc::kExternallyStrobedBestCircleHoughDpParam1 = 1.5;

    bool BallImageProc::kExternallyStrobedUseCLAHEProcessing = true;
    int BallImageProc::kExternallyStrobedCLAHEClipLimit = 6;
    int BallImageProc::kExternallyStrobedCLAHETilesGridSize = 6;

    double BallImageProc::kBestCircleIdentificationMinRadiusRatio = 0.85;
    double BallImageProc::kBestCircleIdentificationMaxRadiusRatio = 1.10;

    int BallImageProc::kGaborMaxWhitePercent = 44; // Nominal 46;
    int BallImageProc::kGaborMinWhitePercent = 38; // Nominal 40;

    // ONNX Detection Configuration
    // TODO: Fix defaults or remove these entirely
    std::string BallImageProc::kDetectionMethod = "legacy";
    std::string BallImageProc::kBallPlacementDetectionMethod = "legacy";
    // Default ONNX model path - can be overridden by config file or (more likely) the PITRAC_ROOT environment variable
    #ifdef _WIN32
    std::string BallImageProc::kONNXModelPath = "../../Software/LMSourceCode/ml_models/pitrac-ball-detection-09-25-25/weights/best.onnx";
    #else
    std::string BallImageProc::kONNXModelPath = "../ml_models/pitrac-ball-detection-09-25-25/weights/best.onnx";
    #endif
    float BallImageProc::kONNXConfidenceThreshold = 0.5f;
    float BallImageProc::kONNXNMSThreshold = 0.4f;
    int BallImageProc::kONNXInputSize = 640;
    int BallImageProc::kSAHISliceHeight = 320;
    int BallImageProc::kSAHISliceWidth = 320;
    float BallImageProc::kSAHIOverlapRatio = 0.2f;
    std::string BallImageProc::kONNXDeviceType = "CPU";

    // Dual-Backend Configuration
    std::string BallImageProc::kONNXBackend = "onnxruntime";  // Default to ONNX Runtime
    bool BallImageProc::kONNXRuntimeAutoFallback = true;     // Enable automatic fallback
    int BallImageProc::kONNXRuntimeThreads = 4;              // ARM64 optimized default

    // ONNX Runtime detector instance - replaces all static ONNX members
    std::unique_ptr<ONNXRuntimeDetector> BallImageProc::onnx_detector_;
    std::atomic<bool> BallImageProc::onnx_detector_initialized_{false};
    std::mutex BallImageProc::onnx_detector_mutex_;

    cv::dnn::Net BallImageProc::yolo_model_;
    bool BallImageProc::yolo_model_loaded_ = false;
    std::mutex BallImageProc::yolo_model_mutex_;

    // Pre-allocated buffers - static members
    cv::Mat BallImageProc::yolo_input_buffer_;
    cv::Mat BallImageProc::yolo_letterbox_buffer_;
    cv::Mat BallImageProc::yolo_resized_buffer_;
    cv::Mat BallImageProc::yolo_blob_buffer_;
    std::vector<cv::Rect> BallImageProc::yolo_detection_boxes_;
    std::vector<float> BallImageProc::yolo_detection_confidences_;
    std::vector<cv::Mat> BallImageProc::yolo_outputs_;


    BallImageProc::BallImageProc() {
        min_ball_radius_ = -1;
        max_ball_radius_ = -1;

        // The following constants are only used internal to the GolfSimCamera class, and so can be initialized in the constructor
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseXRotationDegreesIncrement", kCoarseXRotationDegreesIncrement);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseXRotationDegreesStart", kCoarseXRotationDegreesStart);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseXRotationDegreesEnd", kCoarseXRotationDegreesEnd);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseYRotationDegreesIncrement", kCoarseYRotationDegreesIncrement);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseYRotationDegreesStart", kCoarseYRotationDegreesStart);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseYRotationDegreesEnd", kCoarseYRotationDegreesEnd);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseZRotationDegreesIncrement", kCoarseZRotationDegreesIncrement);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseZRotationDegreesStart", kCoarseZRotationDegreesStart);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseZRotationDegreesEnd", kCoarseZRotationDegreesEnd);

        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kGaborMinWhitePercent", kGaborMinWhitePercent);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kGaborMaxWhitePercent", kGaborMaxWhitePercent);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallCannyLower", kPlacedBallCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallCannyUpper", kPlacedBallCannyUpper);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallStartingParam2", kPlacedBallStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallMinParam2", kPlacedBallMinParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallMaxParam2", kPlacedBallMaxParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallCurrentParam1", kPlacedBallCurrentParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallParam2Increment", kPlacedBallParam2Increment);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedMinHoughReturnCircles", kPlacedMinHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedMaxHoughReturnCircles", kPlacedMaxHoughReturnCircles);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsCannyLower", kStrobedBallsCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsCannyUpper", kStrobedBallsCannyUpper);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsPreCannyBlurSize", kStrobedBallsPreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsPreHoughBlurSize", kStrobedBallsPreHoughBlurSize);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsStartingParam2", kStrobedBallsStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsMinParam2", kStrobedBallsMinParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsMaxParam2", kStrobedBallsMaxParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsCurrentParam1", kStrobedBallsCurrentParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsParam2Increment", kStrobedBallsParam2Increment);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsMinHoughReturnCircles", kStrobedBallsMinHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsMaxHoughReturnCircles", kStrobedBallsMaxHoughReturnCircles);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsUseAltHoughAlgorithm", kStrobedBallsUseAltHoughAlgorithm);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltCannyLower", kStrobedBallsAltCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltCannyUpper", kStrobedBallsAltCannyUpper);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltPreCannyBlurSize", kStrobedBallsAltPreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltPreHoughBlurSize", kStrobedBallsAltPreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltStartingParam2", kStrobedBallsAltStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltMinParam2", kStrobedBallsAltMinParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltMaxParam2", kStrobedBallsAltMaxParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltCurrentParam1", kStrobedBallsAltCurrentParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltHoughDpParam1", kStrobedBallsAltHoughDpParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltParam2Increment", kStrobedBallsAltParam2Increment);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kUseCLAHEProcessing", kUseCLAHEProcessing);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kCLAHEClipLimit", kCLAHEClipLimit);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kCLAHETilesGridSize", kCLAHETilesGridSize);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingBallStartingParam2", kPuttingBallStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingBallMinParam2", kPuttingBallMinParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingBallMaxParam2", kPuttingBallMaxParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingBallCurrentParam1", kPuttingBallCurrentParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingBallParam2Increment", kPuttingBallParam2Increment);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingMinHoughReturnCircles", kPuttingMinHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingMaxHoughReturnCircles", kPuttingMaxHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingHoughDpParam1", kPuttingHoughDpParam1);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvCurrentParam1", kExternallyStrobedEnvCurrentParam1);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMaxParam2", kExternallyStrobedEnvMaxParam2);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvStartingParam2", kExternallyStrobedEnvStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvNarrowingParam2", kExternallyStrobedEnvNarrowingParam2);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvNarrowingDpParam", kExternallyStrobedEnvNarrowingDpParam);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvNarrowingPreCannyBlurSize", kExternallyStrobedEnvNarrowingPreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvNarrowingPreHoughBlurSize", kExternallyStrobedEnvNarrowingPreHoughBlurSize);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvParam2Increment", kExternallyStrobedEnvParam2Increment);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMinHoughReturnCircles", kExternallyStrobedEnvMinHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMaxHoughReturnCircles", kExternallyStrobedEnvMaxHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvPreHoughBlurSize", kExternallyStrobedEnvPreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvPreCannyBlurSize", kExternallyStrobedEnvPreCannyBlurSize);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCircleCannyLower", kExternallyStrobedBestCircleCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCircleCannyUpper", kExternallyStrobedBestCircleCannyUpper);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCirclePreCannyBlurSize", kExternallyStrobedBestCirclePreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCirclePreHoughBlurSize", kExternallyStrobedBestCirclePreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCircleParam1", kExternallyStrobedBestCircleParam1);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCircleParam2", kExternallyStrobedBestCircleParam2);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCircleHoughDpParam1", kExternallyStrobedBestCircleHoughDpParam1);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedUseCLAHEProcessing", kExternallyStrobedUseCLAHEProcessing);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedCLAHEClipLimit", kExternallyStrobedCLAHEClipLimit);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedCLAHETilesGridSize", kExternallyStrobedCLAHETilesGridSize);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvHoughDpParam1", kExternallyStrobedEnvHoughDpParam1);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMaximumSearchRadius", kExternallyStrobedEnvMaximumSearchRadius);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMinimumSearchRadius", kExternallyStrobedEnvMinimumSearchRadius);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedPreHoughBlurSize", kPlacedPreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedPreCannyBlurSize", kPlacedPreCannyBlurSize);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsPreHoughBlurSize", kStrobedBallsPreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingPreHoughBlurSize", kPuttingPreHoughBlurSize);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallHoughDpParam1", kPlacedBallHoughDpParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsHoughDpParam1", kStrobedBallsHoughDpParam1);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kUseBestCircleRefinement", kUseBestCircleRefinement);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kUseBestCircleLargestCircle", kUseBestCircleLargestCircle);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleCannyLower", kBestCircleCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleCannyUpper", kBestCircleCannyUpper);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCirclePreCannyBlurSize", kBestCirclePreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCirclePreHoughBlurSize", kBestCirclePreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleParam1", kBestCircleParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleParam2", kBestCircleParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleHoughDpParam1", kBestCircleHoughDpParam1);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleIdentificationMinRadiusRatio", kBestCircleIdentificationMinRadiusRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleIdentificationMaxRadiusRatio", kBestCircleIdentificationMaxRadiusRatio);


        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kUseDynamicRadiiAdjustment", kUseDynamicRadiiAdjustment);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kNumberRadiiToAverageForDynamicAdjustment", kNumberRadiiToAverageForDynamicAdjustment);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedNarrowingRadiiMinRatio", kStrobedNarrowingRadiiMinRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedNarrowingRadiiMaxRatio", kStrobedNarrowingRadiiMaxRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedNarrowingRadiiDpParam", kStrobedNarrowingRadiiDpParam);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedNarrowingRadiiParam2", kStrobedNarrowingRadiiParam2);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedNarrowingRadiiMinRatio", kPlacedNarrowingRadiiMinRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedNarrowingRadiiMaxRatio", kPlacedNarrowingRadiiMaxRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedNarrowingStartingParam2", kPlacedNarrowingStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedNarrowingRadiiDpParam", kPlacedNarrowingRadiiDpParam);

        // ONNX Detection Configuration values will be loaded later via LoadConfigurationValues()
        // which is called after the JSON config file has been loaded in main()

        GolfSimConfiguration::SetConstant("gs_config.logging.kLogIntermediateSpinImagesToFile", kLogIntermediateSpinImagesToFile);
        
        // Preload model at startup if using experimental detection for either ball placement or flight
        if (kDetectionMethod == "experimental" || kDetectionMethod == "experimental_sahi" ||
            kBallPlacementDetectionMethod == "experimental") {
            GS_LOG_MSG(info, "Detection method is '" + kDetectionMethod + "' / Placement method is '" + kBallPlacementDetectionMethod + "', preloading YOLO model at startup...");

            // Try ONNX Runtime first if configured
            if (kONNXBackend == "onnxruntime") {
                if (PreloadONNXRuntimeModel()) {
                    GS_LOG_MSG(info, "ONNX Runtime model preloaded successfully - first detection will be fast!");
                } else {
                    GS_LOG_MSG(warning, "Failed to preload ONNX Runtime model");
                    if (kONNXRuntimeAutoFallback) {
                        GS_LOG_MSG(info, "Auto-fallback enabled, attempting to preload OpenCV DNN model...");
                        if (PreloadYOLOModel()) {
                            GS_LOG_MSG(info, "OpenCV DNN fallback model preloaded successfully!");
                        } else {
                            GS_LOG_MSG(warning, "Failed to preload both ONNX Runtime and OpenCV DNN models");
                        }
                    }
                }
            } else {
                // Use OpenCV DNN backend
                if (PreloadYOLOModel()) {
                    GS_LOG_MSG(info, "OpenCV DNN model preloaded successfully - first detection will be fast!");
                } else {
                    GS_LOG_MSG(warning, "Failed to preload OpenCV DNN model - will load on first detection");
                }
            }
        }
    }

    BallImageProc::~BallImageProc() {
        // Cleanup is handled by static cleanup, since model is shared across instances
        // Call CleanupONNXRuntime() only on program exit, not per-instance destruction
    }

    /**
     * Given a gray-scale image and a ball search mode (e.g., kStrobed), this function applies
     * CLAHE processing to improve the contrast and edge definition of the balls.  It then
     * applies a Guassian blur and edge detection to the image.
     * 
     * \param search_image  The image to be processed.
     * \param search_mode  Currently can be only kStrobed or kExternallyStrobed
     * \return True on success.
     */
    bool BallImageProc::PreProcessStrobedImage( cv::Mat& search_image, 
                                                BallSearchMode search_mode) {

        GS_LOG_TRACE_MSG(trace, "PreProcessStrobedImage");

        if (search_image.empty()) {
            GS_LOG_MSG(error, "PreProcessStrobedImage called with no image to work with (search_image)");
            return false;
        }

        // setup CLAHE processing dependent on PiTrac-only strobing or externally-strobed
        
        bool use_clahe_processing = true;
        int clahe_tiles_grid_size = -1;
        int clahe_clip_limit = -1;

        if (search_mode == kStrobed) {
            use_clahe_processing = kUseCLAHEProcessing;
            clahe_tiles_grid_size = kCLAHETilesGridSize;
            clahe_clip_limit = kCLAHEClipLimit;
        }
        else if (search_mode == kExternallyStrobed) {
            use_clahe_processing = kExternallyStrobedUseCLAHEProcessing;
            clahe_tiles_grid_size = kExternallyStrobedCLAHETilesGridSize;
            clahe_clip_limit = kExternallyStrobedCLAHEClipLimit;
        }
        else {
            GS_LOG_MSG(error, "PreProcessStrobedImage called with invalid search_mode)");
            return false;
        }

        // Create a CLAHE object

        if (use_clahe_processing) {
            cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();

            // Set CLAHE parameters

            if (clahe_tiles_grid_size < 1) {
                clahe_tiles_grid_size = 1;
                GS_LOG_MSG(warning, "clahe_tiles_grid_size was < 1 - Resetting to 1.");
            }
            if (clahe_clip_limit < 1) {
                clahe_clip_limit = 1;
                GS_LOG_MSG(warning, "kCLAHEClipLimit was < 1 - Resetting to 1.");
            }

            GS_LOG_TRACE_MSG(trace, "Using CLAHE Pre-processing with GridSize = " + std::to_string(clahe_tiles_grid_size) +
                ", ClipLimit = " + std::to_string(clahe_clip_limit));

            clahe->setClipLimit(clahe_clip_limit);
            clahe->setTilesGridSize(cv::Size(clahe_tiles_grid_size, clahe_tiles_grid_size));

            // Apply CLAHE
            cv::Mat equalizedImage;
            clahe->apply(search_image, search_image);

            LoggingTools::DebugShowImage(image_name_ + "  Strobed Ball Image - After CLAHE equalization", search_image);
        }

        double canny_lower = 0.0;
        double canny_upper = 0.0;
        int pre_canny_blur_size = 0;
        int pre_hough_blur_size = 0;

        if (search_mode == kStrobed) {
            if (kStrobedBallsUseAltHoughAlgorithm) {
                canny_lower = kStrobedBallsAltCannyLower;
                canny_upper = kStrobedBallsAltCannyUpper;
                pre_canny_blur_size = kStrobedBallsAltPreCannyBlurSize;
                pre_hough_blur_size = kStrobedBallsAltPreHoughBlurSize;
            }
            else {
                canny_lower = kStrobedBallsCannyLower;
                canny_upper = kStrobedBallsCannyUpper;
                pre_canny_blur_size = kStrobedBallsPreCannyBlurSize;
                pre_hough_blur_size = kStrobedBallsPreHoughBlurSize;
            }
        }
        else if (search_mode == kExternallyStrobed) {
            canny_lower = kExternallyStrobedEnvCannyLower;
            canny_upper = kExternallyStrobedEnvCannyUpper;
            pre_canny_blur_size = kExternallyStrobedEnvPreCannyBlurSize;
            pre_hough_blur_size = kExternallyStrobedEnvPreHoughBlurSize;
        }

        // The size for the blur must be odd - force it up in value by 1 if necessary
        if (pre_canny_blur_size > 0) {
            if (pre_canny_blur_size % 2 != 1) {
                pre_canny_blur_size++;
            }
        }

        if (pre_hough_blur_size > 0) {
            if (pre_hough_blur_size % 2 != 1) {
                pre_hough_blur_size++;
            }
        }


        GS_LOG_MSG(trace, "Main HoughCircle Image Prep - Performing Pre-Hough Blur and Canny for kStrobed mode.");
        GS_LOG_MSG(trace, "  Blur Parameters are: pre_canny_blur_size = " + std::to_string(pre_canny_blur_size) +
            ", pre_hough_blur_size " + std::to_string(pre_hough_blur_size));
        GS_LOG_MSG(trace, "  Canny Parameters are: canny_lower = " + std::to_string(canny_lower) +
            ", canny_upper " + std::to_string(canny_upper));


        if (pre_canny_blur_size > 0) {
            cv::GaussianBlur(search_image, search_image, cv::Size(pre_canny_blur_size, pre_canny_blur_size), 0);
        }
        else {
            GS_LOG_TRACE_MSG(trace, "Skipping pre-Canny Blur");
        }

        // TBD - REMOVED THIS FOR NOW - IT DOESN'T SEEM TO HELP
        for (int i = 0; i < 0; i++) {
            cv::erode(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
            cv::dilate(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
        }

        LoggingTools::DebugShowImage(image_name_ + "  Strobed Ball Image - Ready for Edge Detection", search_image);

        cv::Mat cannyOutput_for_balls;
        if (search_mode == kExternallyStrobed && pre_canny_blur_size == 0) {
            // Don't do the Canny at all if the blur size is zero and we're in comparison mode
            cannyOutput_for_balls = search_image.clone();
        }
        else {
            cv::Canny(search_image, cannyOutput_for_balls, canny_lower, canny_upper);
        }

        LoggingTools::DebugShowImage(image_name_ + "  cannyOutput_for_balls", cannyOutput_for_balls);

        // Blur the lines-only image back to the search_image that the code below uses
        cv::GaussianBlur(cannyOutput_for_balls, search_image, cv::Size(pre_hough_blur_size, pre_hough_blur_size), 0);   // Nominal is 7x7

        return true;
    }

    // Given a picture, see if we can find the golf ball somewhere in that picture.
    // Should be much more successful if called with a calibrated golf ball so that the code has
    // some hints about where to look.
    // Returns a new GolfBall object iff success. 
    bool BallImageProc::GetBall(const cv::Mat& rgbImg, 
                                const GolfBall& baseBallWithSearchParams, 
                                std::vector<GolfBall> &return_balls, 
                                cv::Rect& expectedBallArea, 
                                BallSearchMode search_mode,
                                bool chooseLargestFinalBall,
                                bool report_find_failures) {

        GS_LOG_TRACE_MSG(trace, "GetBall called with PREBLUR_IMAGE = " + std::to_string(PREBLUR_IMAGE) + " IS_COLOR_MASKING = " + 
                    std::to_string(IS_COLOR_MASKING) + " FINAL_BLUR = " + std::to_string(FINAL_BLUR) + " search_mode = " + std::to_string(search_mode));

        if (rgbImg.empty()) {
            GS_LOG_MSG(error, "GetBall called with no image to work with (rgbImg)");
            return false;
        }

        // *** ONNX DETECTION INTEGRATION - Process through full trajectory analysis pipeline ***
        if (kDetectionMethod == "experimental" || kDetectionMethod == "experimental_sahi") {
            GS_LOG_TRACE_MSG(trace, "ONNX Detection Mode - Using ONNX detection, will process through full trajectory analysis");
            
            std::vector<GsCircle> onnx_circles;
            if (DetectBallsONNX(rgbImg, search_mode, onnx_circles)) {
                GS_LOG_TRACE_MSG(trace, "ONNX detected " + std::to_string(onnx_circles.size()) + " circles - converting to GolfBall objects");
                
                // Convert GsCircle results to GolfBall objects for trajectory analysis
                return_balls.clear();
                for (size_t i = 0; i < onnx_circles.size(); ++i) {
                    GolfBall ball;
                    ball.quality_ranking = static_cast<int>(i); // ONNX confidence-based ranking
                    ball.set_circle(onnx_circles[i]);
                    ball.ball_color_ = GolfBall::BallColor::kONNXDetected; // Mark as ONNX-detected
                    ball.measured_radius_pixels_ = onnx_circles[i][2];
                    ball.radius_at_calibration_pixels_ = baseBallWithSearchParams.radius_at_calibration_pixels_;
                    
                    // Set color info - ONNX doesn't analyze color but we need placeholders
                    ball.average_color_ = baseBallWithSearchParams.average_color_;
                    ball.median_color_ = baseBallWithSearchParams.average_color_;
                    ball.std_color_ = GsColorTriplet(0, 0, 0); // Zero std indicates no color analysis
                    
                    return_balls.push_back(ball);
                }
                
                GS_LOG_TRACE_MSG(trace, "ONNX detection complete - returning " + 
                               std::to_string(return_balls.size()) + " balls for trajectory analysis");
                return !return_balls.empty();
            } else {
                GS_LOG_MSG(warning, "ONNX detection failed - no balls found");
                return false;
            }
        }

        GS_LOG_TRACE_MSG(trace, "Using legacy HoughCircles detection");
        GS_LOG_TRACE_MSG(trace, "Looking for a ball with color{ " + LoggingTools::FormatGsColorTriplet(baseBallWithSearchParams.average_color_));
        LoggingTools::DebugShowImage(image_name_ + "  rgbImg", rgbImg);

        // Blur the image to reduce noise - TBD - Would medianBlur be better ?
        // img_blur = cv::medianBlur(grayImage, 5)
        // Blur the image before trying to identify circles (if desired)
        cv::Mat blurImg = area_mask_image_.clone();

        // This seems touchy, too.  Nominal is 7 right now.
        if (PREBLUR_IMAGE) {
            cv::GaussianBlur(rgbImg, blurImg, cv::Size(7, 7), 0);  // nominal was 11x11
            LoggingTools::DebugShowImage(image_name_ + "  Pre-blurred image", blurImg);
        }
        else {
            blurImg = rgbImg.clone();
        }


        // construct a colorMask for the expected ball color range
        // Note - We want to UNDER-colorMask if anything.Just get rid of stuff that is
        // pretty certainly NOT the golf ball
        // Need an HSV image to work with the HSV-based masking function
        int stype = blurImg.type();

        if (stype == CV_8U) {
            GS_LOG_MSG(error, "GetBall called with a 1-channel (grayscale?) image.  Expecting 3 channel RGB");
            return false;
        }


        // We will create our own colorMask if we don't have one already
        // We will not do anything with the areaMask(other than to apply it further below if it exists)
        if (color_mask_image_.empty()) {

            cv::Mat hsvImage;
            cv::cvtColor(blurImg, hsvImage, cv::COLOR_BGR2HSV);

            // Save the colorMask for later debugging as well as for use below
            color_mask_image_ = GetColorMaskImage(hsvImage, baseBallWithSearchParams);
        }

        // LoggingTools::DebugShowImage(image_name_ + "  cv::GaussianBlur(...) hsvImage", hsvImage);
        // LoggingTools::DebugShowImage(image_name_ + "  color_mask_image_", color_mask_image_);

        // Perform a Hough conversion to identify circles or near-circles

        // Convert the blurred version of the original image to required gray-scale for Hough Transform circle detection
        cv::Mat grayImage;
        cv::cvtColor(blurImg, grayImage, cv::COLOR_BGR2GRAY);

        // LoggingTools::DebugShowImage(image_name_ + "  gray image (as well as the result if no colorMasking)", grayImage);

        cv::Mat search_image = cv::Mat::zeros(grayImage.size(), grayImage.type());

        // Bitwise-AND the colorMask and original image
        // NOTE - THIS COLOR MASKING MAY ACTUALLY BE HURTING US!!!
        if (IS_COLOR_MASKING) {
            cv::bitwise_and(grayImage, color_mask_image_, search_image);
            LoggingTools::DebugShowImage(image_name_ + "  colorMasked image (search_image)", search_image);
        }
        else {
            search_image = grayImage;
        }

        // Apply any area mask
        if (false && !area_mask_image_.empty()) {
            cv::bitwise_and(search_image, area_mask_image_, search_image);
        }

        LoggingTools::DebugShowImage(image_name_ + "  Final color AND area-masked image (search_image)", search_image);

        switch (search_mode) {
            case kFindPlacedBall: {

               cv::GaussianBlur(search_image, search_image, cv::Size(kPlacedPreCannyBlurSize, kPlacedPreCannyBlurSize), 0);

                 // TBD - REMOVED THIS FOR NOW
                 for (int i = 0; i < 0; i++) {
                     cv::erode(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
                     cv::dilate(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
                 }

                 LoggingTools::DebugShowImage(image_name_ + "  Placed Ball Image - Ready for Edge Detection", search_image);

                 /*
                 EDPF testEDPF = EDPF(search_image);
                 Mat edgePFImage = testEDPF.getEdgeImage();
                 edgePFImage = edgePFImage * -1 + 255;
                 search_image = edgePFImage;
                 */
                 cv::Mat cannyOutput_for_balls;
                 cv::Canny(search_image, cannyOutput_for_balls, kPlacedBallCannyLower, kPlacedBallCannyUpper);

                 LoggingTools::DebugShowImage(image_name_ + "  cannyOutput_for_balls", cannyOutput_for_balls);

                 // Blur the lines-only image back to the search_image that the code below uses
                 cv::GaussianBlur(cannyOutput_for_balls, search_image, cv::Size(kPlacedPreHoughBlurSize, kPlacedPreHoughBlurSize), 0);   // Nominal is 7x7


                 break;
            }

            case kStrobed: {

                if (!PreProcessStrobedImage(search_image, kStrobed)) {
                    GS_LOG_MSG(error, "Failed to PreProcessStrobedImage");
                    return false;
                }

                break;
            }


            case kExternallyStrobed: {

                // Attempt to remove the lines of the golf-shaft in an externally-strobed environment.  
                // Sadly, this is better accomplished by simply putting IR-black felt over the shaft.
                std::vector<cv::Vec4i> lines;

                if (GolfSimCamera::kExternallyStrobedEnvFilterImage) {
                    if (!GolfSimCamera::CleanExternalStrobeArtifacts(rgbImg, search_image, lines)) {
                        GS_LOG_MSG(warning, "ProcessReceivedCam2Image - failed to CleanExternalStrobeArtifacts.");
                    }

                    LoggingTools::DebugShowImage(image_name_ + "After CleanExternalStrobeArtifacts", search_image);
                }

                if (!PreProcessStrobedImage(search_image, kExternallyStrobed)) {
                    GS_LOG_MSG(error, "Failed to PreProcessStrobedImage");
                    return false;
                }

                break;
            }

            case kPutting: {

                cv::medianBlur(search_image, search_image, kPuttingPreHoughBlurSize);

                for (int i = 0; i < 0; i++) {
                    cv::erode(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
                    cv::dilate(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
                }

                LoggingTools::DebugShowImage(image_name_ + "  Putting Image - Ready for Edge Detection", search_image);

                EDPF testEDPF = EDPF(search_image);
                Mat edgePFImage = testEDPF.getEdgeImage();
                edgePFImage = edgePFImage * -1 + 255;
                search_image = edgePFImage;

                cv::GaussianBlur(search_image, search_image, cv::Size(5, 5), 0);   // Nominal is 7x7

                break;
            }

            case kUnknown: {
            default:
                 GS_LOG_MSG(error, "BallImageProc::GetBall called with invalid search_mode");
                 return false;
                 break;
            }
        }


        LoggingTools::DebugShowImage(image_name_ + "  FINAL blurred/eroded/dilated Putting search_image for Hough Transform{ ", search_image);

        if (GolfSimOptions::GetCommandLineOptions().artifact_save_level_ != ArtifactSaveLevel::kNoArtifacts) {
            // TBD - REMOVE - Not really  useful any more
            // LoggingTools::LogImage("", search_image, std::vector < cv::Point >{}, true, "log_view_final_search_image_for_Hough.png");
        }

        // Apply hough transform on the image - NOTE - param2 is critical to balance between over - and under - identification
        // works            circles = cv::HoughCircles(img_blur, cv::HOUGH_GRADIENT, 1, minDist = 50, param1 = 200, param2 = 25, minRadius = 10, maxRadius = 50)
        // should be image_blur, not search_image ?

        // Param 1 will set the sensitivity; how strong the edges of the circles need to be.Too high and it won't detect anything, too low and it 
        // will find too much clutter.Param 2 will set how many edge points it needs to find to declare that it's found a circle. Again, too high 
        // will detect nothing, too low will declare anything to be a circle.
        // (See https{//dsp.stackexchange.com/questions/22648/in-opecv-function-hough-circles-how-does-parameter-1-and-2-affect-circle-detecti)

        // We will start with a best-guess transform parameter.If that results in one circle, great.And we're done.
        // If we get more than one circle, tighten the parameter so see if we can get just one.If not, we'll sort through the
        // circles further below.But if we don't get any circles with the starting point, loosen the parameter up to see if we 
        // can get at least one.

        bool done = false;
        std::vector<GsCircle> circles;
        double starting_param2;
        double min_param2;
        double max_param2;

        // Min number of circles will override max if necessary
        int min_circles_to_return_from_hough;
        // This is only small for when we are REALLY sure of where the ball is, like during calibration
        int max_circles_to_return_from_hough;

        int minimum_search_radius = 0;

        // Determine reasonable min / max radii if we don't know it
        if (min_ball_radius_ < 0.0) {
            minimum_search_radius = int(CvUtils::CvHeight(search_image) / 15);
        }
        else {
            minimum_search_radius = min_ball_radius_;
        }

        int maximum_search_radius = 0;

        if (max_ball_radius_ < 0.0) {
            maximum_search_radius = int(CvUtils::CvHeight(search_image) / 6);
        }
        else {
            maximum_search_radius = max_ball_radius_;
        }

        // If we are in strobed mode, allow for circles that are overlapping and of lower quality, etc.

        double minimum_distance;

        double currentParam1;    // nominal is 200.  Touchy - higher values sometimes do not work - CURRENT = 100
        double param2_increment;
        double currentDp;

        // Otherwise,if highly-certain we will find just one ball, crank the requirements to prevent false positives, otherwise, relax them
        switch (search_mode) {
            case kFindPlacedBall:
            {
                starting_param2 = kPlacedBallStartingParam2; // Nominal{  25
                min_param2 = kPlacedBallMinParam2;  // Nominal { 15
                max_param2 = kPlacedBallMaxParam2;

                currentParam1 = kPlacedBallCurrentParam1;    // nominal is 200.  Touchy - higher values sometimes do not work - CURRENT = 100
                param2_increment = kPlacedBallParam2Increment;

                min_circles_to_return_from_hough = kPlacedMinHoughReturnCircles;
                max_circles_to_return_from_hough = kPlacedMaxHoughReturnCircles;

                // In the expected image, there should be only one candidate anywhere near the ball
                minimum_distance = (double)minimum_search_radius * 0.5;

                currentDp = kPlacedBallHoughDpParam1;         // Must be between 0 and 2 (double).Nominal is 2, CURRENT = 1.2
                break;
            }
            case kStrobed:
            {
                bool use_alt = kStrobedBallsUseAltHoughAlgorithm;

                starting_param2 = use_alt ? kStrobedBallsAltStartingParam2 : kStrobedBallsStartingParam2;
                min_param2 = use_alt ? kStrobedBallsAltMinParam2 : kStrobedBallsMinParam2;
                max_param2 = use_alt ? kStrobedBallsAltMaxParam2 : kStrobedBallsMaxParam2;
                // In the strobed image, there may be overlapping balls. so the search distance should be small

                // The lower the value, the sloppier the found circles can be.  But crank it up too far and 
                // we don't pick up overlapped circles.
                currentParam1 = use_alt ? kStrobedBallsAltCurrentParam1 : kStrobedBallsCurrentParam1;
                // Don't want to get too crazy loose too fast in order to find more balls
                param2_increment = use_alt ? kStrobedBallsAltParam2Increment : kStrobedBallsParam2Increment;

                minimum_distance = minimum_search_radius * .3; // 0.18f;  // TBD - Parameterize this!

                // We have to have at least two candidate balls to do spin analysis
                // Try for more tp make sure we get all the overlapped balls.
                min_circles_to_return_from_hough = kStrobedBallsMinHoughReturnCircles;
                max_circles_to_return_from_hough = kStrobedBallsMaxHoughReturnCircles;

                currentDp = use_alt ? kStrobedBallsAltHoughDpParam1 : kStrobedBallsHoughDpParam1;         // Must be between 0 and 2 (double).Nominal is 2, CURRENT = 1.2
                break;
            }
            case kExternallyStrobed:
            {
                starting_param2 = kExternallyStrobedEnvStartingParam2;
                min_param2 = kExternallyStrobedEnvMinParam2;
                max_param2 = kExternallyStrobedEnvMaxParam2;
                // In the strobed image, there may be overlapping balls. so the search distance should be small

                // The lower the value, the sloppier the found circles can be.  But crank it up too far and 
                // we don't pick up overlapped circles.
                currentParam1 = kExternallyStrobedEnvCurrentParam1;
                // Don't want to get too crazy loose too fast in order to find more balls
                param2_increment = kExternallyStrobedEnvParam2Increment;

                // We have to have at least two candidate balls to do spin analysis
                // Try for more tp make sure we get all the overlapped balls.
                min_circles_to_return_from_hough = kExternallyStrobedEnvMinHoughReturnCircles;
                max_circles_to_return_from_hough = kExternallyStrobedEnvMaxHoughReturnCircles;

                currentDp = kExternallyStrobedEnvHoughDpParam1;

                /** TBD - REMOVE?  Any reason to have this mode's own radius parameters?
                minimum_search_radius = kExternallyStrobedEnvMinimumSearchRadius;
                maximum_search_radius = kExternallyStrobedEnvMaximumSearchRadius;
                ***/

                minimum_distance = (double)minimum_search_radius * 0.2;

                break;
            }

            case kPutting:
            {
                starting_param2 = kPuttingBallStartingParam2;
                min_param2 = kPuttingBallMinParam2;
                max_param2 = kPuttingBallMaxParam2;
                // In the strobed image, there may be overlapping balls. so the search distance should be small

                // The lower the value, the sloppier the found circles can be.  But crank it up too far and 
                // we don't pick up overlapped circles.
                currentParam1 = kPuttingBallCurrentParam1;
                // Don't want to get too crazy loose too fast in order to find more balls
                param2_increment = kPuttingBallParam2Increment;

                minimum_distance = minimum_search_radius * 0.5;

                // We have to have at least two candidate balls to do spin analysis
                // Try for more tp make sure we get all the overlapped balls.
                min_circles_to_return_from_hough = kPuttingMinHoughReturnCircles;
                max_circles_to_return_from_hough = kPuttingMaxHoughReturnCircles;

                currentDp = kPuttingHoughDpParam1;         // Must be between 0 and 2 (double).Nominal is 2, CURRENT = 1.2
                break;
            }
            case kUnknown:
            default:
                GS_LOG_MSG(error, "BallImageProc::GetBall called with invalid search_mode");
                return false;
                break;
        }


        double currentParam2 = starting_param2;

        int priorNumCircles = 0;
        int finalNumberOfFoundCircles = 0;

        bool currentlyLooseningSearch = false;

        cv::Mat final_search_image;

        // HoughCircles() is expensive - use it only in the region of interest if we have an ROI
        cv::Point offset_sub_to_full;
        cv::Point offset_full_to_sub;
        if (expectedBallArea.tl().x != 0 || expectedBallArea.tl().y != 0 ||
            expectedBallArea.br().x != 0 || expectedBallArea.br().y != 0) {

            // Note - if the expectedBallArea ROI is invalid, it will be corrected.
            final_search_image = CvUtils::GetSubImage(search_image, expectedBallArea, offset_sub_to_full, offset_full_to_sub);
        }
        else {
            // Do nothing if we don't have a sub-image.  Any later offsets will be 0, so will do nothing
            final_search_image = search_image;
        }

        // LoggingTools::DebugShowImage(image_name_ + "  Final sub-image search_image for Hough Transform{ ", final_search_image);
        // LoggingTools::LogImage("", final_search_image, std::vector < cv::Point >{}, true, "log_view_final_sub_image_for_Hough.png");

        cv::HoughModes hough_mode = cv::HOUGH_GRADIENT_ALT;

        if (search_mode != kFindPlacedBall) {
            if (kStrobedBallsUseAltHoughAlgorithm) {
                GS_LOG_TRACE_MSG(trace, "Using HOUGH_GRADIENT_ALT.");
                hough_mode = cv::HOUGH_GRADIENT_ALT;
            }
            else {
                GS_LOG_TRACE_MSG(trace, "Using HOUGH_GRADIENT.");
                hough_mode = cv::HOUGH_GRADIENT;
            }
        }

        if (search_mode == kStrobed || search_mode == kExternallyStrobed || search_mode == kFindPlacedBall) {

            if (kUseDynamicRadiiAdjustment && search_mode != kFindPlacedBall) {

                double min_ratio;
                double max_ratio;
                double narrowing_radii_param2;
                double narrowing_dp_param;

                if (search_mode == kFindPlacedBall) {
                    min_ratio = kPlacedNarrowingRadiiMinRatio;
                    max_ratio = kPlacedNarrowingRadiiMaxRatio;
                    minimum_distance = minimum_search_radius * 0.7;
                    narrowing_radii_param2 = kPlacedNarrowingStartingParam2;
                    narrowing_dp_param = kPlacedNarrowingRadiiDpParam;
                }
                else {
                    min_ratio = kStrobedNarrowingRadiiMinRatio;
                    max_ratio = kStrobedNarrowingRadiiMaxRatio;
                    minimum_distance = minimum_search_radius * 0.8;
                    narrowing_radii_param2 = kStrobedNarrowingRadiiParam2;
                    narrowing_dp_param = kStrobedNarrowingRadiiDpParam;
                }

                // Externally-strobed environments need a looser Param2
                if (search_mode == kExternallyStrobed) {
                    narrowing_radii_param2 = kExternallyStrobedEnvNarrowingParam2;
                    narrowing_dp_param = kExternallyStrobedEnvNarrowingDpParam;
                }
                    
                // For some reason, odd maximum_search_radius values were resulting in bad circle identification
                // These are the wider-ranging radii to make sure we find the ball, however near/far it may be
                minimum_search_radius = CvUtils::RoundAndMakeEven(minimum_search_radius);
                maximum_search_radius = CvUtils::RoundAndMakeEven(maximum_search_radius);

                GS_LOG_TRACE_MSG(trace, "Executing INITIAL houghCircles (to determine narrowed ball diameters) with currentDP = " + std::to_string(narrowing_dp_param) +
                    ", minDist = " + std::to_string(minimum_distance) + ", param1 = " + std::to_string(currentParam1) +
                    ", param2 = " + std::to_string(narrowing_radii_param2) + ", minRadius = " + std::to_string(int(minimum_search_radius)) +
                    ", maxRadius = " + std::to_string(int(maximum_search_radius)));

                // TBD - May want to adjust min / max radius
                // NOTE - Param 1 may be sensitive as well - needs to be 100 for large pictures ?
                // TBD - Need to set minDist to rows / 8, roughly ?
                // The _ALT mode seems to work best for this purpose
                std::vector<GsCircle> test_circles;
                
                    cv::HoughCircles(final_search_image,
                    test_circles,
                    cv::HOUGH_GRADIENT_ALT,
                    narrowing_dp_param,
                    minimum_distance, 
                    kPlacedNarrowingParam1,
                    narrowing_radii_param2,
                    (int)minimum_search_radius,
                    (int)maximum_search_radius );
                

                {
                    int MAX_CIRCLES_TO_EVALUATE = 100;
                    int kMaxCirclesToEmphasize = 8;
                    int i = 0;
                    cv::Mat test_hough_output = final_search_image.clone();

                    if (test_circles.size() == 0) {
                        if (report_find_failures) {
                            GS_LOG_TRACE_MSG(warning, "Initial (narrowing) Hough Transform found 0 balls.");
                        }
                        return false;
                    }

                    if (!RemoveSmallestConcentricCircles(test_circles)) {
                        GS_LOG_TRACE_MSG(warning, "Failed to RemoveSmallestConcentricCircles.");
                        return false;
                    }

                    for (auto& c : test_circles) {

                        i += 1;

                        if (i > MAX_CIRCLES_TO_EVALUATE) {
                            break;
                        }

                        int found_radius = (int)std::round(c[2]);

                        LoggingTools::DrawCircleOutlineAndCenter(test_hough_output, c, std::to_string(i), i, (i > kMaxCirclesToEmphasize));

                    }
                    LoggingTools::DebugShowImage("Initial (for narrowing) Hough-identified Circles", test_hough_output);
                    GS_LOG_TRACE_MSG(trace, "Narrowing Hough found the following circles: {     " + LoggingTools::FormatCircleList(test_circles));
                }

                const int number_balls_to_average = std::min(kNumberRadiiToAverageForDynamicAdjustment, (int)test_circles.size());
                double average = 0.0;

                for (int i = 0; i < number_balls_to_average; i++) {
                    average += test_circles[i][2] / number_balls_to_average;
                }

                minimum_search_radius = CvUtils::RoundAndMakeEven(average * min_ratio);
                maximum_search_radius = CvUtils::RoundAndMakeEven(average * max_ratio);

                minimum_distance = minimum_search_radius * 0.6;

                /* TBD - REMOVE - Not necessary for GRADIENT_ALT now
                if (kUseDynamicRadiiAdjustment && (search_mode == kFindPlacedBall)) {
                    // If we're using dynamic radii adjustment, we'd like to look at potentially several circles in a tight area
                    minimum_distance = 1;
                }
                */

                GS_LOG_TRACE_MSG(trace, "Dynamically narrowing search radii to { " + std::to_string(minimum_search_radius) +
                    ", " + std::to_string(maximum_search_radius) + " } pixels.");
            }

        }

        // NEW: ONNX detection bypass - skip adaptive parameter tuning for ONNX
        if (kDetectionMethod == "experimental" || kDetectionMethod == "experimental_sahi") {
            GS_LOG_TRACE_MSG(trace, "Using ONNX detection - bypassing adaptive parameter tuning");
            
            std::vector<GsCircle> test_circles;
            if (DetectBalls(final_search_image, search_mode, test_circles)) {
                // Apply radius filtering to ONNX results
                auto it = test_circles.begin();
                while (it != test_circles.end()) {
                    if ((*it)[2] < minimum_search_radius || (*it)[2] > maximum_search_radius) {
                        it = test_circles.erase(it);
                    } else {
                        ++it;
                    }
                }
                
                if (!test_circles.empty()) {
                    circles.assign(test_circles.begin(), test_circles.end());
                    
                    // ⚠️  WARNING: This old ONNX coordinate offset logic should NEVER execute 
                    // with the new early bypass. If you see this, the early bypass failed!
                    GS_LOG_MSG(error, "OLD ONNX path executed - this indicates early bypass failure!");
                    
                    // Apply coordinate transformation if using sub-image
                    for (auto& c : circles) {
                        c[0] += offset_sub_to_full.x;
                        c[1] += offset_sub_to_full.y;
                    }
                    
                    finalNumberOfFoundCircles = (int)circles.size();
                    GS_LOG_TRACE_MSG(trace, "ONNX detected " + std::to_string(finalNumberOfFoundCircles) + " circles");
                } else {
                    GS_LOG_TRACE_MSG(trace, "ONNX found circles but all filtered out by radius constraints");
                    if (report_find_failures) {
                        GS_LOG_MSG(warning, "ONNX detection found no balls within radius constraints");
                    }
                    return false;
                }
            } else {
                GS_LOG_TRACE_MSG(trace, "ONNX detection found no balls");
                if (report_find_failures) {
                    GS_LOG_MSG(warning, "ONNX detection failed to find any balls");
                }
                return false;
            }
            
            // Skip to post-processing (jump past the HoughCircles adaptive loop)
            goto post_detection_processing;
        }

        // Adaptive algorithm to dynamically adjust the (very touchy) Hough circle parameters depending on how things are going
        while (!done) {

            minimum_search_radius = CvUtils::RoundAndMakeEven(minimum_search_radius);
            maximum_search_radius = CvUtils::RoundAndMakeEven(maximum_search_radius);

            GS_LOG_TRACE_MSG(trace, "Executing houghCircles with currentDP = " + std::to_string(currentDp) +
                ", minDist = " + std::to_string(minimum_distance) + ", param1 = " + std::to_string(currentParam1) +
                ", param2 = " + std::to_string(currentParam2) + ", minRadius = " + std::to_string(int(minimum_search_radius)) +
                ", maxRadius = " + std::to_string(int(maximum_search_radius)));

            // TBD - May want to adjust min / max radius
            // NOTE - Param 1 may be sensitive as well - needs to be 100 for large pictures ?
            // TBD - Need to set minDist to rows / 8, roughly ?
            std::vector<GsCircle> test_circles;
            
            cv::HoughCircles(final_search_image,
                test_circles,
                hough_mode,
                currentDp,
                /* minDist = */ minimum_distance, // Does this really matter if we are only looking for one circle ?
                /* param1 = */ currentParam1,
                /* param2 = */ currentParam2,
                /* minRadius = */ (int)minimum_search_radius,
                /* maxRadius = */ (int)maximum_search_radius);

            // Save the prior number of circles if we need it later
            if (!circles.empty()) {
                priorNumCircles = (int)round(circles.size());       // round just to make sure we get an int - this should be evenly divisible
            }
            else {
                priorNumCircles = 0;
            }

            int numCircles = 0;

            if (!test_circles.empty()) {
                numCircles = (int)std::round(test_circles.size());
                GS_LOG_TRACE_MSG(trace, "Hough FOUND " + std::to_string(numCircles) + " circles.");
            }
            else {
                numCircles = 0;
            }

            if (!RemoveSmallestConcentricCircles(test_circles)) {
                GS_LOG_TRACE_MSG(warning, "Failed to RemoveSmallestConcentricCircles.");
                return false;
            }

            // If we find only a small number of circles, that may be ok.
            // Might be able to post-process the number down further later.
            if (numCircles >= min_circles_to_return_from_hough && numCircles <= max_circles_to_return_from_hough) {
                // We found what we consider to be a reasonable number of circles
                circles.assign(test_circles.begin(), test_circles.end());
                finalNumberOfFoundCircles = numCircles;
                done = true;
                break;
            }

            // we should take only ONE of the following branches
            if (numCircles > max_circles_to_return_from_hough) {
                // We found TOO MANY circles.
                // // Hopefully, we can either further tighten the transform to reduce the number of candidates,
                // of else we've been broadening and the prior attempt gave 0 circles but now we have too many (more than 1) 
                // (but at least we have SOME circles instead of 0 now)
                GS_LOG_TRACE_MSG(trace, "Found more circles than desired (" + std::to_string(numCircles) + " circles).");

                if ((priorNumCircles == 0) && (currentParam2 != starting_param2)) {
                    // We have too many circles now, and we had no circles before.So this is as good as we can do, at least
                    // using the currently (possibly too-coarse) increment
                    // In this case, just return what we had
                    GS_LOG_TRACE_MSG(trace, "Could not narrow number of balls to just 1");
                    // Save what we have now - deep copy
                    circles.assign(test_circles.begin(), test_circles.end());

                    finalNumberOfFoundCircles = numCircles;
                    done = true;
                }


                // We had too many balls before, and we still do now. So, see if we can tighten up our Hough transform
                if (currentParam2 >= max_param2) {
                    // We've tightened things as much as we want to, but still have too many possible balls
                    // We'll try to sort them out later
                    GS_LOG_TRACE_MSG(trace, "Could not narrow number of balls to just 1.  Produced " + std::to_string(numCircles) + " balls.");

                    // Save what we have now because maybe it's as good as things get
                    circles.assign(test_circles.begin(), test_circles.end());
                    finalNumberOfFoundCircles = numCircles;
                    done = true;
                }
                else {
                    // Next time we might not get any circles, so save what we have now
                    circles.assign(test_circles.begin(), test_circles.end());
                    currentParam2 += param2_increment;
                    currentlyLooseningSearch = false;
                    done = false;
                }
            }
            else {
                // We may have found some circles this time. 
                // Hopefully we either can further loosen the transform to find more, or we can't *BUT* we found some in the earlier attempt 
                // Two possible conditions here -
                //   1 - either we have been progressively tightening(increasing) currentParam2 and we went too far and now
                //       we have zero potential balls; OR
                //   2 - we started not finding ANY balls, kept loosening(decreasing) currentParam2, but we still failed
                if (numCircles == 0 && priorNumCircles == 0) {
                    // We have no circles now, and we had no circles before.So we never found any.
                    // In this case, keep trying to broaden if we can, otherwise, we fail
                    if (currentParam2 <= min_param2) {
                        // We've loosened things as much as we want to, but still haven't identified a single ball
                        if (report_find_failures) {
                            GS_LOG_MSG(error, "Could not find any balls");
                        }
                        done = true;
                    }
                    else {
                        currentParam2 -= param2_increment;
                        currentlyLooseningSearch = true;
                        circles.assign(test_circles.begin(), test_circles.end());
                        done = false;
                    }
                }
                else if (((numCircles > 0 && numCircles < min_circles_to_return_from_hough) && priorNumCircles == 0) ||
                    (currentlyLooseningSearch == true)) {
                    // We found SOME circles, but not as many as we'd like, and we had no circles previously.
                    // So, continue to broaden the search parameters to try to get more if we can

                    // Loosen up our seach parameters to see if we can get some more circles
                    if (currentParam2 <= min_param2) {
                        // We've loosened things as much as we want to, but still haven't identified a single ball
                        GS_LOG_TRACE_MSG(trace, "Could not find as many balls as hoped");
                        // Save what we have now because it's as good as things are going to get
                        circles.assign(test_circles.begin(), test_circles.end());
                        finalNumberOfFoundCircles = numCircles;
                        done = true;
                    }
                    else {
                        currentParam2 -= param2_increment;
                        currentlyLooseningSearch = true;
                        // Save what we have now because maybe it's as good as things get
                        circles.assign(test_circles.begin(), test_circles.end());
                        done = false;
                    }
                }
                else if (numCircles == 0 && priorNumCircles > 0) {
                    // We had some circles previously, but we presumably went too far in terms of tightening and now we have none
                    // Return the prior set of balls(which was apparently more than 1)
                    GS_LOG_TRACE_MSG(trace, "Could only narrow down to " + std::to_string(numCircles) + " balls");
                    finalNumberOfFoundCircles = numCircles;
                    done = true;
                }
            }

            GS_LOG_TRACE_MSG(trace, "Found " + std::to_string(numCircles) + " circles.");
        }

    post_detection_processing:
        // Post-detection processing continues here for both HoughCircles and ONNX

        cv::Mat candidates_image_ = rgbImg.clone();

        // Create a list of the circles with their corresponding criteria for quick sorting
        // Also draw detected circles if in debug mode

        // We may have to sort based on several criteria to find the best ball
        std::vector<CircleCandidateListElement>  foundCircleList;

        int MAX_CIRCLES_TO_EVALUATE = 200;
        bool expectedBallColorExists = false;

        const int kMaxCirclesToEmphasize = 10;


        int i = 0;
        if (finalNumberOfFoundCircles > 0) {

            i = 0;

            GsColorTriplet expectedBallRGBAverage;
            GsColorTriplet expectedBallRGBMedian;
            GsColorTriplet expectedBallRGBStd;


            if (baseBallWithSearchParams.average_color_ != GsColorTriplet(0, 0, 0)) {
                expectedBallRGBAverage = baseBallWithSearchParams.average_color_;
                expectedBallRGBMedian = baseBallWithSearchParams.median_color_;
                expectedBallRGBStd = baseBallWithSearchParams.std_color_;
                expectedBallColorExists = true;
            }
            else {
                // We don't have an expected ball color, so determine how close the candidate
                // is to the center of the masking color range
                expectedBallRGBAverage = baseBallWithSearchParams.GetRGBCenterFromHSVRange();
                expectedBallRGBMedian = expectedBallRGBAverage;  // We don't have anything better
                expectedBallRGBStd = (0, 0, 0);
                expectedBallColorExists = false;
            }

            GS_LOG_TRACE_MSG(trace, "Center of expected ball color (BGR){ " + LoggingTools::FormatGsColorTriplet(expectedBallRGBAverage));
            GS_LOG_TRACE_MSG(trace, "Expected ball median = " + LoggingTools::FormatGsColorTriplet(expectedBallRGBMedian) + " STD{ " + LoggingTools::FormatGsColorTriplet(expectedBallRGBStd));

            // Translate the circle coordinates back to the full image
            for (auto& c : circles) {
                c[0] += offset_sub_to_full.x;
                c[1] += offset_sub_to_full.y;
            }

            for (auto& c : circles) {

                i += 1;

                if (i > MAX_CIRCLES_TO_EVALUATE) {
                    break;
                }

                int found_radius = (int)std::round(c[2]);

                LoggingTools::DrawCircleOutlineAndCenter(candidates_image_, c, std::to_string(i), i, (i > kMaxCirclesToEmphasize));

                // Ignore any really small circles
                if (found_radius >= MIN_BALL_CANDIDATE_RADIUS) {

                    double calculated_color_difference = 0;
                    GsColorTriplet avg_RGB;
                    GsColorTriplet medianRGB;
                    GsColorTriplet stdRGB;

                    float rgb_avg_diff = 0.0;
                    float rgb_median_diff = 0.0;
                    float rgb_std_diff = 0.0;

                    // Putting currently uses ball colors to weed out balls that are formed from the noise of the putting green.
                    if (expectedBallColorExists || search_mode == kPutting) {
                        // Only deal with color if we will be comparing colors
                        std::vector<GsColorTriplet> stats = CvUtils::GetBallColorRgb(rgbImg, c);
                        avg_RGB = { stats[0] };
                        medianRGB = { stats[1] };
                        stdRGB = { stats[2] };

                        // Draw the outer circle if in debug
                        GS_LOG_TRACE_MSG(trace, "Circle of above-minimum radius " + std::to_string(MIN_BALL_CANDIDATE_RADIUS) +
                            " pixels. Average RGB is{ " + LoggingTools::FormatGsColorTriplet(avg_RGB)
                            + ". Average HSV is{ " + LoggingTools::FormatGsColorTriplet(CvUtils::ConvertRgbToHsv(avg_RGB)));

                        // Determine how "different" the average color is from the expected ball color
                        // If we don't have an expected ball color, than we use the RGB center from the  
                        // current mask
                        rgb_avg_diff = CvUtils::ColorDistance(avg_RGB, expectedBallRGBAverage);
                        rgb_median_diff = CvUtils::ColorDistance(medianRGB, expectedBallRGBMedian);   // TBD
                        rgb_std_diff = CvUtils::ColorDistance(stdRGB, expectedBallRGBStd);   // TBD

                        // Even if a potential ball has a really close median color, if the STD is even a little off, we want to down - grade it
                        // The following works to mix the three statistics together appropriately
                        // Will also penalize balls that are found toward the tail end of the list
                        //  NOMINAL - large StdDiff was throwing off? float calculated_color_difference = rgb_avg_diff + (float)(100. * pow(rgb_std_diff, 2.));
                        // TBD - this needs to be optimized.
    //                    double calculated_color_difference = pow(rgb_avg_diff,2) + (float)(2. * pow(rgb_std_diff, 2.)) + (float)(125. * pow(i, 4.));
                        // NOTE - if the flash-times are different for the ball we are using for the color, this is likely to pick the wrong thing.
                        calculated_color_difference = pow(rgb_avg_diff, 2) + (float)(20. * pow(rgb_std_diff, 2.)) + (float)(200. * pow(10 * i, 3.));

                        /* GS_LOG_TRACE_MSG(trace, "Found circle number " + std::to_string(i) + " radius = " + std::to_string(found_radius) +
                            " rgb_avg_diff = " + std::to_string(rgb_avg_diff) +
                            " CALCDiff = " + std::to_string(calculated_color_difference) + " rgbDiff = " + std::to_string(rgb_avg_diff) +
                            " rgb_median_diff = " + std::to_string(rgb_median_diff) + " rgb_std_diff = " + std::to_string(rgb_std_diff));
                        */
                    }

                    foundCircleList.push_back(CircleCandidateListElement{
                                "Ball " + std::to_string(i),
                                c,
                                calculated_color_difference,
                                found_radius,
                                avg_RGB,
                                rgb_avg_diff,
                                rgb_median_diff,
                                rgb_std_diff });
                }
                else {
                    GS_LOG_TRACE_MSG(trace, "Skipping too-small circle of radius = " + std::to_string(c[2]));
                }

            }

            LoggingTools::DebugShowImage(image_name_ + "  Hough-only-identified Circles{", candidates_image_);
        }
        else {
            if (report_find_failures) {
                GS_LOG_MSG(error, "Could not find any circles");
            }
            return false;
        }

        // Determine the average color of a rectangle within each circle, and see which is
        // closest to the color we were expecting(e.g., white)

        // GS_LOG_TRACE_MSG(trace, "Pre-sorted circle list{ " + FormatCircleCandidateList(foundCircleList));

        if (search_mode != BallSearchMode::kStrobed && expectedBallColorExists) {
            // Sort by the difference between the found ball's color and the expected oolor
            std::sort(foundCircleList.begin(), foundCircleList.end(), [](const CircleCandidateListElement& a, const CircleCandidateListElement& b)
                { return (a.calculated_color_difference < b.calculated_color_difference); });
        }
        else {
            // Do nothing if the color differences would be meaningless
        }

        GS_LOG_TRACE_MSG(trace, "Sorted circle list{     " + FormatCircleCandidateList(foundCircleList));

        // Only proceed if at least one circle was found
        // The hough transfer will have returned the "best" circle first(TBD - Confirm)
        // we will still do some post - processing to get rid of anything that looks unreasonable,
        // such as really small circles.

        bool foundCircle = (foundCircleList.size() > 0);


        if (!foundCircle) {
            if (report_find_failures) {
                GS_LOG_MSG(error, "Could not find any circles");
            }
            return false;
        }

        std::vector<CircleCandidateListElement>  candidates;
        std::vector<CircleCandidateListElement>  finalCandidates;

        if ((search_mode == BallSearchMode::kStrobed) && expectedBallColorExists) {
            // Remove any balls whose RGB difference is too great, and then re - sort based on radius and
            // return the biggest radius ball.
            struct CircleCandidateListElement& firstCircleElement = foundCircleList.front();
            float maxRGBDistance = (float)(firstCircleElement.calculated_color_difference + CANDIDATE_BALL_COLOR_TOLERANCE);

            for (auto& e : foundCircleList)
            {
                if (e.calculated_color_difference <= maxRGBDistance)
                {
                    candidates.push_back(e);

                }
            }
            GS_LOG_TRACE_MSG(trace, "Candidates after removing color mismatches{     " + FormatCircleCandidateList(candidates));

            // Sort by radius, largest first, and copy the list to the finalCandidates

            std::sort(candidates.begin(), candidates.end(), [](const CircleCandidateListElement& a, const CircleCandidateListElement& b)
                { return (a.found_radius > b.found_radius); });

            std::copy(std::begin(candidates), std::end(candidates), std::back_inserter(finalCandidates));
        }
        else {
            // If we didn't find a ball with the expected color, then the final candidates are just whatever the
            // interim candidates were
            std::copy(std::begin(foundCircleList), std::end(foundCircleList), std::back_inserter(finalCandidates));
        }

        if (finalCandidates.size() < 1) {
            foundCircle = false;
            if (report_find_failures) {
                GS_LOG_MSG(error, "Could not any final candidate ball circles.");
            }
            return false;
        }

        GsCircle bestCircle = finalCandidates.front().circle;
        if (CvUtils::CircleRadius(bestCircle) < .001) {
            GS_LOG_MSG(error, "BestCircle had 0 radius!");
            return false;
        }

        cv::Mat initial_ball_candidates_image_ = rgbImg.clone(); 
        
        int index = 0;
        for (CircleCandidateListElement& c : finalCandidates) {

            // We have one or more (possibly sketchy) initial ball candidates.  Create a ball and setup its color information
            // so that we can (if desired) use that information to further isolate the ball before we calculate the final
            // x, y, and radius information.  The color mask to get rid of stuff that is 'obviously' not the golf ball
            GolfBall b;

            // TBD - refactor so that the x & y are set from the circle for the ball instead of having to keep separate
            b.quality_ranking = index;  // Rankings start at 0
            b.set_circle(c.circle);
            return_balls.push_back(b);

            // Record the candidate graphically for later analysis
            LoggingTools::DrawCircleOutlineAndCenter(initial_ball_candidates_image_, c.circle, std::to_string(index), index, (index > kMaxCirclesToEmphasize));
            
            index++;
        }


#ifdef PERFORM_FINAL_TARGETTED_BALL_ID   // NOTE - This will currently return only a SINGLE ball, not all the candidates

        GsCircle finalCircle;

        if (!DetermineBestCircle(blurImg, bestCircle, choose_largest_final_ball, final_circle)) {
            GS_LOG_MSG(error, "Failed to DetermineBestCircle.");
            return false;
        }

#else // Not performing any additional, targetted ball ID

    GsCircle finalCircle = bestCircle;

#endif

        // Take the refined (hopefully more precise) circle for the "best" ball and assign that information to
        // update the ball.

        final_result_image_ = rgbImg.clone();
        LoggingTools::DrawCircleOutlineAndCenter(final_result_image_, finalCircle, "Ball");

        // LoggingTools::DebugShowImage(image_name_ + "  Resulting Circle on image", final_result_image_);

        if (CvUtils::CircleRadius(finalCircle) < 0.001) {
            GS_LOG_MSG(error, "CvUtils::GetBallColorRgb called with circle of 0 radius.");
            return false;
        }

        // Setup the "best" (first) ball to return the found information within
        GolfBall& best_ball = return_balls[0];

        // TBD - Too easy to forget to set a parameter here - refactor
        best_ball.ball_circle_ = finalCircle;

#ifdef PERFORM_FINAL_TARGETTED_BALL_ID

#ifdef USE_ELLIPSES_FOR_FINAL_ID
        // If we are using ellipses, save the information for later analysis
        best_ball.ball_ellipse_ = largestEllipse;
#endif
#endif

        best_ball.set_circle(finalCircle);

        std::vector<GsColorTriplet> stats = CvUtils::GetBallColorRgb(rgbImg, finalCircle);
        best_ball.ball_color_ = GolfBall::BallColor::kCalibrated;
        best_ball.average_color_ = stats[0];  // Average RGB
        best_ball.radius_at_calibration_pixels_ = baseBallWithSearchParams.radius_at_calibration_pixels_;

        return true;
    }


    bool BallImageProc::DetermineBestCircle(const cv::Mat& input_gray_image, 
                                            const GolfBall& reference_ball, 
                                            bool choose_largest_final_ball,
                                            GsCircle& final_circle) {


#ifdef GS_USING_IMAGE_EQ
        //cv::equalizeHist(finalChoiceImg, finalChoiceImg);
#endif

        cv::Mat gray_image = input_gray_image.clone();

        // We are pretty sure we got the correct ball, or at least something really close.
        // Now, try to find the best circle within the area around the candidate ball to see
        // if we can get a more precise position and radius.
        // Current theory is to NOT use any color masking in order to make this as precise
        // as possible(since we are already looking for a really narrow area and radii)

        const GsCircle& reference_ball_circle = reference_ball.ball_circle_;

        cv::Vec2i resolution = CvUtils::CvSize(gray_image);
        cv::Vec2i xy = CvUtils::CircleXY(reference_ball_circle);
        int circleX = xy[0]; 
        int circleY = xy[1];
        int ballRadius = (int)std::round(CvUtils::CircleRadius(reference_ball_circle));

        GS_LOG_TRACE_MSG(trace, "DetermineBestCircle using reference_ball_circle with radius = " + std::to_string(ballRadius) + 
        ".  (X,Y) center = (" + std::to_string(circleX) + "," + std::to_string(circleY) + ")");



        // Hough is expensive - use it only in the region of interest
        const double kHoughBestCircleSubImageSizeMultiplier = 1.5;
        int expandedRadiusForHough = (int)(kHoughBestCircleSubImageSizeMultiplier * (double)ballRadius);

        // If the ball is near the screen edge, reduce the width or height accordingly.

        double roi_x = std::round(circleX - expandedRadiusForHough);
        double roi_y = std::round(circleY - expandedRadiusForHough);

        double roi_width = std::round(2. * expandedRadiusForHough);
        double roi_height = roi_width;

        if (roi_x < 0.0) {
            // Ball is near left edge
            roi_width += (roi_x);
            roi_x = 0;
        }

        if (roi_y < 0.0) {
            // Ball is near left edge
            roi_height += (roi_y);
            roi_y = 0;
        }

        if (roi_x > gray_image.cols) {
            // Ball is near right edge
            roi_width -= (roi_x - gray_image.cols);
            roi_x = gray_image.cols;
        }

        if (roi_y > gray_image.rows) {
            // Ball is near right edge
            roi_height += (roi_y - gray_image.rows);
            roi_y = gray_image.rows;
        }

        cv::Rect ball_ROI_rect{ (int)roi_x, (int)roi_y, (int)roi_width, (int)roi_height };

        cv::Point offset_sub_to_full;
        cv::Point offset_full_to_sub;

        cv::Mat finalChoiceSubImg = CvUtils::GetSubImage(gray_image, ball_ROI_rect, offset_sub_to_full, offset_full_to_sub);

        // LoggingTools::DebugShowImage("DetermineBestCircle - finalChoiceSubImg", finalChoiceSubImg);

        int min_ball_radius = int(ballRadius * kBestCircleIdentificationMinRadiusRatio);
        int max_ball_radius = int(ballRadius * kBestCircleIdentificationMaxRadiusRatio);


        // TBD - REMOVED THIS FOR NOW - it was decreasing accuracy.
        for (int i = 0; i < 0; i++) {
            cv::erode(finalChoiceSubImg, finalChoiceSubImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
            cv::dilate(finalChoiceSubImg, finalChoiceSubImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
        }

        // use the radius to try to come up with a unique name for the debug window
        LoggingTools::DebugShowImage("Best Circle" + std::to_string(expandedRadiusForHough) + "  BestBall Image - Ready for Edge Detection", finalChoiceSubImg);

        cv::Mat cannyOutput_for_balls; 

        bool is_externally_strobed = GolfSimOptions::GetCommandLineOptions().lm_comparison_mode_;

        if (!is_externally_strobed) {

            // We're using the same image preparation as for a single, placed ball for now - 
            // TBD - Ensure that's the best approach - Current turned off (see 0 at end)
            cv::GaussianBlur(finalChoiceSubImg, finalChoiceSubImg, cv::Size(kBestCirclePreCannyBlurSize, kBestCirclePreCannyBlurSize), 0);

            cv::Canny(finalChoiceSubImg, cannyOutput_for_balls, kBestCircleCannyLower, kBestCircleCannyUpper);

            LoggingTools::DebugShowImage("Best Circle (Non-externally-strobed)" + std::to_string(expandedRadiusForHough) + "  cannyOutput for best ball", cannyOutput_for_balls);

            // Blur the lines-only image back to the search_image that the code below uses
            cv::GaussianBlur(cannyOutput_for_balls, finalChoiceSubImg, cv::Size(kBestCirclePreHoughBlurSize, kBestCirclePreHoughBlurSize), 0);   // Nominal is 7x7
        }
        else {
            // cv::GaussianBlur(finalChoiceSubImg, finalChoiceSubImg, cv::Size(kExternallyStrobedBestCirclePreCannyBlurSize, kExternallyStrobedBestCirclePreCannyBlurSize), 0);

            // cv::Canny(finalChoiceSubImg, cannyOutput_for_balls, kExternallyStrobedBestCircleCannyLower, kExternallyStrobedBestCircleCannyUpper);
            cannyOutput_for_balls = finalChoiceSubImg.clone();

            LoggingTools::DebugShowImage("Best Circle (externally-strobed)" + std::to_string(expandedRadiusForHough) + "  cannyOutput for best ball", cannyOutput_for_balls);

            // Blur the lines-only image back to the search_image that the code below uses
            cv::GaussianBlur(cannyOutput_for_balls, finalChoiceSubImg, cv::Size(kExternallyStrobedBestCirclePreHoughBlurSize, kExternallyStrobedBestCirclePreHoughBlurSize), 0);   // Nominal is 7x7
        }
        /***** THIS WAS PERFORMING POORLY - TBD - Probably remove
        cv::GaussianBlur(finalChoiceSubImg, finalChoiceSubImg, cv::Size(7, 7), 0);   // Nominal is 7x7


        for (int i = 0; i < 0; i++) {
            cv::erode(finalChoiceSubImg, finalChoiceSubImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
            cv::dilate(finalChoiceSubImg, finalChoiceSubImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
        }

        LoggingTools::DebugShowImage("DetermineBestCircle Image After Morphology", finalChoiceSubImg);
        */
        double currentParam1 = is_externally_strobed ? kExternallyStrobedBestCircleParam1 : kBestCircleParam1;
        double currentParam2 = is_externally_strobed ? kExternallyStrobedBestCircleParam2 : kBestCircleParam2;  // TBD - was 25
        double currentDp = is_externally_strobed ? kExternallyStrobedBestCircleHoughDpParam1 : kBestCircleHoughDpParam1;  // TBD - was 1.3?

        // TBD - Increase?  We want to be able to find several circles really close to one another
        int minimum_inter_ball_distance = 20; // has to be at least 1  .  Larger than 1 effectively turns off multiple balls

        LoggingTools::DebugShowImage("FINAL Best Circle image" + std::to_string(expandedRadiusForHough) + "  finalChoiceSubImg for best ball", finalChoiceSubImg);

        GS_LOG_MSG(info, "DetermineBestCircle - Executing houghCircles with currentDP = " + std::to_string(currentDp) +
            ", minDist (1) = " + std::to_string(minimum_inter_ball_distance) + ", param1 = " + std::to_string(currentParam1) +
            ", param2 = " + std::to_string(currentParam2) + ", minRadius = " + std::to_string(int(min_ball_radius)) +
            ", maxRadius = " + std::to_string(int(max_ball_radius)));

        std::vector<GsCircle> finalTargetedCircles;

        // The _ALT mode appears to be too stringent and often ends up missing balls
        cv::HoughCircles(
            finalChoiceSubImg,
            finalTargetedCircles,
            cv::HOUGH_GRADIENT_ALT,  // cv::HOUGH_GRADIENT,
            currentDp,
            /*minDist = */ minimum_inter_ball_distance, 
            /*param1 = */ currentParam1,
            /*param2 = */ currentParam2,
            /*minRadius = */ min_ball_radius,
            /*maxRadius = */ max_ball_radius);

        if (!finalTargetedCircles.empty()) {
            GS_LOG_TRACE_MSG(trace, "Hough FOUND " + std::to_string(finalTargetedCircles.size()) + " targeted circles.");
        }
        else {
            GS_LOG_TRACE_MSG(trace, "Could not find any circles after performing targeted  Hough Transform");
            // TBD - WAIT - Worst case, we need to at least return the #1 ball that we found from the original Hough search
            return false;
        }

        // Make sure all the numbers of the circles are integers
        // RoundCircleData(finalTargetedCircles);

        // Show the final group of candidates.  They should all be centered around the correct ball
        cv::Mat targetedCandidatesImage = finalChoiceSubImg.clone();

        final_circle = finalTargetedCircles[0];
        double averageRadius = 0;
        double averageX = 0;
        double averageY = 0;
        int averagedBalls = 0;

        int kMaximumBestCirclesToEvaluate = 3;
        int MaxFinalCandidateBallsToAverage = 4;

        int i = 0;
        for (auto& c : finalTargetedCircles) {
            i += 1;
            if (i > (kMaximumBestCirclesToEvaluate) && i != 1)
                break;

            double found_radius = c[2];    // Why were we rounding??? TBD = std::round(c[2]);
            GS_LOG_TRACE_MSG(trace, "Found targeted circle with radius = " + std::to_string(found_radius) + ".  (X,Y) center = (" + std::to_string(c[0]) + "," + std::to_string(c[1]) + ")");
            if (i <= MaxFinalCandidateBallsToAverage) {
                LoggingTools::DrawCircleOutlineAndCenter(targetedCandidatesImage, c, std::to_string(i), i);

                averageRadius += found_radius;
                averageX += std::round(c[0]);
                averageY += std::round(c[1]);
                averagedBalls++;
            }

            if (found_radius > final_circle[2]) {
                final_circle = c;
            }
        }

        averageRadius /= averagedBalls;
        averageX /= averagedBalls;
        averageY /= averagedBalls;

        GS_LOG_TRACE_MSG(trace, "Average Radius was: " + std::to_string(averageRadius) + ". Average (X,Y) = " 
                            + std::to_string(averageX) + ", " + std::to_string(averageY) + ").");

        LoggingTools::DebugShowImage("DetermineBestCircle Hough-identified Targeted Circles{", targetedCandidatesImage);
        // LoggingTools::LogImage("", targetedCandidatesImage, std::vector < cv::Point >{}, true, "log_view_targetted_circles.png");


        // Assume that the first ball will be the highest - quality match
        // Set to false if we want (instead) to use the largeet radius.  For some elliptical
        // ball images, that actually ends up being more accurate.
        if (!choose_largest_final_ball) {
            final_circle = finalTargetedCircles[0];
        }

        // Un-offset the circle back into the full image coordinate system
        final_circle[0] += offset_sub_to_full.x;
        final_circle[1] += offset_sub_to_full.y;

        return true;

    }

    cv::RotatedRect BallImageProc::FindBestEllipseFornaciari(cv::Mat& img, const GsCircle& reference_ball_circle, int mask_radius) {

        // Finding ellipses is expensive - use it only in the region of interest
        Size sz = img.size();

        int circleX = CvUtils::CircleX(reference_ball_circle);
        int circleY = CvUtils::CircleY(reference_ball_circle);
        int ballRadius = (int)std::round(CvUtils::CircleRadius(reference_ball_circle));

        const double cannySubImageSizeMultiplier = 1.35;
        int expandedRadiusForCanny = (int)(cannySubImageSizeMultiplier * (double)ballRadius);
        cv::Rect ball_ROI_rect{ (int)(circleX - expandedRadiusForCanny), (int)(circleY - expandedRadiusForCanny),
                           (int)(2. * expandedRadiusForCanny), (int)(2. * expandedRadiusForCanny) };

        cv::Point offset_sub_to_full;
        cv::Point offset_full_to_sub;

        cv::Mat processedImg = CvUtils::GetSubImage(img, ball_ROI_rect, offset_sub_to_full, offset_full_to_sub);

        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse_fornaciari - Original (SUB) input image for final choices", processedImg);


        // TBD - worth it before Canny?
        // Try to remove the noise around the ball
        // TBD - This can be made better than it is.  Possibly more iterations, different kernel size

        cv::Mat kernel = (cv::Mat_<char>(3, 3) << 0, -1, 0,
            -1, 5, -1,
            0, -1, 0);

        /*** Sharpening hurt most images
        cv::filter2D(processedImg, processedImg, -1, kernel);
        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse_fornaciari - sharpened image", processedImg);
        ***/

        cv::GaussianBlur(processedImg, processedImg, cv::Size(3, 3), 0);  // nominal was 11x11
        cv::erode(processedImg, processedImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
        cv::dilate(processedImg, processedImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);

        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse_fornaciari - blurred/eroded/dilated image", processedImg);


        bool edgeDetectionFailed = false;

#ifdef GS_USING_IMAGE_EQ
        // cv::equalizeHist(processedImg, processedImg);
        // LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse_fornaciari - equalized, processed final image", processedImg);
#endif

        // Parameters Settings (Sect. 4.2)
        int		iThLength = 16;   // nominal 16
        float	fThObb = 3.0f;
        float	fThPos = 1.0f;
        float	fTaoCenters = 0.05f;
        int 	iNs = 16;
        float	fMaxCenterDistance = sqrt(float(sz.width * sz.width + sz.height * sz.height)) * fTaoCenters;

        float	fThScoreScore = 0.72f;

        // Other constant parameters settings. 

        // Gaussian filter parameters, in pre-processing
        Size	szPreProcessingGaussKernelSize = Size(5, 5);    // Nominal is 5, 5
        double	dPreProcessingGaussSigma = 1.0;

        float	fDistanceToEllipseContour = 0.1f;	// (Sect. 3.3.1 - Validation)
        float	fMinReliability = 0.4f;	// Const parameters to discard bad ellipses


        // Initialize Detector with selected parameters
        CEllipseDetectorYaed detector;
        detector.SetParameters(szPreProcessingGaussKernelSize,
            dPreProcessingGaussSigma,
            fThPos,
            fMaxCenterDistance,
            iThLength,
            fThObb,
            fDistanceToEllipseContour,
            fThScoreScore,
            fMinReliability,
            iNs
        );


        // Detect
        vector<Ellipse> ellipses;
        cv::Mat workingImg = processedImg.clone();
        detector.Detect(workingImg, ellipses);

        GS_LOG_TRACE_MSG(trace, "Found " + std::to_string(ellipses.size()) + " candidate ellipses");


        // Find the best ellipse that seems reasonably sized

        cv::Mat ellipseImg = cv::Mat::zeros(img.size(), CV_8UC3);
        cv::RNG rng(12345);
        std::vector<cv::RotatedRect> minEllipse(ellipses.size());
        int numEllipses = 0;

        cv::RotatedRect largestEllipse;

        double largestArea = 0;

        cv::Scalar ellipseColor{ 255, 255, 255 };
        int numDrawn = 0;
        bool foundBestEllipse = false;

        // Look at as many ellipses as we need to in order to find the best (highest ranked) ellipse that is reasonable
        // given the ball that we are looking for
        for (auto& es : ellipses) {
            Ellipse ellipseStruct = es;
            RotatedRect e(Point(cvRound(es._xc), cvRound(es._yc)), Size(cvRound(2.0 * es._a), cvRound(2.0 * es._b)), (float)(es._rad * 180.0 / CV_PI));

            cv::Scalar color = cv::Scalar(rng.uniform(0, 256), rng.uniform(0, 256), rng.uniform(0, 256));

            // Note - All ellipses will be in the coordinate system of the FULL image, not the sub-image

            // Translate the ellipse to the full image coordinates for comparison with the expected position of the ball
            e.center.x += offset_sub_to_full.x;
            e.center.y += offset_sub_to_full.y;

            float xc = e.center.x;
            float yc = e.center.y;
            float a = e.size.width;    // width >= height
            float b = e.size.height;
            float theta = e.angle;  // Deal with this?
            float area = a * b;
            float aspectRatio = std::max(a,b) / std::min(a, b);


            // Cull out unrealistic ellipses based on position and size
            // NOTE - there were too many non-upright ellipses
            // TBD - Need to retest everything with the new aspect ratio restriction
            if ((std::abs(xc - circleX) > (ballRadius / 1.5)) ||
                (std::abs(yc - circleY) > (ballRadius / 1.5)) ||
                area < pow(ballRadius, 2.0) ||
                area > 6 * pow(ballRadius, 2.0) ||
                (!CvUtils::IsUprightRect(theta) && false) ||
                aspectRatio > 1.15) {
                GS_LOG_TRACE_MSG(trace, "Found and REJECTED ellipse, x,y = " + std::to_string(xc) + "," + std::to_string(yc) + " rw,rh = " + std::to_string(a) + "," + std::to_string(b) + " rectArea = " + std::to_string(a * b) + " theta = " + std::to_string(theta) + " aspectRatio = " + std::to_string(aspectRatio) + "(REJECTED)");
                GS_LOG_TRACE_MSG(trace, "     Expected max found ball radius was = " + std::to_string(ballRadius / 1.5) + ", min area: " + std::to_string(pow(ballRadius, 2.0)) + ", max area: " + std::to_string(5 * pow(ballRadius, 2.0)) + ", aspectRatio: " + std::to_string(aspectRatio) + ". (REJECTED)");

                // DEBUG - just for now show the rejected ellipses as well

                if (numDrawn++ > 5) {
                    GS_LOG_TRACE_MSG(trace, "Too many ellipses to draw (skipping no. " + std::to_string(numDrawn) + ").");
                }
                else {
                    ellipse(ellipseImg, e, color, 2);
                }
                numEllipses++;
            }
            else {
                GS_LOG_TRACE_MSG(trace, "Found ellipse, x,y = " + std::to_string(xc) + "," + std::to_string(yc) + " rw,rh = " + std::to_string(a) + "," + std::to_string(b) + " rectArea = " + std::to_string(a * b));

                if (numDrawn++ > 5) {
                    GS_LOG_TRACE_MSG(trace, "Too many ellipses to draw (skipping no. " + std::to_string(numDrawn) + ").");
                    break; // We are too far down the list in quality, so stop
                }
                else {
                    ellipse(ellipseImg, e, color, 2);
                }
                numEllipses++;

                if (area > largestArea) {
                    // Save this ellipse as our current best candidate
                    largestArea = area;
                    largestEllipse = e;
                    foundBestEllipse = true; 
                    // break;  // If we're only going to take the first (best) fit - TBD - this often breaks stuff!
                }
            }
        }

        LoggingTools::DebugShowImage("BallImageProc::FindBestEllipseFornaciari - Ellipses(" + std::to_string(numEllipses) + "):", ellipseImg);

        if (!foundBestEllipse) {
            LoggingTools::Warning("BallImageProc::FindBestEllipseFornaciari - Unable to find ellipse.");
            return largestEllipse;
        }

        return largestEllipse;
    }

    cv::RotatedRect BallImageProc::FindLargestEllipse(cv::Mat& img, const GsCircle& reference_ball_circle, int mask_radius) {

        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - input image for final choices", img);

        int lowThresh = 30;
        int highThresh = 70;

        const double kMinFinalizationCannyMean = 8.0;
        const double kMaxFinalizationCannyMean = 15.0;

        cv::Scalar meanArray;
        cv::Scalar stdDevArray;

        cv::Mat cannyOutput;

        bool edgeDetectDone = false;
        int cannyIterations = 0;

        int circleX = CvUtils::CircleX(reference_ball_circle);
        int circleY = CvUtils::CircleY(reference_ball_circle);
        int ballRadius = (int)std::round(CvUtils::CircleRadius(reference_ball_circle));

        // Canny is expensive - use it only in the region of interest
        const double cannySubImageSizeMultiplier = 1.35;
        int expandedRadiusForCanny = (int)(cannySubImageSizeMultiplier * (double)ballRadius);
        cv::Rect ball_ROI_rect{ (int)(circleX - expandedRadiusForCanny), (int)(circleY - expandedRadiusForCanny),
                           (int)(2. * expandedRadiusForCanny), (int)(2. * expandedRadiusForCanny) };

        cv::Point offset_sub_to_full;
        cv::Point offset_full_to_sub;

        cv::Mat finalChoiceSubImg = CvUtils::GetSubImage(img, ball_ROI_rect, offset_sub_to_full, offset_full_to_sub);
        bool edgeDetectionFailed = false;

        // Try to remove the noise around the ball
        // TBD - This can be made better than it is.  Possibly more iterations, different kernel size
        cv::erode(finalChoiceSubImg, finalChoiceSubImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 7)), cv::Point(-1, -1), 2);
        cv::dilate(finalChoiceSubImg, finalChoiceSubImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 7)), cv::Point(-1, -1), 2);

        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - after erode/dilate of grayscale:", finalChoiceSubImg);

        while (!edgeDetectDone) {

            cv::Canny(finalChoiceSubImg, cannyOutput, lowThresh, highThresh);  // <-- These parameters are critical and touchy - TBD
            // Remove the contour lines that develop at the edge of the mask - they are just artifacts, not real, also
            // try to get rid of some of the noise that should be clearly outside the ball
            cv::circle(cannyOutput, cv::Point(circleX, circleY) + offset_full_to_sub, mask_radius, cv::Scalar{ 0, 0, 0 }, (int)((double)ballRadius / 12.0));
            // Also remove the inner part of the ball, at least to the extent we can safely write that area off
            cv::circle(cannyOutput, cv::Point(circleX, circleY) + offset_full_to_sub, (int)(ballRadius * 0.7), cv::Scalar{ 0, 0, 0 }, cv::FILLED);

            // LoggingTools::DebugShowImage(image_name_ + "  Current Canny SubImage:", cannyOutput);

            cv::meanStdDev(cannyOutput, meanArray, stdDevArray);

            double mean = meanArray.val[0];
            double stddev = stdDevArray.val[0];

            GS_LOG_TRACE_MSG(trace, "Ball circle finalization - Canny edges at tolerance (low,high)= " + std::to_string(lowThresh) + ", " + std::to_string(highThresh) + "): mean: " + std::to_string(mean) + "std : " + std::to_string(stddev));

            // Adjust to get more/less edge lines depending on how busy (percentage white) the image currently is
            const int kCannyToleranceIncrement = 4;

            if (mean > kMaxFinalizationCannyMean) {
                lowThresh += kCannyToleranceIncrement;
                highThresh += kCannyToleranceIncrement;
            }
            else if (mean < kMinFinalizationCannyMean) {
                lowThresh -= kCannyToleranceIncrement;
                highThresh -= kCannyToleranceIncrement;
            }
            else {
                edgeDetectDone = true;
            }

            // Safety net to make sure we never get in an infinite loop
            if (cannyIterations > 30) {
                edgeDetectDone = true;
                edgeDetectionFailed = true;
            }
        }

        if (edgeDetectionFailed) {
            LoggingTools::Warning("Failed to detect edges");
            cv::RotatedRect nullRect;
            return nullRect;
        }

        RemoveLinearNoise(cannyOutput);   // This has been problematic because it can rip up an otherwise good circle

        // LoggingTools::DebugShowImage(image_name_ + "  Canny:", cannyOutput);
        // Try to fill in any gaps in the best ellipse lines
        for (int dilations = 0; dilations < 2; dilations++) {
            cv::dilate(cannyOutput, cannyOutput, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
            cv::erode(cannyOutput, cannyOutput, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
        }
        LoggingTools::DebugShowImage("BallImageProc::FindLargestEllipse - Dilated/eroded Canny:", cannyOutput);



        std::vector<std::vector<cv::Point> > contours;
        std::vector<cv::Vec4i> hierarchy;
        cv::findContours(cannyOutput, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE, cv::Point(0, 0));


        cv::Mat contourImg = cv::Mat::zeros(img.size(), CV_8UC3);
        cv::Mat ellipseImg = cv::Mat::zeros(img.size(), CV_8UC3);
        cv::RNG rng(12345);
        std::vector<cv::RotatedRect> minEllipse(contours.size());
        int numEllipses = 0;

        cv::RotatedRect largestEllipse;
        double largestArea = 0;

        for (size_t i = 0; i < contours.size(); i++)
        {
            cv::Scalar color = cv::Scalar(rng.uniform(0, 256), rng.uniform(0, 256), rng.uniform(0, 256));

            // Note - All ellipses will be in the coordinate system of the FULL image, not the sub-image
            if (contours[i].size() > 25) {
                minEllipse[i] = fitEllipse(contours[i]);

                // Translate the ellipse to the full image coordinates for comparison with the expected position of the ball
                minEllipse[i].center.x += offset_sub_to_full.x;
                minEllipse[i].center.y += offset_sub_to_full.y;

                float xc = minEllipse[i].center.x;
                float yc = minEllipse[i].center.y;
                float a = minEllipse[i].size.width;    // width >= height
                float b = minEllipse[i].size.height;
                float theta = minEllipse[i].angle;  // Deal with this?
                float area = a * b;


                // Cull out unrealistic ellipses based on position and size
                // NOTE - there were too many non-upright ellipses
                if ((std::abs(xc - circleX) > (ballRadius / 1.5)) ||
                        (std::abs(yc - circleY) > (ballRadius / 1.5)) ||
                        area < pow(ballRadius, 2.0) ||
                        area > 5 * pow(ballRadius, 2.0) ||
                        (!CvUtils::IsUprightRect(theta) && false) )  {
                    GS_LOG_TRACE_MSG(trace, "Found and REJECTED ellipse, x,y = " + std::to_string(xc) + "," + std::to_string(yc) + " rw,rh = " + std::to_string(a) + "," + std::to_string(b) + " rectArea = " + std::to_string(a * b) + " theta = " + std::to_string(theta) + "(REJECTED)");

                    // DEBIG - just for now show the rejected ellipses as well
                    
                    ellipse(ellipseImg, minEllipse[i], color, 2);
                    numEllipses++;
                    drawContours(contourImg, contours, (int)i, color, 2, cv::LINE_8, hierarchy, 0);
                    
                }
                else {
                    GS_LOG_TRACE_MSG(trace, "Found ellipse, x,y = " + std::to_string(xc) + "," + std::to_string(yc) + " rw,rh = " + std::to_string(a) + "," + std::to_string(b) + " rectArea = " + std::to_string(a * b));

                    ellipse(ellipseImg, minEllipse[i], color, 2);
                    numEllipses++;
                    drawContours(contourImg, contours, (int)i, color, 2, cv::LINE_8, hierarchy, 0);

                    if (area > largestArea) {
                        // Save this ellipse as our current best candidate
                        largestArea = area;
                        largestEllipse = minEllipse[i];
                    }
                }
            }
        }

        LoggingTools::DebugShowImage("BallImageProc::FindLargestEllipse - Contours:", contourImg);
        LoggingTools::DebugShowImage("BallImageProc::FindLargestEllipse - Ellipses(" + std::to_string(numEllipses) + "):", ellipseImg);

        return largestEllipse;
    }

    // Not working very well yet.  May want to try instead some closing/opening or convex hull model
    bool BallImageProc::RemoveLinearNoise(cv::Mat& img) {
        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - before removing horizontal/vertical lines", img);



#ifndef USING_HORIZ_VERT_REMOVAL


#else
        // Get rid of strongly horizontal and vertical lines, given that the ball should not be affected much
        int minLineLength = std::max(2, img.cols / 25);
        cv::Mat horizontalKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(minLineLength, 1));
        cv::Mat verticalKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, minLineLength));
        // cv::morphologyEx(cannyOutput, cannyOutput, cv::MORPH_ERODE, horizontal_kernel, cv::Point(-1, -1), 2);
        // TBD - shouldn't have to XOR the images, should be able to remove the lines in-place?
        cv::Mat horizontalLinesImg = img.clone();
        cv::erode(img, horizontalLinesImg, horizontalKernel, cv::Point(-1, -1), 1);
        cv::Mat verticalLinesImg = img.clone();
        cv::erode(img, verticalLinesImg, verticalKernel, cv::Point(-1, -1), 1);
        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - horizontal lines to filter", horizontalLinesImg);
        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - vertical lines to filter", verticalLinesImg);
        cv::bitwise_xor(img, horizontalLinesImg, img);
        cv::bitwise_xor(img, verticalLinesImg, img);

        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - after removing horizontal/vertical lines", img);
#endif
        return true;
    }

    // Returns a mask with 1 bits wherever the corresponding pixel is OUTSIDE the upper/lower HSV range
    cv::Mat BallImageProc::GetColorMaskImage(const cv::Mat& hsvImage, 
                                             const GsColorTriplet input_lowerHsv, 
                                             const GsColorTriplet input_upperHsv, 
                                             double wideningAmount) {

        GsColorTriplet lowerHsv = input_lowerHsv;
        GsColorTriplet upperHsv = input_upperHsv;

        // TBD - Straighten out double versus uchar/int here

        for (int i = 0; i < 3; i++) {
            lowerHsv[i] -= kColorMaskWideningAmount;   // (int)std::round(((double)lowerHsv[i] * kColorMaskWideningRatio));
            upperHsv[i] += kColorMaskWideningAmount;   //(int)std::round(((double)upperHsv[i] * kColorMaskWideningRatio));
        }


        // Ensure we didn't go too big on the S or V upper bound (which is 255)
        upperHsv[1] = std::min((int)upperHsv[1], 255);
        upperHsv[2] = std::min((int)upperHsv[2], 255);

        // Because we are creating a binary mask, it should be CV_8U or CV_8S (TBD - I think?)
        cv::Mat color_mask_image_(hsvImage.rows, hsvImage.cols, CV_8U, cv::Scalar(0));
        //        CvUtils::SetMatSize(hsvImage, color_mask_image_);
        // color_mask_image_ = hsvImage.clone();

        // We will need TWO masks if the hue range crosses over the 180 - degreee "loop" point for reddist colors
        // TBD - should we convert the ranges to scalars?
        if ((lowerHsv[0] >= 0) && (upperHsv[0] <= (float)CvUtils::kOpenCvHueMax)) {
            cv::inRange(hsvImage, cv::Scalar(lowerHsv), cv::Scalar(upperHsv), color_mask_image_);
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
                rightMostLowerHsv = cv::Vec3f((float)CvUtils::kOpenCvHueMax + (float)lowerHsv[0], (float)lowerHsv[1], (float)lowerHsv[2]);
                rightMostUpperHsv = cv::Vec3f((float)CvUtils::kOpenCvHueMax, (float)upperHsv[1], (float)upperHsv[2]);
            }
            else {
                // the upper hue is over 180 degrees
                leftMostLowerHsv = cv::Vec3f(0.f, (float)lowerHsv[1], (float)lowerHsv[2]);
                leftMostUpperHsv = cv::Vec3f((float)upperHsv[0] - 180.f, (float)upperHsv[1], (float)upperHsv[2]);
                rightMostLowerHsv = cv::Vec3f((float)lowerHsv[0], (float)lowerHsv[1], (float)lowerHsv[2]);
                rightMostUpperHsv = cv::Vec3f((float)CvUtils::kOpenCvHueMax, (float)upperHsv[1], (float)upperHsv[2]);
            }

            //GS_LOG_TRACE_MSG(trace, "leftMost Lower/Upper HSV{ " + LoggingTools::FormatVec3f(leftMostLowerHsv) + ", " + LoggingTools::FormatVec3f(leftMostUpperHsv) + ".");
            //GS_LOG_TRACE_MSG(trace, "righttMost Lower/Upper HSV{ " + LoggingTools::FormatVec3f(rightMostLowerHsv) + ", " + LoggingTools::FormatVec3f(rightMostUpperHsv) + ".");

            cv::Mat firstColorMaskImage;
            cv::inRange(hsvImage, leftMostLowerHsv, leftMostUpperHsv, firstColorMaskImage);

            cv::Mat secondColorMaskImage;
            cv::inRange(hsvImage, rightMostLowerHsv, rightMostUpperHsv, secondColorMaskImage);

            //LoggingTools::DebugShowImage(image_name_ + "  firstColorMaskImage", firstColorMaskImage);
            //LoggingTools::DebugShowImage(image_name_ + "  secondColorMaskImage", secondColorMaskImage);

            cv::bitwise_or(firstColorMaskImage, secondColorMaskImage, color_mask_image_);
        }

        //LoggingTools::DebugShowImage("BallImagProc::GetColorMaskImage returning color_mask_image_", color_mask_image_);

        return color_mask_image_;
    }


    cv::Mat BallImageProc::GetColorMaskImage(const cv::Mat& hsvImage, const GolfBall& ball, double widening_amount) {

        GsColorTriplet lowerHsv = ball.GetBallLowerHSV(ball.ball_color_);
        GsColorTriplet upperHsv = ball.GetBallUpperHSV(ball.ball_color_);

        return BallImageProc::GetColorMaskImage(hsvImage, lowerHsv, upperHsv, widening_amount);

    }

    bool BallImageProc::BallIsPresent(const cv::Mat& img) {
        GS_LOG_TRACE_MSG(trace, "BallIsPresent: image=" + LoggingTools::SummarizeImage(img));
        return true;

        /*
               // TBD - r is ball radius - should refactor these constants
               dm = radius / (GolfBall.r * f)
               // get pall position in spatial coordinates
               pos_p = nc::array([[x], [y], [1], [dm]] )
               pos_w = P_inv.dot(pos_p)
               return pos_w / pos_w[3][0], time
       */
    }

    std::string BallImageProc::FormatCircleCandidateElement(const struct CircleCandidateListElement& e) {
        // std::locale::global(std::locale("es_CO.UTF-8"));   // Try to get comma for thousands separators - doesn't work?  TBD

        auto f = GS_FORMATLIB_FORMAT("[{: <7}: {: <18} cd={: <15.2f} fr={: <4d} av={: <10} ad={: <9.1f} md={: <9.1f}    sd={: <9.1f}]", 
            e.name,
            LoggingTools::FormatCircle(e.circle),
            e.calculated_color_difference,
            e.found_radius,
            LoggingTools::FormatGsColorTriplet(e.avg_RGB),
            e.rgb_avg_diff,
            e.rgb_median_diff,
            e.rgb_std_diff
        );
        return f;
    }

    std::string BallImageProc::FormatCircleCandidateList(const std::vector<struct CircleCandidateListElement>& candidates) {
        std::string s = "\nName     | Circle                     | Color Diff         |Radius| Avg RGB                    |rgb_avg_diff  |rgb_median_diff | rgb_std_diff\n";
        for (auto& c : candidates)
        {
            s += FormatCircleCandidateElement(c) + "\n";
        }
        return s;
    }

    void BallImageProc::RoundCircleData(std::vector<GsCircle>& circles) {
        for (auto& c : circles) {
            // TBD - original was causing a compile problem?  Maybe just use regular around? nc::around(circles, 0);
            c[0] = std::round(c[0]);
            c[1] = std::round(c[1]);
            c[2] = std::round(c[2]);
        }
    }

    cv::Rect BallImageProc::GetAreaOfInterest(const GolfBall& ball, const cv::Mat& img) {

        // The area of interest is right in front (ball-fly direction) of the ball.  Anything in
        // the ball or behind it could just be lighting changes or the human teeing up.
        int x = (int)ball.ball_circle_[0];
        int y = (int)ball.ball_circle_[1];
        int r = (int)ball.ball_circle_[2];

        // The 1.1 just makes sure we are mostely outside of where the ball currently is
        int xmin = std::max(x, 0);      // OLD: std::max(x + (int)(r*1.1), 0);
        int xmax = std::min(x + 10*r, img.cols);
        int ymin = std::max(y - 6*r, 0);
        int ymax = std::min(y + (int)(r*1.5), img.rows);

        cv::Rect rect{ cv::Point(xmin, ymin), cv::Point(xmax, ymax) };

        return rect;
    }

    bool BallImageProc::WaitForBallMovement(GolfSimCamera &c, cv::Mat& firstMovementImage, const GolfBall& ball, const long waitTimeSecs) {
        BOOST_LOG_FUNCTION();

        GS_LOG_TRACE_MSG(trace, "wait_for_movement called with ball = " + ball.Format());

        //min area of motion detectable - based on ball radius, should be at least as large as a third of a ball
        int min_area = (int)pow(ball.ball_circle_[2],2.0);  // Rougly a third of the ball size

        boost::timer::cpu_timer timer1;

        cv::Mat firstFrame, gray, imageDifference, thresh;
        std::vector<std::vector<cv::Point> > contours;
        std::vector<cv::Vec4i> hierarchy;

        int startupFrameCount = 0;
        int frameLoopCount = 0;

        long r = (int)ball.measured_radius_pixels_;
        cv::Rect ballRect{ (int)( ball.x() - r ), (int)( ball.y() - r ), (int)(2 * r), (int)(2 * r) };

        bool foundMotion = false;

        cv::Mat frame;

        while (!foundMotion) {

            boost::timer::cpu_times elapsedTime = timer1.elapsed();

            if (elapsedTime.wall / 1.0e9 > waitTimeSecs) {
                LoggingTools::Warning("BallImageProc::WaitForBallMovement - time ran out");
                break;
            }

            cv::Mat fullFrame = c.getNextFrame();

            frameLoopCount++;

            if (fullFrame.empty()) {
                LoggingTools::Warning("frame was not captured");
                return(false);
            }

            // We will skip a few frames first for everything stabilize (TBD - is this necessary?)
            if (startupFrameCount < 1) {
                ++startupFrameCount;
                continue;
            }

            // LoggingTools::DebugShowImage("Next Frame", fullFrame);

            // We don't want to look at changes in the image just anywhere, instead narrow down to the
            // area around the ball, especially behind it.
            // TBD - Handed-Specific!

            cv::Rect areaOfInterest = GetAreaOfInterest(ball, fullFrame);
            frame = fullFrame(cv::Range(areaOfInterest.tl().y, areaOfInterest.br().y),
                                         cv::Range(areaOfInterest.tl().x, areaOfInterest.br().x));
            
            LoggingTools::DebugShowImage("Area of Interest", frame);

            //pre processing
            //resize(frame, frame, Size (1200,900));
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            // WAS ORIGINALLY - cv::GaussianBlur(gray, gray, cv::Size(21, 21), 0, 0);
            // A 7x7 kernel is plenty of blurring for our purpose (of removing transient spikes). 
            // It is almost twice as fast as a larger 21x21 kernel!
            cv::GaussianBlur(gray, gray, cv::Size(7, 7), 0, 0);

            //initialize first frame if necessary and don't do any comparison yet (as we only have one frame)
            if (firstFrame.empty()) {
                gray.copyTo(firstFrame);
                continue;
            }

            // Maintain a circular file of recent images so that we can, e.g., perform club face analysis
            // TBD
            //
 
            //LoggingTools::DebugShowImage("First Frame Image", firstFrame);
            //LoggingTools::DebugShowImage("Blurred Image", gray);

            const int kThreshLevel = 70;

            // get difference
            cv::absdiff(firstFrame, gray, imageDifference);
            
            // LoggingTools::DebugShowImage("Difference", imageDifference);
            
            cv::threshold(imageDifference, thresh, kThreshLevel, 255.0, cv::THRESH_BINARY );  //  | cv::THRESH_OTSU);
            // GS_LOG_TRACE_MSG(trace, "Otsu Threshold Value was:" + std::to_string(t));
            
            // fill in any small holes
            // TBD - TAKING TIME?  NECESSARY?
            // cv::dilate(thresh, thresh, cv::Mat(), cv::Point(-1, -1), 2, 1, 1);

            // LoggingTools::DebugShowImage("Threshold image: ", thresh);

            cv::findContours(thresh, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);


            int totalAreaOfDeltas = 0;
            bool atLeastOneLargeAreaOfChange = false;

            //loop over contours
            for (size_t i = 0; i < contours.size(); i++) {
                //get the boundboxes and save the ROI as an Image
                cv::Rect boundRect = cv::boundingRect(cv::Mat(contours[i]));

                /* Only use if the original ball will be included in the area of interest 
                // Quick way to test for rectangle inclusion
                if ((boundRect & ballRect) == boundRect) {
                    // Ignore any changes where the ball is - it could just be a lighting change
                    continue;
                }
                */
                long area = (long)cv::contourArea(contours[i]);
                if (area > min_area) {
                    atLeastOneLargeAreaOfChange = true;
                }
                totalAreaOfDeltas += area;
                cv::rectangle(frame, boundRect.tl(), boundRect.br(), cv::Scalar(255, 255, 0), 3, 8, 0);
            }

            LoggingTools::DebugShowImage("Contours of areas meeting minimum threshold", frame);

            // If we didn't find at least one substantial change in the area of interest, keep waiting
            if (!atLeastOneLargeAreaOfChange || (totalAreaOfDeltas < min_area) ) {
                //GS_LOG_TRACE_MSG(trace, "Didn't find any substantial changes between frames");
                continue;
            }

            foundMotion = true;

            firstMovementImage = frame;            
        }

        timer1.stop();
        boost::timer::cpu_times times = timer1.elapsed();
        std::cout << std::fixed << std::setprecision(8)
            << "Total Frame Loop Count = " << frameLoopCount << std::endl
            << "Startup Frame Loop Count = " << startupFrameCount << std::endl
            << times.wall / 1.0e9 << "s wall, "
            << times.user / 1.0e9 << "s user + "
            << times.system / 1.0e9 << "s system.\n";

        //draw everything 
        LoggingTools::DebugShowImage("First Frame", firstFrame);
        LoggingTools::DebugShowImage("Action feed", frame);
        LoggingTools::DebugShowImage("Difference", imageDifference);
        LoggingTools::DebugShowImage("Thresh", thresh);
        /*
        */

        return foundMotion;
    }

    // img is expected to be a grayscale (1 channel) image
    // TBD - Lowest/highest value is not curently implemented
    void BallImageProc::GetImageCharacteristics(const cv::Mat& img,
                                                const int brightness_percentage,
                                                int& brightness_cutoff,
                                                int& lowest_brightness,
                                                int& highest_brightness) {
        /******  I found out the images are not distributed as a normal distribution, so this doesn't work
        cv::Scalar meanArray;
        cv::Scalar stdDevArray;
        cv::meanStdDev(img, meanArray, stdDevArray);

        double mean = meanArray.val[0];
        double stddev = stdDevArray.val[0];

        double zScore = sqrt(2) * boost::math::erf_inv((double)brightness_percentage / 100.0);

        brightness_cutoff = (int)std::round(mean + ((stddev) * zScore));
        if (brightness_cutoff > 255) {
            brightness_cutoff = 255;
            LoggingTools::Warning("brightness_cutoff was > 255.  brightness_percentage (" + std::to_string(brightness_percentage) + ") may be set too high ? ");
        }
        */

        /// Establish the number of bins
        const int histSize = 256;

        /// Set the ranges ( for B,G,R) )
        float range[] = { 0, 256 };
        const float* histRange = { range };

        bool uniform = true; bool accumulate = false;

        cv::Mat b_hist;

        /// Compute the histograms:
        calcHist(&img, 1, 0, cv::Mat(), b_hist, 1, &histSize, &histRange, uniform, accumulate);

        // Draw the histograms for B, G and R
        int hist_w = 512; int hist_h = 400;
        int bin_w = cvRound((double)hist_w / histSize);

        /*
        cv::Mat histImage(hist_h, hist_w, CV_8UC3, cv::Scalar(0, 0, 0));

        // Normalize the result to [ 0, histImage.rows ]
        cv::normalize(b_hist, b_hist, 0, histImage.rows, cv::NORM_MINMAX, -1, cv::Mat());
        */

        long totalPoints = img.rows * img.cols;
        long accum = 0;
        int i = histSize - 1;
        bool foundPercentPoint = false;
        highest_brightness = -1;
        double targetPoints = (double)totalPoints * (100 - brightness_percentage) / 100.0;

        while (i >= 0 && !foundPercentPoint )
        {
            int numPixelsInBin = cvRound(b_hist.at<float>(i));
            accum += numPixelsInBin;
            foundPercentPoint = (accum >= targetPoints) ? true : false;
            if (highest_brightness < 0 && numPixelsInBin > 0) {
                highest_brightness = i;
            }
            i--;  // move to the next bin to the left
        }

        brightness_cutoff = i + 1;
    }

    bool BallImageProc::RemoveSmallestConcentricCircles(std::vector<GsCircle> &circles) {
        // Remove any concentric (nested) circles that share the same center but have different radii
        // TBD - this shouldn't occur, but the HOUGH_ALT_GRADIENT mode does not seem to respect the minimum
        // distance setting

        // The incoming circles may be in any order, so have to check all pairs.

        for (int i = 0; i < (int)(circles.size()) - 1; i++) {
            GsCircle& circle_current = circles[i];

            for (int j = (int)circles.size() - 1; j > i; j--) {
                GsCircle& circle_other = circles[j];

                if (CvUtils::CircleXY(circle_current) == CvUtils::CircleXY(circle_other)) {
                    // The two circles are concentric.  Remove the smaller circle
                    int radius_current = (int)std::round(circle_current[2]);
                    int radius_other = (int)std::round(circle_other[2]);

                    if (radius_other <= radius_current) {
                        circles.erase(circles.begin() + j);
                    }
                    else {
                        circles.erase(circles.begin() + i);
                        // Skip over the circle we just erased
                        // NOTE - i could go negative for a moment before it's incremented
                        // above.  That's why we are using an int
                        i--;

                        // There should only be one concentric pair, so we can move onto the next
                        // outer loop circle.  If there are more pairs, we will deal with that on
                        // a later loop
                        break;
                    }
                }
            }
        }

        return true;
    }

    const int kReflectionMinimumRGBValue = 245;  // Nominal is 235.  TBD - Not used - remove?

    void BallImageProc::RemoveReflections(const cv::Mat& original_image, cv::Mat& filtered_image, const cv::Mat& mask) {

        int hh = original_image.rows;
        int ww = original_image.cols;

        static int imgNumber = 1;
        // LoggingTools::DebugShowImage("RemoveReflections - input img# " + std::to_string(imgNumber) + " = ", original_image);
        // LoggingTools::DebugShowImage("filtered_image - input img# " + std::to_string(imgNumber) + " = ", filtered_image);
        imgNumber++;

        // LoggingTools::DebugShowImage("RemoveReflections - mask = ", mask);

        // Define the idea of a "bright" relfection dynamically.  The reflection brightness will be in the
        // xx% percentile (e.g., above 98%)
        // Dynamically determine the reflection minimum based on the other values on the
        // golf ball.  Basically figure out "bright" based on being on the high side of the histogram
        const int brightness_percentage = 99;
        int brightness_cutoff;
        int lowestBrightess;
        int highest_brightness;
        GetImageCharacteristics(original_image, brightness_percentage, brightness_cutoff, lowestBrightess, highest_brightness);

        GS_LOG_TRACE_MSG(trace, "Lower cutoff for brightness is " + std::to_string(brightness_percentage) + "%, grayscale value = " + std::to_string(brightness_cutoff));

        brightness_cutoff--;  // Make sure we don't filter out EVERYTHING
        // GsColorTriplet lower = ((uchar)brightness_cutoff, (uchar)brightness_cutoff, (uchar)brightness_cutoff);
        GsColorTriplet lower = ((uchar)kReflectionMinimumRGBValue, (uchar)kReflectionMinimumRGBValue, (uchar)kReflectionMinimumRGBValue);
        GsColorTriplet upper{ 255,255,255 };

        cv::Mat thresh(original_image.rows, original_image.cols, original_image.type(), cv::Scalar(0));
        cv::inRange(original_image, lower, upper, thresh);

        // LoggingTools::DebugShowImage("RemoveReflections - Initial thresholded image = ", thresh);

        // Expand the bright reflection areas, because they are likely to be areas where
        // the Gabor filters will show a lot of edges that will otherwise pollute the statistics

        static const int kReflectionKernelDilationSize = 5; // Nominal was 25?

        const int kCloseKernelSize = 3;  // 7

        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kCloseKernelSize, kCloseKernelSize));
        // Morph is a binary (0 or 255) mask
        cv::Mat morph;
        cv::morphologyEx(thresh, morph, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), /*iterations = */ 1);

        kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kReflectionKernelDilationSize, kReflectionKernelDilationSize));   // originally 25,25
        cv::morphologyEx(morph, morph, cv::MORPH_DILATE, kernel, cv::Point(-1, -1),  /*iterations = */ 1);

        // LoggingTools::DebugShowImage("RemoveReflections - Expanded thresholded image = ", morph);

        // Iterate through the morphed, expanded mask image and set the corresponding pixels to "ignore" in the filtered_image
        for (int x = 0; x < original_image.cols; x++) {
            for (int y = 0; y < original_image.rows; y++) {
                uchar p1 = morph.at<uchar>(x, y);

                if (p1 == 255) {
                    filtered_image.at<uchar>(x, y) = kPixelIgnoreValue;
                }
             }
        }

        LoggingTools::DebugShowImage("RemoveReflections - final filtered image = ", filtered_image);
    }

    // DEPRECATED - No longer used
    cv::Mat BallImageProc::ReduceReflections(const cv::Mat& img, const cv::Mat& mask) {

        int hh = img.rows;
        int ww = img.cols;

        LoggingTools::DebugShowImage("ReduceReflections - input img = ", img);
        LoggingTools::DebugShowImage("ReduceReflections - mask = ", mask);

        // threshold

        GsColorTriplet lower{ kReflectionMinimumRGBValue,kReflectionMinimumRGBValue,kReflectionMinimumRGBValue };
        GsColorTriplet upper{ 255,255,255 };

        cv::Mat thresh(img.rows, img.cols, img.type(), cv::Scalar(0));
        cv::inRange(img, lower, upper, thresh);

        LoggingTools::DebugShowImage("ReduceReflections - thresholded image = ", thresh);

        // apply morphology close and open to make mask
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7));
        cv::Mat morph;
        cv::morphologyEx(thresh, morph, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), /*iterations = */ 1);

        kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(8, 8));   // originally 25,25
        cv::morphologyEx(morph, morph, cv::MORPH_DILATE, kernel, cv::Point(-1, -1),  /*iterations = */ 1);

        // Now re-apply the appropriate mask outside the circle to ensure that those pixels are not considered, given
        // that some of the regions may have been broadened outside the ball area
        cv::bitwise_and(morph, mask, morph);

        LoggingTools::DebugShowImage("ReduceReflections - morphology = ", morph);

        // use mask with input to do inpainting of the bright bits
        // TBD - What radius to use?  Currently 101 was just a guess?
        cv::Mat result1;
        int inPaintRadius = (int)(std::min(ww, hh) / 30);
        cv::inpaint(img, morph, result1, inPaintRadius, cv::INPAINT_TELEA);
        LoggingTools::DebugShowImage("ReduceReflections - result1 (INPAINT_TELEA) (radius=" + std::to_string(inPaintRadius) + ") = ", result1);

        return result1;
    }

    // Returns new coordinates in the passed-in ball, so make a copy of it before
    // calling this if the original information needs to be preserved
    cv::Mat BallImageProc::IsolateBall(const cv::Mat& img, GolfBall& ball) {

        // We will grab a rectangle a little larger than the actual ball size
        const float ballSurroundMult = 1.05f;

        int r1 = (int)std::round(ball.measured_radius_pixels_ * ballSurroundMult);
        int rInc = (long)(r1 - ball.measured_radius_pixels_);
        // Don't assume the ball is well within the larger picture

        int x1 = ball.x() - r1;
        int y1 = ball.y() - r1;
        int x_width = 2 * r1;
        int y_height = 2 * r1;

        // Ensure the isolated image is entirely in the larger image
        x1 = max(0, x1);
        y1 = max(0, y1);

        if (x1 + x_width >= img.cols) {
            x1 = img.cols - x_width - 1;
        }
        if (y1 + y_height >= img.rows) {
            y1 = img.rows - y_height - 1;
        }

        cv::Rect ballRect{ x1, y1, x_width, y_height };

        // Re-center the ball's x and y position in the new, smaller picture
        // This will change the ball that was sent in
        ball.set_x( (float)std::round(rInc + ball.measured_radius_pixels_));
        ball.set_y( (float)std::round(rInc + ball.measured_radius_pixels_));

        cv::Point offset_sub_to_full;
        cv::Point offset_full_to_sub;
        cv::Mat ball_image = CvUtils::GetSubImage(img, ballRect, offset_sub_to_full, offset_full_to_sub);

        // Draw the mask circle slightly smaller than the ball to prevent any bright prenumbra around the isolated ball
        const float referenceBallMaskReductionFactor = 0.995f;

        // Do equalized images help?
#ifdef GS_USING_IMAGE_EQ
        cv::equalizeHist(ball_image, ball_image);
#endif

        cv::Mat finalResult = MaskAreaOutsideBall(ball_image, ball, referenceBallMaskReductionFactor, cv::Scalar(0, 0, 0));

        // LoggingTools::DebugShowImage("finalResult", finalResult);

        return finalResult;
    }

    cv::Mat BallImageProc::MaskAreaOutsideBall(cv::Mat& ball_image, const GolfBall& ball, float mask_reduction_factor, const cv::Scalar& maskValue) {

        // LoggingTools::DebugShowImage("MaskAreaOutsideBall - ball_image", ball_image);

        // A white circle on a black background will act as our first mask to preserve the ball portion of the image

        int mask_radius = (int)(ball.measured_radius_pixels_ * mask_reduction_factor);

        cv::Mat maskImage = cv::Mat::zeros(ball_image.rows, ball_image.cols, ball_image.type());
        cv::circle(maskImage, cv::Point(ball.x(), ball.y()), mask_radius, cv::Scalar(255, 255, 255), -1);
        //LoggingTools::DebugShowImage("1st maskImage", maskImage);

        // At this point, maskImage is an image with a white circle and a black outside

        cv::Mat result = ball_image.clone();
        cv::bitwise_and(ball_image, maskImage, result);
        //LoggingTools::DebugShowImage("Intermediate result", result);

        // Now XOR the image-on-black with a on a rectangle of desired color and a black circle in the middle
        cv::Rect r(cv::Point(0, 0), cv::Point(ball_image.cols, ball_image.rows));
        cv::rectangle(maskImage, r, maskValue, cv::FILLED);
        cv::circle(maskImage, cv::Point(ball.x(), ball.y()), mask_radius, cv::Scalar(0, 0, 0), -1);
        //LoggingTools::DebugShowImage("2nd maskImage", maskImage);

        cv::bitwise_xor(result, maskImage, result);

        // LoggingTools::DebugShowImage("MaskAreaOutsideBall: result", result);

        return result;
    }


    cv::Vec3d BallImageProc::GetBallRotation(const cv::Mat& full_gray_image1, 
                                             const GolfBall& ball1, 
                                             const cv::Mat& full_gray_image2, 
                                             const GolfBall& ball2) {
        // NOTE - This function (and downstream functions) assumes that ball1 is the earlier-in-time ball
        // for a right-handed shot.  So, for example, the expected spin will be largely counter-clockwise
        // from ball 1 to ball 2.
        // Make sure that for left-handed shots this is correct - we will assume that for
        // left-handed shots, ball1 is still to the LEFT of ball 2
        
        BOOST_LOG_FUNCTION();

        GS_LOG_TRACE_MSG(trace, "GetBallRotation called with ball1 = " + ball1.Format() + ",\nball2 = " + ball2.Format());
        LoggingTools::DebugShowImage("full_gray_image1", full_gray_image1);
        LoggingTools::DebugShowImage("full_gray_image2", full_gray_image2);

        // First, get a clean picture of each ball with nothing in the background, both sized the exactly same way 
        // Resize the images so that the balls are the same radius. 

        GolfBall local_ball1 = ball1;
        GolfBall local_ball2 = ball2;


        // NOTE - The ball that is passed into the IsolateBall image will be adjusted
        // to have the new x, y, and radius values relative to the smaller, isolated picture
        cv::Mat ball_image1 = IsolateBall(full_gray_image1, local_ball1);
        cv::Mat ball_image2 = IsolateBall(full_gray_image2, local_ball2);

        LoggingTools::DebugShowImage("ISOLATED full_gray_image1", ball_image1);
        LoggingTools::DebugShowImage("ISOLATED full_gray_image2", ball_image2);

        if (GolfSimOptions::GetCommandLineOptions().artifact_save_level_ != ArtifactSaveLevel::kNoArtifacts && kLogIntermediateSpinImagesToFile) {
            LoggingTools::LogImage("", ball_image1, std::vector < cv::Point >{}, true, "log_view_ISOLATED_full_gray_image1.png");
            LoggingTools::LogImage("", ball_image2, std::vector < cv::Point >{}, true, "log_view_ISOLATED_full_gray_image2.png");
        }

        // Just to test.  Ignore the 0 bin
        // CvUtils::DrawGrayImgHistogram(ball_image1, true);


        // We will assume that the images are now square

        double ball1RadiusMultiplier = 1.0;
        double ball2RadiusMultiplier = 1.0;

        if (ball_image1.rows > ball_image2.rows || ball_image1.cols > ball_image2.cols) {
            ball2RadiusMultiplier = (double)ball_image1.rows / (double)ball_image2.rows;
            int upWidth = ball_image1.cols;
            int upHeight = ball_image1.rows;
            cv::resize(ball_image2, ball_image2, cv::Size(upWidth, upHeight), cv::INTER_LINEAR);
        }
        else if (ball_image2.rows > ball_image1.rows || ball_image2.cols > ball_image1.cols) {
            ball1RadiusMultiplier = (double)ball_image2.rows / (double)ball_image1.rows;
            int upWidth = ball_image2.cols;
            int upHeight = ball_image2.rows;
            cv::resize(ball_image1, ball_image1, cv::Size(upWidth, upHeight), cv::INTER_LINEAR);
        }

        // Save the original, non-equalized images for later QA
        cv::Mat originalBallImg1 = ball_image1.clone();
        cv::Mat originalBallImg2 = ball_image2.clone();

        // Adjust relevant ball radius information accordingly
        local_ball1.measured_radius_pixels_ = local_ball1.measured_radius_pixels_ * ball1RadiusMultiplier;
        local_ball1.ball_circle_[2] = local_ball1.ball_circle_[2] * (float)ball1RadiusMultiplier;
        local_ball1.set_x( (float)((double)local_ball1.x() * ball1RadiusMultiplier));
        local_ball1.set_y( (float)((double)local_ball1.y() * ball1RadiusMultiplier));
        local_ball2.measured_radius_pixels_ = local_ball2.measured_radius_pixels_ * ball2RadiusMultiplier;
        local_ball2.ball_circle_[2] = local_ball2.ball_circle_[2] * (float)ball2RadiusMultiplier;
        local_ball2.set_x( (float)((double)local_ball2.x() * ball2RadiusMultiplier));
        local_ball2.set_y( (float)((double)local_ball2.y() * ball2RadiusMultiplier));


        std::vector < cv::Point > center1 = { cv::Point{(int)local_ball1.x(), (int)local_ball1.y()} };
        LoggingTools::DebugShowImage("Ball1 Image", ball_image1, center1);
        GS_LOG_TRACE_MSG(trace, "Updated (local) ball1 data: " + local_ball1.Format());
        std::vector < cv::Point > center2 = { cv::Point{(int)local_ball2.x(), (int)local_ball2.y()} };
        LoggingTools::DebugShowImage("Ball2 Image", ball_image2, center2);
        GS_LOG_TRACE_MSG(trace, "Updated (local) ball2 data: " + local_ball2.Format());

        float calibrated_binary_threshold = 0;
        cv::Mat ball_image1DimpleEdges = ApplyGaborFilterToBall(ball_image1, local_ball1, calibrated_binary_threshold);
        //  Suggest the same binary threshold between the images as a starting point for the second ball - they are probably similar
        cv::Mat ball_image2DimpleEdges = ApplyGaborFilterToBall(ball_image2, local_ball2, calibrated_binary_threshold, calibrated_binary_threshold);
   
        // TBD = Consider inverting the image to focus only on the inner parts of the dimples that will
        // have fewer pixels?
        //cv::bitwise_not(ball_image1, ball_image1);
        //cv::bitwise_not(ball_image2, ball_image2);

        // LoggingTools::DebugShowImage("Ball1 Dimple Image", ball_image1DimpleEdges);
        // LoggingTools::DebugShowImage("Ball2 Dimple Image", ball_image2DimpleEdges);

        cv::Mat area_mask_image_;
        RemoveReflections(ball_image1, ball_image1DimpleEdges, area_mask_image_);
        RemoveReflections(ball_image2, ball_image2DimpleEdges, area_mask_image_);

        // TBD - In addition to removing reflections, we may also want to remove really dark areas which will
        // comprise the registration marks.  That seems counter-intuitive, but those marks sometimes create large
        // "positive" (on) areas in the Gabor filters

        // The outer edge of the ball doesn't provide much information, so ignore it
        const float finalBallMaskReductionFactor = 0.92f;
        cv::Scalar ignoreColor = cv::Scalar(kPixelIgnoreValue, kPixelIgnoreValue, kPixelIgnoreValue);
        ball_image1DimpleEdges = MaskAreaOutsideBall(ball_image1DimpleEdges, local_ball1, finalBallMaskReductionFactor, ignoreColor);
        ball_image2DimpleEdges = MaskAreaOutsideBall(ball_image2DimpleEdges, local_ball2, finalBallMaskReductionFactor, ignoreColor);
        LoggingTools::DebugShowImage("Final ball_image1DimpleEdges after masking outside", ball_image1DimpleEdges);
        LoggingTools::DebugShowImage("Final ball_image2DimpleEdges after masking outside", ball_image2DimpleEdges);

        // Finally, rotate the second ball image to make up for the angle imparted by any offset of the ball from the
        // center of the camera's view.  Just reset the view using the angle offsets from the camera's perspective
        cv::Vec3d ball2Distances;

        // Find the differences between the offset angles, as they may be similar.
        // These will be the angles that the image will have to be rotated in order
        // to make it appear as it would if it were in the center of the image
        cv::Vec3f angleOffset1 = cv::Vec3f((float)ball1.angles_camera_ortho_perspective_[0], (float)ball1.angles_camera_ortho_perspective_[1], 0);
        cv::Vec3f angleOffset2 = cv::Vec3f((float)ball2.angles_camera_ortho_perspective_[0], (float)ball2.angles_camera_ortho_perspective_[1], 0);


        // We will split the difference in the angles so that the amount of de-rotation we need to do is spread evenly
        // across the two images

        // angleOffsetDeltas1 (and the floating-point version) are the angles that ball 1 must be rotated in
        // order to take it halfway to where ball 2 is
        cv::Vec3f angleOffsetDeltas1Float = (angleOffset2 - angleOffset1) / 2.0;

        // For left-handed shots, the first ball will be higher (and have a larger y angle), than the first, so account for that here
        if (GolfSimOptions::GetCommandLineOptions().golfer_orientation_ == GolferOrientation::kLeftHanded) {
            angleOffsetDeltas1Float[1] = -angleOffsetDeltas1Float[1];  // Account for how our rotations are signed
        }
        cv::Vec3i angleOffsetDeltas1 = CvUtils::Round(angleOffsetDeltas1Float);


        cv::Mat unrotatedBallImg1DimpleEdges = ball_image1DimpleEdges.clone();
        GetRotatedImage(unrotatedBallImg1DimpleEdges, local_ball1, angleOffsetDeltas1, ball_image1DimpleEdges);

        GS_LOG_TRACE_MSG(trace, "Adjusting rotation for camera view of ball 1 to offset (x,y,z)=" + std::to_string(angleOffsetDeltas1[0]) + "," + std::to_string(angleOffsetDeltas1[1]) + "," + std::to_string(angleOffsetDeltas1[2]));
        LoggingTools::DebugShowImage("Final perspective-de-rotated filtered ball_image1DimpleEdges: ", ball_image1DimpleEdges, center1);
        
        // The second rotation deltas will be the remainder of (approximately) the other half of the necessary degrees to get everything to be the same perspective
        cv::Vec3i angleOffsetDeltas2 = CvUtils::Round(  -(( angleOffset2 - angleOffset1) - angleOffsetDeltas1Float) );
        if (GolfSimOptions::GetCommandLineOptions().golfer_orientation_ == GolferOrientation::kLeftHanded) {
            angleOffsetDeltas2[1] = (int)std::round( - ((angleOffset1[1] - angleOffset2[1]) - angleOffsetDeltas1Float[1]) );
        }


        cv::Mat unrotatedBallImg2DimpleEdges = ball_image2DimpleEdges.clone();
        GetRotatedImage(unrotatedBallImg2DimpleEdges, local_ball2, angleOffsetDeltas2, ball_image2DimpleEdges);
        GS_LOG_TRACE_MSG(trace, "Adjusting rotation for camera view of ball 2 to offset (x,y,z)=" + std::to_string(angleOffsetDeltas2[0]) + "," + std::to_string(angleOffsetDeltas2[1]) + "," + std::to_string(angleOffsetDeltas2[2]));
        LoggingTools::DebugShowImage("Final perspective-de-rotated filtered ball_image2DimpleEdges: ", ball_image2DimpleEdges, center1);

        // Although unnecessary for the algorithm, the following DEBUG code shows the original image as it would appear rotated in the same way as the Gabor-filtered balls
        
        cv::Mat normalizedOriginalBallImg1 = originalBallImg1.clone();
        GetRotatedImage(originalBallImg1, local_ball1, angleOffsetDeltas1, normalizedOriginalBallImg1);
        LoggingTools::DebugShowImage("Final rotated originalBall1: ", normalizedOriginalBallImg1, center1);
        cv::Mat normalizedOriginalBallImg2 = originalBallImg2.clone();
        GetRotatedImage(originalBallImg2, local_ball2, angleOffsetDeltas2, normalizedOriginalBallImg2);
        LoggingTools::DebugShowImage("Final rotated originalBall2: ", normalizedOriginalBallImg2, center2);
        
#ifdef __unix__ 
        // Save the normalized ball images to the webserver shared directory so that the user
        // can compare them to the final rotated image.
        GsUISystem::SaveWebserverImage(GsUISystem::kWebServerResultSpinBall1Image, normalizedOriginalBallImg1);
        GsUISystem::SaveWebserverImage(GsUISystem::kWebServerResultSpinBall2Image, normalizedOriginalBallImg2);
#endif



        // Now compute all the possible rotations of the first image so we can figure out which angles make it look like the second ball image
        RotationSearchSpace initialSearchSpace;

        // Initial angle search will be fairly coarse
        initialSearchSpace.anglex_rotation_degrees_increment = kCoarseXRotationDegreesIncrement;
        initialSearchSpace.anglex_rotation_degrees_start = kCoarseXRotationDegreesStart;
        initialSearchSpace.anglex_rotation_degrees_end = kCoarseXRotationDegreesEnd;
        initialSearchSpace.angley_rotation_degrees_increment = kCoarseYRotationDegreesIncrement;
        initialSearchSpace.angley_rotation_degrees_start = kCoarseYRotationDegreesStart;
        initialSearchSpace.angley_rotation_degrees_end = kCoarseYRotationDegreesEnd;
        initialSearchSpace.anglez_rotation_degrees_increment = kCoarseZRotationDegreesIncrement;
        initialSearchSpace.anglez_rotation_degrees_start = kCoarseZRotationDegreesStart;
        initialSearchSpace.anglez_rotation_degrees_end = kCoarseZRotationDegreesEnd;

        cv::Mat outputCandidateElementsMat;
        std::vector< RotationCandidate> candidates;
        cv::Vec3i output_candidate_elements_mat_size;

        ComputeCandidateAngleImages(ball_image1DimpleEdges, initialSearchSpace, outputCandidateElementsMat, output_candidate_elements_mat_size, candidates, local_ball1);

        // Compare the second (presumably rotated) ball image to different candidate rotations of the first ball image to determine the angular change
        std::vector<std::string> comparison_csv_data;
        int best_candidate_index = CompareCandidateAngleImages(&ball_image2DimpleEdges, &outputCandidateElementsMat, &output_candidate_elements_mat_size, &candidates, comparison_csv_data);
        
        cv::Vec3f rotationResult;

        if (best_candidate_index < 0) {
            LoggingTools::Warning("No best candidate found.");
            return rotationResult;
        }

        bool write_spin_analysis_CSV_files = false;

        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kWriteSpinAnalysisCsvFiles", write_spin_analysis_CSV_files);
        
        if (write_spin_analysis_CSV_files) {
            // This data export can be used for, say, Excel analysis - CSV format
            std::string csv_fname_coarse = "spin_analysis_coarse.csv";
            ofstream csv_file_coarse(csv_fname_coarse);
            GS_LOG_TRACE_MSG(trace, "Writing CSV spin data to: " + csv_fname_coarse);
            for (auto& element : comparison_csv_data)
            {
                // Don't use logging utility so that we don't have all the timing crap in the output
                csv_file_coarse << element;
            }
            csv_file_coarse.close();
        }

        // See which angle looked best and then iterate more closely near those angles
        RotationCandidate c = candidates[best_candidate_index];

        std::string s = "Best Coarse Initial Rotation Candidate was #" + std::to_string(best_candidate_index) + " - Rot: (" + std::to_string(c.x_rotation_degrees) + ", " + std::to_string(c.y_rotation_degrees) + ", " + std::to_string(c.z_rotation_degrees) + ") ";
        GS_LOG_MSG(debug, s);

        // Now iterate more closely in the area that looks best
        RotationSearchSpace finalSearchSpace;

        int anglex_window_width = (int)std::round(ceil(initialSearchSpace.anglex_rotation_degrees_increment / 2.));
        int angley_window_width = (int)std::round(ceil(initialSearchSpace.angley_rotation_degrees_increment / 2.));
        int anglez_window_width = (int)std::round(ceil(initialSearchSpace.anglez_rotation_degrees_increment / 2.));


        finalSearchSpace.anglex_rotation_degrees_increment = 1;
        finalSearchSpace.anglex_rotation_degrees_start = c.x_rotation_degrees - anglex_window_width;
        finalSearchSpace.anglex_rotation_degrees_end = c.x_rotation_degrees + anglex_window_width;
        // Probably not worth it to be too fine-grained on the Y axis.
        finalSearchSpace.angley_rotation_degrees_increment = (int) std::round(kCoarseYRotationDegreesIncrement / 2.);
        finalSearchSpace.angley_rotation_degrees_start = c.y_rotation_degrees - angley_window_width;
        finalSearchSpace.angley_rotation_degrees_end = c.y_rotation_degrees + angley_window_width;
        finalSearchSpace.anglez_rotation_degrees_increment = 1;
        finalSearchSpace.anglez_rotation_degrees_start = c.z_rotation_degrees - anglez_window_width;
        finalSearchSpace.anglez_rotation_degrees_end = c.z_rotation_degrees + anglez_window_width;

        cv::Mat finalOutputCandidateElementsMat;
        cv::Vec3i finalOutputCandidateElementsMatSize;
        std::vector< RotationCandidate> finalCandidates;

        // After this, the finalOutputCandidateElementsMat will have X,Y,Z elements with an index into the finalCandidates vector.
        // Each candidate in finalCandidates will have an image, associated X,Y,Z information and a place to put a score
        ComputeCandidateAngleImages(ball_image1DimpleEdges, finalSearchSpace, finalOutputCandidateElementsMat, finalOutputCandidateElementsMatSize, finalCandidates, local_ball1);

        // TBD - change CompareCandidateAngleImages to work directly with the "3D" images
        best_candidate_index = CompareCandidateAngleImages(&ball_image2DimpleEdges, &finalOutputCandidateElementsMat, &finalOutputCandidateElementsMatSize, &finalCandidates, comparison_csv_data);

        // Save all the candidate scores to a CSV file if requested
        if (write_spin_analysis_CSV_files) {

            std::string csv_fname_fine = "spin_analysis_fine.csv";
            ofstream csv_file_fine(csv_fname_fine);
            GS_LOG_TRACE_MSG(trace, "Writing CSV spin data to: " + csv_fname_fine);
            for (auto& element : comparison_csv_data)
            {
                // Don't use logging utility so that we don't have all the timing crap in the output
                csv_file_fine << element;
            }
            csv_file_fine.close();
        }

        // Analyze the fine-grained results
        int best_rot_x = 0;
        int best_rot_y = 0;
        int best_rot_z = 0;

        if (best_candidate_index >= 0) {
            RotationCandidate finalC = finalCandidates[best_candidate_index];
            best_rot_x = finalC.x_rotation_degrees;
            best_rot_y = finalC.y_rotation_degrees;
            best_rot_z = finalC.z_rotation_degrees;

            // TBD - Experiment - are Y and X reversed?  Try it here...
            // best_rot_x = finalC.y_rotation_degrees;
            // best_rot_y = finalC.x_rotation_degrees;

            std::string s = "Best Raw Fine (and final) Rotation Candidate was #" + std::to_string(best_candidate_index) + " - Rot: (" + std::to_string(best_rot_x) + ", " + std::to_string(best_rot_y) + ", " + std::to_string(best_rot_z) + ") ";
            GS_LOG_MSG(debug, s);

            /*** FOR DEBUG ***/
            cv::Mat bestImg3D = finalCandidates[best_candidate_index].img;
            cv::Mat bestImg2D = cv::Mat::zeros(ball_image1DimpleEdges.rows, ball_image1DimpleEdges.cols, ball_image1DimpleEdges.type());
            Unproject3dBallTo2dImage(bestImg3D, bestImg2D, ball2);
            LoggingTools::DebugShowImage("Best Final Rotation Candidate Image", bestImg2D);
        } 
        else {
            LoggingTools::Warning("No best final candidate found.  Returning 0,0,0 spin results.");
            rotationResult = cv::Vec3d(0, 0, 0);
        }

        // The above angular deltas were calculated relative to a coordinate system that is at an angle
        // from the camera to the balls. So...
        // Now translate the spin angles so that the axes are the same as the PiTrac's and Sim's axes, where, 
        // for example, the Z and Y axes are parallel to the ground plane on which PiTrac sits, and the X axis
        // is orthogonal to that plane

        // We negated the Y offset delta before to account for the Sim's rotational scheme, so will undo here.
        // The idea is to determine the angle to the point in space that was between the two balls.
        cv::Vec3f spin_offset_angle;
        spin_offset_angle[0] = angleOffset1[0] + angleOffsetDeltas1Float[0];
        spin_offset_angle[1] = angleOffset1[1] - angleOffsetDeltas1Float[1];

        GS_LOG_TRACE_MSG(trace, "Now normalizing for spin_offset_angle = (" + std::to_string(spin_offset_angle[0]) + ", " + 
                                    std::to_string(spin_offset_angle[1]) + ", " + std::to_string(spin_offset_angle[2]) + ").");

        double spin_offset_angle_radians_X = CvUtils::DegreesToRadians(spin_offset_angle[0]);
        double spin_offset_angle_radians_Y = CvUtils::DegreesToRadians(spin_offset_angle[1]);
        double spin_offset_angle_radians_Z = CvUtils::DegreesToRadians(spin_offset_angle[2]);

        // Perform the normalization to the real-world axes
        int normalized_rot_x = (int)round( (double)best_rot_x * cos(spin_offset_angle_radians_Y) + (double)best_rot_z * sin(spin_offset_angle_radians_Y) );
        int normalized_rot_y = (int)round( (double)best_rot_y * cos(spin_offset_angle_radians_X) - (double)best_rot_z * sin(spin_offset_angle_radians_X) );

        int normalized_rot_z = (int)round((double)best_rot_z * cos(spin_offset_angle_radians_X) * cos(spin_offset_angle_radians_Y));
        normalized_rot_z -= (int)round((double)best_rot_y * sin(spin_offset_angle_radians_X));
        normalized_rot_z -= (int)round((double)best_rot_x * sin(spin_offset_angle_radians_Y));

        rotationResult = cv::Vec3d(normalized_rot_x, normalized_rot_y, normalized_rot_z);

        GS_LOG_TRACE_MSG(trace, "Normalized spin angles (X,Y,Z) = (" + std::to_string(normalized_rot_x) + ", " + std::to_string(normalized_rot_y) + ", " + std::to_string(normalized_rot_z) + ").");
        
        
        // TBD _ DEBUG
        // See how the original image would look if rotated as the GetBallRotation function calculated
        // We will NOT use the normalized rotations, as the UN-normalized rotations will look most correct
        // in the context of the manner they are imaged by the camera.

        cv::Mat resultBball2DImage;

        GetRotatedImage(ball_image1DimpleEdges, local_ball1, cv::Vec3i(best_rot_x, best_rot_y, best_rot_z), resultBball2DImage);


        if (GolfSimOptions::GetCommandLineOptions().artifact_save_level_ != ArtifactSaveLevel::kNoArtifacts && kLogIntermediateSpinImagesToFile) {
            LoggingTools::LogImage("", resultBball2DImage, std::vector < cv::Point >{}, true, "Filtered Ball1_Rotated_By_Best_Angles.png");
        }

        // We want to show apples to apples, so show the normalized images
        cv::Mat test_ball1_image = normalizedOriginalBallImg1.clone();
        GetRotatedImage(normalizedOriginalBallImg1, local_ball1, cv::Vec3i(best_rot_x, best_rot_y, best_rot_z), test_ball1_image);

        // We'll draw a center-dot on the final image here, but we're not going to re-use that image, so it's ok
        cv::Scalar color{ 0, 0, 0 };
        const GsCircle& circle = local_ball1.ball_circle_;
        cv::circle(test_ball1_image, cv::Point((int)local_ball1.x(), (int)local_ball1.y()), (int)circle[2], color, 2 /*thickness*/);
        LoggingTools::DebugShowImage("Final rotated-by-best-angle originalBall1: ", test_ball1_image, center1);


#ifdef __unix__ 
        // Save the final, rotated, normalized ball result image to the webserver shared directory so that the user
        // can compare them to the original normalized images.
        GsUISystem::SaveWebserverImage(GsUISystem::kWebServerResultBallRotatedByBestAngles, test_ball1_image);
#endif

        // Looks like golf folks consider the X (side) spin to be positive if the surface is
        // going from right to left.  So we negate it here.
        rotationResult[0] = -rotationResult[0];

        // Note that we return angles, not angular velocities.  The velocities will
        // be determined later based on the derived ball speed.
        return rotationResult;
    }



    // This structure is used as a callback for the OpenCV forEach() call.
    // After first being setup, the operator() will be called in parallel across
    // different processing cores.
    struct ImgComparisonOp {
        // Must be called prior to using the iteration() operator
        static void setup(const cv::Mat* target_image,
                          const cv::Mat* candidate_elements_mat,
                          std::vector<RotationCandidate>* candidates,
                          std::vector<std::string>* comparisonData ) {
            ImgComparisonOp::comparisonData_ = comparisonData;
            ImgComparisonOp::target_image_ = target_image;
            ImgComparisonOp::candidate_elements_mat_ = candidate_elements_mat;
            ImgComparisonOp::candidates_ = candidates;
        }

        void operator ()(ushort& unusedValue, const int* position) const {
            int x = position[0];
            int y = position[1];
            int z = position[2];

            int elementIndex = (*candidate_elements_mat_).at<ushort>(x, y, z);
            RotationCandidate& c = (*candidates_)[elementIndex];

            // For DEBUG
            // std::string s = "Idx: " + std::to_string(c.index) +
            //   " Rot: (" + std::to_string(c.x_rotation_degrees) + ", " + std::to_string(c.y_rotation_degrees) + ", " + std::to_string(c.z_rotation_degrees) + ") ";
            // GS_LOG_TRACE_MSG(trace, "Rotation Candidate: " + s);
            // LoggingTools::DebugShowImage("Img #" + std::to_string(c.index), c.img);

            // Compare the second ball image to each of the rotated versions of the first ball image to see which is closest
            cv::Vec2i results = BallImageProc::CompareRotationImage(*target_image_, c.img, c.index);
            double scaledScore = (double)results[0] / (double)results[1];
            
            // Save the calculated score for later analysis
            c.pixels_matching = results[0];
            c.pixels_examined = results[1];
            c.score = scaledScore;

            // GS_LOG_TRACE_MSG(trace, "I=" + std::to_string(elementIndex) + ", Rot: (" + std::to_string(c.x_rotation_degrees) + ", " + std::to_string(c.y_rotation_degrees) + ", " + std::to_string(c.z_rotation_degrees) + ") " + ".Score : " + std::to_string(results[0]) + " out of " + std::to_string(results[1]) +
            //    ". Scaled = " + std::to_string(scaledScore);

            // CSV (Excel) File format - Comma-Seperated-Values for Excel spreadsheet export
            // Columns are Idx, Rotx, Roty, Rotz, Score, Out-of, ScaledScore
            std::string s = std::to_string(c.index) + "\t" + std::to_string(c.x_rotation_degrees) + "\t" + std::to_string(c.y_rotation_degrees) + "\t" + std::to_string(c.z_rotation_degrees) + "\t" + std::to_string(results[0]) + "\t" + std::to_string(results[1]) +
                "\t" + std::to_string(scaledScore) + "\n";

            // DEBUG - Save a CSV-compatible string for later analysis
            (*comparisonData_)[c.index] = s;
        }

        static const cv::Mat* target_image_;
        static const cv::Mat* candidate_elements_mat_;
        static std::vector<std::string>* comparisonData_;
        static std::vector<RotationCandidate>* candidates_;
    };

    // Complete storage for ImgComparisonOp struct
    // Create temporary nonce objects because C++ requires references to point to a valid object.
    // the null/nonce references will go out of scope after setup() is called and these references
    // are set to valid objects
    std::vector<std::string>* ImgComparisonOp::comparisonData_ = nullptr;
    const cv::Mat* ImgComparisonOp::target_image_ = nullptr;
    const cv::Mat* ImgComparisonOp::candidate_elements_mat_ = nullptr;
    std::vector<RotationCandidate>* ImgComparisonOp::candidates_ = nullptr;


    // Returns the index within candidates that has the best comparison.
    // Returns -1 on failure.
    int BallImageProc::CompareCandidateAngleImages(const cv::Mat* target_image,
                                                    const cv::Mat* candidate_elements_mat,
                                                    const cv::Vec3i* candidate_elements_mat_size,
                                                    std::vector<RotationCandidate>* candidates,
                                                    std::vector<std::string>& comparison_csv_data) {

        boost::timer::cpu_timer timer1;

        // Assume candidates is a vector that is already pre-sized and filled with candidate information
        // and that the candidate_elements_mat has x, y, and z bounds that are commensurate with the candidates vector
        int xSize = (*candidate_elements_mat_size)[0];
        int ySize = (*candidate_elements_mat_size)[1];
        int zSize = (*candidate_elements_mat_size)[2];

        int numCandidates = xSize * ySize * zSize;
        std::vector<std::string> comparisonData(numCandidates);


        // Iterate through the matrix of candidates

        ImgComparisonOp::setup(target_image, candidate_elements_mat, candidates, &comparisonData);

        //  Serialized version for debugging
        if (kSerializeOpsForDebug) {
            for (int x = 0; x < xSize; x++) {
                for (int y = 0; y < ySize; y++) {
                    for (int z = 0; z < zSize; z++) {
                        ushort unusedValue = 0;
                        int position[]{ x, y, z };
                        ImgComparisonOp()(unusedValue, position);
                    }
                }
            }
        }
        else {
            (*candidate_elements_mat).forEach<ushort>(ImgComparisonOp());
        }

        // Find the best candidate from the comparison results
        double maxScaledScore = -1.0;
        double maxPixelsExamined = -1.0;
        double maxPixelsMatching = -1.0;
        int maxPixelsExaminedIndex = -1;
        int maxPixelsMatchingIndex = -1;
        int maxScaledScoreIndex = -1;
        int bestScaledScoreRotX = 0;
        int bestScaledScoreRotY = 0;
        int bestScaledScoreRotZ = 0;
        int bestPixelsMatchingRotX = 0;
        int bestPixelsMatchingRotY = 0;
        int bestPixelsMatchingRotZ = 0;

        // Find the best candidate
        // First, figure out what the largest number of pixels examined were.
        // If we later get a good score, but the number of examined pixels were
        // really low, then we might not want to pick that one.
        // OR... just pick the highest number of matching pixels?  Probably not,
        // as a far rotation that had few pixels to begin with, but very high
        // correspondence might be the correct one

        double kSpinLowCountPenaltyPower = 2.0;
        double kSpinLowCountPenaltyScalingFactor = 1000.0;
        double kSpinLowCountDifferenceWeightingFactor = 500.0;

        double low_count_penalty = 0.0;
        double final_scaled_score = 0.0;

        // Find the range of numbers of matching pixels and the total
        // most-available pixels in order to insert that into the mix for
        // a combined score
        for (auto& element : *candidates)
        {
            RotationCandidate c = element;

            if (c.pixels_examined > maxPixelsExamined) {
                maxPixelsExamined = c.pixels_examined;
                maxPixelsExaminedIndex = c.index;
            }

            if (c.pixels_matching > maxPixelsMatching) {
                maxPixelsMatching = c.pixels_matching;
                maxPixelsMatchingIndex = c.index;
                bestPixelsMatchingRotX = c.x_rotation_degrees;
                bestPixelsMatchingRotY = c.y_rotation_degrees;
                bestPixelsMatchingRotZ = c.z_rotation_degrees;
            }
        }

        for (auto& element : *candidates)
        {
            RotationCandidate c = element;

            low_count_penalty = std::pow((maxPixelsExamined - (double)c.pixels_examined) / kSpinLowCountDifferenceWeightingFactor,
                                kSpinLowCountPenaltyPower) / kSpinLowCountPenaltyScalingFactor;
            final_scaled_score = (c.score * 10.) - low_count_penalty;

            if (final_scaled_score > maxScaledScore) {
                maxScaledScore = final_scaled_score;
                maxScaledScoreIndex = c.index;
                bestScaledScoreRotX = c.x_rotation_degrees;
                bestScaledScoreRotY = c.y_rotation_degrees;
                bestScaledScoreRotZ = c.z_rotation_degrees;
            }
        }

        std::string s = "Best Candidate based on number of matching pixels was #" + std::to_string(maxPixelsMatchingIndex) +
                            " - Rot: (" + std::to_string(bestPixelsMatchingRotX) + ", " + 
                            std::to_string(bestPixelsMatchingRotY) + ", " + std::to_string(bestPixelsMatchingRotZ) + ") ";
        // GS_LOG_MSG(debug, s);

        s = "Best Candidate based on its scaled score of (" + std::to_string(maxScaledScore) + ") was # " + std::to_string(maxScaledScoreIndex) +
                            " - Rot: (" + std::to_string(bestScaledScoreRotX) + ", " + 
                            std::to_string(bestScaledScoreRotY) + ", " + std::to_string(bestScaledScoreRotZ) + ") ";
        GS_LOG_MSG(debug, s);

        // Transfer all the csv data to the output variable
        comparison_csv_data = comparisonData;

        timer1.stop();
        boost::timer::cpu_times times = timer1.elapsed();
        std::cout << "CompareCandidateAngleImages: ";
        std::cout << std::fixed << std::setprecision(8)
            << times.wall / 1.0e9 << "s wall, "
            << times.user / 1.0e9 << "s user + "
            << times.system / 1.0e9 << "s system.\n";

        return maxScaledScoreIndex;
    }





    cv::Vec2i BallImageProc::CompareRotationImage(const cv::Mat& img1, const cv::Mat& img2, const int index) {

        CV_Assert((img1.rows == img2.rows && img1.rows == img2.cols));

        // DEBUG - create a binary image showing what pixels are the same between them
        cv::Mat testCorrespondenceImg = cv::Mat::zeros(img1.rows, img1.cols, img1.type());

        // This comparison is currently done serially, but we should be processing
        // multiple such image comparisons in parallel
        long score = 0;
        long totalPixelsExamined = 0;
        for (int x = 0; x < img1.cols; x++) {
            for (int y = 0; y < img1.rows; y++) {
                uchar p1 = img1.at<uchar>(x, y);
                uchar p2 = img2.at<cv::Vec2i>(x, y)[1];

                if (p1 != kPixelIgnoreValue && p2 != kPixelIgnoreValue) {
                    // Both points have values, so we can validly compare them
                    totalPixelsExamined++;

                    if (p1 == p2) {
                        score++;
                        // The test image is already zero'd out, so only set the
                        // pixel to 1 if there is a match
                        testCorrespondenceImg.at<uchar>(x, y) = 255;
                    }
                }
                else
                {
                    testCorrespondenceImg.at<uchar>(x, y) = kPixelIgnoreValue;
                }
            }
        }

        // LoggingTools::DebugShowImage("testCorrespondenceImg #" + std::to_string(index), testCorrespondenceImg);
        // WON'T WORK BECAUSE IMG2 is 3D LoggingTools::DebugShowImage("testCandidateImg #" + std::to_string(index), img2);

        cv::Vec2i result(score, totalPixelsExamined);
        return result;
    }


    cv::Mat BallImageProc::CreateGaborKernel(int ks, double sig, double th, double lm, double gm, double ps) {

        int hks = (ks - 1) / 2;
        double theta = th * CV_PI / 180;
        double psi = ps * CV_PI / 180;
        double del = 2.0 / (ks - 1);
        double lmbd = lm / 100.0;
        double Lambda = lm;
        double sigma = sig / ks;
        cv::Mat kernel(ks, ks, CV_32F);
        double gamma = gm;

        kernel = cv::getGaborKernel(cv::Size(ks, ks), sig, theta, Lambda, gamma, psi, CV_32F);
        return kernel;
    }

    cv::Mat BallImageProc::ApplyGaborFilterToBall(const cv::Mat& image_gray, const GolfBall& ball, float & calibrated_binary_threshold, float prior_binary_threshold) {
        // TBD - Not sure we will ever need the ball information?
        CV_Assert( (image_gray.type() == CV_8UC1) );

        cv::Mat img_f32;
        image_gray.convertTo(img_f32, CV_32F, 1.0 / 255, 0);


        // This two-step calculation of the kernel parameters allows us to use the first set in a 
        // testing/playground environment with easier-to-control parameters and then convert as necessary to
        // the final kernal call.  So, DON'T REFACTOR

        // TBD - For equalized images, these numbers are causing too much noise.
        // For the GS camera, am considering  lambda=14, threshold = 4.
#ifdef GS_USING_IMAGE_EQ
        const int kernel_size = 21;
        int pos_sigma = 2;
        int pos_lambda = 6;   // Nominal: 13.  Lambda = 5 and Gamma = 4 or 3 also works well. last was 8
        int pos_gamma = 4;   // Nominal: 4, might try 3
        int pos_th = 60;   // Nominal: 
        int pos_psi = 9;  // Seems to have to be 9 or 27.  Will be multiplied by 3 degrees - CRITICAL - other values do not work at all
        float binary_threshold = 11.;   // *10.  Nominal: 3, might try 4-7
#else
        const int kernel_size = 21; //21;
        int pos_sigma = 2;   // Nominal: 2  (at 30 degree rotation increments)
        int pos_lambda = 6;   // Nominal: 13.  Lambda = 5 and Gamma = 4 or 3 also works well
        int pos_gamma = 4;   // Nominal: 4
        int pos_th = 60;   // Nominal: 
        int pos_psi = 27;  // Will be multiplied by 3 degrees - CRITICAL - other values do not work at all
        float binary_threshold = 8.5;   // *10.  Nominal: 3
#endif
        // Override the starting binary threshold if we have a prior one
        // This prevents the images from looking different simply due to the
        // different thresholds
        if (prior_binary_threshold > 0) {
            binary_threshold = prior_binary_threshold;
        }

        double sig = pos_sigma / 2.0;
        double lm = (double)pos_lambda;
        double th = (double)pos_th * 2;
        double ps = (double)pos_psi * 10.0;
        double gm = (double)pos_gamma / 20.0;   // Nominal:  30

        int white_percent = 0;

        cv::Mat dimpleImg = ApplyTestGaborFilter(img_f32, kernel_size, sig, lm, th, ps, gm, binary_threshold,
            white_percent);

        GS_LOG_TRACE_MSG(trace, "Initial Gabor filter white percent = " + std::to_string(white_percent));

        bool ratheting_threshold_down = (white_percent < kGaborMinWhitePercent);

        // Give it a second go if we're too white or too black and haven't already overridden the binary threshold
        if (prior_binary_threshold < 0 && 
            (white_percent < kGaborMinWhitePercent || white_percent >= kGaborMaxWhitePercent)) {

            // Keep going down or up (depending on the ractchet direction) until we get within a reasonable
            // white-ness range
            while (white_percent < kGaborMinWhitePercent || white_percent >= kGaborMaxWhitePercent) {
                // Try another gabor setting for less/more white

                if (ratheting_threshold_down)
                {
                    if (kGaborMinWhitePercent - white_percent > 5) {
                        binary_threshold = binary_threshold - 1.0F;
                    }
                    else {
                        binary_threshold = binary_threshold - 0.5F;
                    }
                    GS_LOG_TRACE_MSG(trace, "Trying lower gabor binary_threshold setting of " + std::to_string(binary_threshold) + " for better balance.");
                }
                else {
                    if (white_percent - kGaborMaxWhitePercent > 5) {
                        binary_threshold = binary_threshold + 1.0F;
                    }
                    else {
                        binary_threshold = binary_threshold + 0.5F;
                    }
                    GS_LOG_TRACE_MSG(trace, "Trying higher gabor binary_threshold setting of " + std::to_string(binary_threshold) + " for better balance.");
                }

                dimpleImg = ApplyTestGaborFilter(img_f32, kernel_size, sig, lm, th, ps, gm, binary_threshold,
                    white_percent);
                GS_LOG_TRACE_MSG(trace, "Next, refined, Gabor white percent = " + std::to_string(white_percent));

                // If we've gone as far as we can, just return
                if (binary_threshold > 30 || binary_threshold < 2) {
                    GS_LOG_MSG(warning, "Binaary threshold for Gabor filter reached limit of " + std::to_string(binary_threshold));
                    break;
                }

            }

            // Return the final threshold so that the caller can use for subsequent calls
            calibrated_binary_threshold = binary_threshold;

            GS_LOG_TRACE_MSG(trace, "Final Gabor white percent = " + std::to_string(white_percent));
        }

        return dimpleImg;
    }

    cv::Mat BallImageProc::ApplyTestGaborFilter(const cv::Mat& img_f32,
        const int kernel_size, double sig, double lm, double th, double ps, double gm, float binary_threshold,
        int &white_percent  ) {

        cv::Mat dest = cv::Mat::zeros(img_f32.rows, img_f32.cols, img_f32.type());
        cv::Mat accum = cv::Mat::zeros(img_f32.rows, img_f32.cols, img_f32.type());
        cv::Mat kernel;


        // Sweep through a bunch of different angles for the filter in order to pick up features
        // in all directions
        const double thetaIncrement = 11.25; //  5.625; // CURRENT 11.25;  // degrees.  Nominal: 11.25 also works 
        for (double theta = 0; theta <= 360.0; theta += thetaIncrement) {
            kernel = CreateGaborKernel(kernel_size, sig, theta, lm, gm, ps);
            cv::filter2D(img_f32, dest, CV_32F, kernel);

            cv::max(accum, dest, accum);
        }

        cv::Mat accumGray;

        // Convert from the 0.0 to 1.0 range into 0-255
        accum.convertTo(accumGray, CV_8U, 255, 0);

        cv::Mat dimpleEdges = cv::Mat::zeros(accum.rows, accum.cols, accum.type());

        // Threshold the image to either 0 or 255
        const int edgeThresholdLow = (int)std::round(binary_threshold * 10.);
        const int edgeThresholdHigh = 255;
        cv::threshold(accumGray, dimpleEdges, edgeThresholdLow, edgeThresholdHigh, cv::THRESH_BINARY);

        white_percent = (int)std::round(((double)cv::countNonZero(dimpleEdges) * 100.) / ((double)dimpleEdges.rows * dimpleEdges.cols));

        return dimpleEdges;
    }
 
   bool BallImageProc::ComputeCandidateAngleImages(const cv::Mat& base_dimple_image, 
                                                    const RotationSearchSpace& search_space, 
                                                    cv::Mat &outputCandidateElementsMat,
                                                    cv::Vec3i &output_candidate_elements_mat_size, 
                                                    std::vector< RotationCandidate> &output_candidates, 
                                                    const GolfBall& ball) {
        boost::timer::cpu_timer timer1;

        // These are the ranges of angles that we will create candidate images for
        // We probably won't vary the X-axis rotation much if at all.
        // TBD - Consider a coarse pass first, and then use smaller increments over 
        // the best ROI
        int anglex_rotation_degrees_increment = search_space.anglex_rotation_degrees_increment;
        int anglex_rotation_degrees_start = search_space.anglex_rotation_degrees_start;
        int anglex_rotation_degrees_end = search_space.anglex_rotation_degrees_end;
        int angley_rotation_degrees_increment = search_space.angley_rotation_degrees_increment;
        int angley_rotation_degrees_start = search_space.angley_rotation_degrees_start;
        int angley_rotation_degrees_end = search_space.angley_rotation_degrees_end;
        int anglez_rotation_degrees_increment = search_space.anglez_rotation_degrees_increment;
        int anglez_rotation_degrees_start = search_space.anglez_rotation_degrees_start;
        int anglez_rotation_degrees_end = search_space.anglez_rotation_degrees_end;

        // The ball may not be perfectly centered in the middle of the camera's gaze.  To account for that,
        // the system will essentially rotate the ball to the view the camera would have if it were centered.
        // This is done here by shifting the angles that will be simulated by offsets that account for the
        // ball placement
        
        // TBD - Think hard about how we want to apply the angle offset.  For example, we don't want to 
        // "lose" some of the image because of (for example) moving pixels to the front of the ball from behind it,
        // only to then apply the offset and move the ball back where it was before the pixels were lost.

        // CHANGE - we are going to deal with any camera perspective by pre-de-rotating both of the balls
        // so that they can be compared apples to apples.
        /* - TBD - Delete later when we are sure
        int xAngleOffset = ball.angles_camera_ortho_perspective_[0];
        int yAngleOffset = ball.angles_camera_ortho_perspective_[1];
        anglex_rotation_degrees_start += xAngleOffset;
        anglex_rotation_degrees_end += xAngleOffset;

        angley_rotation_degrees_start += yAngleOffset;
        angley_rotation_degrees_end += yAngleOffset;
        */
        /*  REMOVE - The angle rotations are performed elsewhere currently?? */
        int xAngleOffset = 0;
        int yAngleOffset = 0;


        int xSize = (int)std::ceil((anglex_rotation_degrees_end - anglex_rotation_degrees_start) / anglex_rotation_degrees_increment) + 1;
        int ySize = (int)std::ceil((angley_rotation_degrees_end - angley_rotation_degrees_start) / angley_rotation_degrees_increment) + 1;
        int zSize = (int)std::ceil((anglez_rotation_degrees_end - anglez_rotation_degrees_start) / anglez_rotation_degrees_increment) + 1;

        // Let the caller know what size of matrix we are going to return.  OpenCv only gives rows and columns,
        // so we need to handle this ourselves.

        output_candidate_elements_mat_size = cv::Vec3i(xSize, ySize, zSize);

        GS_LOG_TRACE_MSG(trace, "ComputeCandidateAngleImages will compute " + std::to_string(xSize * ySize * zSize) + " images.");

        // Create a new 3D Mat to hold indexes to the results in the vector.  Use a Mat in order to exploit the forEach() function
        int sizes[3] = { xSize, ySize, zSize };
        outputCandidateElementsMat = cv::Mat(3, sizes, CV_16U, cv::Scalar(0));

        short vectorIndex = 0;

        int xIndex = 0;
        int yIndex = 0;
        int zIndex = 0;

        for (int x_rotation_degrees = anglex_rotation_degrees_start, xIndex = 0; x_rotation_degrees <= anglex_rotation_degrees_end; x_rotation_degrees += anglex_rotation_degrees_increment, xIndex++) {
            for (int y_rotation_degrees = angley_rotation_degrees_start, yIndex = 0; y_rotation_degrees <= angley_rotation_degrees_end; y_rotation_degrees += angley_rotation_degrees_increment, yIndex++) {
                for (int z_rotation_degrees = anglez_rotation_degrees_start, zIndex = 0; z_rotation_degrees <= anglez_rotation_degrees_end; z_rotation_degrees += anglez_rotation_degrees_increment, zIndex++) {

                    cv::Mat ball2DImage;
                    // TBD - Instead of this, call the projectTo3D function and then use the resulting
                    // matrix directly in the comparison
                    // GetRotatedImage(base_dimple_image, ball, cv::Vec3i(x_rotation_degrees, y_rotation_degrees, z_rotation_degrees), ball2DImage);

                    // Project the ball out onto a 3D hemisphere at the current x, y, and z-axis rotation
                    cv::Mat ball13DImage = Project2dImageTo3dBall(base_dimple_image, ball, cv::Vec3i(x_rotation_degrees, y_rotation_degrees, z_rotation_degrees));

                    // Save the current image as a possible candidate to compare to later
                    RotationCandidate c;

                    // The angles in the set of images we are building are angles calculated as if the ball was
                    // centered in the camera's image
                    c.index = vectorIndex;
                    c.img = ball13DImage;
                    c.x_rotation_degrees = x_rotation_degrees - xAngleOffset;
                    c.y_rotation_degrees = y_rotation_degrees - yAngleOffset;
                    c.z_rotation_degrees = z_rotation_degrees;
                    c.score = 0.0;

                    // For now, just throw all of the candidates into a big vector indexed by the entries in the matrix
                    output_candidates.push_back(c);
                    outputCandidateElementsMat.at<ushort>(xIndex, yIndex, zIndex) = vectorIndex;

                    vectorIndex++;
                    
                    // Just for debug for small runs - probably too much information
                    /* std::string s = "ComputeCandidateAngleImages - Rotation Candidate: Idx: " + std::to_string(c.index) +
                        " Rot: (" + std::to_string(c.x_rotation_degrees) + ", " + std::to_string(c.y_rotation_degrees) + ", " + std::to_string(c.z_rotation_degrees) + ") ";
                    GS_LOG_MSG(debug, s);
                    */

                    // FOR DEBUG
                    /*
                    cv::Mat outputGrayImg = cv::Mat::zeros(base_dimple_image.rows, base_dimple_image.cols, base_dimple_image.type());
                    Unproject3dBallTo2dImage(ball13DImage, outputGrayImg, ball);
                    LoggingTools::DebugShowImage("Candidate Image at Rot: (" + std::to_string(c.x_rotation_degrees) + ", " + std::to_string(c.y_rotation_degrees) + ", " + std::to_string(c.z_rotation_degrees) + "): ", outputGrayImg);
                    */
                }
            }
        }

        timer1.stop();
        boost::timer::cpu_times times = timer1.elapsed();
        std::cout << "ComputeCandidateAngleImages Time: " << std::fixed << std::setprecision(8)
            << times.wall / 1.0e9 << "s wall, "
            << times.user / 1.0e9 << "s user + "
            << times.system / 1.0e9 << "s system.\n";

        return true;
    }


    void BallImageProc::GetRotatedImage(const cv::Mat& gray_2D_input_image, const GolfBall& ball, const cv::Vec3i rotation, cv::Mat& outputGrayImg) {
       BOOST_LOG_FUNCTION();                    
       
       // Project the ball out onto a 3D hemisphere at the current x, y, and z-axis rotation
       // and then unproject back to 2D matrix (image)
       cv::Mat ball3DImage = Project2dImageTo3dBall(gray_2D_input_image, ball, rotation);

       // TBD - FOR DEBUG
       // outputGrayImg = gray_2D_input_image.clone();

       outputGrayImg = cv::Mat::zeros(gray_2D_input_image.rows, gray_2D_input_image.cols, gray_2D_input_image.type());
       Unproject3dBallTo2dImage(ball3DImage, outputGrayImg, ball);
   }

   // The following struct is used as a callback for the OpenCV forEach() call.
   // After first being setup, the operator() will be called in parallel across
   // different processing cores.
    struct projectionOp {
        // Must be called prior to using the iteration() operator
        static void setup(const GolfBall *currentBall,
                          cv::Mat& projectedImg,
                          const double& x_rotation_degreesAngleRad,
                          const double& y_rotation_degreesAngleRad,
                          const double& z_rotation_degreesAngleRad ) {
            currentBall_ = currentBall;
            projectedImg_ = projectedImg;
            // Copy the rows/cols from the image because openCV will not do so otherwise
            // TBD - Kind of a hack
            projectedImg_.rows = projectedImg.rows;
            projectedImg_.cols = projectedImg.cols;
            x_rotation_degreesAngleRad_ = x_rotation_degreesAngleRad;
            y_rotation_degreesAngleRad_ = y_rotation_degreesAngleRad;
            z_rotation_degreesAngleRad_ = z_rotation_degreesAngleRad;

            // Pre-compute the trig functions for speed.  They will be the same for all pixels in the image
            sinX_ = sin(x_rotation_degreesAngleRad_);
            cosX_ = cos(x_rotation_degreesAngleRad_);
            sinY_ = sin(y_rotation_degreesAngleRad_);
            cosY_ = cos(y_rotation_degreesAngleRad_);
            sinZ_ = sin(z_rotation_degreesAngleRad_);
            cosZ_ = cos(z_rotation_degreesAngleRad_);

            // If some of the angles are 0, then we don't need to do any math at all for that axis or axes
            /* DELETE OLD
            rotatingOnX_ = ((int)std::round(1000 * x_rotation_degreesAngleRad_) != 0) ? true : false;
            rotatingOnY_ = ((int)std::round(1000 * y_rotation_degreesAngleRad_) != 0) ? true : false;
            rotatingOnZ_ = ((int)std::round(1000 * z_rotation_degreesAngleRad_) != 0) ? true : false;
            */
            rotatingOnX_ = (std::abs(x_rotation_degreesAngleRad_) > 0.001) ? true : false;
            rotatingOnY_ = (std::abs(y_rotation_degreesAngleRad_) > 0.001) ? true : false;
            rotatingOnZ_ = (std::abs(z_rotation_degreesAngleRad_) > 0.001) ? true : false;
        }

        // The returned imageXFromCenter and imageYFromCenter are the original imageX & Y in a new coordinate system with the center of the ball at (0,0)
        static void getBallZ(const double imageX, const double imageY, double& imageXFromCenter, double& imageYFromCenter, double& ball3dZ) {
            // Basic idea:  x2 + y2 + z2 = r2  (2's are squared).  Just solve for z where we can

            double r = currentBall_->measured_radius_pixels_;
            double ballCenterX = currentBall_->x();
            double ballCenterY = currentBall_->y();

            // Translate x and y into a new coordinate system that has the origin
            // at the center of the ball.
            imageXFromCenter = imageX - ballCenterX;
            imageYFromCenter = imageY - ballCenterY;

            // Short-cut the math for the outer border
            if (std::abs(imageXFromCenter) > r || std::abs(imageYFromCenter) > r) {
                ball3dZ = 0;
                return;
            }
            // Project the x,y coordinate onto the hemisphere to get the Z-axis position
            // Note that some of the image may be outside the sphere.  Ignore those
            double rSquared = pow(r, 2);
            double xSquarePlusYSquare = pow(imageXFromCenter, 2) + pow(imageYFromCenter, 2);
            double diff = rSquared - xSquarePlusYSquare;
            if (diff < 0.0) {
                ball3dZ = 0;  // Point is off the hemisphere/circle
            }
            else
            {
                // We seem to be spending a lot of time in round() - TBD
                ball3dZ = sqrt(diff);  // (int)std::round(sqrt(diff));
            }
        }

        // The sparse Z values associated with the X,Y pairs of the 3D images will be >= 0, because
        // the X,Y rays from the 2D image will be projected only on the closest hemisphere
        void operator ()(uchar& pixelValue, const int* position) const {
            double imageX = position[0];
            double imageY = position[1];


            // Figure out where the pre-rotated point is
            double imageXFromCenter;
            double imageYFromCenter;
            double ball3dZOfUnrotatedPoint = 0.0;
            getBallZ(imageX, imageY, imageXFromCenter, imageYFromCenter, ball3dZOfUnrotatedPoint);

            bool prerotatedPointNotValid = (ball3dZOfUnrotatedPoint <= 0.0001);  // A 0 value from getBallZ means that the point was outside the ROI

            // The following is a sort of safety feature - TBD - do we need this?
            // If the point we are rotating FROM is not on the visible hemisphere, set its pixel value to Ignore it.
            // Really, any point outside the sphere should already be set to ignore.
            if (prerotatedPointNotValid) {
                // ignore the original, pre-rotated pixel - it came from somehwere outside the hemisphere,
                // possibly from behind it.
                // std::cout << "CV_ELEM_SIZE1(traits::Depth<_Tp>::value): " << CV_ELEM_SIZE1(projectedImg_.traits::Depth<_Tp>::value) << "elemSize1()" << projectedImg_.elemSize1() << std::endl;
                // TBD - Not sure we even need to bother with this?

                projectedImg_.at<cv::Vec2i>((int)imageX, (int)imageY)[0] = (int)ball3dZOfUnrotatedPoint;    // TBD - Wait, is this right?  Why change the Z??
                projectedImg_.at<cv::Vec2i>((int)imageX, (int)imageY)[1] = kPixelIgnoreValue;
            }


            // Note - this method is likely to leave a lot of gaps in the unprojected image.  Consider interpolation?
            // GS_LOG_TRACE_MSG(trace, "projectionOp Result:  [" + std::to_string(imageX) + ", " + std::to_string(imageX) + ", " + std::to_string(ball3dZ) + "]=" + std::to_string(pixelValue));

            double imageZ = ball3dZOfUnrotatedPoint; // Note - the z axis is already situated with the origin in the center

            // X-axis rotation
            if (rotatingOnX_) {
                double tmpImageYFromCenter = imageYFromCenter;  // Want to change both Y and Z at the same time
                imageYFromCenter = (imageYFromCenter * cosX_) - (imageZ * sinX_);
                imageZ = (int)((tmpImageYFromCenter * sinX_) + (imageZ * cosX_));
            }
    
            // Y-axis rotation
            if (rotatingOnY_) {
                double tmpImageXFromCenter = imageXFromCenter;
                imageXFromCenter = (imageXFromCenter * cosY_) + (imageZ * sinY_);
                imageZ = (int)((imageZ * cosY_) - (tmpImageXFromCenter * sinY_));
            }

            // Z-axis rotation
            if (rotatingOnZ_) {
                double tmpImageXFromCenter = imageXFromCenter;
                imageXFromCenter = (imageXFromCenter * cosZ_) - (imageYFromCenter * sinZ_);
                imageYFromCenter = (tmpImageXFromCenter * sinZ_) + (imageYFromCenter * cosZ_);
            }

            // Shift back to coordinates with the origin in the top-left
            imageX = imageXFromCenter + projectionOp::currentBall_->x();
            imageY = imageYFromCenter + projectionOp::currentBall_->y();

            // Get the Z value of the destination, rotated-to point.
            double ball3dZOfRotatedPoint = 0;
            double dummy_rotatedImageXFromCenter;  // Just used as a dummy variable to get the new Z
            double dummy_rotatedImageYFromCenter;  // Just used as a dummy variable to get the new Z

            getBallZ(imageX, imageY, dummy_rotatedImageXFromCenter, dummy_rotatedImageYFromCenter, ball3dZOfRotatedPoint);

            if (currentBall_->PointIsInsideBall(imageX, imageY) && ball3dZOfRotatedPoint < 0.001) {
                GS_LOG_TRACE_MSG(trace, "Project2dImageTo3dBall Z-value pixel within ball at (" + std::to_string(imageX) +
                    ", " + std::to_string(imageY) + ").");
            }

            // Some of the points (like the corners) may rotate out to a place that is outside of the image Mat
            // If so, just ignore that point
            // Also, if the Z point that we've rotated the current pixel to is now *behind* the ball surface that the camera sees, then just ignore it
            // and do absolutely nothing
            if (imageX >= 0 &&
                imageY >= 0 &&
                imageX < projectedImg_.cols &&
                imageY < projectedImg_.rows &&
                ball3dZOfRotatedPoint > 0.0) {
                    // The rotated-to point is on the visible surface of the hemisphere

                    // Instead of performing a zillion round operations, we'll just effectively floor (truncate)
                    // each x and y value.  We'll lose some accuracy, but if everything is floored, it should at least
                    // still be consistent.
                    // projectedImg_.at<cv::Vec2i>((int)imageX, (int)imageY)[0] = (int)std::round(ball3dZOfRotatedPoint);

                    int roundedImageX = (int)(imageX + 0.5);
                    int roundedImageY = (int)(imageY + 0.5);

                    // GS_LOG_TRACE_MSG(trace, "RoundedImage X&Y were: (" + std::to_string(roundedImageX) + ", " + std::to_string(roundedImageY) + ").");


                    // If the final, new pixel came from an invalid place, don't allow it to pollute the rotated image
                    // Not rounding here helped increase performance
                    projectedImg_.at<cv::Vec2i>(roundedImageX, roundedImageY)[0] = (int)(ball3dZOfRotatedPoint);

                    /** TBD - DEBUG ONLY 
                    if (currentBall_->PointIsInsideBall(roundedImageX, roundedImageY) && pixelValue == kPixelIgnoreValue) {
                        GS_LOG_TRACE_MSG(trace, "Project2dImageTo3dBall found ignore pixel within ball at (" + std::to_string(roundedImageX) +
                                    ", " + std::to_string(roundedImageY) + ").");
                    }
                    */
                    projectedImg_.at<cv::Vec2i>(roundedImageX, roundedImageY)[1] = (prerotatedPointNotValid ? kPixelIgnoreValue : pixelValue);
            }
            else {
                /** TBD - DEBUG ONLY
                if (currentBall_->PointIsInsideBall(imageX, imageY)) {
                    GS_LOG_TRACE_MSG(trace, "Project2dImageTo3dBall SKIPPED a pixel at (" + std::to_string(imageX) +
                        ", " + std::to_string(imageY) + ").");
                }
                */
            }
        }

        // The ball information that we are currently operating with
        // Null if not yet set
        static const GolfBall* currentBall_;

        // The 3D grayscale image we are working on
        static cv::Mat projectedImg_;

        // The angles to rotate the Mat when we project it to 3D
        static double x_rotation_degreesAngleRad_;
        static double y_rotation_degreesAngleRad_;
        static double z_rotation_degreesAngleRad_;

        // Precomputed trig results for rotation
        static double sinX_;
        static double cosX_;
        static double sinY_;
        static double cosY_;
        static double sinZ_;
        static double cosZ_;

        static bool rotatingOnX_;
        static bool rotatingOnY_;
        static bool rotatingOnZ_;
    };

    // Complete storage for projectionOp struct
    const GolfBall* projectionOp::currentBall_ = NULL;
    cv::Mat projectionOp::projectedImg_;
    double projectionOp::x_rotation_degreesAngleRad_ = 0;
    double projectionOp::y_rotation_degreesAngleRad_ = 0;
    double projectionOp::z_rotation_degreesAngleRad_ = 0;
    double projectionOp::sinX_ = 0;
    double projectionOp::cosX_ = 0;
    double projectionOp::sinY_ = 0;
    double projectionOp::cosY_ = 0;
    double projectionOp::sinZ_ = 0;
    double projectionOp::cosZ_ = 0;
    bool projectionOp::rotatingOnX_ = true;
    bool projectionOp::rotatingOnY_ = true;
    bool projectionOp::rotatingOnZ_ = true;


    // Positive X-axis angles rotate so that the ball appears to go from left to right
    // positive Y-axis angles move the ball from the top to the bottom
    // positive Z-Axis angles are counter-clockwise looking down the positive z-axis
    // The image_gray input Mat is expected to have pixels with only 0, 255, or kPixelIgnoreValue
    cv::Mat BallImageProc::Project2dImageTo3dBall(const cv::Mat& image_gray, const GolfBall& ball, const cv::Vec3i& rotation_angles_degrees) {

        // Create a new 3D Mat to hold the results
        int sizes[2] = { image_gray.rows, image_gray.cols };  // , image_gray.rows };
        // It's possible that due to rotations, some of the 3D image might have "holes" where
        // the pixel was not set to a value.  Make sure anything we don't set is ignored.
        cv::Mat projectedImg = cv::Mat(2, sizes, CV_32SC2, cv::Scalar(0, kPixelIgnoreValue));
        // TBD - hack to pass the 3D image size to the call-back function
        // Kind of a hack, because a 3D Mat won't usually have these values set.  TBD
        projectedImg.rows = image_gray.rows;
        projectedImg.cols = image_gray.cols;

        // Setup the global structures we need before we do the parallelized callback to process
        // the 2D image
        projectionOp::setup(&ball, 
                            projectedImg, 
                            -(float)CvUtils::DegreesToRadians((double)rotation_angles_degrees[0]),  /* Negative due to rotation in X axis being backward */
                            (float)CvUtils::DegreesToRadians((double)rotation_angles_degrees[1]),
                            (float)CvUtils::DegreesToRadians((double)rotation_angles_degrees[2])  );

        if (kSerializeOpsForDebug) {
            /*  Serialized version for debugging - use the parallel stuff below for release */
            for (int x = 0; x < image_gray.cols; x++) {
                for (int y = 0; y < image_gray.rows; y++) {
                    int position[]{ x, y };
                    uchar pixel = image_gray.at<uchar>(x, y);

                    // FOR DEBUG ONLY

                    // TBD - Translate x and y into a new coordinate system that has the origin
                    // at the center of the ball.
                    if (ball.PointIsInsideBall(x, y) && pixel == kPixelIgnoreValue) {
                        GS_LOG_TRACE_MSG(trace, "Project2dImageTo3dBall found ignore pixel within ball at (" + std::to_string(x) + ", " + std::to_string(y) + ").");
                    }


                    projectionOp()(pixel, position);
                }
            }
        }
        else {
            // Parallel execution with function object.
            image_gray.forEach<uchar>(projectionOp());
        }

        return projectedImg;
    }

    void BallImageProc::Unproject3dBallTo2dImage(const cv::Mat& src3D, cv::Mat& destination_image_gray, const GolfBall& ball) {

        // TBD - We already essentially have a 2D Mat.  So why spend all this time copying?
        // Can we just go on to use the 3D Mat?
        // Currently, this function is only used when we need to display one of the 3D projections.
        for (int x = 0; x < destination_image_gray.cols; x++) {
            for (int y = 0; y < destination_image_gray.rows; y++) {
                int position[]{ x, y };
                // There is only one Z-plane in the reduced image - at z = 0
                // The reduced image is a set of uints, so we seem to need to normalize to 0-255 - TBD - why??
                int maxValueZ = src3D.at<cv::Vec2i>(x, y)[0];
                int pixelValue = src3D.at<cv::Vec2i>(x, y)[1];

                int original_pixel_value = (int)destination_image_gray.at<uchar>(x, y);
                /* ONLY FOR DEBUG - TBD
                if (pixelValue != original_pixel_value) {
                    GS_LOG_TRACE_MSG(trace, "Unproject3dBallTo2dImage found different pixel value of " + std::to_string(pixelValue) +
                        " (was " + std::to_string(original_pixel_value) + ") at( " + std::to_string(x) + ", " + std::to_string(y) + ").");
                }
                // std::cout << "pixel from 3D image: " << (int)pixelValue << std::endl;
                */
                destination_image_gray.at<uchar>(x, y) = pixelValue;  // was uchar

                // FOR DEBUG ONLY
                /* ONLY FOR DEBUG - TBD
                if (ball.PointIsInsideBall(x, y) && pixelValue == kPixelIgnoreValue) {
                    GS_LOG_TRACE_MSG(trace, "Unproject3dBallTo2dImage found ignore pixel within ball at (" + std::to_string(x) + ", " + std::to_string(y) + ").");
                }
                */
            }
        }

        // LoggingTools::DebugShowImage("destination_image_gray", destination_image_gray);
        // We're trying to fill in holes here, but this may be fuzzing up the picture too much
        // See if there is a better morphology or interpolation or something
        // TBD- BAD???cv::morphologyEx(destination_image_gray, destination_image_gray, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));

        
        /**** All of the following attempts for hole-filling have failed:
        LoggingTools::DebugShowImage("(open) destination_image_gray", destination_image_gray);

        cv:: Mat kernel = (cv::Mat_<char>(3, 3) << -1, -1, -1, 
                                             -1,  1, -1, 
                                             -1, -1, -1);

        cv::Mat single_pixels;
        cv::morphologyEx(destination_image_gray, single_pixels, cv::MORPH_HITMISS, kernel);
        LoggingTools::DebugShowImage("single_pixels", single_pixels);
        cv::Mat single_pixels_inv;
        cv::bitwise_not(single_pixels, single_pixels_inv);
        LoggingTools::DebugShowImage("single_pixels_inv", single_pixels_inv);
        cv::bitwise_and(destination_image_gray, destination_image_gray, destination_image_gray, single_pixels_inv);
        LoggingTools::DebugShowImage("(closed) destination_image_gray", destination_image_gray);
        

        OR-----------------

        cv::Mat destination_image_grayComplement;
        cv::bitwise_not(destination_image_gray, destination_image_grayComplement);
        LoggingTools::DebugShowImage("destination_image_grayComplement", destination_image_grayComplement);

        int kernel1Data[9] = { 0, 0, 0,
                               0, 1, 0,
                               0, 0, 0 };
        cv::Mat kernel1 = cv::Mat(3, 3, CV_8U, kernel1Data);

        int kernel2Data[9] = { 1, 1, 1,
                               1, 0, 1,
                               1, 1, 1 };
        cv::Mat kernel2 = cv::Mat(3, 3, CV_8U, kernel2Data);

        cv::Mat hitOrMiss1;
        cv::morphologyEx(destination_image_gray, hitOrMiss1, cv::MORPH_HITMISS, kernel2);
        destination_image_gray = hitOrMiss1;
        /*
        cv::morphologyEx(destination_image_gray, hitOrMiss1, cv::MORPH_ERODE, kernel1);
        LoggingTools::DebugShowImage("hitOrMiss1", hitOrMiss1);
        cv::Mat hitOrMiss2;
        cv::morphologyEx(destination_image_grayComplement, hitOrMiss2, cv::MORPH_ERODE, kernel2);
        LoggingTools::DebugShowImage("hitOrMiss2", hitOrMiss2);
        cv::bitwise_and(hitOrMiss1, hitOrMiss2, destination_image_gray);
        */

        // LoggingTools::DebugShowImage("(closed) destination_image_gray", destination_image_gray);
    }

    /**
     * Detection Algorithm Dispatcher
     * Routes detection to HoughCircles or ONNX based on kDetectionMethod configuration
     */
    bool BallImageProc::DetectBalls(const cv::Mat& preprocessed_img, BallSearchMode search_mode, 
                                   std::vector<GsCircle>& detected_circles) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::DetectBalls - Method: " + kDetectionMethod);
        
        if (kDetectionMethod == "legacy") {
            return DetectBallsHoughCircles(preprocessed_img, search_mode, detected_circles);
        } else if (kDetectionMethod == "experimental" || kDetectionMethod == "experimental_sahi") {
            return DetectBallsONNX(preprocessed_img, search_mode, detected_circles);
        } else {
            GS_LOG_MSG(error, "Unknown detection method: " + kDetectionMethod + ". Falling back to legacy.");
            return DetectBallsHoughCircles(preprocessed_img, search_mode, detected_circles);
        }
    }

    /**
     * Legacy HoughCircles Detection (placeholder - will be extracted from existing GetBall method)
     */
    bool BallImageProc::DetectBallsHoughCircles(const cv::Mat& preprocessed_img, BallSearchMode search_mode, 
                                               std::vector<GsCircle>& detected_circles) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::DetectBallsHoughCircles");
        
        // TODO: Extract existing HoughCircles detection logic from GetBall method
        // This will be implemented when we refactor GetBall to use the dispatcher
        GS_LOG_MSG(error, "HoughCircles detection not yet extracted to separate method");
        return false;
    }

    /**
     * ONNX/YOLO Detection Pipeline
     */
    std::vector<int> BallImageProc::SingleClassNMS(const std::vector<cv::Rect>& boxes,
                                                   const std::vector<float>& confidences,
                                                   float conf_threshold,
                                                   float nms_threshold) {
        
        std::vector<int> indices;
        
        std::vector<std::pair<float, int>> confidence_index_pairs;
        confidence_index_pairs.reserve(boxes.size());
        
        for (size_t i = 0; i < confidences.size(); ++i) {
            if (confidences[i] >= conf_threshold) {
                confidence_index_pairs.emplace_back(confidences[i], static_cast<int>(i));
            }
        }
        
        if (confidence_index_pairs.empty()) {
            return indices;
        }
        
        std::sort(confidence_index_pairs.begin(), confidence_index_pairs.end(),
                 [](const auto& a, const auto& b) { return a.first > b.first; });
        
        std::vector<bool> suppressed(confidence_index_pairs.size(), false);
        
        for (size_t i = 0; i < confidence_index_pairs.size(); ++i) {
            if (suppressed[i]) continue;
            
            int idx_i = confidence_index_pairs[i].second;
            indices.push_back(idx_i);
            const cv::Rect& box_i = boxes[idx_i];
            
            for (size_t j = i + 1; j < confidence_index_pairs.size(); ++j) {
                if (suppressed[j]) continue;
                
                int idx_j = confidence_index_pairs[j].second;
                const cv::Rect& box_j = boxes[idx_j];
                
                int x1 = std::max(box_i.x, box_j.x);
                int y1 = std::max(box_i.y, box_j.y);
                int x2 = std::min(box_i.x + box_i.width, box_j.x + box_j.width);
                int y2 = std::min(box_i.y + box_i.height, box_j.y + box_j.height);
                
                int intersection_width = std::max(0, x2 - x1);
                int intersection_height = std::max(0, y2 - y1);
                float intersection_area = static_cast<float>(intersection_width * intersection_height);
                
                float box_i_area = static_cast<float>(box_i.width * box_i.height);
                float box_j_area = static_cast<float>(box_j.width * box_j.height);
                float union_area = box_i_area + box_j_area - intersection_area;
                
                float iou = (union_area > 0) ? (intersection_area / union_area) : 0.0f;
                
                if (iou > nms_threshold) {
                    suppressed[j] = true;
                }
            }
        }
        
        GS_LOG_TRACE_MSG(trace, "SingleClassNMS: " + std::to_string(boxes.size()) + 
                        " boxes -> " + std::to_string(indices.size()) + " after NMS");
        
        return indices;
    }
    
    bool BallImageProc::PreloadYOLOModel() {
        if (yolo_model_loaded_) {
            GS_LOG_MSG(trace, "YOLO model already loaded, skipping preload");
            return true;
        }

        try {
            std::lock_guard<std::mutex> lock(yolo_model_mutex_);

            if (yolo_model_loaded_) {
                GS_LOG_MSG(trace, "YOLO model already loaded by another thread");
                return true;
            }

            GS_LOG_MSG(info, "Preloading YOLO model at startup for detection method: " + kDetectionMethod);
            GS_LOG_MSG(trace, "Loading YOLO model from: " + kONNXModelPath);
            auto start_time = std::chrono::high_resolution_clock::now();
            
            yolo_model_ = cv::dnn::readNetFromONNX(kONNXModelPath);
            if (yolo_model_.empty()) {
                GS_LOG_MSG(error, "Failed to preload ONNX model: " + kONNXModelPath);
                return false;
            }
            
            if (kONNXDeviceType == "CPU") {
                yolo_model_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                yolo_model_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            } else {
                yolo_model_.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                yolo_model_.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
            }
            
            yolo_letterbox_buffer_ = cv::Mat(kONNXInputSize, kONNXInputSize, CV_8UC3);
            yolo_detection_boxes_.reserve(10);  // Max 10 golf balls
            yolo_detection_confidences_.reserve(10);
            yolo_outputs_.reserve(3);  // Network typically has 1-3 output layers
            
            yolo_model_loaded_ = true;
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            GS_LOG_MSG(trace, "YOLO model preloaded successfully in " + 
                            std::to_string(duration.count()) + "ms. First detection will be fast!");
            
            return true;
        } catch (const cv::Exception& e) {
            GS_LOG_MSG(error, "OpenCV exception during YOLO model preload: " + std::string(e.what()));
            return false;
        } catch (const std::exception& e) {
            GS_LOG_MSG(error, "Exception during YOLO model preload: " + std::string(e.what()));
            return false;
        } catch (...) {
            GS_LOG_MSG(error, "Unknown exception during YOLO model preload");
            return false;
        }
    }

    bool BallImageProc::DetectBallsONNX(const cv::Mat& preprocessed_img, BallSearchMode search_mode,
                                       std::vector<GsCircle>& detected_circles) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::DetectBallsONNX - Dispatching to backend: " + kONNXBackend);

        // Dual-Backend Dispatcher: Try ONNX Runtime first, fallback to OpenCV DNN if needed
        if (kONNXBackend == "onnxruntime") {
            if (DetectBallsONNXRuntime(preprocessed_img, search_mode, detected_circles)) {
                return true;
            } else if (kONNXRuntimeAutoFallback) {
                GS_LOG_MSG(warning, "ONNX Runtime detection failed, falling back to OpenCV DNN");
                return DetectBallsOpenCVDNN(preprocessed_img, search_mode, detected_circles);
            } else {
                return false;
            }
        } else {
            // Use OpenCV DNN backend directly
            return DetectBallsOpenCVDNN(preprocessed_img, search_mode, detected_circles);
        }
    }

    bool BallImageProc::DetectBallsONNXRuntime(const cv::Mat& preprocessed_img, BallSearchMode search_mode,
                                              std::vector<GsCircle>& detected_circles) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::DetectBallsONNXRuntime - Using optimized detector");

        try {
            // Initialize detector only once with double-checked locking pattern (optimized for Pi)
            if (!onnx_detector_initialized_.load(std::memory_order_acquire)) {
                std::lock_guard<std::mutex> lock(onnx_detector_mutex_);
                if (!onnx_detector_initialized_.load(std::memory_order_relaxed)) {

                    // Configure detector with static configuration
                    ONNXRuntimeDetector::Config config;
                    config.model_path = kONNXModelPath;
                    config.confidence_threshold = kONNXConfidenceThreshold;
                    config.nms_threshold = kONNXNMSThreshold;
                    config.input_width = kONNXInputSize;
                    config.input_height = kONNXInputSize;
                    config.num_threads = kONNXRuntimeThreads;

                    // Pi-optimized settings
                    config.use_arm_compute_library = true;
                    config.use_thread_affinity = true;
                    config.use_memory_pool = true;
                    config.use_neon_preprocessing = true;
                    config.use_zero_copy = true;

                    GS_LOG_MSG(info, "Attempting to initialize ONNX Runtime detector with model: " + config.model_path);
                    onnx_detector_ = std::make_unique<ONNXRuntimeDetector>(config);

                    if (!onnx_detector_->Initialize()) {
                        GS_LOG_MSG(error, "Failed to initialize ONNX Runtime detector with model: " + config.model_path);
                        onnx_detector_.reset();  // Clean up failed detector
                        return false;
                    }

                    onnx_detector_initialized_.store(true, std::memory_order_release);
                    GS_LOG_MSG(info, "ONNX Runtime detector initialized successfully");
                }
            }

            // Convert to RGB if needed (minimal overhead)
            cv::Mat input_image;
            if (preprocessed_img.channels() == 1) {
                cv::cvtColor(preprocessed_img, input_image, cv::COLOR_GRAY2RGB);
            } else {
                input_image = preprocessed_img;  // Use directly (no copy)
            }

            // Handle SAHI slicing if enabled
            if (kDetectionMethod == "experimental_sahi") {
                std::vector<cv::Mat> slices;
                slices.reserve(16);  // Pre-allocate for typical slice count

                const int overlap = static_cast<int>(kSAHISliceWidth * kSAHIOverlapRatio);
                for (int y = 0; y < input_image.rows; y += kSAHISliceHeight - overlap) {
                    for (int x = 0; x < input_image.cols; x += kSAHISliceWidth - overlap) {
                        cv::Rect slice_rect(x, y,
                                           std::min(kSAHISliceWidth, input_image.cols - x),
                                           std::min(kSAHISliceHeight, input_image.rows - y));
                        slices.push_back(input_image(slice_rect));
                    }
                }

                // Process all slices in batch for efficiency
                std::vector<std::vector<ONNXRuntimeDetector::Detection>> batch_detections =
                    onnx_detector_->DetectBatch(slices);

                // Convert and merge all detections
                detected_circles.clear();
                detected_circles.reserve(batch_detections.size() * 2);  // Estimate

                size_t slice_idx = 0;
                for (int y = 0; y < input_image.rows; y += kSAHISliceHeight - overlap) {
                    for (int x = 0; x < input_image.cols; x += kSAHISliceWidth - overlap) {
                        if (slice_idx < batch_detections.size()) {
                            for (const auto& detection : batch_detections[slice_idx]) {
                                GsCircle circle;
                                circle[0] = detection.bbox.x + detection.bbox.width * 0.5f + x;   // center_x
                                circle[1] = detection.bbox.y + detection.bbox.height * 0.5f + y;  // center_y
                                circle[2] = std::max(detection.bbox.width, detection.bbox.height) * 0.5f;  // radius
                                detected_circles.push_back(circle);
                            }
                        }
                        ++slice_idx;
                    }
                }
            } else {
                // Single image detection (fastest path)
                std::vector<ONNXRuntimeDetector::Detection> detections = onnx_detector_->Detect(input_image);

                // Convert ONNXRuntimeDetector::Detection to GsCircle format
                detected_circles.clear();
                detected_circles.reserve(detections.size());

                for (const auto& detection : detections) {
                    GsCircle circle;
                    circle[0] = detection.bbox.x + detection.bbox.width * 0.5f;   // center_x
                    circle[1] = detection.bbox.y + detection.bbox.height * 0.5f;  // center_y
                    circle[2] = std::max(detection.bbox.width, detection.bbox.height) * 0.5f;  // radius
                    detected_circles.push_back(circle);
                }
            }

            GS_LOG_TRACE_MSG(trace, "ONNX Runtime detected " + std::to_string(detected_circles.size()) + " balls");
            return !detected_circles.empty();

        } catch (const std::exception& e) {
            GS_LOG_MSG(error, "ONNX Runtime detection failed: " + std::string(e.what()));
            return false;
        }
    }


    bool BallImageProc::DetectBallsOpenCVDNN(const cv::Mat& preprocessed_img, BallSearchMode search_mode,
                                            std::vector<GsCircle>& detected_circles) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::DetectBallsOpenCVDNN - Fallback backend");

        try {
            {
                std::lock_guard<std::mutex> lock(yolo_model_mutex_);
                if (!yolo_model_loaded_) {
                    GS_LOG_MSG(trace, "Loading YOLO model for OpenCV DNN backend...");
                    auto start_time = std::chrono::high_resolution_clock::now();

                    yolo_model_ = cv::dnn::readNetFromONNX(kONNXModelPath);
                    if (yolo_model_.empty()) {
                        GS_LOG_MSG(error, "Failed to load ONNX model for OpenCV DNN: " + kONNXModelPath);
                        return false;
                    }

                    // Set backend and target
                    if (kONNXDeviceType == "CPU") {
                        yolo_model_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                        yolo_model_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    } else {
                        yolo_model_.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                        yolo_model_.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                    }

                    yolo_letterbox_buffer_ = cv::Mat(kONNXInputSize, kONNXInputSize, CV_8UC3);
                    yolo_detection_boxes_.reserve(50);
                    yolo_detection_confidences_.reserve(50);
                    yolo_outputs_.reserve(3);

                    yolo_model_loaded_ = true;

                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                    GS_LOG_MSG(info, "OpenCV DNN model loaded successfully in " +
                                   std::to_string(duration.count()) + "ms (fallback backend)");
                }
            }

            auto processing_start_time = std::chrono::high_resolution_clock::now();
            GS_LOG_MSG(trace, "OpenCV DNN processing started.");

            cv::Mat input_image;
            if (preprocessed_img.channels() == 1) {
                cv::cvtColor(preprocessed_img, input_image, cv::COLOR_GRAY2RGB);
            } else if (preprocessed_img.channels() == 3) {
                input_image = preprocessed_img;
            } else {
                GS_LOG_MSG(error, "Unsupported number of channels: " + std::to_string(preprocessed_img.channels()));
                return false;
            }

            // SAHI slicing
            bool use_sahi = (kDetectionMethod == "experimental_sahi");
            std::vector<cv::Rect> slices;

            if (use_sahi) {
                int overlap = static_cast<int>(kSAHISliceWidth * kSAHIOverlapRatio);
                for (int y = 0; y < input_image.rows; y += kSAHISliceHeight - overlap) {
                    for (int x = 0; x < input_image.cols; x += kSAHISliceWidth - overlap) {
                        cv::Rect slice(x, y,
                                      std::min(kSAHISliceWidth, input_image.cols - x),
                                      std::min(kSAHISliceHeight, input_image.rows - y));
                        slices.push_back(slice);
                    }
                }
                GS_LOG_TRACE_MSG(trace, "OpenCV DNN SAHI: Created " + std::to_string(slices.size()) + " slices");
            } else {
                slices.push_back(cv::Rect(0, 0, input_image.cols, input_image.rows));
            }

            yolo_detection_boxes_.clear();
            yolo_detection_confidences_.clear();

            for (const auto& slice : slices) {
                cv::Mat slice_img = input_image(slice);

                // Create letterboxed input
                float scale = std::min(float(kONNXInputSize) / slice_img.cols,
                                     float(kONNXInputSize) / slice_img.rows);
                int new_width = int(slice_img.cols * scale);
                int new_height = int(slice_img.rows * scale);

                if (yolo_resized_buffer_.size() != cv::Size(new_width, new_height) || yolo_resized_buffer_.type() != CV_8UC3) {
                    yolo_resized_buffer_ = cv::Mat(new_height, new_width, CV_8UC3);
                }
                cv::resize(slice_img, yolo_resized_buffer_, cv::Size(new_width, new_height));

                // Create letterbox with gray padding
                yolo_letterbox_buffer_.setTo(cv::Scalar(114, 114, 114));
                int x_offset = (kONNXInputSize - new_width) / 2;
                int y_offset = (kONNXInputSize - new_height) / 2;
                yolo_resized_buffer_.copyTo(yolo_letterbox_buffer_(cv::Rect(x_offset, y_offset, new_width, new_height)));

                // Create blob
                cv::dnn::blobFromImage(yolo_letterbox_buffer_, yolo_blob_buffer_, 1.0/255.0,
                                      cv::Size(kONNXInputSize, kONNXInputSize),
                                      cv::Scalar(), false, false);  // swapRB=false for YOLOv8 BGR input

                // Run inference
                yolo_model_.setInput(yolo_blob_buffer_);
                yolo_outputs_.clear();
                yolo_model_.forward(yolo_outputs_, yolo_model_.getUnconnectedOutLayersNames());

                // Parse output 
                if (!yolo_outputs_.empty()) {
                    cv::Mat output = yolo_outputs_[0];

                    // Reshape and transpose YOLOv8 output to [detections, features] format
                    if (output.dims == 3 && output.size[0] == 1) {
                        output = output.reshape(1, output.size[1]); // [5, num_detections]
                        cv::transpose(output, output);              // [num_detections, 5]
                    }

                    float* data = (float*)output.data;
                    int num_detections = output.rows;

                    for (int i = 0; i < num_detections; ++i) {
                        float* detection = data + i * output.cols;

                        float cx_letterbox = detection[0];
                        float cy_letterbox = detection[1];
                        float w_letterbox = detection[2];
                        float h_letterbox = detection[3];
                        float confidence = detection[4];

                        if (confidence >= kONNXConfidenceThreshold) {
                            // Convert from letterbox coordinates back to slice coordinates
                            float cx_slice = (cx_letterbox - x_offset) / scale;
                            float cy_slice = (cy_letterbox - y_offset) / scale;
                            float w_slice = w_letterbox / scale;
                            float h_slice = h_letterbox / scale;

                            // Convert center format to top-left format
                            int x = static_cast<int>(cx_slice - w_slice/2) + slice.x;
                            int y = static_cast<int>(cy_slice - h_slice/2) + slice.y;
                            int w = static_cast<int>(w_slice);
                            int h = static_cast<int>(h_slice);

                            // Bounds checking
                            if (w > 0 && h > 0 && x >= 0 && y >= 0 &&
                                x + w <= input_image.cols && y + h <= input_image.rows) {
                                yolo_detection_boxes_.push_back(cv::Rect(x, y, w, h));
                                yolo_detection_confidences_.push_back(confidence);
                            }
                        }
                    }
                }
            }

            // Apply NMS and convert to circles
            std::vector<int> indices = SingleClassNMS(yolo_detection_boxes_, yolo_detection_confidences_,
                                                      kONNXConfidenceThreshold, kONNXNMSThreshold);

            detected_circles.clear();
            detected_circles.reserve(indices.size());
            for (int idx : indices) {
                const cv::Rect& box = yolo_detection_boxes_[idx];
                GsCircle circle;
                circle[0] = box.x + box.width / 2;
                circle[1] = box.y + box.height / 2;
                circle[2] = std::max(box.width, box.height) / 2;
                detected_circles.push_back(circle);
            }

            auto processing_end_time = std::chrono::high_resolution_clock::now();
            auto processing_duration = std::chrono::duration_cast<std::chrono::milliseconds>(processing_end_time - processing_start_time);
            GS_LOG_MSG(trace, "OpenCV DNN completed processing in " + std::to_string(processing_duration.count()) + " ms (fallback)");

            GS_LOG_TRACE_MSG(trace, "OpenCV DNN detected " + std::to_string(detected_circles.size()) + " balls after NMS");
            return !detected_circles.empty();

        } catch (const cv::Exception& e) {
            GS_LOG_MSG(error, "OpenCV DNN detection failed: " + std::string(e.what()));
            return false;
        } catch (const std::exception& e) {
            GS_LOG_MSG(error, "OpenCV DNN fallback detection failed: " + std::string(e.what()));
            return false;
        }
    }

    bool BallImageProc::PreloadONNXRuntimeModel() {
        if (onnx_detector_initialized_.load(std::memory_order_relaxed)) {
            GS_LOG_MSG(trace, "ONNX Runtime detector already preloaded, skipping");
            return true;
        }

        std::lock_guard<std::mutex> lock(onnx_detector_mutex_);
        if (onnx_detector_initialized_.load(std::memory_order_relaxed)) {
            GS_LOG_MSG(trace, "ONNX Runtime detector already preloaded by another thread");
            return true;
        }

        GS_LOG_MSG(info, "Preloading ONNX Runtime detector for ARM64 optimization...");

        try {
            auto start_time = std::chrono::high_resolution_clock::now();

            // Configure detector with static configuration
            ONNXRuntimeDetector::Config config;
            config.model_path = kONNXModelPath;
            config.confidence_threshold = kONNXConfidenceThreshold;
            config.nms_threshold = kONNXNMSThreshold;
            config.input_width = kONNXInputSize;
            config.input_height = kONNXInputSize;
            config.num_threads = kONNXRuntimeThreads;

            // Pi-optimized settings
            config.use_arm_compute_library = true;
            config.use_thread_affinity = true;
            config.use_memory_pool = true;
            config.use_neon_preprocessing = true;
            config.use_zero_copy = true;

            onnx_detector_ = std::make_unique<ONNXRuntimeDetector>(config);

            if (!onnx_detector_->Initialize()) {
                GS_LOG_MSG(error, "Failed to initialize ONNX Runtime detector");
                return false;
            }

            onnx_detector_initialized_.store(true, std::memory_order_release);

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            GS_LOG_MSG(info, "ONNX Runtime detector preloaded successfully in " +
                           std::to_string(duration.count()) + "ms with " +
                           std::to_string(kONNXRuntimeThreads) + " threads (ARM64 optimized)");
            return true;

        } catch (const std::exception& e) {
            GS_LOG_MSG(error, "Failed to preload ONNX Runtime detector: " + std::string(e.what()));
            return false;
        }
    }

    void BallImageProc::CleanupONNXRuntime() {
        std::lock_guard<std::mutex> lock(onnx_detector_mutex_);
        if (onnx_detector_initialized_.load(std::memory_order_relaxed)) {
            GS_LOG_MSG(info, "Cleaning up ONNX Runtime detector...");

            onnx_detector_.reset();
            onnx_detector_initialized_.store(false, std::memory_order_release);

            GS_LOG_MSG(info, "ONNX Runtime detector cleanup completed");
        }
    }

    void BallImageProc::LoadConfigurationValues() {
        // This function should be called AFTER GolfSimConfiguration::Initialize() has loaded the JSON config
        // It reads the ONNX configuration values FROM the JSON and updates the static variables

        GS_LOG_MSG(info, "Loading BallImageProc configuration values from JSON...");

        // Read ONNX/AI Detection configuration values
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXModelPath", kONNXModelPath);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kDetectionMethod", kDetectionMethod);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBallPlacementDetectionMethod", kBallPlacementDetectionMethod);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXConfidenceThreshold", kONNXConfidenceThreshold);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXNMSThreshold", kONNXNMSThreshold);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXInputSize", kONNXInputSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXBackend", kONNXBackend);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXRuntimeAutoFallback", kONNXRuntimeAutoFallback);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXRuntimeThreads", kONNXRuntimeThreads);

        GS_LOG_MSG(info, "Loaded ONNX Model Path: " + kONNXModelPath);
        GS_LOG_MSG(info, "Loaded Detection Method: " + kDetectionMethod);
        GS_LOG_MSG(info, "Loaded Backend: " + kONNXBackend);

        if (!kONNXModelPath.empty()) {
            std::ifstream model_file(kONNXModelPath);
            if (model_file.good()) {
                GS_LOG_MSG(info, "ONNX model file verified to exist at: " + kONNXModelPath);
                model_file.close();
            } else {
                GS_LOG_MSG(error, "ONNX model file NOT FOUND at: " + kONNXModelPath);
            }
        }
    }

}

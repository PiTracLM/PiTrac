/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Performs image processing such as finding a ball in a picture.
// TBD - The separation of responsibilities with the golf_sim_camera needs to be clarified.

#pragma once


#include <iostream>
#include <filesystem>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include "logging_tools.h"
#include "gs_camera.h"
#include "colorsys.h"
#include "golf_ball.h"


namespace golf_sim {

// When comparing what are otherwise b/w images, this value indicates that
// the comparison should not be performed on the particular pixel
const uchar kPixelIgnoreValue = 128;

// Holds one potential rotated golf ball candidate image and associated data
struct RotationCandidate {
    short index = 0;
    cv::Mat img;
    int x_rotation_degrees = 0; // All Rotations are in degrees
    int y_rotation_degrees = 0;
    int z_rotation_degrees = 0;
    int pixels_examined = 0;
    int pixels_matching = 0;
    double score = 0;
};

class BallImageProc
{
public:

    // The following are constants that control how the ball spin algorithm and the
    // ball (circle) identification works.  They are set from the configuration .json file

    static int kCoarseXRotationDegreesIncrement;
    static int kCoarseXRotationDegreesStart;
    static int kCoarseXRrotationDegreesEnd;
    static int kCoarseYRotationDegreesIncrement;
    static int kCoarseYRotationDegreesStart;
    static int kCoarseYRotationDegreesEnd;
    static int kCoarseZRotationDegreesIncrement;
    static int kCoarseZRotationDegreesStart;
    static int kCoarseZRotationDegreesEnd;

    static double kPlacedBallCannyLower;
    static double kPlacedBallCannyUpper;
    static double kPlacedBallStartingParam2;
    static double kPlacedBallMinParam2;
    static double kPlacedBallMaxParam2;
    static double kPlacedBallCurrentParam1;
    static double kPlacedBallParam2Increment;
    static double kPlacedMinHoughReturnCircles;
    static int kPlacedPreHoughBlurSize;
    static int kPlacedPreCannyBlurSize;
    static double kPlacedMaxHoughReturnCircles;

    static double kStrobedBallsCannyLower;
    static double kStrobedBallsCannyUpper;
    static int kStrobedBallsPreCannyBlurSize;
    static int kStrobedBallsPreHoughBlurSize;

    static double kStrobedBallsStartingParam2;
    static double kStrobedBallsMinParam2;
    static double kStrobedBallsMaxParam2;
    static double kStrobedBallsCurrentParam1;
    static double kStrobedBallsParam2Increment;
    static double kStrobedBallsMinHoughReturnCircles;
    static double kStrobedBallsMaxHoughReturnCircles;
    static int kPuttingPreHoughBlurSize;

    static bool kStrobedBallsUseAltHoughAlgorithm;
    static double kStrobedBallsAltCannyLower;
    static double kStrobedBallsAltCannyUpper;
    static int kStrobedBallsAltPreCannyBlurSize;
    static int kStrobedBallsAltPreHoughBlurSize;
    static double kStrobedBallsAltStartingParam2;
    static double kStrobedBallsAltMinParam2;
    static double kStrobedBallsAltMaxParam2;
    static double kStrobedBallsAltCurrentParam1;
    static double kStrobedBallsAltHoughDpParam1;
    static double kStrobedBallsAltParam2Increment;


    static double kPuttingBallStartingParam2;
    static double kPuttingBallMinParam2;
    static double kPuttingBallMaxParam2;
    static double kPuttingBallCurrentParam1;
    static double kPuttingBallParam2Increment;
    static double kPuttingMinHoughReturnCircles;
    static double kPuttingMaxHoughReturnCircles;
    static double kPuttingHoughDpParam1;

    // TBD - Some of these are redundant - put 'em all in ball_image_proc or in gs_camera, but not both
    static double kExternallyStrobedEnvBallCurrentParam1;
    static double kExternallyStrobedEnvBallMinParam2;
    static double kExternallyStrobedEnvBallMaxParam2;
    static double kExternallyStrobedEnvBallStartingParam2;
    static double kExternallyStrobedEnvBallNarrowingParam2;
    static double kExternallyStrobedEnvBallNarrowingDpParam;

    static double kExternallyStrobedEnvBallParam2Increment;
    static double kExternallyStrobedEnvMinHoughReturnCircles;
    static double kExternallyStrobedEnvMaxHoughReturnCircles;
    static double kExternallyStrobedEnvPreHoughBlurSize;
    static double kExternallyStrobedEnvPreCannyBlurSize;

    static double kExternallyStrobedEnvHoughDpParam1;
    static double kExternallyStrobedEnvMinimumSearchRadius;
    static double kExternallyStrobedEnvMaximumSearchRadius;
    static double kStrobedNarrowingRadiiDpParam;
    static double kStrobedNarrowingRadiiParam2;
    static double kExternallyStrobedEnvBallNarrowingPreCannyBlurSize;
    static double kExternallyStrobedEnvBallNarrowingPreHoughBlurSize;


    static bool kUseDynamicRadiiAdjustment;
    static int kNumberRadiiToAverageForDynamicAdjustment;
    static double kStrobedNarrowingRadiiMinRatio;
    static double kStrobedNarrowingRadiiMaxRatio;

    static double kPlacedNarrowingRadiiMinRatio;
    static double kPlacedNarrowingRadiiMaxRatio;
    static double kPlacedNarrowingStartingParam2;
    static double kPlacedNarrowingRadiiDpParam;
    static double kPlacedNarrowingParam1;


    static bool kLogIntermediateSpinImagesToFile;
    static double kPlacedBallHoughDpParam1;
    static double kStrobedBallsHoughDpParam1;
    static bool kUseBestCircleRefinement;
    static bool kUseBestCircleLargestCircle;

    static double kBestCircleCannyLower;
    static double kBestCircleCannyUpper;
    static double kBestCirclePreCannyBlurSize;
    static double kBestCirclePreHoughBlurSize;
    static double kBestCircleParam1;
    static double kBestCircleParam2;
    static double kBestCircleHoughDpParam1;

    static double kExternallyStrobedBestCircleCannyLower;
    static double kExternallyStrobedBestCircleCannyUpper;
    static double kExternallyStrobedBestCirclePreCannyBlurSize;
    static double kExternallyStrobedBestCirclePreHoughBlurSize;
    static double kExternallyStrobedBestCircleParam1;
    static double kExternallyStrobedBestCircleParam2;
    static double kExternallyStrobedBestCircleHoughDpParam1;


    // TBD - Identifying the "best" circle after doing a rough circle identification
    // may be going away.
    static double kBestCircleIdentificationMinRadiusRatio;
    static double kBestCircleIdentificationMaxRadiusRatio;

    static int kGaborMaxWhitePercent;
    static int kGaborMinWhitePercent;


    // This determines which potential 3D angles will be searched for spin processing
    struct RotationSearchSpace {
        int anglex_rotation_degrees_increment = 0;
        int anglex_rotation_degrees_start = 0;
        int anglex_rotation_degrees_end = 0;
        int angley_rotation_degrees_increment = 0;
        int angley_rotation_degrees_start = 0;
        int angley_rotation_degrees_end = 0;
        int anglez_rotation_degrees_increment = 0;
        int anglez_rotation_degrees_start = 0;
        int anglez_rotation_degrees_end = 0;
    };

    // The image in which to try to identify a golf ball - set prior to calling
    // the identification methods
    cv::Mat img_;

    // The ball image processing works in the context of a golf ball
    GolfBall ball_;

    // Any radius less than 0.0 means it is currently unknown
    // If set, searches for balls will be limited to this radius range
    int min_ball_radius_ = -1;
    int max_ball_radius_ = -1;

    // This will be used in any debug windows to identify the image
    std::string image_name_;

    // These will be returned for potential debugging
    // Color-based masking was an early technique that we're moving away from
    cv::Mat color_mask_image_;

    // The location mask is a total(black or white) mask to subset the image down to just
    // the area(s) that we are interested in
    cv::Mat area_mask_image_;

    // Shows the points of the image that were considered as possibly being the golf ball
    cv::Mat candidates_image_;
    
    // Shows the ball that was identified with a circle and center point on top of original image
    cv::Mat final_result_image_;

    BallImageProc();
    ~BallImageProc();

    enum BallSearchMode {
        kUnknown = 0,
        kFindPlacedBall = 1,
        kStrobed = 2,
        kExternalStrobe = 3,
        kPutting = 4
    };

    // Find a golf ball in the picture - this is the main workhorse of the system.
    // if the baseBallWithSearchParams has color information, that information 
    // will be used to search for the ball in the picture
    // The returned balls will have at least the following ball information accurately set:
    //      circle
    // If chooseLargestFinalBall is set true, then even a poorer-matching final ball candidate will be chosen over a smaller, better-scored candidate.
    // If expectingBall is true, then the system will not be as picky when trying to find a ball.  Otherwise, if false (when the system does not
    // know if a ball will be present), the system will require a more perfect ball in order to reduce false positives.
    bool GetBall(  const cv::Mat& img, 
                   const GolfBall& baseBallWithSearchParams, 
                   std::vector<GolfBall> &return_balls, 
                   cv::Rect& expectedBallArea, 
                   BallSearchMode search_mode,
                   bool chooseLargestFinalBall=false,
                   bool report_find_failures =true );

    bool BallIsPresent(const cv::Mat& img);

    // Performs some iterative refinement to try to identify the best ball circle.
    static bool DetermineBestCircle(const cv::Mat& gray_image,
                                    const GolfBall& reference_ball,
                                    bool choose_largest_final_ball,
                                    GsCircle& final_circle);


    // Waits for movement behind the ball (i.e., the club) and returns the first image containing the movement
    // Ignores the first <X> seconds for movement.
    static bool WaitForBallMovement(GolfSimCamera& c, cv::Mat& firstMovementImage, const GolfBall& ball, const long waitTimeSecs);

    // Inputs are two balls and the images within which those balls exist
    // Returns the estimated amount of rotation in x, y, and z axes in degrees
    static cv::Vec3d GetBallRotation(const cv::Mat& full_gray_image1, 
                                    const GolfBall& ball1, 
                                    const cv::Mat& full_gray_image2, 
                                    const GolfBall& ball2);

    static bool ComputeCandidateAngleImages(const cv::Mat& base_dimple_image, 
                                    const RotationSearchSpace& search_space, 
                                    cv::Mat& output_candidate_mat, 
                                    cv::Vec3i& output_candidate_elements_mat_size, 
                                    std::vector< RotationCandidate>& output_candidates, 
                                    const GolfBall& ball);

    // Returns the index within candidates that has the best comparison.
    // Returns -1 on failure.
    static int CompareCandidateAngleImages(const cv::Mat* target_image,
                                            const cv::Mat* candidate_elements_mat,
                                            const cv::Vec3i* candidate_elements_mat_size,
                                            std::vector<RotationCandidate>* candidates,
                                            std::vector<std::string>& comparison_csv_data);

    static cv::Vec2i CompareRotationImage(const cv::Mat& img1, const cv::Mat& img2, const int index = 0);

    static cv::Mat MaskAreaOutsideBall(cv::Mat& ball_image, const GolfBall& ball, float mask_reduction_factor, const cv::Scalar& maskValue = (255, 255, 255));

    static void GetRotatedImage(const cv::Mat& gray_2D_input_image, const GolfBall& ball, const cv::Vec3i rotation, cv::Mat& outputGrayImg);

    // Img would be a constant reference, but we need to perform sub-imaging on it, so keep non-const for now
    // reference_ball_circle is the circle around where the best approximation of where the ball is
    static cv::RotatedRect FindLargestEllipse(cv::Mat& img, const GsCircle& reference_ball_circle, int mask_radius);
    static cv::RotatedRect FindBestEllipseFornaciari(cv::Mat& img, 
                                                    const GsCircle& reference_ball_circle, 
                                                    int mask_radius);

    cv::Mat GetColorMaskImage(const cv::Mat& hsvImage, const GolfBall& ball, double wideningAmount = 0.0);
    static cv::Mat GetColorMaskImage(const cv::Mat& hsvImage,
        const GsColorTriplet input_lowerHsv,
        const GsColorTriplet input_upperHsv,
        double wideningAmount = 0.0);


private:

    // When we create a candidate ball list, the elements of that list include not only 
    // the ball, but also the ball identifier(e.g., 1, 2...),
    // as well as information about the difference between the ball's average/median/std color versus the expected color.
    // The following constants identify where in each element the information is

    struct CircleCandidateListElement {
        std::string     name;
        GsCircle        circle;
        double          calculated_color_difference;
        int             found_radius;
        GsColorTriplet  avg_RGB;
        float           rgb_avg_diff;
        float           rgb_median_diff;
        float           rgb_std_diff;
    };

    static std::string FormatCircleCandidateElement(const struct CircleCandidateListElement& e);
    static std::string FormatCircleCandidateList(const std::vector<struct CircleCandidateListElement>& e);

    // This is an early attempt to remove lines from an image, such as those caused when using the 
    // system with another strobe-based launch monitor
    static bool RemoveLinearNoise(cv::Mat& img);

    inline bool CompareColorDiff(const CircleCandidateListElement& a, const CircleCandidateListElement& b)
    {
        return (a.calculated_color_difference < b.calculated_color_difference);
    }

    void RoundCircleData(std::vector<GsCircle>& circles);

    static cv::Rect GetAreaOfInterest(const GolfBall& ball, const cv::Mat& img);

    // Assumes the ball is fully within the image.
    // Updates the input ball1 to reflect the new position of the ball within the isolated image we are returning.
    static cv::Mat IsolateBall(const cv::Mat& img, GolfBall& ball);

    static cv::Mat ReduceReflections(const cv::Mat& img, const cv::Mat& mask);

    // Will set pixels that were over-saturated in the original_image to be the special "ignore" kPixelIgnoreValue value
    // in the filtered_image.
    static void RemoveReflections(const cv::Mat& original_image, cv::Mat& filtered_image, const cv::Mat& mask);

    // If prior_binary_threshold < 0, then there is no prior threshold and a new one will be determined and returns 
    // in the calibrated_binary_threshold variable.
    static cv::Mat ApplyGaborFilterToBall(const cv::Mat& img, const GolfBall& ball, float& calibrated_binary_threshold, float prior_binary_threshold = -1);

    // Applies the gabor filter with the specified parameters and returns the final image and white percentage
    static cv::Mat ApplyTestGaborFilter(const cv::Mat& img_f32,
        const int kernel_size, double sig, double lm, double th, double ps, double gm, float binary_threshold,
        int& white_percent);

    static cv::Mat CreateGaborKernel(int ks, double sig, double th, double lm, double gm, double ps);

    static cv::Mat Project2dImageTo3dBall(const cv::Mat& image_gray, const GolfBall& ball, const cv::Vec3i& rotation_angles_degrees);

    static void Unproject3dBallTo2dImage(const cv::Mat& src3D, cv::Mat& destination_image_gray, const GolfBall& ball);

    // Given a grayscale (0-255) image and a percentage, this returns in brightness_cutoff from 0-255 
    // that represents the value at which brightness_percentage of the pixels in the image are at or 
    // below that value
    static void GetImageCharacteristics(const cv::Mat& img,
                                        const int brightness_percentage,
                                        int& brightness_cutoff,
                                        int& lowest_brightness,
                                        int& highest_brightness);

};

}

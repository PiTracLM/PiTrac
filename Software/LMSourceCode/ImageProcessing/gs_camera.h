/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

/*
    The golf-sim-camera module operates the hardware camera and deals with tasks 
    The module generally interfaces to the rest of the system by taking images as input 
    and by producing golf_ball objects as output.

    An important function of this class is to identify a set of potential golf balls (circles)
    from a strobed image that may include multiple, possibly-overlapping golf balls.
    See U.S. Patent Application No. 18/428,191 for more details.
*/

#include <string>
#include "logging_tools.h"
#include "cv_utils.h"
#include "gs_globals.h"
#include "camera_hardware.h"
#include "golf_ball.h"

namespace golf_sim {

    struct GsBallAndTimingElement {
        GolfBall ball;
        double time_interval_before_ball_ms = 0;
    };

    using GsBallsAndTimingVector = std::vector<GsBallAndTimingElement>;

    // This structure models a multi-dimensional goodness metric between
    // a pair of balls.  A pair with a good score is a candidate to be used
    // to compare to on another to determine ball spin.
    // 
    // Higher pair scores are better.  Each sub-score should attempt to
    // be between 0 (no good) to 10 (great)
    struct GsBallPairAndSpinCandidateScoreElement {
        GolfBall ball1;
        GolfBall ball2;

        // -1 means not set
        int ball1_index = -1;
        int ball2_index = -1;

        // Each ball will downgrade this score if it is close to the edge
        // (where it is likely to be smeared or otherwise distorted)
        double edge_proximity_score = 0;
        // The closer the balls are the better (lower) the score
        double pair_proximity_score = 0;
        double color_std_score = 0;
        double middle_proximity_score = 0;
        // If the user's leg is likely to be where the ball is (depends on handedness),
        // that's probably a worse ball
        double leg_proximity_score = 0;
        // The closer the two balls are in radii, the better the spin calculation will 
        // likely be.
        double radius_similarity_score = 0;

        double total_pair_score = 0;
    };


    class GolfSimCamera
    {
    public:


        // When running in Windows(instead of the Pi), the following image will
        // be used by default to simulate the PiCameras taking a real picture
        // Other alternatives:
        //FAKE_PHOTO = "./FakePiCameraPhotoOfGolfBall-Clr-Yellow-Flat.png";
        //FAKE_PHOTO = "./FakePiCameraPhotoOfGolfBall-Clr-White-Flat.png";   // Having problems with this one
        static constexpr std::string_view FAKE_PHOTO = "../Images/FakePiCameraPhotoOfGolfBall-Clr-White-2-feet-HiRes_01.png";

        // How much larger the mask should be than the expected size of the ball
        // This allows the calibration to work even if the ball is a little off-center
        static float kBallAreaMaskRadiusRatio;

        // Once the ball has been moved (hit), we expect it's radius will be relatively close to the original radius
        // where is started (or close) as a minimum, as well as a little larger or smaller.
        // Max is a little larger to adjust for possible error and stretching out of balls near the edge
        static float kMaxMovedBallRadiusRatio;  
        static float kMinMovedBallRadiusRatio;

        // Determines whether and how intermediate processing images (of which there are over a dozen) are logged to the 
        // file system.  Doing so is expensive, time-wise.
        static bool kLogIntermediateExposureImagesToFile;
        static bool kLogWebserverImagesToFile;
        static bool kLogDiagnosticImagesToUniqueFiles;

        // The remainder of these constants control the way that the (probably overly-complicated) ball-identification 
        // processing works.  Best to see how they are used in the rest of the code to understand them.
        // These constants get their initial values from the configuration .json file for the system.
        static int kMaximumOffTrajectoryDistance;
        static uint kNumberHighQualityBallsToRetain;

        static cv::Vec3d kCamera1PositionsFromOriginMeters;
        static cv::Vec3d kCamera2PositionsFromOriginMeters;
        static cv::Vec3d kCamera2OffsetFromCamera1OriginMeters;


        static double kMaxStrobedBallColorDifferenceRelaxed;
        static double kMaxStrobedBallColorDifferenceStrict;
        static double kMaxPuttingBallColorDifferenceRelaxed;

        static double kBallProximityMarginPercentRelaxed;
        static double kBallProximityMarginPercentStrict;

        static double kColorDifferenceRgbPostMultiplierForDarker;
        static double kColorDifferenceRgbPostMultiplierForLighter;
        static double kColorDifferenceStdPostMultiplierForDarker;
        static double kColorDifferenceStdPostMultiplierForLighter;

        static double kPreImageWeightingOverall;
        static double kPreImageWeightingBlue;
        static double kPreImageWeightingGreen;
        static double kPreImageWeightingRed;
        static bool kUsePreImageSubtraction;

        static double kMaxDistanceFromTrajectory;

        static int kClosestBallPairEdgeBackoffPixels;

        static double kMaxIntermediateBallRadiusChangePercent;
        static double kMaxPuttingIntermediateBallRadiusChangePercent;
        static double kMaxOverlappedBallRadiusChangeRatio;
        static double kMaxRadiusDifferencePercentageFromBest;

        static double kCamera1CalibrationDistanceToBall;
        static double kCamera2CalibrationDistanceToBall;

        static double kCamera1XOffsetForTilt;
        static double kCamera1YOffsetForTilt;
        static double kCamera2XOffsetForTilt;
        static double kCamera2YOffsetForTilt;

        static double kExpectedBallPositionXcm;
        static double kExpectedBallPositionYcm;
        static double kExpectedBallPositionZcm;
        static double kExpectedBallRadiusPixelsAt40cm;
        static double kMinRadiusRatio;
        static double kMaxRadiusRatio;

        static double kUnlikelyAngleMinimumDistancePixels;
        static double kMaxQualityExposureLaunchAngle;
        static double kMinQualityExposureLaunchAngle;
        static double kMaxPuttingQualityExposureLaunchAngle;
        static double kMinPuttingQualityExposureLaunchAngle;
        static double kNumberAngleCheckExposures;

        static double kStandardBallSpeedSlowdownPercentage;
        static double kPracticeBallSpeedSlowdownPercentage;
        static double kPuttingBallSpeedSlowdownPercentage;
        static bool kCameraRequiresFlushPulse;

        static double kMaxBallsToRetain;

        // The following group of constants configure the system that attempts to 
        // overcome problems associated with using the LM with another (also strobed)
        // LM
        static bool kExternallyStrobedEnvFilterImage;
        static int kExternallyStrobedEnvBottomIgnoreHeight;
        static int kExternallyStrobedEnvFilterHsvLowerH;
        static int kExternallyStrobedEnvFilterHsvUpperH;
        static int kExternallyStrobedEnvFilterHsvLowerS;
        static int kExternallyStrobedEnvFilterHsvUpperS;
        static int kExternallyStrobedEnvFilterHsvLowerV;
        static int kExternallyStrobedEnvFilterHsvUpperV;
        static int kExternallyStrobedEnvCannyLower;
        static int kExternallyStrobedEnvCannyUpper;
        static int kExternallyStrobedEnvPreHoughBlurSize;
        static int kExternallyStrobedEnvPreCannyBlurSize;
        static int kExternallyStrobedEnvHoughLineIntersections;
        static int kExternallyStrobedEnvLinesAngleLower;
        static int kExternallyStrobedEnvLinesAngleUpper;
        static int kExternallyStrobedEnvMaximumHoughLineGap;
        static int kExternallyStrobedEnvMinimumHoughLineLength;

        static bool kPlacedBallUseLargestBall;


        // Refers to the camera_hardware device object associated with this higher-level camera object
        // TBD - name should really be hardwareCamera_
        CameraHardware camera_;
        
        GolfSimCamera();

        ~GolfSimCamera();

        // One of the main workhorses of the system.  It determines a ball in and
        // image by using various circle-identification algorithms and other processing.
        // 
        // Requires the ball be placed in the center of the screen, at a certain
        // distance from the camera.  The expectedBallCenter can be used to specify 
        // a different expected ball position.
        // Returns true iff the input ball was successfully calibrated
        bool GetCalibratedBall(const GolfSimCamera& camera, 
                               const cv::Mat& rgbImg,
                               GolfBall& b, 
                               const cv::Vec2i& expectedBallCenter = cv::Vec2i(0,0),
                               const bool expectBall = true);

        // Currently returns a single ball
        // TBD - Should return a vector of golf ball objects with each ball's current information.  A vector could be returned to support, e.g., 
        // a multi-strobed picture where the camera shutter is open while the strobe flashes multiple times.
        // Returns true iff the input ball was successfully calibrated
        // camera_positions_from_origin[0] is the position for the calibrated ball, [1] is the position for the camera that took this image
        bool GetCurrentBallLocation(const GolfSimCamera& camera, 
                                    const cv::Mat& img,
                                    const GolfBall& calibrated_ball, 
                                    GolfBall& foundBall);


        // In some cases, camera_1 will be the same as for camera_2 (such as comparing two strobed balls from camera 2).
        static bool ComputeBallDeltas(GolfBall& ball1, GolfBall& ball2, const GolfSimCamera& first_camera, const GolfSimCamera& second_camera);

        static bool ComputeSingleBallXYZOrthoCamPerspective(const GolfSimCamera& camera, GolfBall& initial_ball);

        // Use the specified time delay and the already-calculated deltas in the ball to determine velocity
        static void CalculateBallVelocity(GolfBall& b, long time_delay_us);

        // Use the specified time delay and the already-calculated deltas in the ball to determine spin rate
        // Also sets the rotation_results into the ball object
        static void CalculateBallSpinRates(GolfBall& b, const cv::Vec3d& rotation_results, long time_delay_us);

        // Analyze an image with two or more strobed shots of a ball in flight.
        // TBD - Work in progress
        bool AnalyzeStrobedBalls(const cv::Mat& strobed_balls_color_image,
                                 const cv::Mat& strobed_balls_gray_image,
                                 const GolfBall& calibrated_ball,
                                 GsBallsAndTimingVector& return_balls_and_timing,
                                 GsBallsAndTimingVector&  non_overlapping_balls_timing,
                                 GolfBall& face_ball,
                                 GolfBall& ball2,
                                 long& time_between_ball_images_ms);

        // Sets up the LoggingTool root cause and prints out an error if there are less than two strobed balls found
        static void ReportBallSearchError(const int number_balls_found);

        static bool ShowAndLogBalls(const std::string& title,
                             const cv::Mat& img,
                             std::vector<GolfBall>& balls,
                             bool log_image_to_file = true,
                             const int middle_ball_index = -1,
                             const int second_ball_index = -1 );

        bool GetBallDistancesAndRatios(const std::vector<GolfBall>& balls,
                                             std::vector<double>& distances,
                                             std::vector<double>& distance_ratios);

        // The ball closest to the center of the screen (assuming everything else is ok with it) will
        // likely be the ball that will produce the best "face" for spin detection, especially if compared
        // to the view that the first camera has of the initially-positioned ball.
        int GetMostCenteredBallIndex(const std::vector<GolfBall>& balls, const int ball_to_ignore_index = -1);

        // Sorts the ball vector by X positions.
        // Sorts with the "furthest" ball last, i.e., right-most-to-left for a right-handed 
        // golfer, and leftmost-to-rightmost for a left-handed golfer
        void SortBallsByXPosition(std::vector<GolfBall>& balls);

        // NOTE:  the hsv range may include negative or > 180 Hue numbers due to the hue looping at 180 degrees
        static std::vector<GsColorTriplet> GetBallHSVRange(const GsColorTriplet& ball_color_RGB);

        // Somewhat of a pass-through for now, but we want to preserve a layer above the hardware
        bool prepareToTakeVideo();
        cv::Mat getNextFrame();

        bool prepareToTakePhoto();

        cv::Vec2i GetExpectedBallCenter();

        // The focal distance for each camera should already be known, but this function allows a reverse 
        // calculation that takes the meters and imaged ball radius and determines the focal distance from that.
        static double computeFocalDistanceFromBallData(const GolfSimCamera& camera, double ball_radius_pixels, double ball_distance_meters);

        // Returned angles (x,y) (degrees):  x is left or right angle to the "origin" (default, at rest) ball.
        //     camera_positions_from_origin assume that the camera is aimed at the origin.  That is, the origin will
        //          appear in the middle of the camera's view
        //     Input camera positions are in meters.
        //     Positive x angles mean the ball is positioned to the left of the camera, looking out from the camera
        //         at the ball.
        //     Negative y angles mean the ball is below the camera's axis, looking out at the ball from the camera
        cv::Vec2d computeCameraAnglesToBallPlane(cv::Vec3d& camera_positions_from_origin);

        // Accounts for the fact that the camera may not be pointing straight ahead
        bool AdjustXYZDistancesForCameraAngles(const cv::Vec2d& camera_angles,
                                               const cv::Vec3d& original_distances,
                                               cv::Vec3d& adjusted_distances);


        // Returns the difference, in meters, between b2 - b1 for the X, Y, and Z axes
        static bool ComputeXyzDeltaDistances(const GolfBall& b1, const GolfBall& b2, cv::Vec3d& position_deltas_ball_perspective, cv::Vec3d& distance_deltas_camera_perspective);

        // Returns the real-world distances in the X and Y directions from the perspective of the camera,
        // based in part on the input Z-distance from the ball to the camera.  The X,Y origin from which the
        // distances are measured is considered the center of the image.
        // Regardless of the camera angle, the returned distances are as if the camera was facing straight at the
        // plane of the expected ball's line of flight.
        static bool ComputeXyzDistanceFromOrthoCamPerspective(const GolfSimCamera& camera, const GolfBall& b1, cv::Vec3d& distance_deltas);

        static bool ComputeBallXYAnglesFromCameraPerspective(const cv::Vec3d& distances_camera_perspective,
                                              cv::Vec2d& deltaAnglesCameraPerspective);
        
        // Returns the difference, in degrees, between b2 - b1 for the X, Y axes
        // Requires the golf balls to have their distances_camera_perspective; and angles_camera_perspective vector
        static bool getXYDeltaAnglesBallPerspective(const cv::Vec3d& position_deltas_ball_perspective,
                                                  cv::Vec2d&       deltaAnglesBallPerspective);

        // Given the current focal length and sensed ball radius, determine what a distance in pixels in the 
        // X or Y direction is in real-life distance in meters.
        static double convertXDistanceToMeters(const GolfSimCamera& camera, double zDistanceMeters, double xDistancePixels);
        static double convertYDistanceToMeters(const GolfSimCamera& camera, double zDistanceMeters, double yDistancePixels);

        // Given two images of a golf ball, with the first taken before the second, this function
        // takes those images and the time delay between them to determine the velocity and 3D spin
        // speed of the ball.
        // On the result_ball, the following will be set:
        //      cv::Vec3d distance_deltas, 
        //      cv::Vec2d deltaAnglesBallPerspective,
        //      cv::Vec2d angles_camera_perspective,
        //      douible velocity

        bool analyzeShotImages( const GolfSimCamera& camera, 
                                const cv::Mat& rgbImg1,
                                const cv::Mat& rgbImg2,
                                long timeDelayuS,
                                const std::vector<cv::Vec3d>& camera_positions_from_origin,   // Same X,Y,Z coordinate system as for camera(s)
                                GolfBall& result_ball,
                                const cv::Vec2i& expectedBallCenter = cv::Vec2i(0, 0));

        // Returns the total distance in meters based on the x, y, and z distances
        static double GetTotalDistance(cv::Vec3d& distance_deltas);

        // Finds the current color information from the image at the point where the ball exists and
        // sets up the corresponding color information
        static void GetBallColorInformation(const cv::Mat& color_image, GolfBall& b);

        // Because the strobe timing between different images of the ball can differ from ball-pair to pair,
        // this method helps figure ouw what those intervals are.
        bool DetermineStrobeIntervals(int number_overlapping_balls_removed,
                                      std::vector<GolfBall>& input_balls,
                                      int most_centered_ball_index,
                                      int second_ball_index,
                                      long& time_between_ball_images_ms,
                                      GsBallsAndTimingVector& return_balls_and_timing);

        int FindClosestRatioPatternMatchOffset(const std::vector<double> distance_ratios,
                                               const std::vector<double> pulse_ratios,
                                               double& delta_to_closest_ratio);

        bool GetPulseIntervalsAndRatios(std::vector<float>& pulse_pause_intervals, 
                                        std::vector<double>& pulse_pause_ratios,                                        
                                        const int number_pulses_to_collapse = -1,
                                        const int collapse_offset = -1);

        double GetPerpendicularDistanceFromLine(double x, double y, double x1, double y1, double x2, double y2);

        bool FindBestBallOnLineOfFlight(const std::vector<GolfBall>&balls,
                                        const int candidate_ball_index_left,
                                        const int candidate_ball_index_right,
                                        double& ball_1_distance,
                                        double& ball_2_distance,
                                        const GolfBall& line_ball1,
                                        const GolfBall& line_ball2);

        // Returns a score of the closeness of the vector of distance_ratios within the pulse_ratios at an offset
        // of the distance_ratios from the beginning of the pulse_ratios
        double ComputeRatioDistance(const std::vector<double> distance_ratios,
                                    const std::vector<double> pulse_ratios,
                                    int& distance_pattern_offset);

        // If we identified a lot of balls, only retain the top <n>
        void RemoveLowScoringBalls(std::vector<GolfBall>& initial_balls, const int max_balls_to_retain);

        // Erases any balls whose distance (at a right angle) from the line between the best and second-best ball
        // is greater than the max_distance_from_trajectory
        void RemoveOffTrajectoryBalls(std::vector<GolfBall>& initial_balls,
                                      const double max_distance_from_trajectory,
                                      const GolfBall& best_ball,
                                      const GolfBall& second_best_ball);

        void RemoveUnlikelyRadiusChangeBalls(std::vector<GolfBall>& initial_balls, 
                                            const double max_change_percent, 
                                            const double max_overlapped_ball_radius_change_ratio,
                                            const bool preserve_high_quality_balls = true);

        void RemoveTooSmallOrBigBalls(std::vector<GolfBall>& initial_balls, const GolfBall& expected_best_ball);

        void RemoveWrongColorBalls(const cv::Mat& img, 
                                   std::vector<GolfBall>& initial_balls,
                                   const GolfBall& expected_best_ball,
                                   const double max_strobed_ball_color_difference);

        void RemoveUnlikelyAngleLowerQualityBalls(std::vector<GolfBall>& initial_balls);

        void RemoveWrongRadiusBalls(std::vector<GolfBall>& initial_balls,
                                    const GolfBall& expected_best_ball);

        void RemoveNearbyPoorQualityBalls(std::vector<GolfBall>& initial_balls,
                                          const double max_ball_proximity,
                                          const int max_quality_difference);

        void DetermineSecondBall(std::vector<GolfBall>& return_balls,
            const int most_centered_ball_index,
            int& second_ball_index);

        // The img is used as a frame to ensure that the balls that are chosen are not too close to the edges
        static bool FindClosestTwoBalls(const cv::Mat& img,
            const GsBallsAndTimingVector& balls,
            const bool use_edge_backoffs,
            GolfBall& ball1,
            GolfBall& ball2,
            double& timing_interval_uS);

        // Uses a multi-factor scoring algorithm to pick the two best balls
        static bool FindBestTwoSpinBalls(const cv::Mat& img,
            const GsBallsAndTimingVector& balls,
            const bool use_edge_backoffs,
            GolfBall& ball1,
            GolfBall& ball2,
            double& timing_interval_uS);

        // For each pair of balls, determines the angles and velocity, and then averages
        // all of them and returns that average in output_averaged_ball
        static bool ComputeAveragedStrobedBallData(const GolfSimCamera& camera, 
                                            const GsBallsAndTimingVector& balls_and_timing,
                                            GolfBall& output_averaged_ball);

        // Return_balls will hold the set of balls that are non-overlapping with other balls
        // Will also remove/collapse pulse intervals as necessary to ensure they stay 
        // correlated wiht the return_balls vector.
        uint RemoveOverlappingBalls(const std::vector<GolfBall>& initial_balls,
                                         const double ball_proximity_margin_percent,
                                         const bool attempt_removal_of_off_trajectory_balls,
                                         std::vector<GolfBall>& return_balls,
                                         const GolfBall& best_ball, 
                                         const GolfBall& second_best_ball,
                                         const bool preserve_high_quality_balls = true);

        double AdjustDistanceForSlowing(const double initial_right_distance);

        // Analyze the ball exposures in the image and return ball2 with the trajectory, spin, etc. information
        // exposures_image returns an image of the ball exposures that were identified.
        static bool ProcessReceivedCam2Image(const cv::Mat& ball1_mat, 
                                             const cv::Mat& strobed_ball_mat, 
                                             const cv::Mat& camera2_pre_image_color,
                                             GolfBall& result_ball,
                                             cv::Vec3d& rotationResults,
                                             cv::Mat& exposures_image,
                                             std::vector<GolfBall>& exposure_balls);

        static bool ProcessSpin(GolfSimCamera& camera, 
                                const cv::Mat& strobed_balls_gray_image,
                                const GsBallsAndTimingVector& non_overlapping_balls_and_timing,
                                std::vector<cv::Vec3d>& camera2_to_camera2_positions_from_origin,
                                GolfBall& result_ball,
                                cv::Vec3d& rotationResults);

        static void DrawFilterLines(const std::vector<cv::Vec4i>& lines,
                                    cv::Mat& image, 
                                    const cv::Scalar& color, 
                                    const int thickness = 1);

        // Returns the lines used to try to remove the golf club shaft artifacts
        static bool CleanExternalStrobeArtifacts(const cv::Mat& image, cv::Mat& output_image, std::vector<cv::Vec4i>& lines);

        // Determine how much we widen the color mask from the average color of the ball
        // TBD - determine whether H, S, and V need different multipliers ?
        // DEPRECATED - code is not currently relying much on color
        static constexpr float MIN_COLOR_RANGE_RATIO = 0.6f;
        static constexpr float MAX_COLOR_RANGE_RATIO = 1.45f;
        static constexpr float EXTRA_MAX_HUE_RANGE_RATIO = 1.1f;   // An extra boost for the HUE ? TBD

        // Min values will be subtracted from the average HSV values, max will be added
        static constexpr int H_MIN_CAL_COLOR_WIDENING_AMOUNT = 60;  // 12 worked for green ball, but white seems to need more
        static constexpr int S_MIN_CAL_COLOR_WIDENING_AMOUNT = 50;
        static constexpr int V_MIN_CAL_COLOR_WIDENING_AMOUNT = 120;
        static constexpr int H_MAX_CAL_COLOR_WIDENING_AMOUNT = 80;
        static constexpr int S_MAX_CAL_COLOR_WIDENING_AMOUNT = 80;
        static constexpr int V_MAX_CAL_COLOR_WIDENING_AMOUNT = 60;


    private:

        // Distance is meters that the ball is from the lens.
        // The size of the ball is assumed to be a standard constant
        // NOTE - getCameraParameters must already have been called before this function is called
        static int getExpectedBallRadiusPixels(const int resolution_x_, const double distance);

        // Return the distance of the ball in meters
        double getBallDistance(const GolfBall& calibrated_ball);

        // Compute the distance to the ball based on the known radius of the ball in the real world
        static double ComputeDistanceToBallUsingRadius(const GolfSimCamera& camera, const GolfBall& ball);
    };
}

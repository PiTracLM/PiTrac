/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "gs_format_lib.h"
#include "golf_ball.h"
#include "logging_tools.h"
#include "cv_utils.h"


namespace golf_sim {

// This dictionary may not be heavily used (if at all), but let's retain the work product 
// that went into figuring out the HSV values.
// Use the hsv_range_finder utility in this same directory
// Calibrate this dynamically when a ball is placed in a known position
// static const std::map< BallColor, BallColorRange> BallHSVRangeDict;
static std::map< GolfBall::BallColor, BallColorRange> BallHSVRangeDict;

double GolfBall::kBallRadiusMeters = 21.335e-3;


void GolfBall::InitMembers()
{
    if (BallHSVRangeDict.size() == 0)
    {
        BallColorRange cr{ cv::Vec3b(30, 0, 100), cv::Vec3b(170, 100, 255), cv::Vec3b(90, 0, 255) };

        BallHSVRangeDict[BallColor::kWhite] = BallColorRange(cv::Vec3b(30, 0, 100), cv::Vec3b(170, 100, 255), cv::Vec3b(90, 0, 255));
        BallHSVRangeDict[BallColor::kOrange] = BallColorRange(cv::Vec3b(0, 30, 80), cv::Vec3b(35, 255, 255), cv::Vec3b(5, 225, 222));   // Very touchy, much higher Hmax and things fail
        BallHSVRangeDict[BallColor::kYellow] = BallColorRange(cv::Vec3b(20, 50, 70), cv::Vec3b(70, 255, 255), cv::Vec3b(12, 123, 210));
        BallHSVRangeDict[BallColor::kOpticGreen] = BallColorRange(cv::Vec3b(10, 80, 130), cv::Vec3b(35, 165, 255), cv::Vec3b(20, 124, 208));
        BallHSVRangeDict[BallColor::kUnknown] = BallColorRange(cv::Vec3b(0, 0, 40), cv::Vec3b(180, 255, 255), cv::Vec3b(0, 0, 0));
    }

    // All zero's signifies thaht there is no average color set yet
    average_color_ = (0, 0, 0);
    median_color_ = (0, 0, 0);
    std_color_ = (0, 0, 0);

    expected_ROI_ = cv::Rect(0, 0, 0, 0);
    position_deltas_ball_perspective_ = cv::Vec3d(0, 0, 0);
    angles_ball_perspective_ = cv::Vec2d(0, 0);
    ball_rotation_angles_camera_ortho_perspective_ = cv::Vec3d(0, 0, 0);
    distances_ortho_camera_perspective_ = cv::Vec3d(0, 0, 0);
    angles_camera_ortho_perspective_ = cv::Vec2f(0, 0);
}

void GolfBall::DeInitMembers()
{
    BallHSVRangeDict.clear();
}

GolfBall::GolfBall() 
{
    InitMembers();
}

GolfBall::~GolfBall()
{
    DeInitMembers();
}

void GolfBall::set_circle(const GsCircle& c){ 
    ball_circle_ = c;  
    x_ = (int)std::round(c[0]);
    y_ = (int)std::round(c[1]);
    measured_radius_pixels_ = c[2];
}


GsColorTriplet GolfBall::GetBallLowerHSV(BallColor ball_color) const
{
    const
        BOOST_LOG_FUNCTION();

    GsColorTriplet hsv;

    if (ball_color_ == BallColor::kCalibrated)
    {
        // Get range based on the average color that has actually been measured empirically
        hsv = ball_hsv_range_.min;
    }
    else
    {
        // Use the coarse ball-color settings based on the color
        hsv = BallHSVRangeDict[ball_color].min;
    }

    // logging.debug("GetBallLowerHSV returning: " + str(hsv));
    return hsv;
}

GsColorTriplet GolfBall::GetBallUpperHSV(BallColor ball_color) const
{
    BOOST_LOG_FUNCTION();

    GsColorTriplet hsv;

    if (ball_color_ == BallColor::kCalibrated)
    {
        // Get range based on the specific average color that has been measured
        hsv = ball_hsv_range_.max;
    }
    else
    {
        // Use the coarse ball-color settings based on the color
        hsv = BallHSVRangeDict[ball_color].max;
    }

    return hsv;
}


GsColorTriplet GolfBall::GetRGBCenterFromHSVRange() const {

    BOOST_LOG_FUNCTION();

    /* Use the coarse ball-color settings
    The following did not work at all.Half - way in the
    HSV range for white ended up being green!

    Instead of doing it that way, we will just hard-code the
    optimum perfect values in the golf ball object.
    */

    GsColorTriplet hsvCenter = BallHSVRangeDict[ball_color_].center;
    GsColorTriplet rgb = CvUtils::ConvertHsvToRgb(hsvCenter);

    return rgb;
}

std::string GolfBall::Format() const {

/*
    auto f1 = GS_FORMATLIB_FORMAT("[Ball(x,y)=({: >4},{: <4}), r={: <6.1f} | Circle{: <20} | avgC:{: <15} stdC:{: <15} | cal={: <6} | DistFromLens={: <4.3f}in. | CalFocLen={: <3.3f} | TBD]\n",
        x_, y_, measured_radius_pixels_,
        LoggingTools::FormatCircle(ball_circle_),
        LoggingTools::FormatGsColorTriplet(average_color_), LoggingTools::FormatGsColorTriplet(std_color_),
        calibrated,
        CvUtils::MetersToInches(distance_to_z_plane_from_lens_),
        calibrated_focal_length_);

    auto f2 = GS_FORMATLIB_FORMAT("  (all inches)      [DistDeltasBall(x,y,z)=({: <6.1f},{: <6.1f},{: <6.1f})\n        DistDeltasCam(x,y,z)=({: <6.1f},{: <6.1f},{: <6.1f})\n        DistCam(x,y,z)=({: <6.1f},{: <6.1f},{: <6.1f})\n        [AnglesCam(x,y,z)=({: >4},{: <4})]\n",
        CvUtils::MetersToInches(position_deltas_ball_perspective_[0]),
        CvUtils::MetersToInches(position_deltas_ball_perspective_[1]),
        CvUtils::MetersToInches(position_deltas_ball_perspective_[2]),
        CvUtils::MetersToInches(distance_deltas_camera_perspective_[0]),
        CvUtils::MetersToInches(distance_deltas_camera_perspective_[1]),
        CvUtils::MetersToInches(distance_deltas_camera_perspective_[2]),
        CvUtils::MetersToInches(distances_ortho_camera_perspective_[0]),
        CvUtils::MetersToInches(distances_ortho_camera_perspective_[1]),
        CvUtils::MetersToInches(distances_ortho_camera_perspective_[2]),
        angles_camera_ortho_perspective_[0],
        angles_camera_ortho_perspective_[1]
*/
    auto f1 = GS_FORMATLIB_FORMAT("[Ball No. {: >4}  (x,y)=({: >4},{: <4}), r={: <6.2f} | Circle{: <20} | cal={: <6} | DistFromLens={: <4.3f}m | CalFocLen={: <3.3f} | TBD]\n",
        quality_ranking, x_, y_, measured_radius_pixels_,
        LoggingTools::FormatCircle(ball_circle_),
        calibrated,
        distance_to_z_plane_from_lens_,
        calibrated_focal_length_);
    /**** FOR INCHES - TBD
    auto f2 = GS_FORMATLIB_FORMAT("        [BallAngles(x,y)=({: <6.2f},{: <6.2f})]\n        [DistDeltasBall(x,y,z)=({: <6.2f},{: <6.2f},{: <6.2f})  (all inches)\n        DistDeltasCam(x,y,z)=({: <6.2f},{: <6.2f},{: <6.2f})\n        DistCam(x,y,z)=({: <6.2f},{: <6.2f},{: <6.2f})\n        [AnglesCam(x,y,z)=({: <6.2f},{: <6.2f})]\n",
        angles_ball_perspective_[0],
        angles_ball_perspective_[1],
        CvUtils::MetersToInches(position_deltas_ball_perspective_[0]),
        CvUtils::MetersToInches(position_deltas_ball_perspective_[1]),
        CvUtils::MetersToInches(position_deltas_ball_perspective_[2]),
        CvUtils::MetersToInches(distance_deltas_camera_perspective_[0]),
        CvUtils::MetersToInches(distance_deltas_camera_perspective_[1]),
        CvUtils::MetersToInches(distance_deltas_camera_perspective_[2]),
        CvUtils::MetersToInches(distances_ortho_camera_perspective_[0]),
        CvUtils::MetersToInches(distances_ortho_camera_perspective_[1]),
        CvUtils::MetersToInches(distances_ortho_camera_perspective_[2]),
        angles_camera_ortho_perspective_[0],
        angles_camera_ortho_perspective_[1]);
    ****/

    auto f2 = GS_FORMATLIB_FORMAT("        [BallAngles(x,y)=({: <6.3f},{: <6.3f})]\n        [DistDeltasBall(x,y,z)=({: <6.3f},{: <6.3f},{: <6.3f})  (all inches)\n        DistDeltasCam(x,y,z)=({: <6.3f},{: <6.3f},{: <6.3f})\n        DistCam(x,y,z)=({: <6.3f},{: <6.3f},{: <6.3f})\n        [AnglesCam(x,y)=({: <6.3f},{: <6.3f})]\n",
        angles_ball_perspective_[0],
        angles_ball_perspective_[1],
        (position_deltas_ball_perspective_[0]),
        (position_deltas_ball_perspective_[1]),
        (position_deltas_ball_perspective_[2]),
        (distance_deltas_camera_perspective_[0]),
        (distance_deltas_camera_perspective_[1]),
        (distance_deltas_camera_perspective_[2]),
        (distances_ortho_camera_perspective_[0]),
        (distances_ortho_camera_perspective_[1]),
        (distances_ortho_camera_perspective_[2]),
        angles_camera_ortho_perspective_[0],
        angles_camera_ortho_perspective_[1]);

    auto f3 = GS_FORMATLIB_FORMAT("        avgC:{: <15} stdC:{: <15}\n",
        LoggingTools::FormatGsColorTriplet(average_color_), LoggingTools::FormatGsColorTriplet(std_color_));


    return f1 + f2 + f3;
}


void GolfBall::PrintBallFlightResults() const {

    // TBD - move to the ball object calls
    GolfBall ball = *this;

    GS_LOG_TRACE_MSG(trace, "------------------------- Ball Results -------------------------------------");

    GS_LOG_TRACE_MSG(trace, "Calculated X,Y,Z location deltas (ball perspective in inches) are: " + std::to_string(CvUtils::MetersToInches(ball.position_deltas_ball_perspective_[0])) + ", " +
        std::to_string(CvUtils::MetersToInches(ball.position_deltas_ball_perspective_[1])) + ", " + std::to_string(CvUtils::MetersToInches(ball.position_deltas_ball_perspective_[2])));

    GS_LOG_TRACE_MSG(trace, "Calculated X,Y angles (ball perspective) (in degrees) are: " + std::to_string(ball.angles_ball_perspective_[0]) + ", " +
        std::to_string(ball.angles_ball_perspective_[1]));

    GS_LOG_TRACE_MSG(trace, "Calculated X,Y,Z rotation angles (camera perspective) (in degrees) are: " + std::to_string(ball.ball_rotation_angles_camera_ortho_perspective_[0]) + ", " +
        std::to_string(ball.ball_rotation_angles_camera_ortho_perspective_[1]) + ", " + std::to_string(ball.ball_rotation_angles_camera_ortho_perspective_[2]) + ".");

    GS_LOG_TRACE_MSG(trace, "Calculated ball velocity (m/s)= " + std::to_string(ball.velocity_) + ", or " + std::to_string(ball.velocity_ * 2.237) + " mph.");

    GS_LOG_TRACE_MSG(trace, "Calculated ball spin (x,y,z) in RPM = " + std::to_string(ball.rotation_speeds_RPM_[0]) + ", " + std::to_string(ball.rotation_speeds_RPM_[1]) + ", " + std::to_string(ball.rotation_speeds_RPM_[2]) + ".");
}

void GolfBall::AverageBalls(const std::vector<GolfBall>& ball_vector, GolfBall& averaged_ball) {
    double number_balls = (double)ball_vector.size();

    averaged_ball.set_x(0.0F);
    averaged_ball.set_y(0.0F);
    averaged_ball.velocity_ = 0;
    averaged_ball.position_deltas_ball_perspective_ = { 0, 0, 0 };
    averaged_ball.angles_ball_perspective_ = { 0,0 };
    averaged_ball.ball_rotation_angles_camera_ortho_perspective_ = { 0, 0, 0 };
    averaged_ball.rotation_speeds_RPM_ = { 0, 0, 0 };

    for (const GolfBall& b: ball_vector) {
        averaged_ball.velocity_ += b.velocity_ / number_balls;

        long x = averaged_ball.x();
        x += (long)(b.x() / number_balls);
        averaged_ball.set_x(x);

        long y = averaged_ball.y();
        y += (long)(b.y() / number_balls);
        averaged_ball.set_y(y);

        // NOTE - Not clear how often the position deltas should be averaged?
        averaged_ball.position_deltas_ball_perspective_[0] += b.position_deltas_ball_perspective_[0] / number_balls;
        averaged_ball.position_deltas_ball_perspective_[1] += b.position_deltas_ball_perspective_[1] / number_balls;
        averaged_ball.position_deltas_ball_perspective_[2] += b.position_deltas_ball_perspective_[2] / number_balls;

        averaged_ball.angles_ball_perspective_[0] += b.angles_ball_perspective_[0] / number_balls;
        averaged_ball.angles_ball_perspective_[1] += b.angles_ball_perspective_[1] / number_balls;

        averaged_ball.angles_camera_ortho_perspective_[0] += b.angles_camera_ortho_perspective_[0] / number_balls;
        averaged_ball.angles_camera_ortho_perspective_[1] += b.angles_camera_ortho_perspective_[1] / number_balls;

        averaged_ball.ball_rotation_angles_camera_ortho_perspective_[0] += b.ball_rotation_angles_camera_ortho_perspective_[0] / number_balls;
        averaged_ball.ball_rotation_angles_camera_ortho_perspective_[1] += b.ball_rotation_angles_camera_ortho_perspective_[1] / number_balls;
        averaged_ball.ball_rotation_angles_camera_ortho_perspective_[2] += b.ball_rotation_angles_camera_ortho_perspective_[2] / number_balls;

        averaged_ball.rotation_speeds_RPM_[0] += b.rotation_speeds_RPM_[0] / number_balls;
        averaged_ball.rotation_speeds_RPM_[1] += b.rotation_speeds_RPM_[1] / number_balls;
        averaged_ball.rotation_speeds_RPM_[2] += b.rotation_speeds_RPM_[2] / number_balls;
    }
}

bool GolfBall::CheckIfBallMoved(const GolfBall& ball_to_compare, const int max_center_move_pixels, const int max_radius_change_percent) {
    bool moved = false;

    if ( abs(x_ - ball_to_compare.x()) > max_center_move_pixels ||
         abs(y_ - ball_to_compare.y()) > max_center_move_pixels) {
            moved = true;
    }

    if ( abs( measured_radius_pixels_ - ball_to_compare.measured_radius_pixels_) > (measured_radius_pixels_ * max_radius_change_percent/100.) ) {
        moved = true;
    }

    return moved;
}

double GolfBall::PixelDistanceFromBall(const GolfBall& ball2) const {

    double x_distance = std::abs(CvUtils::CircleX(ball_circle_) - CvUtils::CircleX(ball2.ball_circle_));
    double y_distance = std::abs(CvUtils::CircleY(ball_circle_) - CvUtils::CircleY(ball2.ball_circle_));

    double distance = std::sqrt( x_distance * x_distance  +  y_distance * y_distance );
    return distance;
}

bool GolfBall::PointIsInsideBall(double x, double y) const {
    double x_distance = std::abs(CvUtils::CircleX(ball_circle_) - x);
    double y_distance = std::abs(CvUtils::CircleY(ball_circle_) - y);

    double distance = std::sqrt(x_distance * x_distance + y_distance * y_distance);
    
    return (distance < ball_circle_[2] * 0.85);
}


}

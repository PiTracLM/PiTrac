/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#include "ball_geometry.hpp"

#include <cmath>

namespace golf_sim {
namespace ball_geometry {

namespace {

cv::Matx33d RotationAboutX(double radians) {
    const double c = std::cos(radians), s = std::sin(radians);
    return cv::Matx33d(1, 0, 0,
                       0, c, -s,
                       0, s, c);
}

cv::Matx33d RotationAboutY(double radians) {
    const double c = std::cos(radians), s = std::sin(radians);
    return cv::Matx33d(c, 0, s,
                       0, 1, 0,
                       -s, 0, c);
}

} // namespace

cv::Matx33d CameraRotation(double pan_deg, double tilt_deg) {
    const double to_rad = CV_PI / 180.0;
    return RotationAboutY(-pan_deg * to_rad) * RotationAboutX(-tilt_deg * to_rad);
}

cv::Vec3d PixelDirection(const Lens& lens, double x_px, double y_px) {
    // Image rows count downward, the camera's Y points up
    const cv::Vec3d ray((x_px - lens.cx) / lens.fx, -(y_px - lens.cy) / lens.fy, 1.0);
    return ray / cv::norm(ray);
}

double BallRange(const Lens& lens, double x_px, double y_px, double radius_px, double ball_radius_m) {
    const double cos_off_axis = PixelDirection(lens, x_px, y_px)[2];
    // Across the direction to the image center, a ball's image is as wide as the angle
    // it covers, magnified by the longer path to an off-center spot on the sensor.
    const double focal_px = 0.5 * (lens.fx + lens.fy);
    const double half_angle = std::atan(radius_px * cos_off_axis / focal_px);
    return ball_radius_m / std::sin(half_angle);
}

cv::Vec3d BallPosition(const Lens& lens, const cv::Matx33d& camera_rotation,
                       double x_px, double y_px, double range_m) {
    return camera_rotation * (range_m * PixelDirection(lens, x_px, y_px));
}

bool SolveCameraAngles(const Lens& lens, double x_px, double y_px,
                       const cv::Vec3d& known_position, cv::Vec2d& pan_tilt_deg) {
    const double distance = cv::norm(known_position);
    if (distance < 1e-9) {
        return false;
    }
    const cv::Vec3d target = known_position / distance;
    const cv::Vec3d ray = PixelDirection(lens, x_px, y_px);

    // Tilting about X is the only step that changes height, so it must lift the ray
    // to the target's height: ray_y * cos(tilt) + ray_z * sin(tilt) = target_y
    const double reach = std::hypot(ray[1], ray[2]);
    if (reach < 1e-9 || std::abs(target[1]) > reach) {
        return false;
    }
    const double tilt = std::asin(target[1] / reach) - std::atan2(ray[1], ray[2]);
    if (std::abs(tilt) > CV_PI / 2.0) {
        return false;
    }

    const cv::Vec3d tilted = RotationAboutX(-tilt) * ray;
    // Panning then turns the tilted ray about the vertical onto the target
    const double pan = std::atan2(tilted[0], tilted[2]) - std::atan2(target[0], target[2]);

    const double to_deg = 180.0 / CV_PI;
    pan_tilt_deg = cv::Vec2d(pan * to_deg, tilt * to_deg);
    return true;
}

} // namespace ball_geometry
} // namespace golf_sim

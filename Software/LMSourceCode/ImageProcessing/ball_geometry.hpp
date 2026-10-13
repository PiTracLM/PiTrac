/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

// Where a ball is in space, from where it is in the image and how big it looks.
//
// A ball of known size at line-of-sight distance d covers a known angle. That angle
// and the direction of the pixel it is seen at are all that is needed, for any lens
// and any distance. The lens enters only through its measured pixel focal lengths
// and optical center, which the lens calibration provides.
//
// Frame: X right, Y up, Z out from the launch monitor. Camera pan is positive to the
// left and tilt is positive upward, as in the rest of the camera code.

#pragma once

#include <opencv2/core.hpp>

namespace golf_sim {
namespace ball_geometry {

// Pixel focal lengths and optical center of an undistorted image
struct Lens {
    double fx = 0;
    double fy = 0;
    double cx = 0;
    double cy = 0;
};

// The camera's axes expressed in the launch monitor frame
cv::Matx33d CameraRotation(double pan_deg, double tilt_deg);

// Unit vector from the lens toward a pixel, in the camera's own frame (Y up)
cv::Vec3d PixelDirection(const Lens& lens, double x_px, double y_px);

// Line-of-sight distance to a ball. radius_px is the ball's size across the direction
// to the image center, which stays true when the ball is off to one side.
double BallRange(const Lens& lens, double x_px, double y_px, double radius_px, double ball_radius_m);

// Ball center in the launch monitor frame, given its line-of-sight distance
cv::Vec3d BallPosition(const Lens& lens, const cv::Matx33d& camera_rotation,
                       double x_px, double y_px, double range_m);

// Pan and tilt that make a ball seen at a pixel sit in the direction of its known
// position. Returns false if no tilt within 90 degrees can do it.
bool SolveCameraAngles(const Lens& lens, double x_px, double y_px,
                       const cv::Vec3d& known_position, cv::Vec2d& pan_tilt_deg);

} // namespace ball_geometry
} // namespace golf_sim

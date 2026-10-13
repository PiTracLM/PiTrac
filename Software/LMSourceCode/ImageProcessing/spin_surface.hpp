/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

// Input preparation for the spin model and the local search that sharpens its answer.
//
// This mirrors spin_model/surface_input.py and spin_model/local_search.py in the
// Machine-Learning repo. The model is trained on what the Python produces, so the
// two have to stay in step.

#pragma once

#include <opencv2/core.hpp>

namespace golf_sim {
namespace spin_surface {

// What the model sees of one ball. Both planes are CV_32FC1 and square.
struct SurfaceInput {
    cv::Mat value;  // surface detail in [-1, 1], 0 where invalid
    cv::Mat valid;  // 1 on usable ball pixels, 0 off the ball and on glare
};

// raw_gray_crop is the un-equalized square crop around the ball (CV_8UC1) with the
// ball centered, and radius is the ball radius in that crop's pixels.
SurfaceInput MakeSurfaceInput(const cv::Mat& raw_gray_crop, float radius, int size);

// Rotations follow the model's convention: p_b = R * p_a, with the image row as X
// and the column as Y. The Euler angles are the ones the rotation search uses.
cv::Matx33d EulerToMatrix(double x_deg, double y_deg, double z_deg);
cv::Vec3d MatrixToEuler(const cv::Matx33d& R);

// Looks at rotations within a few degrees of start and returns the one under which
// ball a's surface lines up best with ball b's. match_score gets that correlation.
cv::Matx33d RefineRotation(const SurfaceInput& a, const SurfaceInput& b,
                           const cv::Matx33d& start, float* match_score = nullptr);

} // namespace spin_surface
} // namespace golf_sim

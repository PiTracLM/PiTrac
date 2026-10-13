/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#include "spin_surface.hpp"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>

namespace golf_sim {
namespace spin_surface {

namespace {

// Input preparation. See surface_input.py for why each step is there.
const float kBallMaskFraction = 0.97f;     // fraction of the radius kept as "on the ball"
const float kLocalSigmaFraction = 0.22f;   // width of the local brightness average, relative to the radius
const float kGlareLevel = 1.15f;           // brightness over the local average that counts as glare
const float kReferenceRadius = 58.0f;      // radius the two band-pass sigmas are quoted for
const float kFineSigma = 1.2f;
const float kCoarseSigma = 4.0f;
const float kValueScale = 0.1f;

// Local search. See local_search.py.
const float kCropPadding = 1.05f;          // crop half-width over ball radius, as in IsolateBall
const float kUsableRadius = 0.86f;         // fraction of the radius compared; the limb is too foreshortened
const float kMinOverlap = 0.3f;            // share of the usable disk both balls must show
const float kSearchHalfWidthDeg = 12.0f;
const float kCoarseStepDeg = 3.0f;
const float kFineHalfWidthDeg = 2.0f;
const float kFineStepDeg = 1.0f;

cv::Matx33d RotationVectorToMatrix(double x_deg, double y_deg, double z_deg) {
    const double to_rad = CV_PI / 180.0;
    cv::Vec3d w(x_deg * to_rad, y_deg * to_rad, z_deg * to_rad);
    const double angle = cv::norm(w);
    if (angle < 1e-9) {
        return cv::Matx33d::eye();
    }
    const cv::Vec3d k = w / angle;
    const cv::Matx33d K(0, -k[2], k[1],
                        k[2], 0, -k[0],
                        -k[1], k[0], 0);
    return cv::Matx33d::eye() + std::sin(angle) * K + (1.0 - std::cos(angle)) * (K * K);
}

// One pixel of ball b that lies on the part of the surface worth comparing
struct SurfacePoint {
    float x, y, z;   // unit-sphere position, x along rows and y along columns
    float value;
    bool valid;
};

float SampleBilinear(const cv::Mat& plane, float row, float col) {
    const int r0 = (int)std::floor(row);
    const int c0 = (int)std::floor(col);
    const float fr = row - r0;
    const float fc = col - c0;
    float sum = 0.0f;
    for (int dr = 0; dr <= 1; dr++) {
        const int r = r0 + dr;
        if (r < 0 || r >= plane.rows) continue;
        const float* line = plane.ptr<float>(r);
        const float wr = dr ? fr : 1.0f - fr;
        for (int dc = 0; dc <= 1; dc++) {
            const int c = c0 + dc;
            if (c < 0 || c >= plane.cols) continue;
            sum += wr * (dc ? fc : 1.0f - fc) * line[c];
        }
    }
    return sum;
}

// Zero-mean correlation between ball a turned by R and ball b, over the surface both show
float MatchScore(const SurfaceInput& a, const std::vector<SurfacePoint>& points_b,
                 const cv::Matx33d& R, float radius, float center) {
    const float usable_sq = kUsableRadius * kUsableRadius;
    const float r00 = (float)R(0, 0), r01 = (float)R(0, 1), r02 = (float)R(0, 2);
    const float r10 = (float)R(1, 0), r11 = (float)R(1, 1), r12 = (float)R(1, 2);
    const float r20 = (float)R(2, 0), r21 = (float)R(2, 1), r22 = (float)R(2, 2);

    double n = 0, sum_a = 0, sum_b = 0, sum_aa = 0, sum_bb = 0, sum_ab = 0;
    for (const SurfacePoint& p : points_b) {
        if (!p.valid) continue;
        // Where this point of b was on a: R transposed times p
        const float sz = r02 * p.x + r12 * p.y + r22 * p.z;
        if (sz <= 0.5f) continue;
        const float sx = r00 * p.x + r10 * p.y + r20 * p.z;
        const float sy = r01 * p.x + r11 * p.y + r21 * p.z;
        if (sx * sx + sy * sy > usable_sq) continue;

        const float row = sx * radius + center;
        const float col = sy * radius + center;
        if (SampleBilinear(a.valid, row, col) <= 0.9f) continue;
        const double va = SampleBilinear(a.value, row, col);
        const double vb = p.value;
        n += 1; sum_a += va; sum_b += vb;
        sum_aa += va * va; sum_bb += vb * vb; sum_ab += va * vb;
    }

    if (n / (double)points_b.size() < kMinOverlap) {
        return -1.0f;
    }
    const double mean_a = sum_a / n;
    const double mean_b = sum_b / n;
    const double var_a = std::max(sum_aa - n * mean_a * mean_a, 0.0);
    const double var_b = std::max(sum_bb - n * mean_b * mean_b, 0.0);
    return (float)((sum_ab - n * mean_a * mean_b) / (std::sqrt(var_a) * std::sqrt(var_b) + 1e-6));
}

} // namespace

SurfaceInput MakeSurfaceInput(const cv::Mat& raw_gray_crop, float radius, int size) {
    CV_Assert(raw_gray_crop.type() == CV_8UC1);
    const int rows = raw_gray_crop.rows;
    const int cols = raw_gray_crop.cols;

    cv::Mat crop;
    raw_gray_crop.convertTo(crop, CV_32F);

    cv::Mat disk(rows, cols, CV_32F);
    const float mask_radius_sq = (kBallMaskFraction * radius) * (kBallMaskFraction * radius);
    for (int r = 0; r < rows; r++) {
        float* line = disk.ptr<float>(r);
        const float dy = r - rows / 2.0f;
        for (int c = 0; c < cols; c++) {
            const float dx = c - cols / 2.0f;
            line[c] = (dx * dx + dy * dy < mask_radius_sq) ? 1.0f : 0.0f;
        }
    }

    // Brightness of the ball around each pixel, counting only pixels on the ball
    const double local_sigma = kLocalSigmaFraction * radius;
    const int k = (int)(kLocalSigmaFraction * radius * 6) | 1;
    cv::Mat weighted, coverage;
    cv::GaussianBlur(crop.mul(disk), weighted, cv::Size(k, k), local_sigma);
    cv::GaussianBlur(disk, coverage, cv::Size(k, k), local_sigma);
    cv::Mat local = weighted / cv::max(coverage, 1e-3);
    cv::Mat flat = crop / cv::max(local, 1.0);

    const double scale = radius / kReferenceRadius;
    cv::Mat fine, coarse;
    cv::GaussianBlur(flat, fine, cv::Size(0, 0), kFineSigma * scale);
    cv::GaussianBlur(flat, coarse, cv::Size(0, 0), kCoarseSigma * scale);
    cv::Mat detail = (fine - coarse) / kValueScale;
    cv::min(detail, 1.0, detail);
    cv::max(detail, -1.0, detail);

    cv::Mat glare = flat > kGlareLevel;
    cv::dilate(glare, glare, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
    cv::Mat valid_native = (disk > 0) & ~glare;

    SurfaceInput input;
    cv::Mat valid_resized;
    cv::resize(detail, input.value, cv::Size(size, size), 0, 0, cv::INTER_LINEAR);
    cv::resize(valid_native, valid_resized, cv::Size(size, size), 0, 0, cv::INTER_NEAREST);
    input.valid = cv::Mat(size, size, CV_32F);

    for (int r = 0; r < size; r++) {
        float* value = input.value.ptr<float>(r);
        float* valid = input.valid.ptr<float>(r);
        const uchar* keep = valid_resized.ptr<uchar>(r);
        for (int c = 0; c < size; c++) {
            valid[c] = keep[c] ? 1.0f : 0.0f;
            // The training images store the value in 8 bits, so round it the same way here
            value[c] = keep[c] ? (float)(std::nearbyint(value[c] * 127.0f) / 127.0) : 0.0f;
        }
    }
    return input;
}

cv::Matx33d EulerToMatrix(double x_deg, double y_deg, double z_deg) {
    const double to_rad = CV_PI / 180.0;
    const double x = -x_deg * to_rad;
    const double y = y_deg * to_rad;
    const double z = z_deg * to_rad;
    const cv::Matx33d rx(1, 0, 0,
                         0, std::cos(x), -std::sin(x),
                         0, std::sin(x), std::cos(x));
    const cv::Matx33d ry(std::cos(y), 0, std::sin(y),
                         0, 1, 0,
                         -std::sin(y), 0, std::cos(y));
    const cv::Matx33d rz(std::cos(z), -std::sin(z), 0,
                         std::sin(z), std::cos(z), 0,
                         0, 0, 1);
    return rz * ry * rx;
}

cv::Vec3d MatrixToEuler(const cv::Matx33d& R) {
    const double sy = std::min(1.0, std::max(-1.0, -R(2, 0)));
    const double y_rad = std::asin(sy);

    double x_neg_rad, z_rad;
    if (std::abs(std::cos(y_rad)) > 1e-6) {
        x_neg_rad = std::atan2(R(2, 1), R(2, 2));
        z_rad = std::atan2(R(1, 0), R(0, 0));
    } else {
        x_neg_rad = 0.0;
        z_rad = std::atan2(-R(0, 1), R(1, 1));
    }

    const double to_deg = 180.0 / CV_PI;
    return cv::Vec3d(-x_neg_rad * to_deg, y_rad * to_deg, z_rad * to_deg);
}

cv::Matx33d RefineRotation(const SurfaceInput& a, const SurfaceInput& b,
                           const cv::Matx33d& start, float* match_score) {
    const int size = b.value.rows;
    const float center = size / 2.0f;
    const float radius = center / kCropPadding;

    std::vector<SurfacePoint> points_b;
    points_b.reserve((size_t)size * size);
    for (int r = 0; r < size; r++) {
        const float* value = b.value.ptr<float>(r);
        const float* valid = b.valid.ptr<float>(r);
        const float x = (r - center) / radius;
        for (int c = 0; c < size; c++) {
            const float y = (c - center) / radius;
            const float rr = x * x + y * y;
            if (rr > kUsableRadius * kUsableRadius) continue;
            points_b.push_back({ x, y, std::sqrt(1.0f - rr), value[c], valid[c] > 0.9f });
        }
    }

    cv::Matx33d best = start;
    float best_score = -1.0f;
    const float passes[2][2] = { { kSearchHalfWidthDeg, kCoarseStepDeg }, { kFineHalfWidthDeg, kFineStepDeg } };
    for (const auto& pass : passes) {
        const cv::Matx33d base = best;
        const int steps = (int)std::lround(pass[0] / pass[1]);
        float pass_score = -2.0f;
        for (int ix = -steps; ix <= steps; ix++) {
            for (int iy = -steps; iy <= steps; iy++) {
                for (int iz = -steps; iz <= steps; iz++) {
                    const cv::Matx33d candidate =
                        RotationVectorToMatrix(ix * pass[1], iy * pass[1], iz * pass[1]) * base;
                    const float score = MatchScore(a, points_b, candidate, radius, center);
                    if (score > pass_score) {
                        pass_score = score;
                        best = candidate;
                    }
                }
            }
        }
        best_score = pass_score;
    }

    if (match_score != nullptr) {
        *match_score = best_score;
    }
    return best;
}

} // namespace spin_surface
} // namespace golf_sim

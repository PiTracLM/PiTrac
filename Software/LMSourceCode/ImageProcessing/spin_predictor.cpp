/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#include "spin_predictor.hpp"
#include "spin_surface.hpp"
#include "logging_tools.h"

#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace golf_sim {

SpinPredictor::SpinPredictor(const Config& config)
    : config_(config) {
}

SpinPredictor::~SpinPredictor() {
#ifdef HAS_NCNN
    net_.clear();
#endif
}

bool SpinPredictor::Initialize() {
#ifndef HAS_NCNN
    GS_LOG_MSG(error, "SpinPredictor: NCNN not available (compiled without HAS_NCNN)");
    return false;
#else
    if (!std::filesystem::exists(config_.param_path)) {
        GS_LOG_MSG(error, "SpinPredictor: param file not found: " + config_.param_path);
        return false;
    }
    if (!std::filesystem::exists(config_.bin_path)) {
        GS_LOG_MSG(error, "SpinPredictor: bin file not found: " + config_.bin_path);
        return false;
    }

    net_.opt.num_threads = config_.num_threads;
    net_.opt.use_fp16_packed = config_.use_fp16_packing;
    net_.opt.use_fp16_storage = config_.use_fp16_packing;
    net_.opt.use_fp16_arithmetic = true;
    net_.opt.use_packing_layout = true;

    if (net_.load_param(config_.param_path.c_str()) != 0) {
        GS_LOG_MSG(error, "SpinPredictor: failed to load param: " + config_.param_path);
        return false;
    }
    if (net_.load_model(config_.bin_path.c_str()) != 0) {
        GS_LOG_MSG(error, "SpinPredictor: failed to load model: " + config_.bin_path);
        return false;
    }

    initialized_ = true;
    GS_LOG_MSG(info, "SpinPredictor initialized (" + std::to_string(config_.num_threads) +
               " threads, input=" + std::to_string(config_.input_size) + "px)");

    cv::Mat dummy = cv::Mat::zeros(config_.input_size, config_.input_size, CV_8UC1);
    const float dummy_radius = config_.input_size / 2.1f;
    for (int i = 0; i < 3; i++) {
        Predict(dummy, dummy_radius, dummy, dummy_radius);
    }
    GS_LOG_MSG(info, "SpinPredictor warmup complete (3 iterations)");

    return true;
#endif
}

SpinPredictor::Result SpinPredictor::Predict(
    const cv::Mat& raw_ball_1, float radius_1,
    const cv::Mat& raw_ball_2, float radius_2,
    const cv::Vec3i& view_rotation_1,
    const cv::Vec3i& view_rotation_2) {

    Result result;

#ifndef HAS_NCNN
    GS_LOG_MSG(error, "SpinPredictor::Predict called without NCNN support");
    return result;
#else
    if (!initialized_) {
        GS_LOG_MSG(error, "SpinPredictor::Predict called before Initialize()");
        return result;
    }

    auto t_start = std::chrono::high_resolution_clock::now();

    const int s = config_.input_size;

    const spin_surface::SurfaceInput input1 = spin_surface::MakeSurfaceInput(raw_ball_1, radius_1, s);
    const spin_surface::SurfaceInput input2 = spin_surface::MakeSurfaceInput(raw_ball_2, radius_2, s);

    ncnn::Mat in0(s, s, 2);
    ncnn::Mat in1(s, s, 2);

    const size_t plane_bytes = (size_t)s * s * sizeof(float);
    std::memcpy((float*)in0.channel(0), input1.value.data, plane_bytes);
    std::memcpy((float*)in0.channel(1), input1.valid.data, plane_bytes);
    std::memcpy((float*)in1.channel(0), input2.value.data, plane_bytes);
    std::memcpy((float*)in1.channel(1), input2.valid.data, plane_bytes);

    ncnn::Extractor ex = net_.create_extractor();
    if (ex.input("in0", in0) != 0 || ex.input("in1", in1) != 0) {
        GS_LOG_MSG(error, "SpinPredictor: failed to set input blobs");
        return result;
    }

    ncnn::Mat out;
    if (ex.extract("out0", out) != 0 || out.data == nullptr) {
        GS_LOG_MSG(error, "SpinPredictor: failed to extract output");
        return result;
    }

    // The model gets the rotation roughly right; the search around its answer makes it precise
    const cv::Matx33d predicted = GramSchmidt((const float*)out.data);
    const cv::Matx33d refined = spin_surface::RefineRotation(input1, input2, predicted, &result.match_score);

    // The model saw the balls as photographed. Re-express its rotation between the
    // two perspective-corrected views, which is what the rest of the pipeline expects.
    const cv::Matx33d view1 = spin_surface::EulerToMatrix(view_rotation_1[0], view_rotation_1[1], view_rotation_1[2]);
    const cv::Matx33d view2 = spin_surface::EulerToMatrix(view_rotation_2[0], view_rotation_2[1], view_rotation_2[2]);
    const cv::Vec3d euler = spin_surface::MatrixToEuler(view2 * refined * view1.t());
    result.x_deg = euler[0];
    result.y_deg = euler[1];
    result.z_deg = euler[2];

    auto t_end = std::chrono::high_resolution_clock::now();
    result.inference_ms = std::chrono::duration<float, std::milli>(t_end - t_start).count();

    if (std::abs(result.z_deg) > config_.z_fallback_threshold) {
        result.z_used_fallback = true;
        GS_LOG_MSG(info, "SpinPredictor: Z=" + std::to_string(result.z_deg) +
                   "° exceeds threshold, flagging for fallback");
    }

    return result;
#endif
}

cv::Matx33d SpinPredictor::GramSchmidt(const float* r6d) {
    double a1[3] = { r6d[0], r6d[1], r6d[2] };
    double a2[3] = { r6d[3], r6d[4], r6d[5] };

    double norm1 = std::sqrt(a1[0]*a1[0] + a1[1]*a1[1] + a1[2]*a1[2]);
    if (norm1 < 1e-8) {
        return cv::Matx33d::eye();
    }
    double b1[3] = { a1[0]/norm1, a1[1]/norm1, a1[2]/norm1 };

    double dot = b1[0]*a2[0] + b1[1]*a2[1] + b1[2]*a2[2];
    double v2[3] = { a2[0] - dot*b1[0], a2[1] - dot*b1[1], a2[2] - dot*b1[2] };
    double norm2 = std::sqrt(v2[0]*v2[0] + v2[1]*v2[1] + v2[2]*v2[2]);
    if (norm2 < 1e-8) {
        return cv::Matx33d::eye();
    }
    double b2[3] = { v2[0]/norm2, v2[1]/norm2, v2[2]/norm2 };

    double b3[3] = {
        b1[1]*b2[2] - b1[2]*b2[1],
        b1[2]*b2[0] - b1[0]*b2[2],
        b1[0]*b2[1] - b1[1]*b2[0],
    };

    return cv::Matx33d(b1[0], b2[0], b3[0],
                       b1[1], b2[1], b3[1],
                       b1[2], b2[2], b3[2]);
}

} // namespace golf_sim

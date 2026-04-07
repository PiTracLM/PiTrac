/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#include "spin_predictor.hpp"
#include "logging_tools.h"

#include <cmath>
#include <chrono>
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
    for (int i = 0; i < 3; i++) {
        Predict(dummy, dummy);
    }
    GS_LOG_MSG(info, "SpinPredictor warmup complete (3 iterations)");

    return true;
#endif
}

SpinPredictor::Result SpinPredictor::Predict(
    const cv::Mat& dimple_edges_1,
    const cv::Mat& dimple_edges_2) {

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

    cv::Mat img1, img2;
    cv::resize(dimple_edges_1, img1, cv::Size(s, s), 0, 0, cv::INTER_NEAREST);
    cv::resize(dimple_edges_2, img2, cv::Size(s, s), 0, 0, cv::INTER_NEAREST);

    ncnn::Mat in0(s, s, 2);
    ncnn::Mat in1(s, s, 2);

    TernaryToTwoChannel(img1, (float*)in0.data, s);
    TernaryToTwoChannel(img2, (float*)in1.data, s);

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

    const float* r6d = (const float*)out.data;
    Rotation6DToEuler(r6d, result.x_deg, result.y_deg, result.z_deg);

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

void SpinPredictor::TernaryToTwoChannel(const cv::Mat& gabor_img,
                                         float* out_data, int size) {
    const int n_pixels = size * size;
    float* edge_channel = out_data;
    float* valid_channel = out_data + n_pixels;

    for (int y = 0; y < size; y++) {
        const uchar* row = gabor_img.ptr<uchar>(y);
        for (int x = 0; x < size; x++) {
            int idx = y * size + x;
            uchar pixel = row[x];
            edge_channel[idx] = (pixel == 255) ? 1.0f : 0.0f;
            valid_channel[idx] = (pixel != 128) ? 1.0f : 0.0f;
        }
    }
}

void SpinPredictor::GramSchmidt(const float* r6d, double R[3][3]) {
    double a1[3] = { r6d[0], r6d[1], r6d[2] };
    double a2[3] = { r6d[3], r6d[4], r6d[5] };

    double norm1 = std::sqrt(a1[0]*a1[0] + a1[1]*a1[1] + a1[2]*a1[2]);
    if (norm1 < 1e-8) {
        R[0][0] = 1; R[0][1] = 0; R[0][2] = 0;
        R[1][0] = 0; R[1][1] = 1; R[1][2] = 0;
        R[2][0] = 0; R[2][1] = 0; R[2][2] = 1;
        return;
    }
    double b1[3] = { a1[0]/norm1, a1[1]/norm1, a1[2]/norm1 };

    double dot = b1[0]*a2[0] + b1[1]*a2[1] + b1[2]*a2[2];
    double v2[3] = { a2[0] - dot*b1[0], a2[1] - dot*b1[1], a2[2] - dot*b1[2] };
    double norm2 = std::sqrt(v2[0]*v2[0] + v2[1]*v2[1] + v2[2]*v2[2]);
    if (norm2 < 1e-8) {
        R[0][0] = 1; R[0][1] = 0; R[0][2] = 0;
        R[1][0] = 0; R[1][1] = 1; R[1][2] = 0;
        R[2][0] = 0; R[2][1] = 0; R[2][2] = 1;
        return;
    }
    double b2[3] = { v2[0]/norm2, v2[1]/norm2, v2[2]/norm2 };

    double b3[3] = {
        b1[1]*b2[2] - b1[2]*b2[1],
        b1[2]*b2[0] - b1[0]*b2[2],
        b1[0]*b2[1] - b1[1]*b2[0],
    };

    R[0][0] = b1[0]; R[0][1] = b2[0]; R[0][2] = b3[0];
    R[1][0] = b1[1]; R[1][1] = b2[1]; R[1][2] = b3[1];
    R[2][0] = b1[2]; R[2][1] = b2[2]; R[2][2] = b3[2];
}

void SpinPredictor::Rotation6DToEuler(const float* r6d,
                                       double& x_deg, double& y_deg, double& z_deg) {
    double R[3][3];
    GramSchmidt(r6d, R);

    double sy = -R[2][0];
    if (sy > 1.0) sy = 1.0;
    if (sy < -1.0) sy = -1.0;
    double y_rad = std::asin(sy);
    double cy = std::cos(y_rad);

    double x_neg_rad, z_rad;
    if (std::abs(cy) > 1e-6) {
        x_neg_rad = std::atan2(R[2][1], R[2][2]);
        z_rad = std::atan2(R[1][0], R[0][0]);
    } else {
        x_neg_rad = 0.0;
        z_rad = std::atan2(-R[0][1], R[1][1]);
    }

    x_deg = -x_neg_rad * 180.0 / M_PI;
    y_deg = y_rad * 180.0 / M_PI;
    z_deg = z_rad * 180.0 / M_PI;
}

} // namespace golf_sim

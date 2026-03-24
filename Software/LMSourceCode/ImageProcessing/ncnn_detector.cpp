/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#include "ncnn_detector.hpp"
#include "logging_tools.h"
#include <algorithm>
#include <filesystem>
#include <numeric>

namespace golf_sim {

NCNNDetector::NCNNDetector(const Config& config)
    : config_(config) {
}

NCNNDetector::~NCNNDetector() {
    net_.clear();
}

bool NCNNDetector::Initialize() {
    if (!std::filesystem::exists(config_.param_path)) {
        GS_LOG_MSG(error, "NCNN param file not found: " + config_.param_path);
        return false;
    }
    if (!std::filesystem::exists(config_.bin_path)) {
        GS_LOG_MSG(error, "NCNN bin file not found: " + config_.bin_path);
        return false;
    }

    net_.opt.num_threads = config_.num_threads;
    net_.opt.use_fp16_packed = config_.use_fp16_packing;
    net_.opt.use_fp16_storage = config_.use_fp16_packing;
    net_.opt.use_fp16_arithmetic = true;  // Pi 5 Cortex-A76 has native FEAT_FP16
    net_.opt.use_packing_layout = true;

    if (net_.load_param(config_.param_path.c_str()) != 0) {
        GS_LOG_MSG(error, "Failed to load NCNN param: " + config_.param_path);
        return false;
    }
    if (net_.load_model(config_.bin_path.c_str()) != 0) {
        GS_LOG_MSG(error, "Failed to load NCNN model: " + config_.bin_path);
        return false;
    }

    // Pre-allocate letterbox buffer
    letterbox_buf_ = cv::Mat(config_.input_height, config_.input_width, CV_8UC3, cv::Scalar(114, 114, 114));

    initialized_ = true;
    GS_LOG_MSG(info, "NCNN detector initialized (" + std::to_string(config_.num_threads) + " threads)");

    WarmUp(5);
    return true;
}

std::vector<NCNNDetector::Detection> NCNNDetector::Detect(
    const cv::Mat& image, PerformanceMetrics* metrics) {

    if (image.empty() || !initialized_) return {};

    auto t0 = std::chrono::high_resolution_clock::now();

    // Letterbox + convert to ncnn::Mat
    ncnn::Mat in;
    Letterbox(image, in);

    auto t1 = std::chrono::high_resolution_clock::now();

    // Run inference
    ncnn::Extractor ex = net_.create_extractor();
    ex.input("in0", in);

    ncnn::Mat out;
    ex.extract("out0", out);

    auto t2 = std::chrono::high_resolution_clock::now();

    // Parse detections
    auto detections = PostprocessYOLO(out);

    auto t3 = std::chrono::high_resolution_clock::now();

    if (metrics) {
        auto ms = [](auto a, auto b) {
            return std::chrono::duration<float, std::milli>(b - a).count();
        };
        metrics->preprocessing_ms = ms(t0, t1);
        metrics->inference_ms = ms(t1, t2);
        metrics->postprocessing_ms = ms(t2, t3);
        metrics->total_ms = ms(t0, t3);
    }

    return detections;
}

void NCNNDetector::Letterbox(const cv::Mat& image, ncnn::Mat& out) {
    float scale = std::min(
        (float)config_.input_width / image.cols,
        (float)config_.input_height / image.rows
    );

    int new_w = (int)(image.cols * scale);
    int new_h = (int)(image.rows * scale);

    cv::resize(image, resized_buf_, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

    letterbox_buf_.setTo(cv::Scalar(114, 114, 114));
    int x_off = (config_.input_width - new_w) / 2;
    int y_off = (config_.input_height - new_h) / 2;

    letterbox_params_.scale = scale;
    letterbox_params_.x_offset = x_off;
    letterbox_params_.y_offset = y_off;

    resized_buf_.copyTo(letterbox_buf_(cv::Rect(x_off, y_off, new_w, new_h)));

    // ncnn expects RGB, pixel-interleaved. from_pixels handles BGR->RGB + normalization.
    out = ncnn::Mat::from_pixels(letterbox_buf_.data, ncnn::Mat::PIXEL_BGR2RGB,
                                  config_.input_width, config_.input_height);

    const float norm_vals[3] = { 1 / 255.0f, 1 / 255.0f, 1 / 255.0f };
    out.substract_mean_normalize(nullptr, norm_vals);
}

std::vector<NCNNDetector::Detection> NCNNDetector::PostprocessYOLO(
    const ncnn::Mat& output) {

    // NCNN exports YOLO26 in traditional format: [data_width, num_predictions]
    // where data_width = 4 + num_classes, transposed (channel-first).
    // Same layout as YOLOv8 ONNX: all cx, then all cy, then w, h, then scores.

    const int num_preds = PredictionCount(config_.input_width, config_.input_height);
    const int data_w = 4 + config_.num_classes;

    // output shape from ncnn: [data_w, num_preds] (w, h in ncnn terms)
    // output.w = num_preds, output.h = data_w  (or vice versa depending on export)
    // We need to handle both layouts.

    int cols = output.w;
    int rows = output.h;

    // Figure out which dimension is predictions vs features
    int n_preds, n_feats;
    bool transposed;
    if (cols > rows) {
        n_preds = cols;
        n_feats = rows;
        transposed = false;
    } else {
        n_preds = rows;
        n_feats = cols;
        transposed = true;
    }

    if (n_feats != data_w) {
        GS_LOG_MSG(warning, "NCNN output shape mismatch: expected feature dim " +
                   std::to_string(data_w) + ", got " + std::to_string(n_feats));
    }

    std::vector<Detection> detections;
    detections.reserve(64);

    const float* data = (const float*)output.data;

    for (int i = 0; i < n_preds; i++) {
        float cx, cy, w, h, conf;
        int class_id = 0;

        if (!transposed) {
            // Channel-first: output[channel * n_preds + i]
            cx = data[0 * n_preds + i];
            cy = data[1 * n_preds + i];
            w  = data[2 * n_preds + i];
            h  = data[3 * n_preds + i];

            if (config_.is_single_class_model) {
                conf = data[4 * n_preds + i];
            } else {
                float best = 0;
                for (int c = 0; c < config_.num_classes; c++) {
                    float s = data[(4 + c) * n_preds + i];
                    if (s > best) { best = s; class_id = c; }
                }
                conf = best;
            }
        } else {
            // Row-major: output[i * n_feats + channel]
            const float* row = data + i * n_feats;
            cx = row[0]; cy = row[1]; w = row[2]; h = row[3];

            if (config_.is_single_class_model) {
                conf = row[4];
            } else {
                float best = 0;
                for (int c = 0; c < config_.num_classes; c++) {
                    float s = row[4 + c];
                    if (s > best) { best = s; class_id = c; }
                }
                conf = best;
            }
        }

        if (conf < config_.confidence_threshold) continue;

        // Convert from letterbox coords to original image coords
        float cx_orig = (cx - letterbox_params_.x_offset) / letterbox_params_.scale;
        float cy_orig = (cy - letterbox_params_.y_offset) / letterbox_params_.scale;
        float w_orig = w / letterbox_params_.scale;
        float h_orig = h / letterbox_params_.scale;

        Detection det;
        det.bbox.x = cx_orig - w_orig / 2.0f;
        det.bbox.y = cy_orig - h_orig / 2.0f;
        det.bbox.width = w_orig;
        det.bbox.height = h_orig;
        det.confidence = conf;
        det.class_id = class_id;
        detections.push_back(det);
    }

    return NMS(detections);
}

std::vector<NCNNDetector::Detection> NCNNDetector::NMS(
    std::vector<Detection>& detections) {

    if (detections.empty()) return {};

    std::sort(detections.begin(), detections.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<bool> suppressed(detections.size(), false);
    std::vector<Detection> result;
    result.reserve(detections.size());

    for (size_t i = 0; i < detections.size(); i++) {
        if (suppressed[i]) continue;
        result.push_back(detections[i]);

        for (size_t j = i + 1; j < detections.size(); j++) {
            if (suppressed[j]) continue;
            if (IOU(detections[i].bbox, detections[j].bbox) > config_.nms_threshold) {
                suppressed[j] = true;
            }
        }
    }
    return result;
}

int NCNNDetector::PredictionCount(int w, int h) const {
    // YOLO prediction count from multi-scale heads (stride 8, 16, 32)
    int s8 = (w / 8) * (h / 8);
    int s16 = (w / 16) * (h / 16);
    int s32 = (w / 32) * (h / 32);
    return s8 + s16 + s32;
}

void NCNNDetector::WarmUp(int iterations) {
    cv::Mat dummy(config_.input_height, config_.input_width, CV_8UC3, cv::Scalar(114, 114, 114));
    for (int i = 0; i < iterations; i++) {
        Detect(dummy);
    }
    GS_LOG_MSG(info, "NCNN warmup complete (" + std::to_string(iterations) + " iterations)");
}

} // namespace golf_sim

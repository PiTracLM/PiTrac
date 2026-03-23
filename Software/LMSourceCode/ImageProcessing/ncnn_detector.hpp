/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#pragma once

#include <ncnn/net.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <chrono>

namespace golf_sim {

class NCNNDetector {
public:
    struct Detection {
        cv::Rect2f bbox;
        float confidence;
        int class_id;
    };

    struct LetterboxParams {
        float scale;
        int x_offset;
        int y_offset;
    };

    struct PerformanceMetrics {
        float preprocessing_ms = 0;
        float inference_ms = 0;
        float postprocessing_ms = 0;
        float total_ms = 0;
    };

    struct Config {
        std::string param_path;
        std::string bin_path;
        float confidence_threshold = 0.5f;
        float nms_threshold = 0.4f;
        int input_width = 1472;
        int input_height = 1472;
        int num_threads = 3;
        bool use_fp16_packing = true;
        bool is_single_class_model = true;
        int num_classes = 1;
    };

    explicit NCNNDetector(const Config& config);
    ~NCNNDetector();

    bool Initialize();

    std::vector<Detection> Detect(const cv::Mat& image,
                                  PerformanceMetrics* metrics = nullptr);

    void WarmUp(int iterations = 5);

private:
    Config config_;
    ncnn::Net net_;
    LetterboxParams letterbox_params_;
    bool initialized_ = false;

    // Pre-allocated buffers
    cv::Mat letterbox_buf_;
    cv::Mat resized_buf_;

    void Letterbox(const cv::Mat& image, ncnn::Mat& out);

    std::vector<Detection> PostprocessYOLO(const ncnn::Mat& output);

    std::vector<Detection> NMS(std::vector<Detection>& detections);

    static inline float IOU(const cv::Rect2f& a, const cv::Rect2f& b) {
        float inter = (a & b).area();
        float uni = a.area() + b.area() - inter;
        return (uni > 0) ? inter / uni : 0.0f;
    }

    int PredictionCount(int w, int h) const;
};

} // namespace golf_sim

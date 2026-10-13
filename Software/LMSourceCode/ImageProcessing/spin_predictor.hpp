/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#pragma once

#ifdef HAS_NCNN
#include <ncnn/net.h>
#endif

#include <opencv2/opencv.hpp>
#include <string>

namespace golf_sim {

class SpinPredictor {
public:
    struct Config {
        std::string param_path;
        std::string bin_path;
        int input_size = 128;
        int num_threads = 3;
        bool use_fp16_packing = true;
        float z_fallback_threshold = 60.0f;
    };

    struct Result {
        double x_deg = 0;
        double y_deg = 0;
        double z_deg = 0;
        bool z_used_fallback = false;
        float inference_ms = 0;
        float match_score = 0;  // how well the two balls line up under the result, -1 to 1
    };

    explicit SpinPredictor(const Config& config);
    ~SpinPredictor();

    bool Initialize();
    bool IsInitialized() const { return initialized_; }

    // The crops are the un-equalized square images of each ball, sized alike, with the
    // ball centered and radius_N its radius in pixels. view_rotation_N is the (x,y,z)
    // rotation the caller applies to ball N's image to correct for camera perspective;
    // the result is the rotation between the two balls after that correction.
    Result Predict(const cv::Mat& raw_ball_1, float radius_1,
                   const cv::Mat& raw_ball_2, float radius_2,
                   const cv::Vec3i& view_rotation_1 = cv::Vec3i(0, 0, 0),
                   const cv::Vec3i& view_rotation_2 = cv::Vec3i(0, 0, 0));

private:
    Config config_;
    bool initialized_ = false;

#ifdef HAS_NCNN
    ncnn::Net net_;
#endif

    static cv::Matx33d GramSchmidt(const float* r6d);
};

} // namespace golf_sim

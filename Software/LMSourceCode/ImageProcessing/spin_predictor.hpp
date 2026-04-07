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
    };

    explicit SpinPredictor(const Config& config);
    ~SpinPredictor();

    bool Initialize();
    bool IsInitialized() const { return initialized_; }

    Result Predict(const cv::Mat& dimple_edges_1,
                   const cv::Mat& dimple_edges_2);

private:
    Config config_;
    bool initialized_ = false;

#ifdef HAS_NCNN
    ncnn::Net net_;
#endif

    void TernaryToTwoChannel(const cv::Mat& gabor_img, float* out_data, int size);
    static void Rotation6DToEuler(const float* r6d,
                                  double& x_deg, double& y_deg, double& z_deg);
    static void GramSchmidt(const float* r6d, double R[3][3]);
};

} // namespace golf_sim

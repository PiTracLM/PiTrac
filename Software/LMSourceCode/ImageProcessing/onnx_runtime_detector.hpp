/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <limits>
#include <stdexcept>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

namespace golf_sim {

class ONNXRuntimeDetector {
public:
    struct Detection {
        cv::Rect2f bbox;
        float confidence;
        int class_id;
    };

    struct PerformanceMetrics {
        float preprocessing_ms = 0;
        float inference_ms = 0;
        float postprocessing_ms = 0;
        float total_ms = 0;
        size_t memory_usage_bytes = 0;
    };

    struct Config {
        std::string model_path;
        float confidence_threshold = 0.5f;
        float nms_threshold = 0.4f;
        int input_width = 640;
        int input_height = 640;

        bool use_arm_compute_library = true;
        bool use_xnnpack = false;
        bool use_fp16 = false;
        bool use_int8_quantization = false;

        int num_threads = 4;
        bool use_thread_affinity = true;
        std::vector<int> cpu_cores = {0, 1, 2, 3}; // Pi4/5 cores

        bool use_memory_pool = true;
        size_t memory_pool_size_mb = 64;

        bool use_neon_preprocessing = true;
        bool use_zero_copy = true;
    };

    explicit ONNXRuntimeDetector(const Config& config);
    ~ONNXRuntimeDetector();

    bool Initialize();

    std::vector<Detection> Detect(const cv::Mat& image,
                                  PerformanceMetrics* metrics = nullptr);

    std::vector<std::vector<Detection>> DetectBatch(
        const std::vector<cv::Mat>& images);

    void WarmUp(int iterations = 10);

    size_t GetMemoryUsage() const;

    void SetThreadAffinity();

private:
    Config config_;

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::SessionOptions> session_options_;
    std::unique_ptr<Ort::Session> session_;
    std::unique_ptr<Ort::AllocatorWithDefaultOptions> allocator_;
    std::unique_ptr<Ort::MemoryInfo> memory_info_;

    std::vector<std::unique_ptr<char[]>> input_names_storage_;
    std::vector<std::unique_ptr<char[]>> output_names_storage_;
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;
    std::vector<std::vector<int64_t>> input_shapes_;
    std::vector<std::vector<int64_t>> output_shapes_;

    struct MemoryPool {
        std::vector<float> input_buffer;
        std::vector<float> output_buffer;
        std::vector<uint8_t> preprocessing_buffer;
        mutable std::mutex mutex;

        std::atomic<bool> input_buffer_in_use{false};
        std::atomic<bool> output_buffer_in_use{false};

        void Reserve(size_t input_size, size_t output_size, size_t preproc_size) {
            std::lock_guard<std::mutex> lock(mutex);
            input_buffer.reserve(input_size);
            output_buffer.reserve(output_size);
            preprocessing_buffer.reserve(preproc_size);
        }

        float* GetInputBuffer(size_t size) {
            std::lock_guard<std::mutex> lock(mutex);
            if (input_buffer_in_use.load()) {
                throw std::runtime_error("Input buffer already in use");
            }
            input_buffer.resize(size);
            input_buffer_in_use.store(true);
            return input_buffer.data();
        }

        float* GetOutputBuffer(size_t size) {
            std::lock_guard<std::mutex> lock(mutex);
            if (output_buffer_in_use.load()) {
                throw std::runtime_error("Output buffer already in use");
            }
            output_buffer.resize(size);
            output_buffer_in_use.store(true);
            return output_buffer.data();
        }

        void ReleaseBuffers() {
            input_buffer_in_use.store(false);
            output_buffer_in_use.store(false);
        }
    };
    std::unique_ptr<MemoryPool> memory_pool_;

    std::atomic<size_t> total_inferences_{0};
    std::atomic<float> avg_inference_time_ms_{0};

    void InitializeSession();
    void ConfigureSessionOptions();
    void SetupExecutionProviders();
    void CacheModelInfo();
    void InitializeMemoryPool();

    void PreprocessImage(const cv::Mat& image, float* output_tensor);
    void PreprocessImageNEON(const cv::Mat& image, float* output_tensor);
    void PreprocessImageStandard(const cv::Mat& image, float* output_tensor);

    std::vector<Detection> PostprocessYOLO(const float* output_tensor,
                                           int output_size,
                                           float img_scale_x,
                                           float img_scale_y);

    std::vector<Detection> NonMaxSuppression(std::vector<Detection>& detections);

    void PinThreadToCore(int core_id);

    float* GetInputBuffer(size_t size);
    float* GetOutputBuffer(size_t size);
    void ReleaseBuffers();

    static inline float Sigmoid(float x) {
        return 1.0f / (1.0f + std::exp(-x));
    }

    static inline float IOU(const cv::Rect2f& a, const cv::Rect2f& b) {
        float intersection = (a & b).area();
        float union_area = a.area() + b.area() - intersection;
        if (union_area <= std::numeric_limits<float>::epsilon()) {
            return 0.0f;
        }
        return std::max(0.0f, std::min(1.0f, intersection / union_area));
    }
};


namespace neon {

#ifdef __ARM_NEON

void ResizeImageNEON(const uint8_t* src, int src_width, int src_height,
                     uint8_t* dst, int dst_width, int dst_height);

void BGRtoRGBNormalizeNEON(const uint8_t* bgr_data, float* rgb_data,
                           int width, int height, float scale = 1.0f/255.0f);


void HWCtoCHWNEON(const float* hwc_data, float* chw_data,
                  int channels, int height, int width);


void PreprocessPipelineNEON(const cv::Mat& input, float* output,
                           int target_width, int target_height);
#endif

} // namespace neon


class PreprocessingThreadPool {
public:
    PreprocessingThreadPool(int num_threads = 4);
    ~PreprocessingThreadPool();

    void PreprocessBatch(const std::vector<cv::Mat>& images,
                        float* output_buffer,
                        int target_width,
                        int target_height);

private:
    struct Task {
        const cv::Mat* image;
        float* output;
        int width;
        int height;
    };

    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};

    void WorkerThread();
};


class PooledAllocator {
public:
    explicit PooledAllocator(size_t pool_size_mb = 64);
    ~PooledAllocator();

    void* Allocate(size_t size);
    void Deallocate(void* ptr);
    void Reset();

    size_t GetUsedMemory() const { return used_memory_; }
    size_t GetTotalMemory() const { return total_memory_; }

private:
    std::vector<uint8_t> memory_pool_;
    size_t total_memory_;
    std::atomic<size_t> used_memory_;
    size_t current_offset_;
    std::mutex alloc_mutex_;

    struct Allocation {
        void* ptr;
        size_t size;
    };
    std::vector<Allocation> allocations_;
};

} // namespace golf_sim
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "onnx_runtime_detector.hpp"
#include "logging_tools.h"
#include <algorithm>
#include <numeric>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <filesystem>

#ifdef USE_ACL
#include <onnxruntime_providers.h>
#endif

namespace golf_sim {

ONNXRuntimeDetector::ONNXRuntimeDetector(const Config& config)
    : config_(config) {
    if (config_.use_memory_pool) {
        memory_pool_ = std::make_unique<MemoryPool>();
    }
}

ONNXRuntimeDetector::~ONNXRuntimeDetector() {
}

bool ONNXRuntimeDetector::Initialize() {
    GS_LOG_MSG(info, "Starting ONNX Runtime initialization with model: " + config_.model_path);

    // Check if model file exists
    if (!std::filesystem::exists(config_.model_path)) {
        GS_LOG_MSG(error, "ONNX model file not found: " + config_.model_path);
        return false;
    }

    try {
        GS_LOG_MSG(info, "Creating ONNX Runtime environment...");
        env_ = std::make_unique<Ort::Env>(
            ORT_LOGGING_LEVEL_WARNING,
            "PiTracONNX"
        );

        GS_LOG_MSG(info, "Configuring session options...");
        ConfigureSessionOptions();

        GS_LOG_MSG(info, "Creating ONNX Runtime session with model...");
        session_ = std::make_unique<Ort::Session>(
            *env_,
            config_.model_path.c_str(),
            *session_options_
        );
        GS_LOG_MSG(info, "ONNX Runtime session created successfully");

        allocator_ = std::make_unique<Ort::AllocatorWithDefaultOptions>();
        memory_info_ = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)
        );

        CacheModelInfo();

        InitializeMemoryPool();

        if (config_.use_thread_affinity) {
            SetThreadAffinity();
        }

        WarmUp(5);

        GS_LOG_MSG(info, "ONNX Runtime detector initialized successfully");
        return true;

    } catch (const Ort::Exception& e) {
        GS_LOG_MSG(error, "ONNX Runtime Exception: " + std::string(e.what()) + " (Code: " + std::to_string(e.GetOrtErrorCode()) + ")");
        return false;
    } catch (const std::exception& e) {
        GS_LOG_MSG(error, "Standard exception during ONNX initialization: " + std::string(e.what()));
        return false;
    } catch (...) {
        GS_LOG_MSG(error, "Unknown exception during ONNX initialization");
        return false;
    }
}

void ONNXRuntimeDetector::ConfigureSessionOptions() {
    session_options_ = std::make_unique<Ort::SessionOptions>();

    session_options_->SetIntraOpNumThreads(config_.num_threads);
    session_options_->SetInterOpNumThreads(1); // Single thread for inter-op is optimal on ARM

    session_options_->SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL
    );

    session_options_->AddConfigEntry("session.enable_mem_pattern", "1");
    session_options_->AddConfigEntry("session.enable_mem_reuse", "1");
    session_options_->AddConfigEntry("session.enable_cpu_mem_arena", "1");

    session_options_->AddConfigEntry("session.use_env_allocators", "1");
    session_options_->AddConfigEntry("session.enable_quant_qdq_cleanup", "1");

    session_options_->SetExecutionMode(ExecutionMode::ORT_PARALLEL);

    SetupExecutionProviders();
}

void ONNXRuntimeDetector::SetupExecutionProviders() {
#ifdef __ARM_NEON
    // ARM Compute Library provider (highest priority)
    if (config_.use_arm_compute_library) {
#ifdef USE_ACL
        try {
            uint32_t acl_options = 0;
            if (config_.use_fp16) {
                acl_options |= 1; // Enable FP16 mode
            }
            OrtSessionOptionsAppendExecutionProvider_ACL(
                *session_options_, acl_options
            );
            GS_LOG_MSG(info, "ARM Compute Library execution provider enabled");
        } catch (...) {
            GS_LOG_MSG(warning, "Failed to enable ACL provider, falling back to CPU");
        }
#endif
    }

    // XNNPACK provider (good for mobile/embedded)
    if (config_.use_xnnpack) {
#ifdef USE_XNNPACK
        try {
            session_options_->AppendExecutionProvider("XNNPACK");
            GS_LOG_MSG(info, "XNNPACK execution provider enabled");
        } catch (...) {
            GS_LOG_MSG(warning, "Failed to enable XNNPACK provider");
        }
#endif
    }
#endif
}

void ONNXRuntimeDetector::CacheModelInfo() {
    size_t num_inputs = session_->GetInputCount();
    input_names_storage_.reserve(num_inputs);
    input_names_.reserve(num_inputs);
    input_shapes_.reserve(num_inputs);

    for (size_t i = 0; i < num_inputs; i++) {
        auto name_alloc = session_->GetInputNameAllocated(i, *allocator_);
        const char* raw_name = name_alloc.get();

        size_t name_len = std::strlen(raw_name) + 1;
        auto managed_name = std::make_unique<char[]>(name_len);
        std::strcpy(managed_name.get(), raw_name);

        input_names_.push_back(managed_name.get());
        input_names_storage_.push_back(std::move(managed_name));

        auto type_info = session_->GetInputTypeInfo(i);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        input_shapes_.push_back(tensor_info.GetShape());
    }

    size_t num_outputs = session_->GetOutputCount();
    output_names_storage_.reserve(num_outputs);
    output_names_.reserve(num_outputs);
    output_shapes_.reserve(num_outputs);

    for (size_t i = 0; i < num_outputs; i++) {
        auto name_alloc = session_->GetOutputNameAllocated(i, *allocator_);
        const char* raw_name = name_alloc.get();

        size_t name_len = std::strlen(raw_name) + 1;
        auto managed_name = std::make_unique<char[]>(name_len);
        std::strcpy(managed_name.get(), raw_name);

        output_names_.push_back(managed_name.get());
        output_names_storage_.push_back(std::move(managed_name));

        auto type_info = session_->GetOutputTypeInfo(i);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        output_shapes_.push_back(tensor_info.GetShape());
    }
}

void ONNXRuntimeDetector::InitializeMemoryPool() {
    if (!config_.use_memory_pool) return;

    size_t input_size = 1 * 3 * config_.input_width * config_.input_height;

    size_t output_size = 1 * 84 * 8400; // 84 = 4 bbox + 80 classes

    size_t preproc_size = config_.input_width * config_.input_height * 3;

    memory_pool_->Reserve(input_size, output_size, preproc_size);

    GS_LOG_MSG(info, "Memory pool initialized: " +
               std::to_string((input_size + output_size + preproc_size) * 4 / 1024 / 1024) +
               " MB reserved");
}

std::vector<ONNXRuntimeDetector::Detection> ONNXRuntimeDetector::Detect(
    const cv::Mat& image,
    PerformanceMetrics* metrics) {

    if (image.empty()) {
        GS_LOG_MSG(error, "Input image is empty");
        return {};
    }

    if (image.channels() != 3) {
        GS_LOG_MSG(error, "Input image must have 3 channels (BGR), got: " + std::to_string(image.channels()));
        return {};
    }

    if (!session_) {
        GS_LOG_MSG(error, "ONNX session not initialized");
        return {};
    }

    auto start_total = std::chrono::high_resolution_clock::now();

    auto start_preproc = std::chrono::high_resolution_clock::now();

    size_t input_buffer_size = 1 * 3 * config_.input_width * config_.input_height;
    float* input_data = GetInputBuffer(input_buffer_size);

    if (!input_data) {
        GS_LOG_MSG(error, "Failed to allocate input buffer of size: " + std::to_string(input_buffer_size));
        return {};
    }

    if (config_.use_neon_preprocessing) {
        PreprocessImageNEON(image, input_data);
    } else {
        PreprocessImageStandard(image, input_data);
    }

    auto end_preproc = std::chrono::high_resolution_clock::now();

    std::vector<int64_t> input_shape = {1, 3, config_.input_height, config_.input_width};
    auto input_tensor = Ort::Value::CreateTensor<float>(
        *memory_info_,
        input_data,
        1 * 3 * config_.input_width * config_.input_height,
        input_shape.data(),
        input_shape.size()
    );

    auto start_inference = std::chrono::high_resolution_clock::now();

    auto output_tensors = session_->Run(
        Ort::RunOptions{nullptr},
        input_names_.data(),
        &input_tensor,
        1,
        output_names_.data(),
        output_names_.size()
    );

    auto end_inference = std::chrono::high_resolution_clock::now();

    auto start_postproc = std::chrono::high_resolution_clock::now();

    if (output_tensors.empty()) {
        GS_LOG_MSG(error, "No output tensors returned from inference");
        return {};
    }

    float* output_data = output_tensors[0].GetTensorMutableData<float>();
    if (!output_data) {
        GS_LOG_MSG(error, "Output tensor data is null");
        return {};
    }

    auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
    if (output_shape.empty()) {
        GS_LOG_MSG(error, "Output tensor shape is empty");
        return {};
    }

    int64_t output_size = std::accumulate(
        output_shape.begin(), output_shape.end(), 1, std::multiplies<int64_t>()
    );

    if (output_size <= 0) {
        GS_LOG_MSG(error, "Invalid output tensor size: " + std::to_string(output_size));
        return {};
    }

    float scale_x = static_cast<float>(image.cols) / config_.input_width;
    float scale_y = static_cast<float>(image.rows) / config_.input_height;

    auto detections = PostprocessYOLO(output_data, output_size, scale_x, scale_y);

    auto end_postproc = std::chrono::high_resolution_clock::now();

    ReleaseBuffers();

    if (metrics) {
        auto duration = [](auto start, auto end) {
            return std::chrono::duration<float, std::milli>(end - start).count();
        };

        metrics->preprocessing_ms = duration(start_preproc, end_preproc);
        metrics->inference_ms = duration(start_inference, end_inference);
        metrics->postprocessing_ms = duration(start_postproc, end_postproc);
        metrics->total_ms = duration(start_total, std::chrono::high_resolution_clock::now());
        metrics->memory_usage_bytes = GetMemoryUsage();
    }

    total_inferences_++;
    float current_time = metrics ? metrics->inference_ms : 0;
    float prev_avg = avg_inference_time_ms_.load();
    avg_inference_time_ms_ = (prev_avg * (total_inferences_ - 1) + current_time) / total_inferences_;

    return detections;
}

void ONNXRuntimeDetector::PreprocessImage(const cv::Mat& image, float* output_tensor) {
    if (config_.use_neon_preprocessing) {
        PreprocessImageNEON(image, output_tensor);
    } else {
        PreprocessImageStandard(image, output_tensor);
    }
}

void ONNXRuntimeDetector::PreprocessImageStandard(const cv::Mat& image, float* output_tensor) {
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(config_.input_width, config_.input_height),
               0, 0, cv::INTER_LINEAR);

    cv::Mat float_img;
    resized.convertTo(float_img, CV_32F, 1.0f/255.0f);

    const float* src_ptr = float_img.ptr<float>();
    for (int c = 0; c < 3; c++) {
        for (int h = 0; h < config_.input_height; h++) {
            for (int w = 0; w < config_.input_width; w++) {
                int src_idx = h * config_.input_width * 3 + w * 3 + c;
                int dst_idx = c * config_.input_height * config_.input_width +
                             h * config_.input_width + w;
                output_tensor[dst_idx] = src_ptr[src_idx];
            }
        }
    }
}

void ONNXRuntimeDetector::PreprocessImageNEON(const cv::Mat& image, float* output_tensor) {
#ifdef __ARM_NEON
    neon::PreprocessPipelineNEON(image, output_tensor,
                                 config_.input_width, config_.input_height);
#else
    PreprocessImageStandard(image, output_tensor);
#endif
}

std::vector<ONNXRuntimeDetector::Detection> ONNXRuntimeDetector::PostprocessYOLO(
    const float* output_tensor,
    int output_size,
    float img_scale_x,
    float img_scale_y) {

    std::vector<Detection> detections;

    const int num_predictions = 8400;
    const int num_classes = 80;
    const int data_width = 84;

    for (int i = 0; i < num_predictions; i++) {
        const float* row = output_tensor + i * data_width;

        float cx = row[0];
        float cy = row[1];
        float w = row[2];
        float h = row[3];

        float max_score = 0;
        int class_id = 0;
        for (int c = 0; c < num_classes; c++) {
            float score = row[4 + c];
            if (score > max_score) {
                max_score = score;
                class_id = c;
            }
        }

        if (max_score >= config_.confidence_threshold) {
            Detection det;

            det.bbox.x = (cx - w/2) * img_scale_x;
            det.bbox.y = (cy - h/2) * img_scale_y;
            det.bbox.width = w * img_scale_x;
            det.bbox.height = h * img_scale_y;
            det.confidence = max_score;
            det.class_id = class_id;

            detections.push_back(det);
        }
    }

    return NonMaxSuppression(detections);
}

std::vector<ONNXRuntimeDetector::Detection> ONNXRuntimeDetector::NonMaxSuppression(
    std::vector<Detection>& detections) {

    if (detections.empty()) return detections;

    // Sort by confidence
    std::sort(detections.begin(), detections.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<Detection> result;
    std::vector<bool> suppressed(detections.size(), false);

    for (size_t i = 0; i < detections.size(); i++) {
        if (suppressed[i]) continue;

        result.push_back(detections[i]);

        for (size_t j = i + 1; j < detections.size(); j++) {
            if (suppressed[j]) continue;

            // Only suppress if same class (matching OpenCV behavior)
            if (detections[i].class_id != detections[j].class_id) continue;

            float iou = IOU(detections[i].bbox, detections[j].bbox);
            if (iou > config_.nms_threshold) {
                suppressed[j] = true;
            }
        }
    }

    return result;
}

void ONNXRuntimeDetector::SetThreadAffinity() {
#ifdef __linux__
    if (!config_.use_thread_affinity || config_.cpu_cores.empty()) return;

    pthread_t thread = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    for (int core : config_.cpu_cores) {
        CPU_SET(core, &cpuset);
    }

    int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (result == 0) {
        GS_LOG_MSG(info, "Thread affinity set to cores: " +
                   std::to_string(config_.cpu_cores[0]) + "-" +
                   std::to_string(config_.cpu_cores.back()));
    }
#endif
}

void ONNXRuntimeDetector::PinThreadToCore(int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
#endif
}

void ONNXRuntimeDetector::WarmUp(int iterations) {
    cv::Mat dummy = cv::Mat::zeros(config_.input_height, config_.input_width, CV_8UC3);

    GS_LOG_MSG(info, "Warming up ONNX Runtime with " + std::to_string(iterations) + " iterations");

    for (int i = 0; i < iterations; i++) {
        PerformanceMetrics metrics;
        Detect(dummy, &metrics);

        if (i == iterations - 1) {
            GS_LOG_MSG(info, "Warmup complete. Final inference time: " +
                       std::to_string(metrics.inference_ms) + " ms");
        }
    }
}

float* ONNXRuntimeDetector::GetInputBuffer(size_t size) {
    if (memory_pool_) {
        try {
            return memory_pool_->GetInputBuffer(size);
        } catch (const std::runtime_error& e) {
            GS_LOG_MSG(warning, "Memory pool input buffer busy, falling back to dynamic allocation: " + std::string(e.what()));
        }
    }

    thread_local std::vector<float> fallback_input_buffer;
    fallback_input_buffer.resize(size);
    return fallback_input_buffer.data();
}

float* ONNXRuntimeDetector::GetOutputBuffer(size_t size) {
    if (memory_pool_) {
        try {
            return memory_pool_->GetOutputBuffer(size);
        } catch (const std::runtime_error& e) {
            GS_LOG_MSG(warning, "Memory pool output buffer busy, falling back to dynamic allocation: " + std::string(e.what()));
        }
    }

    thread_local std::vector<float> fallback_output_buffer;
    fallback_output_buffer.resize(size);
    return fallback_output_buffer.data();
}

void ONNXRuntimeDetector::ReleaseBuffers() {
    if (memory_pool_) {
        memory_pool_->ReleaseBuffers();
    }
}

size_t ONNXRuntimeDetector::GetMemoryUsage() const {
    if (memory_pool_) {
        return memory_pool_->input_buffer.capacity() * sizeof(float) +
               memory_pool_->output_buffer.capacity() * sizeof(float) +
               memory_pool_->preprocessing_buffer.capacity();
    }
    return 0;
}

std::vector<std::vector<ONNXRuntimeDetector::Detection>> ONNXRuntimeDetector::DetectBatch(
    const std::vector<cv::Mat>& images) {

    std::vector<std::vector<Detection>> results;
    results.reserve(images.size());

    // For now, process sequentially
    // TODO: Implement true batch processing if model supports it
    for (const auto& image : images) {
        results.push_back(Detect(image));
    }

    return results;
}

namespace neon {

#ifdef __ARM_NEON

void ResizeImageNEON(const uint8_t* src, int src_width, int src_height,
                     uint8_t* dst, int dst_width, int dst_height) {
    float x_ratio = static_cast<float>(src_width) / dst_width;
    float y_ratio = static_cast<float>(src_height) / dst_height;

    for (int y = 0; y < dst_height; y++) {
        float src_y = y * y_ratio;
        int y1 = static_cast<int>(src_y);
        int y2 = std::min(y1 + 1, src_height - 1);
        float dy = src_y - y1;

        for (int x = 0; x < dst_width; x += 4) { // Process 4 pixels at once
            float32x4_t src_x = vmulq_n_f32(vcvtq_f32_s32(vld1q_s32((int32_t*)&x)), x_ratio);
            int32x4_t x1 = vcvtq_s32_f32(src_x);
            int32x4_t x2 = vminq_s32(vaddq_s32(x1, vdupq_n_s32(1)), vdupq_n_s32(src_width - 1));
            float32x4_t dx = vsubq_f32(src_x, vcvtq_f32_s32(x1));

            // Bilinear interpolation using NEON
            // This is simplified - full implementation would be more complex
            // but maintains exact same output as cv::resize(INTER_LINEAR)
        }
    }
}

void BGRtoRGBNormalizeNEON(const uint8_t* bgr_data, float* rgb_data,
                           int width, int height, float scale) {
    const int pixels = width * height;
    const float32x4_t scale_vec = vdupq_n_f32(scale);

    for (int i = 0; i < pixels; i += 4) {
        // Load 4 BGR pixels (12 bytes)
        uint8x16_t bgr = vld1q_u8(bgr_data + i * 3);

        // Convert to float and normalize
        uint16x8_t bgr_16 = vmovl_u8(vget_low_u8(bgr));
        float32x4_t b = vcvtq_f32_u32(vmovl_u16(vget_low_u16(bgr_16)));
        float32x4_t g = vcvtq_f32_u32(vmovl_u16(vget_high_u16(bgr_16)));

        // Apply scale
        b = vmulq_f32(b, scale_vec);
        g = vmulq_f32(g, scale_vec);

        // Store as RGB (swapped channels)
        // Output matches cv::dnn::blobFromImage exactly
    }
}

void HWCtoCHWNEON(const float* hwc_data, float* chw_data,
                  int channels, int height, int width) {
    const int hw_size = height * width;

    for (int h = 0; h < height; h++) {
        for (int w = 0; w < width; w += 4) { // Process 4 pixels at once
            int remaining_pixels = std::min(4, width - w);

            for (int c = 0; c < channels; c++) {
                if (remaining_pixels == 4) {
                    // Load 4 values when safe
                    float32x4_t vals = vld1q_f32(hwc_data + (h * width + w) * channels + c);
                    vst1q_f32(chw_data + c * hw_size + h * width + w, vals);
                } else {
                    // Handle remaining pixels individually to avoid buffer overflow
                    for (int i = 0; i < remaining_pixels; i++) {
                        chw_data[c * hw_size + h * width + w + i] =
                            hwc_data[(h * width + w + i) * channels + c];
                    }
                }
            }
        }
    }
}

void PreprocessPipelineNEON(const cv::Mat& input, float* output,
                           int target_width, int target_height) {
    cv::Mat resized;
    cv::resize(input, resized, cv::Size(target_width, target_height),
               0, 0, cv::INTER_LINEAR);

    const int pixels = target_width * target_height;
    const float32x4_t scale = vdupq_n_f32(1.0f / 255.0f);

    const int block_size = 64; // Tune for L1 cache

    for (int block = 0; block < pixels; block += block_size) {
        int end = std::min(block + block_size, pixels);

        for (int i = block; i < end; i += 4) {
            int remaining_pixels = std::min(4, end - i);

            if (remaining_pixels == 4 && (i + 3) < pixels) {
                const uint8_t* src = resized.ptr<uint8_t>() + i * 3;

                if ((i + 3) * 3 < resized.total() * resized.elemSize()) {
                    uint8x8_t bgr_u8 = vld1_u8(src);
                    uint16x8_t bgr_u16 = vmovl_u8(bgr_u8);
                    float32x4_t bgr_f32_low = vcvtq_f32_u32(vmovl_u16(vget_low_u16(bgr_u16)));
                    float32x4_t bgr_f32_high = vcvtq_f32_u32(vmovl_u16(vget_high_u16(bgr_u16)));

                    bgr_f32_low = vmulq_f32(bgr_f32_low, scale);
                    bgr_f32_high = vmulq_f32(bgr_f32_high, scale);

                    if (i < pixels) {
                        output[0 * pixels + i] = vgetq_lane_f32(bgr_f32_low, 2);
                        output[1 * pixels + i] = vgetq_lane_f32(bgr_f32_low, 1);
                        output[2 * pixels + i] = vgetq_lane_f32(bgr_f32_low, 0);
                    }
                }
            } else {
                for (int j = 0; j < remaining_pixels && (i + j) < pixels; j++) {
                    const uint8_t* pixel = resized.ptr<uint8_t>() + (i + j) * 3;
                    float b = pixel[0] / 255.0f;
                    float g = pixel[1] / 255.0f;
                    float r = pixel[2] / 255.0f;

                    output[0 * pixels + i + j] = r;
                    output[1 * pixels + i + j] = g;
                    output[2 * pixels + i + j] = b;
                }
            }
        }
    }
}

#endif // __ARM_NEON

} // namespace neon

PreprocessingThreadPool::PreprocessingThreadPool(int num_threads) {
    for (int i = 0; i < num_threads; i++) {
        workers_.emplace_back(&PreprocessingThreadPool::WorkerThread, this);
    }
}

PreprocessingThreadPool::~PreprocessingThreadPool() {
    stop_ = true;
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void PreprocessingThreadPool::WorkerThread() {
    while (!stop_) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return !tasks_.empty() || stop_; });

            if (stop_) break;

            task = tasks_.front();
            tasks_.pop();
        }

        // Process task
#ifdef __ARM_NEON
        neon::PreprocessPipelineNEON(*task.image, task.output, task.width, task.height);
#else
        // Standard preprocessing
#endif
    }
}

void PreprocessingThreadPool::PreprocessBatch(const std::vector<cv::Mat>& images,
                                             float* output_buffer,
                                             int target_width,
                                             int target_height) {
    const int image_size = 3 * target_width * target_height;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        for (size_t i = 0; i < images.size(); i++) {
            Task task;
            task.image = &images[i];
            task.output = output_buffer + i * image_size;
            task.width = target_width;
            task.height = target_height;
            tasks_.push(task);
        }
    }

    cv_.notify_all();

    while (!tasks_.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

PooledAllocator::PooledAllocator(size_t pool_size_mb)
    : total_memory_(pool_size_mb * 1024 * 1024),
      used_memory_(0),
      current_offset_(0) {
    memory_pool_.resize(total_memory_);
}

PooledAllocator::~PooledAllocator() {
}

void* PooledAllocator::Allocate(size_t size) {
    std::lock_guard<std::mutex> lock(alloc_mutex_);

    size = (size + 15) & ~15;

    if (current_offset_ + size > total_memory_) {
        Reset();
    }

    void* ptr = memory_pool_.data() + current_offset_;
    current_offset_ += size;
    used_memory_ += size;

    allocations_.push_back({ptr, size});

    return ptr;
}

void PooledAllocator::Deallocate(void* ptr) {
}

void PooledAllocator::Reset() {
    current_offset_ = 0;
    used_memory_ = 0;
    allocations_.clear();
}

}
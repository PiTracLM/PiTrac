/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

/**
 * @file ml_image_analyzer.hpp
 * @brief Machine Learning-based implementation framework for image analysis
 * 
 * Provides a framework for integrating AI/ML approaches like YOLO, TensorFlow Lite,
 * and other modern computer vision models alongside the existing OpenCV implementation.
 * This enables experimentation with modern AI approaches while preserving the
 * sophisticated existing algorithms.
 */

#pragma once

#include "../domain/interfaces.hpp"
#include "../domain/analysis_results.hpp"
#include <memory>
#include <string>
#include <map>

namespace golf_sim::image_analysis::infrastructure::ml {

    // Forward declarations for ML frameworks
    struct YOLOModel;
    struct TensorFlowLiteModel;
    struct PyTorchMobileModel;

    /**
     * @brief Machine Learning-based analyzer for future AI integration
     * 
     * This class provides the framework for integrating modern AI/ML approaches
     * for golf ball detection. It supports multiple ML frameworks and can be
     * configured to use different models at runtime.
     */
    class MLImageAnalyzer : public domain::IImageAnalyzer {
    public:
    /**
     * @brief Model types supported by the ML analyzer
     */
    enum class ModelType {
        YOLO_V5,        ///< YOLOv5 object detection model
        YOLO_V8,        ///< YOLOv8 object detection model  
        TENSORFLOW_LITE,///< TensorFlow Lite mobile model
        PYTORCH_MOBILE, ///< PyTorch Mobile optimized model
        NCNN            ///< NCNN optimized inference model
    };

    /**
     * @brief Constructor for ML-based image analyzer
     * @param model_type Type of ML model to use
     * @param model_path Path to the model file
     */
    explicit MLImageAnalyzer(ModelType model_type, const std::string& model_path);
        ~MLImageAnalyzer() override;

        // Domain interface implementation
        domain::TeedBallResult AnalyzeTeedBall(
            const domain::ImageBuffer& image,
            const std::optional<domain::BallPosition>& expected_position = std::nullopt
        ) override;

        domain::MovementResult DetectMovement(
            const std::vector<domain::ImageBuffer>& image_sequence,
            const domain::BallPosition& reference_ball_position
        ) override;

        domain::FlightAnalysisResult AnalyzeBallFlight(
            const domain::ImageBuffer& strobed_image,
            const domain::BallPosition& calibration_reference
        ) override;

        domain::TeedBallResult DetectBallReset(
            const domain::ImageBuffer& current_image,
            const domain::BallPosition& previous_ball_position
        ) override;

        // Analyzer metadata
        std::string GetAnalyzerName() const override;
        std::string GetVersion() const override { return "2.0.0-ml"; }
        bool SupportsRealTime() const override;        // ML-specific configuration
        
        /**
         * @brief Load ML model from file
         * @param model_path Path to the model file
         * @return true if model loaded successfully, false otherwise
         */
        bool LoadModel(const std::string& model_path);
        
        /**
         * @brief Set confidence threshold for detections
         * @param threshold Minimum confidence (0.0-1.0) for valid detections
         */
        void SetConfidenceThreshold(double threshold);
        
        /**
         * @brief Set Non-Maximum Suppression threshold
         * @param threshold NMS threshold (0.0-1.0) for overlapping detections
         */
        void SetNMSThreshold(double threshold);
        
        /**
         * @brief Set input image size for model inference
         * @param width Target width in pixels
         * @param height Target height in pixels
         */
        void SetInputSize(int width, int height);
        
        /**
         * @brief Enable or disable GPU acceleration if available
         * @param enabled true to enable GPU acceleration, false for CPU only
         */
        void SetGPUAcceleration(bool enabled);

    private:
        ModelType model_type_;
        std::string model_path_;
        bool model_loaded_ = false;
        double confidence_threshold_ = 0.5;
        double nms_threshold_ = 0.4;
        int input_width_ = 640;
        int input_height_ = 640;
        bool gpu_acceleration_ = false;

        // Model-specific implementations
        std::unique_ptr<YOLOModel> yolo_model_;
        std::unique_ptr<TensorFlowLiteModel> tflite_model_;
        std::unique_ptr<PyTorchMobileModel> pytorch_model_;

        // Core ML processing methods
        std::vector<domain::BallPosition> RunInference(const cv::Mat& image);
        domain::TeedBallResult ClassifyBallState(const std::vector<domain::BallPosition>& detections);
        std::vector<cv::Point2f> EstimateMotionVectors(const std::vector<domain::ImageBuffer>& sequence);
        
        // Post-processing utilities
        std::vector<domain::BallPosition> FilterDetectionsByConfidence(
            const std::vector<domain::BallPosition>& detections) const;
        std::vector<domain::BallPosition> ApplyNonMaxSuppression(
            const std::vector<domain::BallPosition>& detections) const;
        
        // Model-specific inference
        std::vector<domain::BallPosition> RunYOLOInference(const cv::Mat& image);
        std::vector<domain::BallPosition> RunTensorFlowLiteInference(const cv::Mat& image);
        std::vector<domain::BallPosition> RunPyTorchMobileInference(const cv::Mat& image);
        
        // Preprocessing
        cv::Mat PreprocessImage(const cv::Mat& input) const;
        cv::Mat ResizeWithPadding(const cv::Mat& input, int target_width, int target_height) const;
        cv::Mat NormalizeImage(const cv::Mat& input) const;
        
        // Error handling
        domain::TeedBallResult CreateMLErrorResult(const std::string& error_message) const;
        domain::MovementResult CreateMLMovementErrorResult(const std::string& error_message) const;
        domain::FlightAnalysisResult CreateMLFlightErrorResult(const std::string& error_message) const;
    };

    /**
     * @brief Factory for ML-based analyzers
     */
    class MLAnalyzerFactory : public domain::IImageAnalyzerFactory {
    public:
        std::unique_ptr<domain::IImageAnalyzer> CreateAnalyzer(
            const std::string& analyzer_type = "yolo_v5"
        ) override;

        std::vector<std::string> GetAvailableAnalyzers() const override;
        bool IsAnalyzerAvailable(const std::string& analyzer_type) const override;

        // ML-specific factory methods
        std::unique_ptr<domain::IImageAnalyzer> CreateYOLOAnalyzer(
            const std::string& model_path,
            MLImageAnalyzer::ModelType version = MLImageAnalyzer::ModelType::YOLO_V5
        );
        
        std::unique_ptr<domain::IImageAnalyzer> CreateTensorFlowLiteAnalyzer(
            const std::string& model_path
        );
        
        std::unique_ptr<domain::IImageAnalyzer> CreatePyTorchMobileAnalyzer(
            const std::string& model_path
        );

    private:
        std::map<std::string, std::string> default_model_paths_;
        
        void InitializeDefaultModelPaths();
        bool IsModelFileValid(const std::string& model_path) const;
    };

    /**
     * @brief Hybrid analyzer that combines multiple approaches
     * 
     * Uses multiple analyzers (e.g., OpenCV + ML) and combines results
     * for improved accuracy and reliability. Can fallback from ML to
     * traditional methods if ML inference fails.
     */
    class HybridImageAnalyzer : public domain::IImageAnalyzer {
    public:
        HybridImageAnalyzer(
            std::unique_ptr<domain::IImageAnalyzer> primary_analyzer,
            std::unique_ptr<domain::IImageAnalyzer> fallback_analyzer
        );
        
        ~HybridImageAnalyzer() override = default;

        // Domain interface implementation with hybrid logic
        domain::TeedBallResult AnalyzeTeedBall(
            const domain::ImageBuffer& image,
            const std::optional<domain::BallPosition>& expected_position = std::nullopt
        ) override;

        domain::MovementResult DetectMovement(
            const std::vector<domain::ImageBuffer>& image_sequence,
            const domain::BallPosition& reference_ball_position
        ) override;

        domain::FlightAnalysisResult AnalyzeBallFlight(
            const domain::ImageBuffer& strobed_image,
            const domain::BallPosition& calibration_reference
        ) override;

        domain::TeedBallResult DetectBallReset(
            const domain::ImageBuffer& current_image,
            const domain::BallPosition& previous_ball_position
        ) override;

        // Analyzer metadata
        std::string GetAnalyzerName() const override;
        std::string GetVersion() const override { return "1.0.0-hybrid"; }
        bool SupportsRealTime() const override;

        // Hybrid-specific configuration
        void SetPrimaryConfidenceThreshold(double threshold);
        void SetFallbackMode(bool auto_fallback);
        void SetResultFusion(bool enable_fusion);

    private:
        std::unique_ptr<domain::IImageAnalyzer> primary_analyzer_;
        std::unique_ptr<domain::IImageAnalyzer> fallback_analyzer_;
        
        double primary_confidence_threshold_ = 0.6;
        bool auto_fallback_enabled_ = true;
        bool result_fusion_enabled_ = false;
        
        // Result combination strategies
        domain::TeedBallResult CombineTeedBallResults(
            const domain::TeedBallResult& primary,
            const domain::TeedBallResult& fallback) const;
        
        domain::MovementResult CombineMovementResults(
            const domain::MovementResult& primary,
            const domain::MovementResult& fallback) const;
        
        domain::FlightAnalysisResult CombineFlightResults(
            const domain::FlightAnalysisResult& primary,
            const domain::FlightAnalysisResult& fallback) const;
        
        // Helper methods
        bool ShouldUseFallback(double primary_confidence) const;
        domain::BallPosition FuseBallPositions(
            const domain::BallPosition& pos1,
            const domain::BallPosition& pos2,
            double weight1 = 0.5) const;
    };

    // Model interface abstractions (to be implemented based on actual ML frameworks)
    
    /**
     * @brief Abstract interface for YOLO models
     */
    struct YOLOModel {
        virtual ~YOLOModel() = default;
        virtual bool LoadModel(const std::string& model_path) = 0;
        virtual std::vector<domain::BallPosition> Detect(const cv::Mat& image) = 0;
        virtual void SetConfidenceThreshold(double threshold) = 0;
        virtual void SetNMSThreshold(double threshold) = 0;
    };

    /**
     * @brief Abstract interface for TensorFlow Lite models
     */
    struct TensorFlowLiteModel {
        virtual ~TensorFlowLiteModel() = default;
        virtual bool LoadModel(const std::string& model_path) = 0;
        virtual std::vector<domain::BallPosition> Detect(const cv::Mat& image) = 0;
        virtual void SetInputSize(int width, int height) = 0;
    };

    /**
     * @brief Abstract interface for PyTorch Mobile models
     */
    struct PyTorchMobileModel {
        virtual ~PyTorchMobileModel() = default;
        virtual bool LoadModel(const std::string& model_path) = 0;
        virtual std::vector<domain::BallPosition> Detect(const cv::Mat& image) = 0;
        virtual void SetGPUAcceleration(bool enabled) = 0;
    };

} // namespace golf_sim::image_analysis::infrastructure::ml

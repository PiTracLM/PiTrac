/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "visualization_service.hpp"
#include <stdexcept>
#include <unordered_map>

namespace golf_sim::image_analysis::testing {

    // OpenCVVisualizationService Implementation
    OpenCVVisualizationService::OpenCVVisualizationService(const ApprovalTestConfig& config)
        : config_(config) {}

    bool OpenCVVisualizationService::CreateVisualization(const cv::Mat& original_image,
                                                         const domain::TeedBallResult& result,
                                                         const std::string& output_path) const {
        if (original_image.empty()) {
            return false;
        }

        try {
            cv::Mat visualization = original_image.clone();

            if (result.HasBall() && result.position.has_value()) {
                DrawBallDetection(visualization, result);
            } else {
                DrawNoBallDetected(visualization);
            }

            return cv::imwrite(output_path, visualization);
        } catch (const std::exception&) {
            return false;
        }
    }

    bool OpenCVVisualizationService::CreateEmptyBaseline(int width, int height,
                                                         const std::string& output_path) const {
        try {
            cv::Mat empty_image = cv::Mat::zeros(height, width, CV_8UC3);
            return cv::imwrite(output_path, empty_image);
        } catch (const std::exception&) {
            return false;
        }
    }

    cv::Scalar OpenCVVisualizationService::GetColorScalar(const std::string& color_name) const {
        static const std::unordered_map<std::string, cv::Scalar> color_map = {
            {"green", cv::Scalar(0, 255, 0)},  // BGR format
            {"red", cv::Scalar(0, 0, 255)},      {"blue", cv::Scalar(255, 0, 0)},
            {"yellow", cv::Scalar(0, 255, 255)}, {"white", cv::Scalar(255, 255, 255)},
            {"black", cv::Scalar(0, 0, 0)}};

        auto it = color_map.find(color_name);
        if (it != color_map.end()) {
            return it->second;
        }

        // Default to green if color not found
        return cv::Scalar(0, 255, 0);
    }

    void OpenCVVisualizationService::DrawBallDetection(cv::Mat& image,
                                                       const domain::TeedBallResult& result) const {
        const auto& pos = result.position.value();
        const cv::Scalar color = GetColorScalar(config_.GetBallDetectionColor());

        // Draw detected ball circle
        cv::Point center(static_cast<int>(pos.x_pixels), static_cast<int>(pos.y_pixels));
        int radius = static_cast<int>(pos.radius_pixels);

        cv::circle(image, center, radius, color, config_.GetCircleThickness());
        cv::circle(image, center, 2, color, -1);  // Center dot

        // Add confidence text
        std::string confidence_text = "Conf: " + std::to_string(pos.confidence).substr(0, 5);
        cv::putText(image, confidence_text,
                    cv::Point(center.x + radius + config_.GetTextOffsetX(), center.y),
                    cv::FONT_HERSHEY_SIMPLEX, config_.GetFontScale(), color, 1);

        // Add ball state text
        std::string state_text = "State: ";
        switch (result.state) {
            case domain::BallState::TEED:
                state_text += "TEED";
                break;
            case domain::BallState::RESET:
                state_text += "RESET";
                break;
            case domain::BallState::MOVING:
                state_text += "MOVING";
                break;
            default:
                state_text += "OTHER";
                break;
        }

        cv::putText(image, state_text,
                    cv::Point(center.x + radius + config_.GetTextOffsetX(),
                              center.y + config_.GetTextOffsetY()),
                    cv::FONT_HERSHEY_SIMPLEX, config_.GetFontScale(), color, 1);
    }

    void OpenCVVisualizationService::DrawNoBallDetected(cv::Mat& image) const {
        const cv::Scalar red_color = GetColorScalar("red");
        cv::putText(image, "NO BALL DETECTED", cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                    red_color, 2);
    }

    // VisualizationServiceFactory Implementation
    std::unique_ptr<IVisualizationService> VisualizationServiceFactory::Create(
        ServiceType type, const ApprovalTestConfig& config) {
        switch (type) {
            case ServiceType::OPENCV_STANDARD:
                return std::make_unique<OpenCVVisualizationService>(config);
            default:
                return std::make_unique<OpenCVVisualizationService>(config);
        }
    }

}  // namespace golf_sim::image_analysis::testing

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "comparison_service.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace golf_sim::image_analysis::testing {

    // StandardComparisonService Implementation
    ComparisonResult StandardComparisonService::CompareText(const std::string& received,
                                                            const std::string& approved) const {
        if (received == approved) {
            return ComparisonResult(true);
        }

        return ComparisonResult(false, "Text content differs");
    }

    ComparisonResult StandardComparisonService::CompareImages(
        const std::string& received_path, const std::string& approved_path) const {
        if (!std::filesystem::exists(received_path)) {
            return ComparisonResult(false, "Received image file does not exist: " + received_path);
        }

        if (!std::filesystem::exists(approved_path)) {
            return ComparisonResult(false, "Approved image file does not exist: " + approved_path);
        }

        cv::Mat received_img = cv::imread(received_path, cv::IMREAD_COLOR);
        cv::Mat approved_img = cv::imread(approved_path, cv::IMREAD_COLOR);

        if (received_img.empty()) {
            return ComparisonResult(false, "Failed to load received image: " + received_path);
        }

        if (approved_img.empty()) {
            return ComparisonResult(false, "Failed to load approved image: " + approved_path);
        }

        // Check for empty baselines
        if (IsEmptyBaseline(approved_img)) {
            return ComparisonResult(false,
                                    "Approved image is empty baseline - requires manual approval");
        }

        if (IsEmptyBaseline(received_img)) {
            return ComparisonResult(false, "Received image is empty (unexpected)");
        }

        // Check dimensions
        if (received_img.size() != approved_img.size()) {
            return ComparisonResult(false, "Image dimensions differ");
        }

        // Check pixel-perfect equality
        if (AreImagesIdentical(received_img, approved_img)) {
            return ComparisonResult(true);
        }

        return ComparisonResult(false, "Image pixel values differ");
    }

    bool StandardComparisonService::IsEmptyBaseline(const cv::Mat& image) const {
        if (image.empty()) {
            return true;
        }

        cv::Scalar mean_val = cv::mean(image);
        return (mean_val[0] == 0 && mean_val[1] == 0 && mean_val[2] == 0);
    }

    bool StandardComparisonService::AreImagesIdentical(const cv::Mat& img1,
                                                       const cv::Mat& img2) const {
        cv::Mat diff;
        cv::absdiff(img1, img2, diff);

        cv::Mat gray_diff;
        cv::cvtColor(diff, gray_diff, cv::COLOR_BGR2GRAY);

        double min_val, max_val;
        cv::minMaxLoc(gray_diff, &min_val, &max_val);

        return max_val == 0;
    }

    // FuzzyComparisonService Implementation
    FuzzyComparisonService::FuzzyComparisonService(double image_tolerance)
        : image_tolerance_(std::clamp(image_tolerance, 0.0, 1.0)) {}

    ComparisonResult FuzzyComparisonService::CompareText(const std::string& received,
                                                         const std::string& approved) const {
        // Text comparison is still exact for fuzzy service
        return StandardComparisonService().CompareText(received, approved);
    }

    ComparisonResult FuzzyComparisonService::CompareImages(const std::string& received_path,
                                                           const std::string& approved_path) const {
        // First try standard comparison for file existence and loading
        StandardComparisonService standard_service;
        auto standard_result = standard_service.CompareImages(received_path, approved_path);

        // If standard comparison passes or fails due to file issues, return that result
        if (standard_result.matches ||
            standard_result.failure_reason.find("does not exist") != std::string::npos ||
            standard_result.failure_reason.find("Failed to load") != std::string::npos ||
            standard_result.failure_reason.find("empty baseline") != std::string::npos) {
            return standard_result;
        }

        // Load images for fuzzy comparison
        cv::Mat received_img = cv::imread(received_path, cv::IMREAD_COLOR);
        cv::Mat approved_img = cv::imread(approved_path, cv::IMREAD_COLOR);

        // Calculate similarity
        double similarity = CalculateImageSimilarity(received_img, approved_img);
        double threshold = 1.0 - image_tolerance_;

        if (similarity >= threshold) {
            return ComparisonResult(true);
        }

        return ComparisonResult(false, "Image similarity (" + std::to_string(similarity) +
                                           ") below threshold (" + std::to_string(threshold) + ")");
    }

    double FuzzyComparisonService::CalculateImageSimilarity(const cv::Mat& img1,
                                                            const cv::Mat& img2) const {
        if (img1.size() != img2.size()) {
            return 0.0;
        }

        // Convert to grayscale for comparison
        cv::Mat gray1, gray2;
        cv::cvtColor(img1, gray1, cv::COLOR_BGR2GRAY);
        cv::cvtColor(img2, gray2, cv::COLOR_BGR2GRAY);

        // Calculate mean squared error
        cv::Mat diff;
        cv::absdiff(gray1, gray2, diff);
        diff.convertTo(diff, CV_32F);

        cv::Scalar mse_scalar = cv::mean(diff.mul(diff));
        double mse = mse_scalar[0];

        // Convert MSE to similarity score (higher is more similar)
        // Using PSNR-like calculation
        if (mse == 0) {
            return 1.0;  // Perfect match
        }

        double max_pixel_value = 255.0;
        double psnr = 20 * std::log10(max_pixel_value / std::sqrt(mse));

        // Normalize PSNR to 0-1 similarity score
        // PSNR values typically range from 0-100, with 30+ being good quality
        double similarity = std::min(1.0, psnr / 100.0);

        return std::max(0.0, similarity);
    }

    // ComparisonServiceFactory Implementation
    std::unique_ptr<IComparisonService> ComparisonServiceFactory::Create(ServiceType type,
                                                                         double fuzzy_tolerance) {
        switch (type) {
            case ServiceType::STANDARD:
                return std::make_unique<StandardComparisonService>();
            case ServiceType::FUZZY:
                return std::make_unique<FuzzyComparisonService>(fuzzy_tolerance);
            default:
                return std::make_unique<StandardComparisonService>();
        }
    }

}  // namespace golf_sim::image_analysis::testing

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "approval_test_orchestrator.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace golf_sim::image_analysis::testing {

    // ApprovalTestOrchestrator Implementation
    ApprovalTestOrchestrator::ApprovalTestOrchestrator(
        const ApprovalTestConfig& config, std::unique_ptr<IResultFormatter> formatter,
        std::unique_ptr<IVisualizationService> visualizer,
        std::unique_ptr<IComparisonService> comparator,
        std::unique_ptr<IDiffLauncher> diff_launcher)
        : config_(config),
          formatter_(std::move(formatter)),
          visualizer_(std::move(visualizer)),
          comparator_(std::move(comparator)),
          diff_launcher_(std::move(diff_launcher)) {
        // Ensure required directories exist
        config_.EnsureDirectoriesExist();
    }

    ApprovalTestResult ApprovalTestOrchestrator::RunImageApprovalTest(
        const std::string& image_filename, const std::string& test_name,
        infrastructure::OpenCVImageAnalyzer& analyzer, const std::chrono::microseconds& timestamp) {
        ApprovalTestResult result(test_name);

        try {
            // Load the PiTrac test image
            cv::Mat test_image = LoadPiTracImage(image_filename);

            // Create image buffer for analysis
            domain::ImageBuffer image_buffer(test_image, timestamp, test_name);

            // Perform analysis
            auto analysis_result = analyzer.AnalyzeTeedBall(image_buffer);

            // Format the result
            std::string formatted_result = formatter_->FormatTeedBallResult(analysis_result);

            // Save received artifacts
            if (!SaveReceivedArtifacts(test_name, formatted_result, test_image, analysis_result)) {
                result.failure_message = "Failed to save received artifacts for " + test_name;
                return result;
            }

            // Generate file paths
            std::string approved_text_path = GenerateArtifactPath(
                test_name, config_.GetApprovedSuffix(), config_.GetTextFileExtension());
            std::string approved_image_path = GenerateArtifactPath(
                test_name, config_.GetApprovedSuffix(), config_.GetImageFileExtension());
            std::string received_text_path = GenerateArtifactPath(
                test_name, config_.GetReceivedSuffix(), config_.GetTextFileExtension());
            std::string received_image_path = GenerateArtifactPath(
                test_name, config_.GetReceivedSuffix(), config_.GetImageFileExtension());

            // Check what approved files exist and handle accordingly
            bool approved_text_exists = std::filesystem::exists(approved_text_path);
            bool approved_image_exists = std::filesystem::exists(approved_image_path);

            if (!approved_text_exists && !approved_image_exists) {
                // New test - no approved files exist
                return HandleNewTest(test_name, received_text_path, received_image_path);
            } else if (!approved_text_exists || !approved_image_exists) {
                // Partial baseline - some approved files missing
                return HandlePartialBaseline(test_name, approved_text_path, approved_image_path,
                                             received_text_path, received_image_path);
            } else {
                // Full baseline exists - compare artifacts
                return CompareArtifacts(test_name, approved_text_path, approved_image_path,
                                        received_text_path, received_image_path, formatted_result);
            }

        } catch (const std::exception& e) {
            result.failure_message = "Exception during approval test: " + std::string(e.what());
            return result;
        }
    }

    ApprovalTestResult ApprovalTestOrchestrator::RunMovementApprovalTest(
        const std::vector<domain::ImageBuffer>& image_sequence,
        const domain::BallPosition& reference_position, const std::string& test_name,
        infrastructure::OpenCVImageAnalyzer& analyzer) {
        ApprovalTestResult result(test_name);

        try {
            // Analyze movement
            auto movement_result = analyzer.DetectMovement(image_sequence, reference_position);

            // Format the result
            std::string formatted_result = formatter_->FormatMovementResult(movement_result);

            // Generate file paths
            std::string approved_text_path = GenerateArtifactPath(
                test_name, config_.GetApprovedSuffix(), config_.GetTextFileExtension());
            std::string received_text_path = GenerateArtifactPath(
                test_name, config_.GetReceivedSuffix(), config_.GetTextFileExtension());

            // Save received artifact
            if (!WriteTextFile(received_text_path, formatted_result)) {
                result.failure_message = "Failed to save received text artifact for " + test_name;
                return result;
            }

            // Check if approved file exists
            if (!std::filesystem::exists(approved_text_path)) {
                // Create empty approved file for new test
                if (!WriteTextFile(approved_text_path,
                                   "# Empty baseline - no approved content exists yet\n# Review "
                                   "the received content and approve if correct\n# Use "
                                   "approve_changes.ps1 to approve this test\n")) {
                    result.failure_message = "Failed to create baseline file for " + test_name;
                    return result;
                }

                // Launch diff for review
                DiffInfo diff_info(approved_text_path, received_text_path,
                                   test_name + "_new_baseline", true);
                diff_launcher_->LaunchTextDiff(diff_info);

                result.failure_message =
                    "New test detected - approved file created as empty baseline for " + test_name +
                    "\nReceived file: " + received_text_path +
                    "\nApproved file: " + approved_text_path +
                    "\nReview the received content and use approve_changes.ps1 to approve if "
                    "correct.";
                return result;
            } else {
                // Compare with existing approved content
                std::string approved_content = ReadTextFile(approved_text_path);
                auto comparison_result =
                    comparator_->CompareText(formatted_result, approved_content);

                result.text_matches = comparison_result.matches;
                result.images_match = true;  // No image comparison for movement tests

                if (comparison_result.matches) {
                    result.passed = true;
                    return result;
                } else {
                    // Launch diff for review
                    DiffInfo diff_info(approved_text_path, received_text_path, test_name + "_text");
                    diff_launcher_->LaunchTextDiff(diff_info);

                    result.failure_message =
                        "Movement approval test failed for " + test_name +
                        "\nText content differs between approved and received files.\n" +
                        "Received file: " + received_text_path + "\n" +
                        "Approved file: " + approved_text_path + "\n" +
                        "VS Code diff launched for review. Check the differences and approve if "
                        "intended.";
                    return result;
                }
            }

        } catch (const std::exception& e) {
            result.failure_message =
                "Exception during movement approval test: " + std::string(e.what());
            return result;
        }
    }

    cv::Mat ApprovalTestOrchestrator::LoadPiTracImage(const std::string& filename) const {
        std::string full_path = config_.GetPiTracImagesDir() + filename;
        cv::Mat image = cv::imread(full_path, cv::IMREAD_COLOR);

        if (image.empty()) {
            throw std::runtime_error("Failed to load PiTrac test image: " + full_path);
        }

        return image;
    }

    std::string ApprovalTestOrchestrator::GenerateArtifactPath(const std::string& test_name,
                                                               const std::string& suffix,
                                                               const std::string& extension) const {
        return config_.GetApprovalArtifactsDir() + test_name + suffix + extension;
    }

    bool ApprovalTestOrchestrator::SaveReceivedArtifacts(
        const std::string& test_name, const std::string& formatted_result,
        const cv::Mat& original_image, const domain::TeedBallResult& analysis_result) const {
        // Save text artifact
        std::string received_text_path = GenerateArtifactPath(
            test_name, config_.GetReceivedSuffix(), config_.GetTextFileExtension());
        if (!WriteTextFile(received_text_path, formatted_result)) {
            return false;
        }

        // Save visualization artifact
        std::string received_image_path = GenerateArtifactPath(
            test_name, config_.GetReceivedSuffix(), config_.GetImageFileExtension());
        return visualizer_->CreateVisualization(original_image, analysis_result,
                                                received_image_path);
    }

    ApprovalTestResult ApprovalTestOrchestrator::HandleNewTest(
        const std::string& test_name, const std::string& received_text_path,
        const std::string& received_image_path) const {
        ApprovalTestResult result(test_name);

        // Generate approved file paths
        std::string approved_text_path = GenerateArtifactPath(
            test_name, config_.GetApprovedSuffix(), config_.GetTextFileExtension());
        std::string approved_image_path = GenerateArtifactPath(
            test_name, config_.GetApprovedSuffix(), config_.GetImageFileExtension());

        // Create empty approved text file
        if (!WriteTextFile(
                approved_text_path,
                "# Empty baseline - no approved content exists yet\n# Review the received content "
                "and approve if correct\n# Use approve_changes.ps1 to approve this test\n")) {
            result.failure_message = "Failed to create baseline text file for " + test_name;
            return result;
        }

        // Create empty approved image with same dimensions as received
        cv::Mat received_img = cv::imread(received_image_path, cv::IMREAD_COLOR);
        int width = received_img.empty() ? config_.GetDefaultImageWidth() : received_img.cols;
        int height = received_img.empty() ? config_.GetDefaultImageHeight() : received_img.rows;

        if (!visualizer_->CreateEmptyBaseline(width, height, approved_image_path)) {
            result.failure_message = "Failed to create baseline image file for " + test_name;
            return result;
        }

        // Launch diff for review
        DiffInfo diff_info(approved_text_path, received_text_path, test_name + "_new_baseline",
                           true);
        diff_launcher_->LaunchTextDiff(diff_info);

        result.failure_message =
            "New test detected - approved files created as empty baselines for " + test_name +
            "\nReceived file: " + received_text_path + "\nApproved file: " + approved_text_path +
            "\nReview the received content and use approve_changes.ps1 to approve if correct.";
        return result;
    }

    ApprovalTestResult ApprovalTestOrchestrator::HandlePartialBaseline(
        const std::string& test_name, const std::string& approved_text_path,
        const std::string& approved_image_path, const std::string& received_text_path,
        const std::string& received_image_path) const {
        ApprovalTestResult result(test_name);

        bool approved_text_exists = std::filesystem::exists(approved_text_path);
        bool approved_image_exists = std::filesystem::exists(approved_image_path);

        if (approved_text_exists && !approved_image_exists) {
            // Text baseline exists but image baseline missing
            cv::Mat received_img = cv::imread(received_image_path, cv::IMREAD_COLOR);
            int width = received_img.empty() ? config_.GetDefaultImageWidth() : received_img.cols;
            int height = received_img.empty() ? config_.GetDefaultImageHeight() : received_img.rows;

            if (!visualizer_->CreateEmptyBaseline(width, height, approved_image_path)) {
                result.failure_message = "Failed to create missing baseline image for " + test_name;
                return result;
            }

            // Launch image diff
            DiffInfo img_diff_info(approved_image_path, received_image_path,
                                   test_name + "_missing_image");
            diff_launcher_->LaunchImageDiff(img_diff_info);

            result.failure_message =
                "Missing approved visualization file created as empty baseline: " +
                approved_image_path + "\nText baseline exists but image baseline was missing." +
                "\nVS Code opened to compare empty approved vs received image." +
                "\nTo approve: copy \"" + received_image_path + "\" \"" + approved_image_path +
                "\"";
            return result;
        }

        if (!approved_text_exists && approved_image_exists) {
            // Image baseline exists but text baseline missing
            if (!WriteTextFile(approved_text_path,
                               "# Empty baseline - no text analysis result exists yet\n# Review "
                               "the received content and approve if correct\n")) {
                result.failure_message = "Failed to create missing baseline text for " + test_name;
                return result;
            }

            // Launch text diff
            DiffInfo text_diff_info(approved_text_path, received_text_path,
                                    test_name + "_missing_text");
            diff_launcher_->LaunchTextDiff(text_diff_info);

            result.failure_message =
                "Missing approved text file created as empty baseline: " + approved_text_path +
                "\nImage baseline exists but text baseline was missing." +
                "\nVS Code diff launched to review the received content.";
            return result;
        }

        result.failure_message = "Unexpected partial baseline state for " + test_name;
        return result;
    }

    ApprovalTestResult ApprovalTestOrchestrator::CompareArtifacts(
        const std::string& test_name, const std::string& approved_text_path,
        const std::string& approved_image_path, const std::string& received_text_path,
        const std::string& received_image_path, const std::string& formatted_result) const {
        ApprovalTestResult result(test_name);

        try {
            // Compare text content
            std::string approved_content = ReadTextFile(approved_text_path);
            auto text_comparison = comparator_->CompareText(formatted_result, approved_content);
            result.text_matches = text_comparison.matches;

            // Compare image content
            auto image_comparison =
                comparator_->CompareImages(received_image_path, approved_image_path);
            result.images_match = image_comparison.matches;

            if (result.text_matches && result.images_match) {
                result.passed = true;
                return result;
            }

            // Handle failures by launching appropriate diff tools
            std::string failure_msg = "Approval test failed for " + test_name + "\n";

            if (!result.text_matches) {
                DiffInfo text_diff_info(approved_text_path, received_text_path,
                                        test_name + "_text");
                diff_launcher_->LaunchTextDiff(text_diff_info);
                failure_msg += "Text content differs between approved and received files.\n";
            }

            if (!result.images_match) {
                DiffInfo image_diff_info(approved_image_path, received_image_path,
                                         test_name + "_image");
                diff_launcher_->LaunchImageDiff(image_diff_info);
                failure_msg += "Image content differs between approved and received files.\n";
            }

            failure_msg += "Received file: " + received_text_path + "\n";
            failure_msg += "Approved file: " + approved_text_path + "\n";
            failure_msg +=
                "VS Code diff launched for review. Check the differences and approve if intended.";

            result.failure_message = failure_msg;
            return result;

        } catch (const std::exception& e) {
            result.failure_message =
                "Exception during artifact comparison: " + std::string(e.what());
            return result;
        }
    }

    std::string ApprovalTestOrchestrator::ReadTextFile(const std::string& file_path) const {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for reading: " + file_path);
        }

        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    }

    bool ApprovalTestOrchestrator::WriteTextFile(const std::string& file_path,
                                                 const std::string& content) const {
        try {
            std::ofstream file(file_path);
            if (!file.is_open()) {
                return false;
            }
            file << content;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    // ApprovalTestOrchestratorFactory Implementation
    std::unique_ptr<ApprovalTestOrchestrator> ApprovalTestOrchestratorFactory::CreateStandard() {
        const auto& config = ApprovalTestConfig::Instance();

        auto formatter = ResultFormatterFactory::Create(
            ResultFormatterFactory::FormatterType::STANDARD_APPROVAL);
        auto visualizer = VisualizationServiceFactory::Create(
            VisualizationServiceFactory::ServiceType::OPENCV_STANDARD, config);
        auto comparator =
            ComparisonServiceFactory::Create(ComparisonServiceFactory::ServiceType::STANDARD);
        auto diff_launcher =
            DiffLauncherFactory::Create(DiffLauncherFactory::LauncherType::AUTO_DETECT, config);

        return std::make_unique<ApprovalTestOrchestrator>(
            config, std::move(formatter), std::move(visualizer), std::move(comparator),
            std::move(diff_launcher));
    }

    std::unique_ptr<ApprovalTestOrchestrator>
    ApprovalTestOrchestratorFactory::CreateWithFuzzyComparison(double image_tolerance) {
        const auto& config = ApprovalTestConfig::Instance();

        auto formatter = ResultFormatterFactory::Create(
            ResultFormatterFactory::FormatterType::STANDARD_APPROVAL);
        auto visualizer = VisualizationServiceFactory::Create(
            VisualizationServiceFactory::ServiceType::OPENCV_STANDARD, config);
        auto comparator = ComparisonServiceFactory::Create(
            ComparisonServiceFactory::ServiceType::FUZZY, image_tolerance);
        auto diff_launcher =
            DiffLauncherFactory::Create(DiffLauncherFactory::LauncherType::AUTO_DETECT, config);

        return std::make_unique<ApprovalTestOrchestrator>(
            config, std::move(formatter), std::move(visualizer), std::move(comparator),
            std::move(diff_launcher));
    }

    std::unique_ptr<ApprovalTestOrchestrator> ApprovalTestOrchestratorFactory::CreateCompact() {
        const auto& config = ApprovalTestConfig::Instance();

        auto formatter =
            ResultFormatterFactory::Create(ResultFormatterFactory::FormatterType::COMPACT);
        auto visualizer = VisualizationServiceFactory::Create(
            VisualizationServiceFactory::ServiceType::OPENCV_STANDARD, config);
        auto comparator =
            ComparisonServiceFactory::Create(ComparisonServiceFactory::ServiceType::STANDARD);
        auto diff_launcher =
            DiffLauncherFactory::Create(DiffLauncherFactory::LauncherType::AUTO_DETECT, config);

        return std::make_unique<ApprovalTestOrchestrator>(
            config, std::move(formatter), std::move(visualizer), std::move(comparator),
            std::move(diff_launcher));
    }

}  // namespace golf_sim::image_analysis::testing

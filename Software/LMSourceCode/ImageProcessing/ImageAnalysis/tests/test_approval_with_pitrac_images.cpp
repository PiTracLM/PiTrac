/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

/**
 * @file test_approval_with_pitrac_images.cpp
 * @brief Approval tests using clean architecture and SOLID principles
 *
 * This test file implements approval testing using a clean architecture framework:
 * - Single Responsibility Principle: Each class has one reason to change
 * - Open/Closed Principle: New formatters and services can be added without modification
 * - Liskov Substitution Principle: All service implementations are interchangeable
 * - Interface Segregation Principle: Focused interfaces for specific concerns
 * - Dependency Inversion Principle: Depends on abstractions, not concretions
 *
 * The framework consists of:
 * - Configuration management (ApprovalTestConfig)
 * - Result formatting strategies (IResultFormatter implementations)
 * - Visualization services (IVisualizationService implementations)
 * - Comparison services (IComparisonService implementations)
 * - Diff launching services (IDiffLauncher implementations)
 * - Orchestration facade (ApprovalTestOrchestrator)
 */

#define BOOST_TEST_MODULE ApprovalTestsWithPiTracImagesRefactored
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <opencv2/opencv.hpp>
#include "../application/image_analysis_service.hpp"
#include "../infrastructure/opencv_image_analyzer.hpp"
#include "approval/approval_test_orchestrator.hpp"

using namespace golf_sim::image_analysis;
using namespace golf_sim::image_analysis::testing;

BOOST_AUTO_TEST_SUITE(ApprovalTestsWithPiTracImagesRefactored)

/**
 * @brief Clean test fixture following dependency injection principles
 *
 * Demonstrates proper separation of concerns and dependency management.
 * No longer a god object - focused only on test setup and orchestration.
 */
struct CleanApprovalTestFixture {
    CleanApprovalTestFixture() {
        // Initialize dependencies using dependency injection
        analyzer = std::make_unique<infrastructure::OpenCVImageAnalyzer>();

        // Create orchestrator using factory (dependency injection container pattern)
        orchestrator = ApprovalTestOrchestratorFactory::CreateStandard();

        // Generate consistent timestamp for reproducible tests
        test_timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    }

    /**
     * @brief Run a single image approval test
     * @param image_filename Name of image in PiTrac images directory
     * @param test_name Unique test identifier
     */
    void RunSingleImageTest(const std::string& image_filename, const std::string& test_name) {
        auto result = orchestrator->RunImageApprovalTest(image_filename, test_name, *analyzer,
                                                         test_timestamp);

        if (!result.passed) {
            BOOST_FAIL(result.failure_message);
        } else {
            BOOST_TEST_MESSAGE("Approval test passed for " + test_name);
        }
    }

    /**
     * @brief Run a movement analysis approval test
     * @param image_filenames Vector of image filenames for sequence analysis
     * @param test_name Unique test identifier
     */
    void RunMovementAnalysisTest(const std::vector<std::string>& image_filenames,
                                 const std::string& test_name) {
        if (image_filenames.size() < 2) {
            BOOST_FAIL("Movement analysis requires at least 2 images");
            return;
        }

        // Load images and create sequence
        std::vector<domain::ImageBuffer> sequence;
        const auto& config = ApprovalTestConfig::Instance();

        for (size_t i = 0; i < image_filenames.size(); ++i) {
            std::string full_path = config.GetPiTracImagesDir() + image_filenames[i];
            cv::Mat image = cv::imread(full_path, cv::IMREAD_COLOR);

            if (image.empty()) {
                BOOST_FAIL("Failed to load image: " + full_path);
                return;
            }

            auto timestamp_offset =
                test_timestamp + std::chrono::microseconds(i * 33333);  // ~30fps spacing
            sequence.emplace_back(image, timestamp_offset, test_name + "_" + std::to_string(i));
        }

        // Get reference position from first image
        auto first_result = analyzer->AnalyzeTeedBall(sequence[0]);
        domain::BallPosition reference_position;

        if (first_result.position.has_value()) {
            reference_position = first_result.position.value();
        } else {
            // Create default reference position if no ball detected
            reference_position = domain::BallPosition(320, 240, 20, 0.5, test_timestamp, "default");
        }

        // Run movement approval test
        auto result = orchestrator->RunMovementApprovalTest(sequence, reference_position, test_name,
                                                            *analyzer);

        if (!result.passed) {
            BOOST_FAIL(result.failure_message);
        } else {
            BOOST_TEST_MESSAGE("Movement approval test passed for " + test_name);
        }
    }

   protected:
    std::unique_ptr<infrastructure::OpenCVImageAnalyzer> analyzer;
    std::unique_ptr<ApprovalTestOrchestrator> orchestrator;
    std::chrono::microseconds test_timestamp;
};

// Test cases using clean architecture - much cleaner and more maintainable
BOOST_FIXTURE_TEST_CASE(test_log_ball_final_found_ball_img_clean, CleanApprovalTestFixture) {
    RunSingleImageTest("log_ball_final_found_ball_img.png", "log_ball_final_found_ball_img");
}

BOOST_FIXTURE_TEST_CASE(test_gs_log_img_log_ball_final_found_ball_img_clean,
                        CleanApprovalTestFixture) {
    RunSingleImageTest("gs_log_img__log_ball_final_found_ball_img.png",
                       "gs_log_img_log_ball_final_found_ball_img");
}

BOOST_FIXTURE_TEST_CASE(test_log_cam2_last_strobed_img_clean, CleanApprovalTestFixture) {
    RunSingleImageTest("log_cam2_last_strobed_img.png", "log_cam2_last_strobed_img");
}

BOOST_FIXTURE_TEST_CASE(test_log_cam2_last_strobed_img_232_fast_clean, CleanApprovalTestFixture) {
    RunSingleImageTest("log_cam2_last_strobed_img_232_fast.png",
                       "log_cam2_last_strobed_img_232_fast");
}

BOOST_FIXTURE_TEST_CASE(test_spin_ball_1_gray_image1_clean, CleanApprovalTestFixture) {
    RunSingleImageTest("spin_ball_1_gray_image1.png", "spin_ball_1_gray_image1");
}

BOOST_FIXTURE_TEST_CASE(test_spin_ball_2_gray_image1_clean, CleanApprovalTestFixture) {
    RunSingleImageTest("spin_ball_2_gray_image1.png", "spin_ball_2_gray_image1");
}

BOOST_FIXTURE_TEST_CASE(test_log_ball_final_found_ball_img_232_fast_clean,
                        CleanApprovalTestFixture) {
    RunSingleImageTest("log_ball_final_found_ball_img_232_fast.png",
                       "log_ball_final_found_ball_img_232_fast");
}

// Movement analysis test using clean architecture
BOOST_FIXTURE_TEST_CASE(test_movement_analysis_with_strobed_images_clean,
                        CleanApprovalTestFixture) {
    std::vector<std::string> strobed_sequence = {"log_cam2_last_strobed_img.png",
                                                 "log_cam2_last_strobed_img_232_fast.png"};

    RunMovementAnalysisTest(strobed_sequence, "movement_analysis_strobed_sequence");
}

// Demonstration of framework extensibility - fuzzy comparison strategy
BOOST_FIXTURE_TEST_CASE(test_with_fuzzy_comparison_strategy, CleanApprovalTestFixture) {
    // Create orchestrator with fuzzy comparison for tolerance testing
    auto fuzzy_orchestrator =
        ApprovalTestOrchestratorFactory::CreateWithFuzzyComparison(0.02);  // 2% tolerance

    auto result = fuzzy_orchestrator->RunImageApprovalTest(
        "log_ball_final_found_ball_img.png", "fuzzy_comparison_test", *analyzer, test_timestamp);

    if (!result.passed) {
        BOOST_FAIL(result.failure_message);
    } else {
        BOOST_TEST_MESSAGE("Fuzzy comparison strategy test passed");
    }
}

// Demonstration of framework extensibility - compact formatting strategy
BOOST_FIXTURE_TEST_CASE(test_with_compact_formatting_strategy, CleanApprovalTestFixture) {
    // Create orchestrator with compact formatting for minimal output
    auto compact_orchestrator = ApprovalTestOrchestratorFactory::CreateCompact();

    auto result = compact_orchestrator->RunImageApprovalTest(
        "spin_ball_1_gray_image1.png", "compact_format_test", *analyzer, test_timestamp);

    if (!result.passed) {
        BOOST_FAIL(result.failure_message);
    } else {
        BOOST_TEST_MESSAGE("Compact formatting strategy test passed");
    }
}

BOOST_AUTO_TEST_SUITE_END()

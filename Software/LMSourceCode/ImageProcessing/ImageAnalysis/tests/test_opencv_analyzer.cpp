/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

/**
 * @file test_opencv_analyzer.cpp
 * @brief Integration tests for OpenCV-based image analyzer
 *
 * Tests the OpenCV implementation against the domain interface,
 * verifying that the adapter correctly wraps existing functionality.
 * Using Boost Test Framework consistent with Camera bounded context.
 */

#define BOOST_TEST_MODULE OpenCVAnalyzerTests
#include <boost/test/unit_test.hpp>
#include <opencv2/opencv.hpp>
#include "../infrastructure/opencv_image_analyzer.hpp"

using namespace golf_sim::image_analysis;

BOOST_AUTO_TEST_SUITE(OpenCVAnalyzerTests)

struct OpenCVAnalyzerFixture {
    OpenCVAnalyzerFixture() {
        analyzer = std::make_unique<infrastructure::OpenCVImageAnalyzer>();

        // Create test images
        test_image_with_ball = CreateTestImageWithBall();
        test_image_without_ball = CreateTestImageWithoutBall();

        test_timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    }

    cv::Mat CreateTestImageWithBall() {
        cv::Mat image = cv::Mat::zeros(480, 640, CV_8UC3);

        // Draw a white circle to simulate a golf ball
        cv::Point center(320, 240);  // Center of image
        int radius = 20;
        cv::Scalar color(255, 255, 255);               // White
        cv::circle(image, center, radius, color, -1);  // Filled circle

        // Add some noise to make it more realistic
        cv::Mat noise(480, 640, CV_8UC3);
        cv::randn(noise, cv::Scalar(0, 0, 0), cv::Scalar(10, 10, 10));
        image += noise;

        return image;
    }

    cv::Mat CreateTestImageWithoutBall() {
        cv::Mat image = cv::Mat::zeros(480, 640, CV_8UC3);

        // Add some background texture but no ball
        cv::Mat noise(480, 640, CV_8UC3);
        cv::randn(noise, cv::Scalar(128, 128, 128), cv::Scalar(20, 20, 20));
        image += noise;

        return image;
    }

    std::vector<domain::ImageBuffer> CreateMovementSequence() {
        std::vector<domain::ImageBuffer> sequence;

        // Create 5 images with ball moving across the frame
        for (int i = 0; i < 5; ++i) {
            cv::Mat image = cv::Mat::zeros(480, 640, CV_8UC3);

            // Ball moves from left to right
            cv::Point center(100 + i * 50, 240);
            cv::circle(image, center, 20, cv::Scalar(255, 255, 255), -1);

            sequence.emplace_back(image,
                                  test_timestamp + std::chrono::microseconds(i * 33333),  // ~30fps
                                  "movement_frame_" + std::to_string(i));
        }

        return sequence;
    }

    cv::Mat CreateStrobedImage() {
        cv::Mat image = cv::Mat::zeros(480, 640, CV_8UC3);

        // Create multiple ball positions to simulate strobed capture
        std::vector<cv::Point> positions = {cv::Point{100, 240}, cv::Point{150, 230},
                                            cv::Point{200, 220}, cv::Point{250, 210}};

        for (const auto& pos : positions) {
            cv::circle(image, pos, 15, cv::Scalar(255, 255, 255), -1);
        }

        return image;
    }

    std::unique_ptr<infrastructure::OpenCVImageAnalyzer> analyzer;
    cv::Mat test_image_with_ball;
    cv::Mat test_image_without_ball;
    std::chrono::microseconds test_timestamp;
};

// Test basic analyzer properties
BOOST_FIXTURE_TEST_CASE(AnalyzerProperties, OpenCVAnalyzerFixture) {
    BOOST_CHECK_EQUAL(analyzer->GetAnalyzerName(), "OpenCV Image Analyzer");
    BOOST_CHECK(!analyzer->GetVersion().empty());
    BOOST_CHECK(analyzer->SupportsRealTime());
}

// Test teed ball detection
BOOST_FIXTURE_TEST_CASE(DetectsTeedBall, OpenCVAnalyzerFixture) {
    domain::ImageBuffer image_buffer{test_image_with_ball, test_timestamp, "test_with_ball"};

    auto result = analyzer->AnalyzeTeedBall(image_buffer);

    BOOST_CHECK_EQUAL(result.state, domain::BallState::TEED);
    BOOST_CHECK(result.position.has_value());
    BOOST_CHECK_GT(result.confidence, 0.5);  // Should have decent confidence
    BOOST_CHECK(!result.analysis_method.empty());

    if (result.position) {
        // Ball should be detected near center of image
        BOOST_CHECK_CLOSE(result.position->x_pixels, 320, 15.0);      // 15% tolerance
        BOOST_CHECK_CLOSE(result.position->y_pixels, 240, 20.0);      // 20% tolerance
        BOOST_CHECK_CLOSE(result.position->radius_pixels, 20, 50.0);  // 50% tolerance
    }
}

BOOST_FIXTURE_TEST_CASE(DetectsNoBall, OpenCVAnalyzerFixture) {
    domain::ImageBuffer image_buffer{test_image_without_ball, test_timestamp, "test_without_ball"};

    auto result = analyzer->AnalyzeTeedBall(image_buffer);

    BOOST_CHECK_EQUAL(result.state, domain::BallState::ABSENT);
    BOOST_CHECK(!result.position.has_value());
    BOOST_CHECK_LT(result.confidence, 0.5);  // Low confidence when no ball
}

// Test movement detection
BOOST_FIXTURE_TEST_CASE(DetectsMovement, OpenCVAnalyzerFixture) {
    auto movement_sequence = CreateMovementSequence();

    domain::BallPosition reference_position{100, 240, 20, 0.9, test_timestamp, "reference"};

    auto result = analyzer->DetectMovement(movement_sequence, reference_position);

    BOOST_CHECK(result.movement_detected);
    BOOST_CHECK_GT(result.movement_confidence, 0.5);
    BOOST_CHECK(result.last_known_position.has_value());
    BOOST_CHECK(!result.analysis_method.empty());
}

// Performance test
BOOST_FIXTURE_TEST_CASE(PerformanceTeedBallAnalysis, OpenCVAnalyzerFixture) {
    domain::ImageBuffer image_buffer{test_image_with_ball, test_timestamp, "performance_test"};

    const int num_iterations = 50;  // Reduced for faster testing
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_iterations; ++i) {
        auto result = analyzer->AnalyzeTeedBall(image_buffer);
        // Ensure result is used to prevent optimization
        volatile bool has_ball = result.position.has_value();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    // Should process 50 frames in reasonable time
#ifdef _DEBUG
    BOOST_CHECK_LT(duration.count(), 5000);  // 5 seconds for debug builds
#else
    BOOST_CHECK_LT(duration.count(), 2500);  // 2.5 seconds for release builds
#endif

    double avg_time_ms = static_cast<double>(duration.count()) / num_iterations;
    BOOST_TEST_MESSAGE("Average processing time: " << avg_time_ms << " ms per frame");

// Should be fast enough for real-time
#ifdef _DEBUG
    BOOST_CHECK_LT(avg_time_ms, 100.0);  // 100ms per frame for debug builds
#else
    BOOST_CHECK_LT(avg_time_ms, 50.0);  // 50ms per frame for release builds
#endif
}

// Error handling tests
BOOST_FIXTURE_TEST_CASE(HandlesEmptyImage, OpenCVAnalyzerFixture) {
    cv::Mat empty_image;

    // After domain improvements, ImageBuffer constructor validates input
    // Empty images should throw an exception during construction
    BOOST_CHECK_THROW(domain::ImageBuffer(empty_image, test_timestamp, "empty_image"),
                      std::invalid_argument);

    // Test with a minimal valid image instead to test analyzer error handling
    cv::Mat tiny_image = cv::Mat::zeros(1, 1, CV_8UC3);
    domain::ImageBuffer tiny_buffer{tiny_image, test_timestamp, "tiny_image"};

    auto result = analyzer->AnalyzeTeedBall(tiny_buffer);

    BOOST_CHECK_EQUAL(result.state, domain::BallState::ABSENT);
    BOOST_CHECK(!result.position.has_value());
    BOOST_CHECK_LT(result.confidence, 0.1);
}

BOOST_AUTO_TEST_SUITE_END()

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

/**
 * @file test_image_analysis_domain.cpp
 * @brief Unit tests for domain layer of image analysis bounded context
 *
 * Tests the business logic and domain objects without dependencies
 * on external frameworks or infrastructure. Using Boost Test Framework
 * consistent with Camera bounded context.
 */

#define BOOST_TEST_MODULE ImageAnalysisDomainTests
#include <boost/test/unit_test.hpp>
#include <chrono>
#include "../domain/analysis_results.hpp"
#include "../domain/value_objects.hpp"

using namespace golf_sim::image_analysis::domain;

BOOST_AUTO_TEST_SUITE(ImageAnalysisDomainTests)

// Test fixture for common setup
struct DomainTestFixture {
    DomainTestFixture() {
        test_timestamp = std::chrono::microseconds{1000000};  // 1 second

        valid_position = BallPosition{100.0, 200.0, 15.0, 0.9, test_timestamp, "test_detection"};

        invalid_position = BallPosition{0.0, 0.0, 0.0, 0.0, test_timestamp, "test_detection"};
    }

    std::chrono::microseconds test_timestamp;
    BallPosition valid_position;
    BallPosition invalid_position;
};

// Test BallPosition value object
BOOST_FIXTURE_TEST_CASE(BallPositionValidation, DomainTestFixture) {
    BOOST_CHECK(valid_position.IsValid());
    BOOST_CHECK(!invalid_position.IsValid());
}

BOOST_FIXTURE_TEST_CASE(BallPositionDistanceCalculation, DomainTestFixture) {
    BallPosition pos1{100.0, 200.0, 15.0, 0.9};
    BallPosition pos2{103.0, 204.0, 15.0, 0.8};

    double distance = pos1.DistanceFrom(pos2);
    BOOST_CHECK_CLOSE(distance, 5.0, 2.0);  // sqrt(3^2 + 4^2) = 5.0, 2% tolerance
}

BOOST_FIXTURE_TEST_CASE(BallPositionEquality, DomainTestFixture) {
    BallPosition pos1{100.0, 200.0, 15.0, 0.9};
    BallPosition pos2{100.0, 200.0, 15.0, 0.9};
    BallPosition pos3{101.0, 200.0, 15.0, 0.9};

    BOOST_CHECK(pos1.IsNearlyEqual(pos2, 1.0));
    BOOST_CHECK(!pos1.IsNearlyEqual(pos3, 0.5));
    BOOST_CHECK(pos1.IsNearlyEqual(pos3, 2.0));
}

// Test TeedBallResult
BOOST_FIXTURE_TEST_CASE(TeedBallResultConstruction, DomainTestFixture) {
    TeedBallResult result{BallState::TEED, valid_position, 0.95, "test_method"};

    BOOST_CHECK_EQUAL(result.state, BallState::TEED);
    BOOST_CHECK(result.position.has_value());
    BOOST_CHECK_EQUAL(result.confidence, 0.95);
    BOOST_CHECK_EQUAL(result.analysis_method, "test_method");
}

BOOST_FIXTURE_TEST_CASE(TeedBallResultAbsentState, DomainTestFixture) {
    TeedBallResult result{BallState::ABSENT, std::nullopt, 0.1, "test_method"};

    BOOST_CHECK_EQUAL(result.state, BallState::ABSENT);
    BOOST_CHECK(!result.position.has_value());
}

// Test MovementResult
BOOST_FIXTURE_TEST_CASE(MovementResultDetection, DomainTestFixture) {
    std::vector<cv::Point2f> motion_vectors = {cv::Point2f{1.0f, 2.0f}, cv::Point2f{2.0f, 3.0f}};

    MovementResult result;
    result.movement_detected = true;
    result.last_known_position = valid_position;
    result.movement_confidence = 0.85;
    result.movement_magnitude = 5.0;
    result.motion_vectors = motion_vectors;
    result.analysis_method = "optical_flow";

    BOOST_CHECK(result.movement_detected);
    BOOST_CHECK(result.last_known_position.has_value());
    BOOST_CHECK_EQUAL(result.motion_vectors.size(), 2);
    BOOST_CHECK_EQUAL(result.analysis_method, "optical_flow");
}

// Test FlightAnalysisResult
BOOST_FIXTURE_TEST_CASE(FlightAnalysisMultipleBalls, DomainTestFixture) {
    std::vector<BallPosition> ball_positions = {
        BallPosition{100, 200, 15, 0.9, test_timestamp, "strobe_1"},
        BallPosition{110, 190, 14, 0.8, test_timestamp, "strobe_2"},
        BallPosition{120, 180, 13, 0.7, test_timestamp, "strobe_3"}};

    cv::Vec3d spin_rates{100.0, 200.0, 50.0};  // degrees/second
    cv::Vec3d velocity{25.0, 15.0, 30.0};      // m/s

    FlightAnalysisResult result;
    result.detected_balls = ball_positions;
    result.spin_rates = spin_rates;
    result.velocity_vector = velocity;
    result.temporal_spacing_us = 5000.0;  // 5ms between strobes
    result.confidence = 0.9;
    result.analysis_method = "strobed_analysis";

    BOOST_CHECK_EQUAL(result.detected_balls.size(), 3);
    BOOST_CHECK(result.spin_rates.has_value());
    BOOST_CHECK(result.velocity_vector.has_value());
    BOOST_CHECK_EQUAL(result.temporal_spacing_us, 5000.0);
}

// Test ImageBuffer
BOOST_FIXTURE_TEST_CASE(ImageBufferConstruction, DomainTestFixture) {
    cv::Mat test_image = cv::Mat::zeros(480, 640, CV_8UC3);

    ImageBuffer buffer{test_image, test_timestamp, "camera_1", "test_metadata"};

    BOOST_CHECK(!buffer.data.empty());
    BOOST_CHECK_EQUAL(buffer.timestamp.count(), test_timestamp.count());
    BOOST_CHECK_EQUAL(buffer.camera_id, "camera_1");
    BOOST_CHECK_EQUAL(buffer.metadata, "test_metadata");
    BOOST_CHECK(buffer.IsValid());
}

BOOST_FIXTURE_TEST_CASE(ImageBufferValidation, DomainTestFixture) {
    cv::Mat empty_image;

    // After domain improvements, ImageBuffer constructor validates input
    // Empty images should throw an exception during construction
    BOOST_CHECK_THROW(ImageBuffer(empty_image, test_timestamp, "camera_1", "invalid"),
                      std::invalid_argument);
}

// Test BallState enum
BOOST_AUTO_TEST_CASE(BallStateValues) {
    BOOST_CHECK_EQUAL(static_cast<int>(BallState::ABSENT), 0);
    BOOST_CHECK_NE(BallState::TEED, BallState::MOVING);
    BOOST_CHECK_NE(BallState::MOVING, BallState::RESET);
}

// Performance tests
BOOST_FIXTURE_TEST_CASE(BallPositionPerformance, DomainTestFixture) {
    const int num_iterations = 10000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_iterations; ++i) {
        BallPosition pos{static_cast<double>(i), static_cast<double>(i * 2), 15.0, 0.9};

        volatile bool is_valid = pos.IsValid();  // Prevent optimization
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Should be reasonably fast - less than 100ms for 10k operations in debug mode
    BOOST_CHECK_LT(duration.count(), 100000);
}

BOOST_AUTO_TEST_SUITE_END()

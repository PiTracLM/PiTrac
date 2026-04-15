/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

/**
 * @file test_domain_validation.cpp
 * @brief Comprehensive validation tests for domain layer input validation
 *
 * Tests the new input validation and invariant enforcement added to
 * domain value objects. Ensures migration-safe behavior and proper
 * error handling for invalid inputs.
 */

#define BOOST_TEST_MODULE DomainValidationTests
#include <boost/test/unit_test.hpp>
#include <limits>
#include <opencv2/opencv.hpp>
#include "../domain/analysis_results.hpp"
#include "../domain/value_objects.hpp"

using namespace golf_sim::image_analysis::domain;

BOOST_AUTO_TEST_SUITE(DomainValidationTests)

// BallPosition Validation Tests
BOOST_AUTO_TEST_CASE(BallPositionValidConfidenceRange) {
    // Valid confidence values should not throw
    BOOST_CHECK_NO_THROW(BallPosition(100.0, 200.0, 15.0, 0.0));
    BOOST_CHECK_NO_THROW(BallPosition(100.0, 200.0, 15.0, 0.5));
    BOOST_CHECK_NO_THROW(BallPosition(100.0, 200.0, 15.0, 1.0));
}

BOOST_AUTO_TEST_CASE(BallPositionInvalidConfidenceThrowsException) {
    // Confidence < 0.0 should throw
    BOOST_CHECK_THROW(BallPosition(100.0, 200.0, 15.0, -0.1), std::invalid_argument);

    // Confidence > 1.0 should throw
    BOOST_CHECK_THROW(BallPosition(100.0, 200.0, 15.0, 1.1), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(BallPositionInvalidRadiusThrowsException) {
    // Negative radius should throw
    BOOST_CHECK_THROW(BallPosition(100.0, 200.0, -1.0, 0.5), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(BallPositionNaNValuesThrowException) {
    double nan_val = std::numeric_limits<double>::quiet_NaN();

    BOOST_CHECK_THROW(BallPosition(nan_val, 200.0, 15.0, 0.5), std::invalid_argument);

    BOOST_CHECK_THROW(BallPosition(100.0, nan_val, 15.0, 0.5), std::invalid_argument);

    BOOST_CHECK_THROW(BallPosition(100.0, 200.0, nan_val, 0.5), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(BallPositionInfiniteValuesThrowException) {
    double inf_val = std::numeric_limits<double>::infinity();

    BOOST_CHECK_THROW(BallPosition(inf_val, 200.0, 15.0, 0.5), std::invalid_argument);

    BOOST_CHECK_THROW(BallPosition(100.0, inf_val, 15.0, 0.5), std::invalid_argument);

    BOOST_CHECK_THROW(BallPosition(100.0, 200.0, inf_val, 0.5), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(BallPositionIsNearlyEqualValidationArgumentValidation) {
    BallPosition pos1(100.0, 200.0, 15.0, 0.9);
    BallPosition pos2(105.0, 205.0, 15.0, 0.8);

    // Valid tolerance should not throw
    [[maybe_unused]] bool result1 = false;
    [[maybe_unused]] bool result2 = false;
    BOOST_CHECK_NO_THROW(result1 = pos1.IsNearlyEqual(pos2, 10.0));
    BOOST_CHECK_NO_THROW(result2 = pos1.IsNearlyEqual(pos2, 0.0));
    // Negative tolerance should throw
    BOOST_CHECK_THROW(
        { [[maybe_unused]] auto result = pos1.IsNearlyEqual(pos2, -1.0); }, std::invalid_argument);
}

// ImageBuffer Validation Tests
BOOST_AUTO_TEST_CASE(ImageBufferValidImageDoesNotThrow) {
    cv::Mat valid_image = cv::Mat::ones(100, 100, CV_8UC3);
    BOOST_CHECK_NO_THROW([[maybe_unused]] ImageBuffer buffer(valid_image));
}

BOOST_AUTO_TEST_CASE(ImageBufferEmptyImageThrowsException) {
    cv::Mat empty_image;

    // Test that empty image throws exception
    BOOST_CHECK_THROW({ [[maybe_unused]] ImageBuffer buffer(empty_image); }, std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(ImageBufferZeroDimensionImageThrowsException) {
    cv::Mat zero_width_image = cv::Mat::ones(100, 0, CV_8UC3);
    cv::Mat zero_height_image = cv::Mat::ones(0, 100, CV_8UC3);

    // Test zero-width image
    BOOST_CHECK_THROW(
        { [[maybe_unused]] ImageBuffer buffer1(zero_width_image); }, std::invalid_argument);

    // Test zero-height image
    BOOST_CHECK_THROW(
        { [[maybe_unused]] ImageBuffer buffer2(zero_height_image); }, std::invalid_argument);
}

// GetConfidenceLevel Validation Tests
BOOST_AUTO_TEST_CASE(GetConfidenceLevelValidRange) {
    [[maybe_unused]] ConfidenceLevel level1, level2, level3;
    BOOST_CHECK_NO_THROW(level1 = GetConfidenceLevel(0.0));
    BOOST_CHECK_NO_THROW(level2 = GetConfidenceLevel(0.5));
    BOOST_CHECK_NO_THROW(level3 = GetConfidenceLevel(1.0));

    // Test boundary conditions
    BOOST_CHECK_EQUAL(GetConfidenceLevel(0.0), ConfidenceLevel::VERY_LOW);
    BOOST_CHECK_EQUAL(GetConfidenceLevel(0.29), ConfidenceLevel::VERY_LOW);
    BOOST_CHECK_EQUAL(GetConfidenceLevel(0.3), ConfidenceLevel::LOW);
    BOOST_CHECK_EQUAL(GetConfidenceLevel(0.49), ConfidenceLevel::LOW);
    BOOST_CHECK_EQUAL(GetConfidenceLevel(0.5), ConfidenceLevel::MEDIUM);
    BOOST_CHECK_EQUAL(GetConfidenceLevel(0.69), ConfidenceLevel::MEDIUM);
    BOOST_CHECK_EQUAL(GetConfidenceLevel(0.7), ConfidenceLevel::HIGH);
    BOOST_CHECK_EQUAL(GetConfidenceLevel(0.89), ConfidenceLevel::HIGH);
    BOOST_CHECK_EQUAL(GetConfidenceLevel(0.9), ConfidenceLevel::VERY_HIGH);
    BOOST_CHECK_EQUAL(GetConfidenceLevel(1.0), ConfidenceLevel::VERY_HIGH);
}

BOOST_AUTO_TEST_CASE(GetConfidenceLevelInvalidRangeThrowsException) {
    BOOST_CHECK_THROW(
        { [[maybe_unused]] auto level = GetConfidenceLevel(-0.1); }, std::invalid_argument);

    BOOST_CHECK_THROW(
        { [[maybe_unused]] auto level = GetConfidenceLevel(1.1); }, std::invalid_argument);
}

// Test that analysis result methods work correctly with validation
BOOST_AUTO_TEST_CASE(TeedBallResultGetConfidenceLevelWithValidation) {
    TeedBallResult result;
    result.confidence = 0.8;

    [[maybe_unused]] ConfidenceLevel level;
    BOOST_CHECK_NO_THROW(level = result.GetConfidenceLevel());
    BOOST_CHECK_EQUAL(result.GetConfidenceLevel(), ConfidenceLevel::HIGH);
}

BOOST_AUTO_TEST_CASE(MovementResultGetConfidenceLevelWithValidation) {
    MovementResult result;
    result.movement_confidence = 0.6;

    [[maybe_unused]] ConfidenceLevel level;
    BOOST_CHECK_NO_THROW(level = result.GetConfidenceLevel());
    BOOST_CHECK_EQUAL(result.GetConfidenceLevel(), ConfidenceLevel::MEDIUM);
}

BOOST_AUTO_TEST_CASE(FlightAnalysisResultGetConfidenceLevelWithValidation) {
    FlightAnalysisResult result;
    result.confidence = 0.4;

    [[maybe_unused]] ConfidenceLevel level;
    BOOST_CHECK_NO_THROW(level = result.GetConfidenceLevel());
    BOOST_CHECK_EQUAL(result.GetConfidenceLevel(), ConfidenceLevel::LOW);
}

// Test [[nodiscard]] behavior doesn't prevent compilation
BOOST_AUTO_TEST_CASE(NodiscardAttributesCompileCorrectly) {
    BallPosition pos(100.0, 200.0, 15.0, 0.9);

    // These should compile without warnings when [[nodiscard]] is applied
    bool is_valid = pos.IsValid();
    double distance = pos.DistanceFrom(pos);
    bool nearly_equal = pos.IsNearlyEqual(pos, 1.0);

    BOOST_CHECK(is_valid);
    BOOST_CHECK_EQUAL(distance, 0.0);
    BOOST_CHECK(nearly_equal);
}

BOOST_AUTO_TEST_SUITE_END()

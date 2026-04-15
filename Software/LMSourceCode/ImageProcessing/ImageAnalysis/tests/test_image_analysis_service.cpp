/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

/**
 * @file test_image_analysis_service.cpp
 * @brief Unit tests for ImageAnalysisService input validation
 *
 * Tests input validation functions that will be added to the service.
 * Focuses on validation logic rather than full service implementation.
 */

#define BOOST_TEST_MODULE ImageAnalysisServiceValidationTests
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include "../application/image_analysis_service.hpp"

using namespace golf_sim::image_analysis::application;

// =============================================================================
// Input Validation Helper Functions (to be added to service)
// =============================================================================

namespace {
    // These validation functions will be added to ImageAnalysisService

    void ValidateConfidenceThreshold(double threshold) {
        if (threshold < 0.0 || threshold > 1.0) {
            throw std::invalid_argument("Confidence threshold must be between 0.0 and 1.0, got: " +
                                        std::to_string(threshold));
        }
    }

    void ValidateAnalyzerConfig(const AnalyzerConfig& config) {
        if (config.type.empty()) {
            throw std::invalid_argument("Analyzer type cannot be empty");
        }

        ValidateConfidenceThreshold(config.confidence_threshold);

        if (config.nms_threshold < 0.0 || config.nms_threshold > 1.0) {
            throw std::invalid_argument("NMS threshold must be between 0.0 and 1.0, got: " +
                                        std::to_string(config.nms_threshold));
        }

        if (config.input_width <= 0) {
            throw std::invalid_argument("Input width must be positive, got: " +
                                        std::to_string(config.input_width));
        }

        if (config.input_height <= 0) {
            throw std::invalid_argument("Input height must be positive, got: " +
                                        std::to_string(config.input_height));
        }
    }

    void ValidateAnalyzerType(const std::string& analyzer_type) {
        if (analyzer_type.empty()) {
            throw std::invalid_argument("Analyzer type cannot be empty");
        }

        // This would check against available analyzers in real implementation
        static const std::vector<std::string> valid_types = {"opencv", "yolo", "tensorflow_lite",
                                                             "hybrid"};

        if (std::find(valid_types.begin(), valid_types.end(), analyzer_type) == valid_types.end()) {
            throw std::invalid_argument("Unknown analyzer type: " + analyzer_type);
        }
    }
}  // namespace

BOOST_AUTO_TEST_SUITE(ImageAnalysisServiceValidationTests)

// =============================================================================
// Confidence Threshold Validation Tests
// =============================================================================

BOOST_AUTO_TEST_CASE(test_validate_confidence_threshold_valid_values) {
    // Valid confidence thresholds
    BOOST_CHECK_NO_THROW(ValidateConfidenceThreshold(0.0));
    BOOST_CHECK_NO_THROW(ValidateConfidenceThreshold(0.5));
    BOOST_CHECK_NO_THROW(ValidateConfidenceThreshold(1.0));
    BOOST_CHECK_NO_THROW(ValidateConfidenceThreshold(0.001));
    BOOST_CHECK_NO_THROW(ValidateConfidenceThreshold(0.999));
}

BOOST_AUTO_TEST_CASE(test_validate_confidence_threshold_invalid_values) {
    // Invalid confidence thresholds
    BOOST_CHECK_THROW(ValidateConfidenceThreshold(-0.1), std::invalid_argument);
    BOOST_CHECK_THROW(ValidateConfidenceThreshold(1.1), std::invalid_argument);
    BOOST_CHECK_THROW(ValidateConfidenceThreshold(-1.0), std::invalid_argument);
    BOOST_CHECK_THROW(ValidateConfidenceThreshold(2.0), std::invalid_argument);
    BOOST_CHECK_THROW(ValidateConfidenceThreshold(-0.001), std::invalid_argument);
    BOOST_CHECK_THROW(ValidateConfidenceThreshold(1.001), std::invalid_argument);
}

// =============================================================================
// Analyzer Configuration Validation Tests
// =============================================================================

BOOST_AUTO_TEST_CASE(test_validate_analyzer_config_valid) {
    AnalyzerConfig config;
    config.type = "opencv";
    config.confidence_threshold = 0.7;
    config.nms_threshold = 0.4;
    config.input_width = 640;
    config.input_height = 480;

    BOOST_CHECK_NO_THROW(ValidateAnalyzerConfig(config));
}

BOOST_AUTO_TEST_CASE(test_validate_analyzer_config_empty_type) {
    AnalyzerConfig config;
    config.type = "";  // Invalid

    BOOST_CHECK_THROW(ValidateAnalyzerConfig(config), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(test_validate_analyzer_config_invalid_confidence) {
    AnalyzerConfig config;
    config.type = "opencv";
    config.confidence_threshold = -0.1;  // Invalid

    BOOST_CHECK_THROW(ValidateAnalyzerConfig(config), std::invalid_argument);

    config.confidence_threshold = 1.5;  // Invalid
    BOOST_CHECK_THROW(ValidateAnalyzerConfig(config), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(test_validate_analyzer_config_invalid_nms_threshold) {
    AnalyzerConfig config;
    config.type = "opencv";
    config.nms_threshold = -0.1;  // Invalid

    BOOST_CHECK_THROW(ValidateAnalyzerConfig(config), std::invalid_argument);

    config.nms_threshold = 1.5;  // Invalid
    BOOST_CHECK_THROW(ValidateAnalyzerConfig(config), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(test_validate_analyzer_config_invalid_dimensions) {
    AnalyzerConfig config;
    config.type = "opencv";

    config.input_width = 0;  // Invalid
    BOOST_CHECK_THROW(ValidateAnalyzerConfig(config), std::invalid_argument);

    config.input_width = -1;  // Invalid
    BOOST_CHECK_THROW(ValidateAnalyzerConfig(config), std::invalid_argument);

    config.input_width = 640;
    config.input_height = 0;  // Invalid
    BOOST_CHECK_THROW(ValidateAnalyzerConfig(config), std::invalid_argument);

    config.input_height = -1;  // Invalid
    BOOST_CHECK_THROW(ValidateAnalyzerConfig(config), std::invalid_argument);
}

// =============================================================================
// Analyzer Type Validation Tests
// =============================================================================

BOOST_AUTO_TEST_CASE(test_validate_analyzer_type_valid) {
    BOOST_CHECK_NO_THROW(ValidateAnalyzerType("opencv"));
    BOOST_CHECK_NO_THROW(ValidateAnalyzerType("yolo"));
    BOOST_CHECK_NO_THROW(ValidateAnalyzerType("tensorflow_lite"));
    BOOST_CHECK_NO_THROW(ValidateAnalyzerType("hybrid"));
}

BOOST_AUTO_TEST_CASE(test_validate_analyzer_type_invalid) {
    BOOST_CHECK_THROW(ValidateAnalyzerType(""), std::invalid_argument);
    BOOST_CHECK_THROW(ValidateAnalyzerType("nonexistent"), std::invalid_argument);
    BOOST_CHECK_THROW(ValidateAnalyzerType("invalid_type"), std::invalid_argument);
    BOOST_CHECK_THROW(ValidateAnalyzerType("OPENCV"), std::invalid_argument);  // Case sensitive
}

// =============================================================================
// Error Message Quality Tests
// =============================================================================

BOOST_AUTO_TEST_CASE(test_error_messages_contain_actual_values) {
    try {
        ValidateConfidenceThreshold(-0.5);
        BOOST_FAIL("Expected exception was not thrown");
    } catch (const std::invalid_argument& e) {
        std::string error_msg = e.what();
        BOOST_CHECK(error_msg.find("-0.5") != std::string::npos);
        BOOST_CHECK(error_msg.find("between 0.0 and 1.0") != std::string::npos);
    }

    try {
        ValidateAnalyzerType("bad_type");
        BOOST_FAIL("Expected exception was not thrown");
    } catch (const std::invalid_argument& e) {
        std::string error_msg = e.what();
        BOOST_CHECK(error_msg.find("bad_type") != std::string::npos);
        BOOST_CHECK(error_msg.find("Unknown analyzer type") != std::string::npos);
    }
}

// =============================================================================
// Edge Case Tests
// =============================================================================

BOOST_AUTO_TEST_CASE(test_boundary_conditions) {
    // Test exact boundary values
    BOOST_CHECK_NO_THROW(ValidateConfidenceThreshold(0.0));
    BOOST_CHECK_NO_THROW(ValidateConfidenceThreshold(1.0));

    // Test just outside boundaries
    BOOST_CHECK_THROW(ValidateConfidenceThreshold(-0.000001), std::invalid_argument);
    BOOST_CHECK_THROW(ValidateConfidenceThreshold(1.000001), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(test_configuration_with_minimal_valid_values) {
    AnalyzerConfig config;
    config.type = "opencv";
    config.confidence_threshold = 0.0;
    config.nms_threshold = 0.0;
    config.input_width = 1;   // Minimal valid
    config.input_height = 1;  // Minimal valid

    BOOST_CHECK_NO_THROW(ValidateAnalyzerConfig(config));
}

BOOST_AUTO_TEST_CASE(test_configuration_with_maximal_valid_values) {
    AnalyzerConfig config;
    config.type = "tensorflow_lite";
    config.confidence_threshold = 1.0;
    config.nms_threshold = 1.0;
    config.input_width = 4096;   // Large but reasonable
    config.input_height = 4096;  // Large but reasonable

    BOOST_CHECK_NO_THROW(ValidateAnalyzerConfig(config));
}

BOOST_AUTO_TEST_SUITE_END()

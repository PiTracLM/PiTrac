/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "result_formatter.hpp"
#include <iomanip>
#include <sstream>

namespace golf_sim::image_analysis::testing {

    // StandardApprovalFormatter Implementation
    std::string StandardApprovalFormatter::FormatTeedBallResult(
        const domain::TeedBallResult& result) const {
        std::ostringstream summary;

        summary << "=== Teed Ball Analysis Result Summary ===\n";
        summary << "Ball State: " << FormatBallState(result.state) << "\n";
        summary << "Has Ball: " << (result.HasBall() ? "YES" : "NO") << "\n";
        summary << "Confidence: " << std::fixed << std::setprecision(3) << result.confidence
                << "\n";
        summary << "Analysis Method: " << result.analysis_method << "\n";
        summary << "Confidence Level: " << FormatConfidenceLevel(result.GetConfidenceLevel())
                << "\n";

        if (result.position.has_value()) {
            summary << FormatBallPosition(result.position.value());
        } else {
            summary << "Ball Position: NOT DETECTED\n";
        }

        if (!result.debug_info.empty()) {
            summary << FormatDebugInfo(result.debug_info);
        }

        summary << "============================================\n";

        return summary.str();
    }

    std::string StandardApprovalFormatter::FormatMovementResult(
        const domain::MovementResult& result) const {
        std::ostringstream summary;

        summary << "=== Movement Analysis Result ===\n";
        summary << "Movement Detected: " << (result.movement_detected ? "YES" : "NO") << "\n";
        summary << "Confidence: " << std::fixed << std::setprecision(3)
                << result.movement_confidence << "\n";
        summary << "Movement Magnitude: " << result.movement_magnitude << "\n";
        summary << "Time Since First Movement: " << result.time_since_first_movement.count()
                << " microseconds\n";
        summary << "Analysis Method: " << result.analysis_method << "\n";
        summary << "Motion Vectors: " << result.motion_vectors.size() << "\n";

        if (result.last_known_position.has_value()) {
            const auto& pos = result.last_known_position.value();
            summary << "Last Known Position:\n";
            summary << "  X: " << pos.x_pixels << " pixels\n";
            summary << "  Y: " << pos.y_pixels << " pixels\n";
            summary << "  Radius: " << pos.radius_pixels << " pixels\n";
            summary << "  Confidence: " << pos.confidence << "\n";
        }

        summary << "================================\n";

        return summary.str();
    }

    std::string StandardApprovalFormatter::FormatBallState(domain::BallState state) const {
        switch (state) {
            case domain::BallState::ABSENT:
                return "ABSENT";
            case domain::BallState::TEED:
                return "TEED";
            case domain::BallState::RESET:
                return "RESET";
            case domain::BallState::MOVING:
                return "MOVING";
            default:
                return "INVALID";
        }
    }

    std::string StandardApprovalFormatter::FormatConfidenceLevel(
        domain::ConfidenceLevel level) const {
        switch (level) {
            case domain::ConfidenceLevel::LOW:
                return "LOW";
            case domain::ConfidenceLevel::MEDIUM:
                return "MEDIUM";
            case domain::ConfidenceLevel::HIGH:
                return "HIGH";
            case domain::ConfidenceLevel::VERY_HIGH:
                return "VERY_HIGH";
            default:
                return "UNKNOWN";
        }
    }

    std::string StandardApprovalFormatter::FormatBallPosition(
        const domain::BallPosition& position) const {
        std::ostringstream pos_info;
        pos_info << "Ball Position:\n";
        pos_info << "  X: " << std::fixed << std::setprecision(2) << position.x_pixels
                 << " pixels\n";
        pos_info << "  Y: " << std::fixed << std::setprecision(2) << position.y_pixels
                 << " pixels\n";
        pos_info << "  Radius: " << std::fixed << std::setprecision(2) << position.radius_pixels
                 << " pixels\n";
        pos_info << "  Confidence: " << std::fixed << std::setprecision(3) << position.confidence
                 << "\n";
        pos_info << "  Detection Method: " << position.detection_method << "\n";
        pos_info << "  Valid: " << (position.IsValid() ? "YES" : "NO") << "\n";
        return pos_info.str();
    }

    std::string StandardApprovalFormatter::FormatDebugInfo(
        const std::vector<std::string>& debug_info) const {
        std::ostringstream debug_section;
        debug_section << "Debug Information:\n";
        for (const auto& debug : debug_info) {
            debug_section << "  - " << debug << "\n";
        }
        return debug_section.str();
    }

    // CompactFormatter Implementation
    std::string CompactFormatter::FormatTeedBallResult(const domain::TeedBallResult& result) const {
        std::ostringstream summary;
        summary << "Ball:" << (result.HasBall() ? "YES" : "NO")
                << " State:" << static_cast<int>(result.state) << " Conf:" << std::fixed
                << std::setprecision(2) << result.confidence;

        if (result.position.has_value()) {
            const auto& pos = result.position.value();
            summary << " Pos:(" << static_cast<int>(pos.x_pixels) << ","
                    << static_cast<int>(pos.y_pixels) << "," << static_cast<int>(pos.radius_pixels)
                    << ")";
        }

        summary << "\n";
        return summary.str();
    }

    std::string CompactFormatter::FormatMovementResult(const domain::MovementResult& result) const {
        std::ostringstream summary;
        summary << "Movement:" << (result.movement_detected ? "YES" : "NO")
                << " Conf:" << std::fixed << std::setprecision(2) << result.movement_confidence
                << " Mag:" << result.movement_magnitude << "\n";
        return summary.str();
    }

    // ResultFormatterFactory Implementation
    std::unique_ptr<IResultFormatter> ResultFormatterFactory::Create(FormatterType type) {
        switch (type) {
            case FormatterType::STANDARD_APPROVAL:
                return std::make_unique<StandardApprovalFormatter>();
            case FormatterType::COMPACT:
                return std::make_unique<CompactFormatter>();
            default:
                return std::make_unique<StandardApprovalFormatter>();
        }
    }

}  // namespace golf_sim::image_analysis::testing

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "logging_tools.h"
#include "gs_shot_parameters.h"

namespace golf_sim {

    GsShotParameters::GsShotParameters() {
        velocity_is_set_ = false;
        HLA_is_set_ = false;
        VLA_is_set_ = false;
    }

    GsShotParameters::~GsShotParameters() {
    }

    void GsShotParameters::SetInternalParameter(const ShotParameter& parameter, bool is_present) {

        switch (parameter) {
            case kShotParameterNone:
                velocity_is_set_ = !is_present;
                HLA_is_set_ = !is_present;
                VLA_is_set_ = !is_present;
                break;
            case kBallVelocity:
                velocity_is_set_ = is_present;
                HLA_is_set_ = !is_present;
                VLA_is_set_ = !is_present;
                break;
            case kHLA:
                velocity_is_set_ = !is_present;
                HLA_is_set_ = is_present;
                VLA_is_set_ = !is_present;
                break;
            case kVLA:
                velocity_is_set_ = !is_present;
                HLA_is_set_ = !is_present;
                VLA_is_set_ = is_present;
                break;
            case kShotParameterAll:
                velocity_is_set_ = is_present;
                HLA_is_set_ = is_present;
                VLA_is_set_ = is_present;
                break;
            default:
                GS_LOG_TRACE_MSG(warning, "GsShotParameters::SetInternalParameter--Invalid parameter: " + std::to_string( static_cast<int>(parameter) ) );
		}
    }

    void GsShotParameters::SetParameter(const ShotParameter& parameter, bool is_present) {

        SetInternalParameter(parameter, is_present);
    }

    bool GsShotParameters::ParameterIsPresent(const ShotParameter& parameter) const {

        switch (parameter) {
            case kShotParameterNone:
                return !velocity_is_set_ && !HLA_is_set_ && !VLA_is_set_;
            case kBallVelocity:
                return velocity_is_set_;
            case kHLA:
                return HLA_is_set_;
            case kVLA:
                return VLA_is_set_;
            case kShotParameterAll:
                return velocity_is_set_ && HLA_is_set_ && VLA_is_set_;
            default:
                GS_LOG_TRACE_MSG(warning, "GsShotParameters::SetInternalParameter--Invalid parameter: " + std::to_string(static_cast<int>(parameter)));
                return false;
        }
    }

    std::string GsShotParameters::Format() const {
        std::string s;
        s = "Set Shot Parameters are the following:  \n";

        if (ParameterIsPresent(ShotParameter::kBallVelocity)) {
            s += "    BallSpeed  \n";
        }

        if (ParameterIsPresent(ShotParameter::kVLA)) {
            s += "    VLA'      \n";
        }

        if (ParameterIsPresent(ShotParameter::kHLA)) {
            s += "    HLA      \n";
        }

        return s;
    }
}
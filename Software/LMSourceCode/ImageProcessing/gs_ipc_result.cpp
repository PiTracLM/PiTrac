/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#ifdef __unix__  // Ignore in Windows environment

#include "logging_tools.h"

#include "gs_ipc_result.h"
#include "cv_utils.h"

namespace golf_sim {

    GsIPCResult::GsIPCResult() {
    }

    GsIPCResult::~GsIPCResult() {
    }

    std::string GsIPCResult::FormatResultType(const GsIPCResultType t) const {

        std::string s;

        std::map<GsIPCResultType, std::string> result_table =
        { {   GsIPCResultType::kUnknown, "Unknown" },
            { GsIPCResultType::kWaitingForBallToAppear, "Waiting For Ball" },
            { GsIPCResultType::kMultipleBallsPresent, "Multiple Balls Present" },
            { GsIPCResultType::kPausingForBallStabilization, "Waiting For Placement To Stabilize" },
            { GsIPCResultType::kBallPlacedAndReadyForHit, "Ball Placed" },
            { GsIPCResultType::kHit, "Hit" },
            { GsIPCResultType::kError, "Error" },
            { GsIPCResultType::kCalibrationResults, "Calibration Results" }
        };

        if (result_table.count(t) == 0) {
            s = "SYSTEM ERROR:  Invalid GsIPCResultType: " + std::to_string((int)t);
        }
        s = result_table[t];

        return s;
    }

    std::string GsIPCResult::Format() const {
        int carry_yards = CvUtils::MetersToYards(carry_meters_);
        float speed_mph = CvUtils::MetersPerSecondToMPH(speed_mpers_);

        std::string result_type = FormatResultType(result_type_);

        std::string s = "GsIPCResult:  Carry: " + std::to_string(carry_yards) + " yards.\n" +
            "              Speed: " + std::to_string(speed_mph) + " mph.\n" +
            "       Launch Angle: " + std::to_string(launch_angle_deg_) + " degrees.\n" +
            "         Side Angle: " + std::to_string(side_angle_deg_) + " degrees.\n" +
            "          Back Spin: " + std::to_string(back_spin_rpm_) + " rpm.\n" +
            "          Side Spin: " + std::to_string(side_spin_rpm_) + " rpm.\n" +
            "         Confidence: " + std::to_string(confidence_) + " 0-10(most).\n" +
            "          Club Type: " + std::to_string(club_type_) + " 0-Unselected, 1-Driver, 2-Iron, 3-Putter\n" +
            "        Result Type: " + result_type + ".\n" +
            "            Message: " + message_ + ".";

        return s;
    }

}

#endif // #ifdef __unix__  // Ignore in Windows environment

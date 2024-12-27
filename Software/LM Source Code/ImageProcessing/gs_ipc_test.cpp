/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#ifdef __unix__  // Ignore in Windows environment

#include "logging_tools.h"

#include "gs_ipc_test.h"
#include "cv_utils.h"

namespace golf_sim {

    GsIPCTest::GsIPCTest() {
    }

    GsIPCTest::~GsIPCTest() {
    }

    std::string GsIPCTest::FormatResultType(const GsIPCTestType t) const {

        std::string s;

        std::map<GsIPCTestType, std::string> result_table =
        { {   GsIPCTestType::kUnknownTest, "Unknown" },
            { GsIPCTestType::kBallLocation, "Ball Location" }
        };

        if (result_table.count(t) == 0) {
            s = "SYSTEM ERROR:  Invalid GsIPCTestType: " + std::to_string((int)t);
        }
        s = result_table[t];

        return s;
    }

    std::string GsIPCTest::Format() const {
        std::string test_type = FormatResultType(test_type_);

        std::string s = "GsIPCTest:  Test Type: " + test_type + "." +
            "                       X Distance: " + std::to_string(ball_distance_x_cm_) + " cm." +
            "                       Y Distance: " + std::to_string(ball_distance_y_cm_) + " cm." +
            "                       Z Distance: " + std::to_string(ball_distance_z_cm_) + " cm.";

        return s;
    }

}

#endif // #ifdef __unix__  // Ignore in Windows environment

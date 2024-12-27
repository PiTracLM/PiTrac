/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// "TruGolf Simulators" and other marks such as E6 may be trademarked by TruGolf, Inc.
// The PiTrac project is not endorsed, sponsored by or associated with TrueGolf products or services.


#include <regex>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "logging_tools.h"

#include "gs_e6_results.h"

namespace golf_sim {

    GsE6Results::GsE6Results() {
    }

    GsE6Results::GsE6Results(const GolfBall& ball) : GsResults(ball) {
    }

    // TBD - Copy constructor?
    GsE6Results::GsE6Results(const GsResults& results) : GsResults(results) {
    }
    

    GsE6Results::~GsE6Results() {

    }


    std::string GsE6Results::Format() const {
        // Create a JSON object based on https://e6golf.com/E6ConnectV1.html
        // Create a property tree object
        boost::property_tree::ptree root;

        // Create the three required children objects and add to the JSON root
        boost::property_tree::ptree ball_data_child;
        boost::property_tree::ptree club_data_child;
        boost::property_tree::ptree shot_data_options_child;

        // Root-level values
        root.put("Type", "SetBallData");

        // E6 Enforces certain ranges.  Make sure we do, too
        int back_spin_rpm = std::min(back_spin_rpm_, 19999);
        back_spin_rpm = std::max(back_spin_rpm_, -999);

        float speed_mph = std::min(speed_mph_, (float)249.9);
        speed_mph = std::max(speed_mph_, (float)0.09);

        int side_spin_rpm = std::min(side_spin_rpm_, 5999);
        side_spin_rpm = std::max(side_spin_rpm_, -5999);


        // Ball data - some of the values such as tilt are not required and we don't include them
        ball_data_child.put("BackSpin", FormatDoubleAsString(back_spin_rpm_));
        ball_data_child.put("BallSpeed", FormatDoubleAsString(speed_mph_));
        ball_data_child.put("LaunchAngle", FormatDoubleAsString(vla_deg_));
        ball_data_child.put("LaunchDirection", FormatDoubleAsString(hla_deg_));
        ball_data_child.put("SideSpin", FormatDoubleAsString(side_spin_rpm_));

        root.add_child("BallData", ball_data_child);

        std::string result = GenerateStringFromJsonTree(root);

        if (result == "") {
            GS_LOG_MSG(warning, "GsE6Results::Format() returning empty string.");
        }

        return result;
    }

}

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include <regex>
#include "gs_format_lib.h"
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "logging_tools.h"

#include "gs_gspro_results.h"

namespace golf_sim {

    GsGSProResults::GsGSProResults() {
    }

    GsGSProResults::GsGSProResults(const GolfBall& ball) : GsResults(ball) {
    }

    // TBD - Copy constructor?
    GsGSProResults::GsGSProResults(const GsResults& results) : GsResults(results) {
    }
    

    GsGSProResults::~GsGSProResults() {

    }


    std::string GsGSProResults::Format() const {
        // Create a JSON object based on https://gsprogolf.com/GSProConnectV1.html
        // Create a property tree object
        boost::property_tree::ptree root;

        // Create the three required children objects and add to the JSON root
        boost::property_tree::ptree ball_data_child;
        boost::property_tree::ptree club_data_child;
        boost::property_tree::ptree shot_data_options_child;

        // Root-level values
        root.put("DeviceID", "PiTrac LM 0.1");
        root.put("Units", "Yards");
        // The shot number should be increased with each shot
        root.put("ShotNumber", shot_number_);
        root.put("APIversion", "1");

        // Ball data - some of these values are not required
        ball_data_child.put("Speed", FormatDoubleAsString(speed_mph_));
        ball_data_child.put("SpinAxis", FormatDoubleAsString(GetSpinAxis()));
        ball_data_child.put("TotalSpin", "0.0");
        ball_data_child.put("BackSpin", FormatDoubleAsString(back_spin_rpm_));
        ball_data_child.put("SideSpin", FormatDoubleAsString(side_spin_rpm_));
        ball_data_child.put("HLA", FormatDoubleAsString(hla_deg_));
        ball_data_child.put("VLA", FormatDoubleAsString(vla_deg_));
        ball_data_child.put("CarryDistance", FormatDoubleAsString(carry_distance_yards_));

        // Club data - we don't currently implement any of this, but
        // just to be safe, we will still send the information
        club_data_child.put("Speed", "0.0");
        club_data_child.put("AngleOfAttack", "0.0");
        club_data_child.put("FaceToTarget", "0.0");
        club_data_child.put("Lie", "0.0");
        club_data_child.put("Loft", "0.0");
        club_data_child.put("Path", "0.0");
        club_data_child.put("SpeedAtImpact", "0.0");
        club_data_child.put("VerticalFaceImpact", "0.0");
        club_data_child.put("HorizontalFaceImpact", "0.0");
        club_data_child.put("ClosureRate", "0.0");
        
        if (!result_message_is_keepalive_) {
            // Only the ball data is valid
            shot_data_options_child.put("ContainsBallData", true);
            shot_data_options_child.put("ContainsClubData", false);
            // TBD - Consider if we want to send the next two values in a heartbeat?
            shot_data_options_child.put("LaunchMonitorIsReady", true);
            shot_data_options_child.put("LaunchMonitorBallDetected", true);
            shot_data_options_child.put("IsHeartBeat", false);
        }
        else {
            shot_data_options_child.put("ContainsBallData", false);
            shot_data_options_child.put("ContainsClubData", false);
            shot_data_options_child.put("LaunchMonitorIsReady", true);
            shot_data_options_child.put("LaunchMonitorBallDetected", true);
            shot_data_options_child.put("IsHeartBeat", true);
        }

        root.add_child("BallData", ball_data_child);
        root.add_child("ClubData", club_data_child);
        root.add_child("ShotDataOptions", shot_data_options_child);

        std::string result = GenerateStringFromJsonTree(root);

        if (result == "") {
            GS_LOG_MSG(warning, "GsGSProResults::Format() returning empty string.");
        }

        return result;
    }

}

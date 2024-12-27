/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Representation of the results of processing a golf shot

#include <regex>
#include "gs_format_lib.h"
#include "math.h"
#include "logging_tools.h"
#include "cv_utils.h"

#include "gs_results.h"

namespace golf_sim {

    GsResults::GsResults() {
    }

    GsResults::~GsResults() {
    }

    GsResults::GsResults(const GolfBall& ball) {
        shot_number_ = 0;
        speed_mph_ = CvUtils::MetersPerSecondToMPH(ball.velocity_);
        hla_deg_ = (float)(ball.angles_ball_perspective_[0]);
        vla_deg_ = (float)(ball.angles_ball_perspective_[1]);
        back_spin_rpm_ = ball.rotation_speeds_RPM_[2];
        side_spin_rpm_ = ball.rotation_speeds_RPM_[0];
        // TBD - Not sure club type should be set here,
        // but this is a reasonable default for now
        club_type_ = GolfSimClubs::GetCurrentClubType();
    }

    float GsResults::GetSpinAxis() const {
        if (std::abs(side_spin_rpm_) <= 0.0001) {
            return 0.0;
        }
        float spin_axis = atan((float)side_spin_rpm_ / (float)back_spin_rpm_ + 0.00001) * (180. / kPi);
        return spin_axis;
    }

    std::string GsResults::Format() const {
        std::string s;
        s =  "Shot No.:         " + std::to_string(shot_number_) + "\n";
        s += "Speed (mph):      " + std::to_string(speed_mph_) + "\n";
        s += "Launch Angle:     " + std::to_string(vla_deg_) + "\n";
        s += "Side Angle:       " + std::to_string(hla_deg_) + "\n";
        s += "Back Spin (rpm):  " + std::to_string(back_spin_rpm_) + "\n";
        s += "Side Spin:        " + std::to_string(side_spin_rpm_) + "\n";
        s += "Spin Axis (deg.): " + std::to_string(GetSpinAxis()) + "\n";
        s += "Club Type: (1D 3P)" + std::to_string(club_type_) + "\n";

        return s;
    }

    std::string GsResults::FormatDoubleAsString(const double original_value) {

        double value = std::round(original_value * 10.0) / 10.0;

        auto s = GS_FORMATLIB_FORMAT("{: <1.1f}", value);

        std::string result;

        // If we don't quote 0.0, it ends up as 0, and some systems don't like that
        if (s == "0.0") {
            result = "0.0";
        }
        else {
            result = s;
        }

        return result;
    }

    std::string GsResults::GenerateStringFromJsonTree(const boost::property_tree::ptree& root) {

        // Write the property tree to a JSON string
        std::stringstream ss;
        boost::property_tree::write_json(ss, root);

        // Get the JSON string
        std::string json_string = ss.str();

        // Remove any quotes around data values that should be numbers
        // Apparently this is a deficiency in the boost library, see
        // https://stackoverflow.com/questions/2855741/why-does-boost-property-tree-write-json-save-everything-as-string-is-it-possibl

        std::regex reg("\\\"([+-]?[0-9]+\\.{0,1}[0-9]*)\\\"");
        std::string result = std::regex_replace(json_string, reg, "$1");

        std::string subStringToRemove = "\"true\"";
        std::string subStringToReplace = "true";
        boost::replace_all(result, subStringToRemove, subStringToReplace);

        subStringToRemove = "\"false\"";
        subStringToReplace = "false";
        boost::replace_all(result, subStringToRemove, subStringToReplace);

        subStringToRemove = "\"APIversion\": 1,";
        subStringToReplace = "\"APIversion\": \"1\",";
        boost::replace_all(result, subStringToRemove, subStringToReplace);

        return result;
    }

}

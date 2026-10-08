/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Representation of the results of processing a golf shot

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
        speed_mph_ = (float)CvUtils::MetersPerSecondToMPH((float)ball.velocity_);
        hla_deg_ = (float)(ball.angles_ball_perspective_[0]);
        vla_deg_ = (float)(ball.angles_ball_perspective_[1]);
        back_spin_rpm_ = (int)ball.rotation_speeds_RPM_[2];
        side_spin_rpm_ = (int)ball.rotation_speeds_RPM_[0];
        // TBD - Not sure club type should be set here,
        // but this is a reasonable default for now
        club_type_ = GolfSimClubs::GetCurrentClubType();

        // TBD - Even though this is a constructor, it might be a reasonable
        // place to calculate the Carry yardarge.
    }

    float GsResults::GetSpinAxis() const {
        if (std::abs(side_spin_rpm_) <= 0.0001) {
            return 0.0;
        }
        float spin_axis = (float)atan((float)side_spin_rpm_ / (float)back_spin_rpm_ + 0.00001) * (180.F / (float)kPi);
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

        // TBD - Add internal carry value.

        return s;
    }

}

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

 // Representation of the results of processing a golf shot


#pragma once

#include <boost/property_tree/json_parser.hpp>

#include "logging_tools.h"
#include "golf_ball.h"
#include "gs_clubs.h"

// Base class for representing and transferring Golf Sim results

namespace golf_sim {

    class GsResults {

    public:
        GsResults();
        GsResults(const GolfBall& ball);
        virtual ~GsResults();
        virtual std::string Format() const;

        // Negative means tilted to the left when the ball is viewed from
        // behind looking down along the line of flight away from the golfer.
        // Negative means the ball will curve to the left.  Negative side spin
        // will result in a positive spin axis meaning the ball will curve
        // to the right.
        float GetSpinAxis() const;


    public:
        long shot_number_ = 0;
        float speed_mph_ = 0;
        float hla_deg_ = 0.;
        float vla_deg_ = 0.;
        int back_spin_rpm_ = 0;
        int side_spin_rpm_ = 0;     // Negative is left (counter-clockwise from above ball)
        GolfSimClubs::GsClubType club_type_ = GolfSimClubs::GsClubType::kNotSelected;
    };

}

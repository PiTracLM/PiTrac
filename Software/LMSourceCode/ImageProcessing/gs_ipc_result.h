/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#ifdef __unix__  // Ignore in Windows environment


#include <msgpack.hpp>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/dnn/all_layers.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>

#include "logging_tools.h"
#include "gs_clubs.h"


// The primary object for communications to the Golf Sim user interface

namespace golf_sim {

    enum class GsIPCResultType { kUnknown = 0, 
        kInitializing = 1, 
        kWaitingForBallToAppear = 2,
        kWaitingForSimulatorArmed = 3,
        kPausingForBallStabilization = 4,
        kMultipleBallsPresent = 5,
        kBallPlacedAndReadyForHit = 6,
        kHit = 7,
        kError = 8,
        kCalibrationResults = 9,
        kControlMessage = 10};

    class GsIPCResult {

    public:

        GsIPCResult();
        virtual ~GsIPCResult();

        // Returns a string representation of this result
        std::string Format() const;

        std::string FormatResultType(const GsIPCResultType t) const;

    public:
        int carry_meters_ = 0;
        float speed_mpers_ = 0;
        float launch_angle_deg_ = 0.;
        float side_angle_deg_ = 0.;
        int back_spin_rpm_ = 0;
        int side_spin_rpm_ = 0;     // Negative is left (counter-clockwise from above ball)
        int confidence_ = 0;  // 10 - the results are as confident as the system can be.  0 - no confidence at all.  Probably an error occrued. Not fully implemented yet.
        GolfSimClubs::GsClubType club_type_ = GolfSimClubs::GsClubType::kNotSelected;
        GsIPCResultType result_type_ = GsIPCResultType::kUnknown;
        std::string message_{ "" };
        std::vector<std::string> log_messages_;

        MSGPACK_DEFINE( carry_meters_,
                        speed_mpers_,
                        launch_angle_deg_,
                        side_angle_deg_,
                        back_spin_rpm_,
                        side_spin_rpm_,
                        confidence_,
                        club_type_,
                        result_type_,
                        message_,
                        log_messages_);

    };

}
// This needs to be placed outside the namespace
MSGPACK_ADD_ENUM(golf_sim::GolfSimClubs::GsClubType);
MSGPACK_ADD_ENUM(golf_sim::GsIPCResultType);


#endif // #ifdef __unix__  // Ignore in Windows environment

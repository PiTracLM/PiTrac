/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Base class for control messages, such as those sent between the LM and
// external systems like the web dashboard and third-party golf simulators

#pragma once

#ifdef __unix__  // Ignore in Windows environment


#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/dnn/all_layers.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>

#include "logging_tools.h"


// The primary object for control-type communications from the Golf Sim user interface

namespace golf_sim {

    // TBD - Add a change-player type?
    enum class GsIPCControlMsgType { 
        kUnknown = 0, 
        kClubChangeToPutter = 1,
        kClubChangeToDriver = 2,
    };

    class GsIPCControlMsg {

    public:

        GsIPCControlMsg();
        virtual ~GsIPCControlMsg();

        // Returns a string representation of this result
        std::string Format() const;

        static std::string FormatControlMessageType(const GsIPCControlMsgType t);

    public:
        GsIPCControlMsgType control_type_ = GsIPCControlMsgType::kUnknown;

    };

}


#endif // #ifdef __unix__  // Ignore in Windows environment

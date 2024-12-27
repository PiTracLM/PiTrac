/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Base class for incomigin IPC messages, such as those sent between the LM and
// external systems like the LM Monitor GUI and third-party golf simulators

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


// The primary object for control-type communications from the Golf Sim user interface

namespace golf_sim {

    // TBD - Add a change-player type?
    enum class GsIPCControlMsgType { 
        kUnknown = 0, 
        kClubChangeToPutter = 1,
        kClubChangeToDriver = 2,
    };

    // This class is mostly designed to compartmentalize the details of (De)serializing
    // these IPC messages.
    class GsIPCControlMsg {

    public:

        GsIPCControlMsg();
        virtual ~GsIPCControlMsg();

        // Returns a string representation of this result
        std::string Format() const;

        static std::string FormatControlMessageType(const GsIPCControlMsgType t);

    public:
        GsIPCControlMsgType control_type_ = GsIPCControlMsgType::kUnknown;

        MSGPACK_DEFINE( control_type_ );

    };

}
// This needs to be placed outside the namespace
MSGPACK_ADD_ENUM(golf_sim::GsIPCControlMsgType);


#endif // #ifdef __unix__  // Ignore in Windows environment

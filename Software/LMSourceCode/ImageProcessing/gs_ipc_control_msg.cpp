/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#ifdef __unix__  // Ignore in Windows environment

#include "logging_tools.h"

#include "gs_ipc_control_msg.h"
#include "cv_utils.h"

namespace golf_sim {

    GsIPCControlMsg::GsIPCControlMsg() {
    }

    GsIPCControlMsg::~GsIPCControlMsg() {
    }

    std::string GsIPCControlMsg::FormatControlMessageType(const GsIPCControlMsgType t)  {

        std::string s;

        std::map<GsIPCControlMsgType, std::string> result_table =
        { {   GsIPCControlMsgType::kUnknown, "Unknown" },
            { GsIPCControlMsgType::kClubChangeToPutter, "Change club to putter" },
            { GsIPCControlMsgType::kClubChangeToDriver, "Change club to driver" }
        };

        if (result_table.count(t) == 0) {
            return "SYSTEM ERROR:  Invalid GsIPCControlMsgType: " + std::to_string((int)t);
        }
        return result_table[t];
    }

    std::string GsIPCControlMsg::Format() const {

        std::string control_type = FormatControlMessageType(control_type_);

        std::string s = "GsIPCControlMsg:  ControlType: " +  control_type + ".";

        return s;
    }

}

#endif // #ifdef __unix__  // Ignore in Windows environment

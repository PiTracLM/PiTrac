/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Control message types used by the closed-source E6 object (gs_e6_response).
// e6_auth_shim.cpp defines FormatControlMessageType for it.

#pragma once

#ifdef __unix__  // Ignore in Windows environment

#include <string>

namespace golf_sim {

    enum class GsIPCControlMsgType { 
        kUnknown = 0, 
        kClubChangeToPutter = 1,
        kClubChangeToDriver = 2,
    };

    class GsIPCControlMsg {

    public:

        static std::string FormatControlMessageType(const GsIPCControlMsgType t);

    };

}


#endif // #ifdef __unix__  // Ignore in Windows environment

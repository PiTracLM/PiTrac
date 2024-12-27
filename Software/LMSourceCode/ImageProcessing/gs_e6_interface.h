/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

 // "TruGolf Simulators" and other marks such as E6 may be trademarked by TruGolf, Inc.
 // The PiTrac project is not endorsed, sponsored by or associated with TrueGolf products or services.


#pragma once

 // Base class for representing and transferring Golf Sim results to the E6 golf simulator

#include <boost/asio.hpp>

#include "gs_results.h"
#include "gs_sim_socket_interface.h"

using namespace boost::asio;
using ip::tcp;

namespace golf_sim {

    class GsE6Interface : public GsSimSocketInterface {

    public:
        GsE6Interface();
        virtual ~GsE6Interface();

        // Returns true iff the GSPro interface is to be used
        static bool InterfaceIsPresent();

        // Must be called before SendResults is called.
        virtual bool Initialize();

        // Deals with, for example, shutting down any socket connection
        virtual void DeInitialize();

        virtual bool SendResults(const GsResults& results);

        virtual void SetSimSystemArmed(const bool is_armed);
        virtual bool GetSimSystemArmed();

    protected:

        virtual std::string GenerateResultsDataToSend(const GsResults& results);

        virtual bool ProcessReceivedData(const std::string received_data);

    protected:
        static long kE6InterMessageDelayMs;
    };

}

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <boost/asio.hpp>

#include "gs_results.h"
#include "gs_sim_socket_interface.h"

using namespace boost::asio;
using ip::tcp;

// Base class for representing and transferring Golf Sim results

namespace golf_sim {

    class GsGSProInterface : public GsSimSocketInterface {

    public:
        GsGSProInterface();
        virtual ~GsGSProInterface();

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
    };

}

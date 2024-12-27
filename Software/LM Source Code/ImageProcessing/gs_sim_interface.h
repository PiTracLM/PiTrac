/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>


#include "logging_tools.h"
#include "golf_ball.h"
#include "gs_results.h"


// Base class for interfaces to 3rd-party golf simulators

namespace golf_sim {

    class GsSimInterface {

    public:
        enum GolfSimulatorType {
            kNone = 0,
            kGSPro = 1,
            kE6 = 2
        };

        GsSimInterface();
        virtual ~GsSimInterface();

        // Create and initialize and sim interfaces that are configured
        static bool InitializeSims();

        // De-initialize and destory and sim interfaces that are configured
        static void DeInitializeSims();

        // To be called from the launch monitor
        static bool SendResultsToGolfSims(const GsResults& results);

        // If the interface is present (usually indicated in the config.json file),
        // this method returns true;
        static bool InterfaceIsPresent();

        // Allows the shot counter to be incremented from outside the simulator
        // interface for such purposes and ensuring the counter keeps going even
        // when a failure occurs.

        static void IncrementShotCounter();

        // Will be overridden by each derived class

        virtual bool Initialize();

        // De-initialize and destroy and sim interfaces that are configured
        virtual void DeInitialize();

        // Base class behavior is to simply print out the JSON
        virtual bool SendResults(const GsResults& results);

        // Sends a string without any other side-effects
        // Returns the number of bytes written
        virtual int SendSimMessage(const std::string& message);

        virtual void SetSimSystemArmed(const bool is_armed);
        virtual bool GetSimSystemArmed();

        // These static functions operate at the collection level for all interfaces
        static long GetShotCounter() { return shot_counter_; };

        // Find the GSPro or E6 or whatever interface (if available) by type
        static GsSimInterface *GetSimInterfaceByType(GolfSimulatorType sim_type);

        // Returns true only if each of the available interfaces is armed
        static bool GetAllSystemsArmed();

    protected:

        // Typical derived-class behavior will be to convert the results into a
        // sim-specific data packet, such as a JSON string
        virtual std::string GenerateResultsDataToSend(const GsResults& results);

        // Called when the LM receives data
        virtual bool ProcessReceivedData(const std::string received_data);

    protected:

        // Holds pointers to derived interfaces for each attached sim
        static std::vector<GsSimInterface*> interfaces_;

        static std::string launch_monitor_id_string_;
        
        // True if all the attached sims have been initialized
        static bool sims_initialized_;

        static long shot_counter_;

        // True if all THIS sim has been initialized
        bool initialized_;

        GolfSimulatorType simulator_type_;

        // Must be true before the simulator system is ready to accept shot data
        // Only relevant for derived, non-virtual classes for whom arming is an
        // actual thing.
        bool sim_system_is_armed_ = false;

        boost::mutex sim_arming_mutex_;
    };

}

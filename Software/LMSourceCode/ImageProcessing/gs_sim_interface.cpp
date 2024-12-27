/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "logging_tools.h"
#include "cv_utils.h"
#include "gs_options.h"
#include "gs_config.h"

#include "gs_sim_interface.h"
#include "gs_gspro_interface.h"
#include "gs_e6_interface.h"

namespace golf_sim {

    /* TBD - REMOVE - No longer static
    bool GsSimInterface::sim_system_is_armed_ = false;
    boost::mutex GsSimInterface::sim_arming_mutex_;
    */
    std::string GsSimInterface::launch_monitor_id_string_ = "PiTrac LM 0.1";

    std::vector<GsSimInterface*> GsSimInterface::interfaces_;
    bool GsSimInterface::sims_initialized_ = false;

    // The first shot number the golf simulator receives should be 1, not 0, and
    // the system will increment the counter first before storing information
    long GsSimInterface::shot_counter_ = 0;


    GsSimInterface::GsSimInterface() {
        GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.kLaunchMonitorIdString", launch_monitor_id_string_);
    }

    GsSimInterface::~GsSimInterface() {
    }

    bool GsSimInterface::InitializeSims() {

        GS_LOG_TRACE_MSG(trace, "GsSimInterface::InitializeSims()");

        // Create and add an interface to the global vector of interfaces
        // for each configured sim

#ifdef __unix__  // Ignore in Windows environment

        if (GsGSProInterface::InterfaceIsPresent()) {
            GS_LOG_TRACE_MSG(trace, "GSPro simulator interface detected.");

            GsGSProInterface* gspro_sim = new GsGSProInterface();
            if (gspro_sim == nullptr) {
                GS_LOG_MSG(error, "Could not create a GSPro simulator interface.");
                return false;
            }

            gspro_sim->simulator_type_ = GolfSimulatorType::kGSPro;

            interfaces_.push_back(gspro_sim);

            if (!gspro_sim->Initialize()) {
                GS_LOG_MSG(error, "GSPro simulator interface could not be initialized.");
                return false;
            }
        }

        if (GsE6Interface::InterfaceIsPresent()) {
            GS_LOG_TRACE_MSG(trace, "E6 simulator interface detected.");
            GsE6Interface* e6_sim = new GsE6Interface();
            if (e6_sim == nullptr) {
                GS_LOG_MSG(error, "Could not create an E6 simulator interface.");
                return false;
            }

            e6_sim->simulator_type_ = GolfSimulatorType::kE6;

            interfaces_.push_back(e6_sim);

            if (!e6_sim->Initialize()) {
                GS_LOG_MSG(error, "E6 simulator interface could not be initialized.");
                return false;
            }
        }

        if (interfaces_.size() == 0) {
            GS_LOG_TRACE_MSG(trace, "No simulator interface detected.");
        }
#endif
        shot_counter_ = 0;

        sims_initialized_ = true;

        return true;
    }


    void GsSimInterface::DeInitializeSims() {

        GS_LOG_TRACE_MSG(trace, "GsSimInterface::DeInitializeSims()");

#ifdef __unix__  // Ignore in Windows environment

        for (auto interface : interfaces_) {
            if (interface == nullptr) {
                GS_LOG_MSG(error, "GsSimInterface::DeInitializeSims() found a null interface");
                continue;
            }

            interface->DeInitialize();
            delete interface;
        }
#endif
        sims_initialized_ = false;
    }


    void GsSimInterface::SetSimSystemArmed(const bool is_armed) {
        boost::lock_guard<boost::mutex> lock(sim_arming_mutex_);

        // At the generic level, we'll just allow for setting and getting
        // the system-armed status even though this is a virtual interface
        GS_LOG_TRACE_MSG(trace, "GsSimInterface::SetSimSystemArmed called.");

        sim_system_is_armed_ = is_armed;
    }    

    bool GsSimInterface::GetSimSystemArmed() {
        boost::lock_guard<boost::mutex> lock(sim_arming_mutex_);

        return sim_system_is_armed_;
    }


    int GsSimInterface::SendSimMessage(const std::string& message) {
        GS_LOG_MSG(warning, "GsSimInterface::SendSimMessage - message was:\n" + message);
        return 0;
    }


    bool GsSimInterface::SendResultsToGolfSims(const GsResults& input_results) {

        // Make a local copy of the results so that we can set the shot_counter
        GsResults results = input_results;
        results.shot_number_ = shot_counter_;

        if (results.speed_mph_ > 200.0) {
            GS_LOG_MSG(warning, "GsSimInterface::SendResultsToGolfSim got out of bounds speed_mph.  Settting to 200.");
            results.speed_mph_ = 200.0;
        }

        bool status = true;

#ifdef __unix__  // Ignore in Windows environment

        // Loop through any interfaces that we are configured for and send the results
        for (auto interface : interfaces_) {
            if (interface == nullptr) {
                GS_LOG_MSG(error, "GsSimInterface::DeInitializeSims() found a null interface");
                continue;
            }

            interface->SendResults(results);
        }

#endif
        // Increment the shot counter even if there was a failure
        IncrementShotCounter();

        return status;
    }

    bool GsSimInterface::GetAllSystemsArmed() {
        bool all_systems_armed = true;

        // Loop through any interfaces that we are configured for and send the results
        for (auto interface : interfaces_) {
            if (interface == nullptr) {
                GS_LOG_MSG(error, "GsSimInterface::DeInitializeSims() found a null interface");
                continue;
            }

            // If even one interface is not armed, then we're not "all" ready
            if (!interface->GetSimSystemArmed()) {
                all_systems_armed = false;
            }
        }

        return all_systems_armed;
    }


    GsSimInterface* GsSimInterface::GetSimInterfaceByType(GolfSimulatorType sim_type) {

        // Loop through any interfaces that we are configured for and send the results
        for (auto interface : interfaces_) {
            if (interface == nullptr) {
                GS_LOG_MSG(error, "GsSimInterface::DeInitializeSims() found a null interface");
                continue;
            }

            if (interface->simulator_type_ == sim_type) {
                return interface;
            }
        }

        return nullptr;
    }


    void GsSimInterface::IncrementShotCounter() {
        shot_counter_++;
    }

    bool GsSimInterface::InterfaceIsPresent() {
        // The base interface isn't a real interface, so cannot be 'present'
        return false;
    }

    bool GsSimInterface::Initialize() {
        // The base interface isn't a real interface, so cannot be initialized
        return false;
    }

    void GsSimInterface::DeInitialize() {
        // The base interface isn't a real interface, so cannot be de-initializeds
        return;
    }

    bool GsSimInterface::SendResults(const GsResults& results) {
        GS_LOG_TRACE_MSG(trace, "GsSimInterface::SendResults - No Golf Sim connected to Launch Monitor.  Results are: " + results.Format());
        return true;
    }

    std::string GsSimInterface::GenerateResultsDataToSend(const GsResults& results) {
        return results.Format();
    }

    bool GsSimInterface::ProcessReceivedData(const std::string received_data) {
        GS_LOG_TRACE_MSG(trace, "GsSimInterface::ProcessReceivedData - No Golf Sim connected to Launch Monitor, so not doing anything with data.  Data was:\n" + received_data);
        return true;
    }

}

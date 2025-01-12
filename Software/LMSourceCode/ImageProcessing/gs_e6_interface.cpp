/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// "TruGolf Simulators" and other marks such as E6 may be trademarked by TruGolf, Inc.
// The PiTrac project is not endorsed, sponsored by or associated with TrueGolf products or services.


#include <iostream>

#ifdef __unix__  // Ignore in Windows environment

#include <pthread.h>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "logging_tools.h"
#include "gs_options.h"
#include "gs_config.h"
#include "gs_events.h"
#include "gs_ipc_control_msg.h"

#include "gs_e6_interface.h"
#include "gs_e6_results.h"
#include "gs_e6_response.h"

using namespace boost::asio;
using ip::tcp;


namespace golf_sim {

    long GsE6Interface::kE6InterMessageDelayMs = 50;

    GsE6Interface::GsE6Interface() {

        if (!GolfSimOptions::GetCommandLineOptions().e6_host_address_.empty()) {
            socket_connect_address_ = GolfSimOptions::GetCommandLineOptions().e6_host_address_;
        }
        else {
            GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.E6.kE6ConnectAddress", socket_connect_address_);
        }

        GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.E6.kE6ConnectPort", socket_connect_port_);
        GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.E6.kE6InterMessageDelayMs", kE6InterMessageDelayMs);
    }

    GsE6Interface::~GsE6Interface() {

    }

    bool GsE6Interface::InterfaceIsPresent() {
        // TBD - For now, just see if the JSON file has E6 information.
        // If it does, assume that the interface is present and has been selected
        // for use.
        std::string test_socket_connect_address;
        if (!GolfSimOptions::GetCommandLineOptions().e6_host_address_.empty()) {
            test_socket_connect_address = GolfSimOptions::GetCommandLineOptions().e6_host_address_;
            GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.E6.kE6ConnectAddress", test_socket_connect_address);
            return true;
        }
        else {
            GS_LOG_TRACE_MSG(trace, "GsE6Interface::InterfaceIsPresent - Not Present.");
            return false;
        }
        return (test_socket_connect_address != "");
    }


    bool GsE6Interface::Initialize() {
        // Setup the socket connect here first so
        // that we don't have to repeatedly do so.  May also want to
        // setup a keep-alive ping to the E6 system.
        GS_LOG_TRACE_MSG(trace, "GsE6Interface Initialize called.");

	// Get the connection address from the command line if possible
        if (!GolfSimOptions::GetCommandLineOptions().e6_host_address_.empty()) {
            socket_connect_address_ = GolfSimOptions::GetCommandLineOptions().e6_host_address_;
        }
        else {
            GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.E6.kE6ConnectAddress", socket_connect_address_);
        }

        GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.E6.kE6ConnectPort", socket_connect_port_);

        if (!GsSimSocketInterface::Initialize()) {
            GS_LOG_MSG(error, "GsE6Interface could not Initialize.");
            return false;
        }

        // Give the new thread a moment to get running
        usleep(500);

        initialized_ = true;

        const std::string kE6KHandshakeMessage = "{\"Type\":\"Handshake\"}";

        SendSimMessage(kE6KHandshakeMessage);

        // We should later receive a handshake message back

        return true;
    }


    void GsE6Interface::DeInitialize() {

        // Send disconnect message to TruGolf before we finish up
        std::string results_msg = "{\"Type\":\"Disconnect\"}";

        SendSimMessage(results_msg);

        GsSimSocketInterface::DeInitialize();
    }


    void GsE6Interface::SetSimSystemArmed(const bool is_armed) {
        boost::lock_guard<boost::mutex> lock(sim_arming_mutex_);

        GS_LOG_TRACE_MSG(trace, "GsE6Interface::SetSimSystemArmed called.");

        sim_system_is_armed_ = is_armed;
    }

    bool GsE6Interface::GetSimSystemArmed() {
        boost::lock_guard<boost::mutex> lock(sim_arming_mutex_);

        return sim_system_is_armed_;
    }


    bool GsE6Interface::SendResults(const GsResults& input_results) {

        GS_LOG_TRACE_MSG(trace, "GsE6Interface::SendResults called.");

        if (!initialized_) {
            GS_LOG_MSG(error, "GsE6Interface::SendResults called before the interface was intialized.");
            return false;
        }

        if (!GetSimSystemArmed()) {
            GS_LOG_MSG(warning, "GsE6Interface::SendResults called before the E6 system was armed.");
            return false;
        }

        if (receive_thread_exited_) {
            // If we ended the recieve thread, try re-initializing the connection

            GS_LOG_MSG(error, "GsGSProInterface::SendResults called before the interface was intialized.");

            DeInitialize();
            if (!Initialize()) {
                GS_LOG_MSG(error, "GsE6Interface::SendResults called before the interface was intialized.");
            return false;
            }
        }

        GsE6Results results(input_results);

        size_t write_length = -1;

        std::string results_msg = results.Format();

        GS_LOG_MSG(info, "Sending E6 shot results message:\n" + results_msg);
        write_length = SendSimMessage(results_msg);

        if (write_length <= 0) {
            GS_LOG_MSG(error, "GsE6Interface::SendResults was not able to send Ball Data.");
            return false;
        }

        // E6 also requires SendClubData and a SendShot messages along with the ball data
        // Give E6 a moment to proces the earlier message
        usleep(kE6InterMessageDelayMs * 1000);

        boost::property_tree::ptree root;
        root.put("Type", "SetClubData");

        // Create the three required children objects and add to the JSON root
        boost::property_tree::ptree club_data_child;

        // Create a dummy club data - we really don't have this information
        // Head speed is feet per second
        club_data_child.put("ClubHeadSpeed", GsResults::FormatDoubleAsString(0.0)); // (results.speed_mph_ / 3600.) * 5280.));
        club_data_child.put("ClubAngleFace", GsResults::FormatDoubleAsString(0.0));
        club_data_child.put("ClubAnglePath", GsResults::FormatDoubleAsString(0.0));
        club_data_child.put("ClubHeadSpeedMPH", GsResults::FormatDoubleAsString(0.0));  // results.speed_mph_));

        root.add_child("ClubData", club_data_child);

        std::string club_data_message = GsE6Results::GenerateStringFromJsonTree(root);

        if (club_data_message == "") {
            GS_LOG_MSG(warning, "GsE6Results::Format() returning empty string.");
        }

        write_length = SendSimMessage(club_data_message);

        if (write_length <= 0) {
            GS_LOG_MSG(error, "GsE6Interface::SendResults was not able to send Club Data.");
            return false;
        }

        // ShotData
        usleep(kE6InterMessageDelayMs * 1000);

        results_msg = "{\"Type\":\"SendShot\"}";

        write_length = SendSimMessage(results_msg);

        if (write_length <= 0) {
            GS_LOG_MSG(error, "GsE6Interface::SendResults was not able to send SendShot message.");
            return false;
        }

        // If we successfully sent a shot, we assume that E6 is no longer armed
        SetSimSystemArmed(false);

        GS_LOG_TRACE_MSG(trace, "Finished Sending E6 results input message:\n" + results.Format());

        return true;
    }


    std::string GsE6Interface::GenerateResultsDataToSend(const GsResults& input_results) {

        GsE6Results e6_results(input_results);

        std::string results_string = e6_results.Format();

        GS_LOG_TRACE_MSG(trace, "GsE6Interface::GenerateResultsDataToSend) returning:\n" + results_string);

        return results_string;
    }


    bool GsE6Interface::ProcessReceivedData(const std::string received_data) {

        GsE6Response e6_response;
        std::string e6_response_string;

        if (!e6_response.ProcessJson(received_data, e6_response_string)) {
            GS_LOG_MSG(error, "Failed GsE6Interface::ProcessReceivedData - Could not process json: " + received_data);
            return false;
        }

        if (e6_response_string == "") {
            // GS_LOG_TRACE_MSG(trace, "GsE6Interface::ProcessReceivedData had no response string to return to E6");
            return true;
        }
        else {
            GS_LOG_TRACE_MSG(trace, "GsE6Interface::ProcessReceivedData about to send response of: " + e6_response_string);

            size_t write_length = SendSimMessage(e6_response_string);

            if (write_length <= 0) {
                GS_LOG_MSG(error, "GsE6Interface::ProcessReceivedData failed to send date message: " + e6_response_string);
                return false;
            }
        }

        return true;
    }




}
#endif

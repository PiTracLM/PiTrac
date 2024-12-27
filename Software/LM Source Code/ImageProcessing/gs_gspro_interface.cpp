/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

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

#include "gs_gspro_interface.h"
#include "gs_gspro_response.h"
#include "gs_gspro_results.h"

using namespace boost::asio;
using ip::tcp;


namespace golf_sim {


    GsGSProInterface::GsGSProInterface() {
        GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress", socket_connect_address_);
        GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectPort", socket_connect_port_);
    }

    GsGSProInterface::~GsGSProInterface() {

    }

    bool GsGSProInterface::InterfaceIsPresent() {
        // TBD - For now, just see if the JSON file has GSPro information.
        // If it does, assume that the interface is present and has been selected
        // for use.
        std::string test_socket_connect_address;

        GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress", test_socket_connect_address);
        GS_LOG_TRACE_MSG(trace, "GsGSProInterface::InterfaceIsPresent - kGSProConnectAddress=" + test_socket_connect_address);
        return (test_socket_connect_address != "");
    }

        

    bool GsGSProInterface::Initialize() {
        // Setup the socket connect here first so
        // that we don't have to repeatedly do so.  May also want to
        // setup a keep-alive ping to the GSPro system.
        GS_LOG_TRACE_MSG(trace, "GsGSProInterface Initialize called.");

        GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress", socket_connect_address_);
        GolfSimConfiguration::SetConstant("gs_config.golf_simulator_interfaces.GSPro.kGSProConnectPort", socket_connect_port_);

        if (!GsSimSocketInterface::Initialize()) {
            GS_LOG_MSG(error, "GsGSProInterface could not Initialize.");
            return false;
        }

#ifdef __unix__  // Ignore in Windows environment
        // Give the new thread a moment to get running
        usleep(500);
#endif

        initialized_ = true;

        // Send an initial "I'm alive" message
        GsGSProResults keep_alive_results;
        keep_alive_results.result_message_is_keepalive_ = true;

        // TBD - Currently, it doesn't appear we get a response for a keep-alive ?
        SendResults(keep_alive_results);
        return true;
    }

    void GsGSProInterface::DeInitialize() {

        // TBD - Send disconnect message to TruGolf before we finish up
        /*
        results_msg = "{\"Type\":\"Disconnect\"}";

        SendSimMessage(results_msg);
        */
        GsSimSocketInterface::DeInitialize();
    }


    void GsGSProInterface::SetSimSystemArmed(const bool is_armed) {
        boost::lock_guard<boost::mutex> lock(sim_arming_mutex_);

        GS_LOG_TRACE_MSG(trace, "GsGSProInterface::SetSimSystemArmed called.");

        sim_system_is_armed_ = is_armed;
    }

    bool GsGSProInterface::GetSimSystemArmed() {
        boost::lock_guard<boost::mutex> lock(sim_arming_mutex_);

        // The GSPro system is always ready to receive shot information,
        // at least as far as we know
        return true;
    }

    bool GsGSProInterface::SendResults(const GsResults& input_results) {

        if (!initialized_) {
            GS_LOG_MSG(error, "GsGSProInterface::SendResults called before the interface was intialized.");
            return false;
        }

        if (receive_thread_exited_) {
            GS_LOG_MSG(error, "GsGSProInterface::SendResults called before the interface was intialized.");

            // If we ended the recieve thread, try re-initializing the connection
            DeInitialize();
            if (!Initialize()) {
                GS_LOG_MSG(error, "GsGSProInterface::SendResults called before the interface was intialized.");
            return false;
            }
        }

        GsGSProResults results(input_results);

        GS_LOG_TRACE_MSG(trace, "Sending GSPro results input message:\n" + results.Format());

        try {
            static std::array<char, 2000> buf;
            boost::system::error_code error;

            std::string results_msg = results.Format();

            size_t write_length = SendSimMessage(results_msg);
        }
        catch (std::exception& e)
        {
            GS_LOG_MSG(error, "Failed TestExternalSimMessage - Error was: " + std::string(e.what()));
            return false;
        }

        return true;
    }



    std::string GsGSProInterface::GenerateResultsDataToSend(const GsResults& input_results) {

        GsGSProResults gspro_results(input_results);
        return gspro_results.Format();
    }

    bool GsGSProInterface::ProcessReceivedData(const std::string received_data) {

        GsGSProResponse gspro_response;
        if (!gspro_response.ParseJson(received_data)) {
            GS_LOG_MSG(error, "Failed TestExternalSimMessage - Could not parse json: " + received_data);
            return false;
        }

        //  May need to enter a club-change control message
        if (gspro_response.return_code_ == GsGSProResponse::ReturnCode::kPlayerInformation) {
            GS_LOG_MSG(info, "Received GSPro kPlayerInformation Result of: \n" + gspro_response.Format());

            GsIPCControlMsgType club_instruction;

            if (gspro_response.player_club_ == GsGSProResponse::PlayerClub::kPutter) {
                club_instruction = GsIPCControlMsgType::kClubChangeToPutter;
            }
            else if (gspro_response.player_club_ == GsGSProResponse::PlayerClub::kDriver) {
                club_instruction = GsIPCControlMsgType::kClubChangeToDriver;
            }
            else {
                GS_LOG_MSG(warning, "Received GSPro unknown club information.  Player_club was: " + std::to_string((int)gspro_response.player_club_));
                club_instruction = GsIPCControlMsgType::kUnknown;
            }

            // The the instruction to switch clubs to the main FSM
            GolfSimEventElement control_message{ new GolfSimEvent::ControlMessage{ club_instruction } };
            GolfSimEventQueue::QueueEvent(control_message);
        }
        else {
            GS_LOG_MSG(info, "GsSimSocketInterface::ProcessReceivedData Received unknown GSPro result type.  Result was: \n" + gspro_response.Format());
        }

        return true;
    }




}
#endif

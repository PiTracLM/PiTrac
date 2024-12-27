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

    GsSimSocketInterface::GsSimSocketInterface() {
    }

    GsSimSocketInterface::~GsSimSocketInterface() {

    }

    bool GsSimSocketInterface::InterfaceIsPresent() {
        // The socket interface is basically just a base class, so cannot on it's own ber present
        GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface InterfaceIsPresent should not have been called.");
        return false;
    }

    bool GsSimSocketInterface::Initialize() {

        // Derived classes must set the socket connection address and port before calling this function

        // Setup the socket connect here first so
        // that we don't have to repeatedly do so.  May also want to
        // setup a keep-alive ping to the SimSocket system.
        GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface Initialize called.");

        try
        {
            io_context_ = new boost::asio::io_context();

            if (io_context_ == nullptr) {
                GS_LOG_MSG(error, "GsSimSocketInterface could not create a new io_context.");
                return false;
            }

            tcp::resolver resolver(*io_context_);
            GS_LOG_TRACE_MSG(trace, "Connecting to SimSocketServer at address: " + socket_connect_address_ + ":" + socket_connect_port_);
            tcp::resolver::results_type endpoints = resolver.resolve(socket_connect_address_, socket_connect_port_);

            // Create the socket if we haven't done so already
            if (socket_ == nullptr) {
                socket_ = new tcp::socket(*io_context_);

                if (socket_ == nullptr) {
                    GS_LOG_MSG(error, "GsSimSocketInterface could not create a new socket.");
                    return false;
                }
            }


            boost::asio::connect(*socket_, endpoints);

            receiver_thread_ = std::unique_ptr<std::thread>(new std::thread(&GsSimSocketInterface::ReceiveSocketData, this));

            // GS_LOG_TRACE_MSG(trace, "Thread was created.  Thread id: " + std::string(receiver_thread_.get()->get_id()) );

            // socket_->set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ 10 });
        }
        catch (std::exception& e)
        {
            GS_LOG_MSG(error, "Failed TestSimSocketMessage - Error was: " + std::string(e.what()));
            return false;
        }

#ifdef __unix__  // Ignore in Windows environment
        // Give the new thread a moment to get running
        usleep(500);
#endif

        initialized_ = true;

        // Derived classes will need to deal with any initial messaging after the socket is established.

        return true;
    }

    void GsSimSocketInterface::ReceiveSocketData() {

        receive_thread_exited_ = false;

        static std::array<char, 2000> buf;
        boost::system::error_code error;
        std::string received_data_string;

        while (GolfSimGlobals::golf_sim_running_)
        {
            // We don't want to re-enter this while we're processing
            // a received message
            // boost::lock_guard<boost::mutex> lock(sim_socket_receive_mutex_);

            GS_LOG_TRACE_MSG(trace, "Waiting to receive data from SimSocketserver.");

            size_t len = 0;

            try {
                // Read_some should be blocking, but if the socket has closed, it will return immediately
                len = socket_->read_some(boost::asio::buffer(buf), error);
            }
            catch (std::exception& e)
            {
                GS_LOG_MSG(error, "GsSimSocketInterface::ReceiveSocketData failed to read from socket - Error was: " + std::string(e.what()));
            }

            if (len == 0) {
                GS_LOG_MSG(warning, "Received 0-length message from server. Will attempt to re-initialize");
                /// TBD - Are we sure we want to exit?
                receive_thread_exited_ = true;
                return; 
            }

            // Null-terminate the string
            buf[len] = (char)0;
            received_data_string = std::string(buf.data());
            GS_LOG_TRACE_MSG(trace, "   Read some data (" + std::to_string(len) + " bytes) : " + received_data_string);

            if (error == boost::asio::error::eof) {
                // Connection closed cleanly by peer.  
                // In this case, we may want to de-initialize
                GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::ReceiveSocketData Received EOF");
                receive_thread_exited_ = true;
                return;
            }
            else if (error) {
                GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::ReceiveSocketData Received Error");
                throw boost::system::system_error(error); // Some other error.
            }
            // Derived classes will, for example, parse the message and inject any
            // relevant events into the FSM.

            GS_LOG_TRACE_MSG(trace, "Received SimSocket message of: \n" + received_data_string);

            if (!ProcessReceivedData(received_data_string)) {
                GS_LOG_MSG(error, "Failed GsSimSocketInterface::ReceiveSocketData - Could process data: " + received_data_string);
                return;
            }
        }

        GS_LOG_MSG(error, "GsSimSocketInterface::ReceiveSocketData Exiting");
    }

    void GsSimSocketInterface::DeInitialize() {

        GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::DeInitialize() called.");
        try {

            if (receiver_thread_ != nullptr) {
                /***  TBD - Was locking up
                GS_LOG_TRACE_MSG(trace, "Waiting for join of receiver_thread_.");
                receiver_thread_->join();
                receiver_thread_.release();
                delete receiver_thread_.get();
                */
                GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::DeInitialize() killing receive thread.");

#ifdef __unix__  // Ignore in Windows environment
                pthread_cancel(receiver_thread_.get()->native_handle());
#endif
                receiver_thread_ = nullptr;
            }

            // TBD - not sure how to deinitialize the TCP socket stuff
            delete socket_;
            socket_ = nullptr;
            delete io_context_;
            io_context_ = nullptr;

            GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::DeInitialize() completed.");
        }
        catch (std::exception& e)
        {
            GS_LOG_MSG(error, "Failed GsSimSocketInterface::DeInitialize() - Error was: " + std::string(e.what()));
        }

        initialized_ = false;
    }

    int GsSimSocketInterface::SendSimMessage(const std::string& message) {
        size_t write_length = 0;
        boost::system::error_code error;

        GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::SendSimMessage - Message was: " + message);

        // We don't want to re-enter this while we're processing
        // a received message
        boost::lock_guard<boost::mutex> lock(sim_socket_send_mutex_);


        try {

            write_length = socket_->write_some(boost::asio::buffer(message), error);
        }
        catch (std::exception& e)
        {
            GS_LOG_MSG(error, "Failed TestE6Message - Error was: " + std::string(e.what()) + ". Error code was:" + std::to_string(error.value()) );
            return -2;
        }

        return write_length;
    }


    bool GsSimSocketInterface::SendResults(const GsResults& results) {

        if (!initialized_) {
            GS_LOG_MSG(error, "GsSimSocketInterface::SendResults called before the interface was intialized.");
            return false;
        }

        if (receive_thread_exited_) {
            GS_LOG_MSG(error, "GsSimSocketInterface::SendResults called before the interface was intialized - trying to re-initialize.");
            // If we ended the recieve thread, try re-initializing the connection
            DeInitialize();
            if (!Initialize()) {
                GS_LOG_MSG(error, "GsSimSocketInterface::SendResults could not re-intialize thew interface.");
            return false;
            }
        }

        GS_LOG_TRACE_MSG(trace, "Sending GsSimSocketInterface::SendResult results input message:\n" + results.Format());

        size_t write_length = -1;

        try {
            static std::array<char, 2000> buf;
            boost::system::error_code error;

            std::string results_msg = GenerateResultsDataToSend(results);

            write_length = SendSimMessage(results_msg);
        }
        catch (std::exception& e)
        {
            GS_LOG_MSG(error, "Failed TestSimSocketMessage - Error was: " + std::string(e.what()));
            return false;
        }

        GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::SendResult sent " + std::to_string(write_length) + " bytes.");

        return true;
    }

    std::string GsSimSocketInterface::GenerateResultsDataToSend(const GsResults& results) {
        return results.Format();
    }

    bool GsSimSocketInterface::ProcessReceivedData(const std::string received_data) {
        GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::ProcessReceivedData - No Scoket-based Golf Sim connected to Launch Monitor, so not doing anything with data.  Data was:\n" + received_data);
        return true;
    }

}
#endif

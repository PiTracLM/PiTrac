/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include <iostream>

#ifdef __unix__ // Ignore in Windows environment

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <pthread.h>

#include "gs_config.h"
#include "gs_events.h"
#include "gs_ipc_control_msg.h"
#include "gs_options.h"
#include "logging_tools.h"

#include "gs_gspro_interface.h"
#include "gs_gspro_response.h"
#include "gs_gspro_results.h"
#include "gs_sim_socket_interface.h"

using namespace boost::asio;
using ip::tcp;

namespace golf_sim {

GsSimSocketInterface::GsSimSocketInterface() {}

GsSimSocketInterface::~GsSimSocketInterface() {}

bool GsSimSocketInterface::InterfaceIsPresent() {
  // The socket interface is basically just a base class, so cannot on it's own
  // ber present
  GS_LOG_TRACE_MSG(
      trace,
      "GsSimSocketInterface InterfaceIsPresent should not have been called.");
  return false;
}

bool GsSimSocketInterface::Initialize() {

  // Derived classes must set the socket connection address and port before
  // calling this function

  // Setup the socket connect here first so
  // that we don't have to repeatedly do so.  May also want to
  // setup a keep-alive ping to the SimSocket system.
  GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface Initialize called.");

  // Treat empty/disabled config as disabled
  if (socket_connect_address_.empty() ||
      socket_connect_address_ == "disabled" || socket_connect_port_.empty() ||
      socket_connect_port_ == "0") {
    SetConnectionState(SimConnState::kDisabled, "No sim host/port configured");
    GS_LOG_MSG(info, "Sim socket disabled (no host/port configured).");
    return false; // InterfaceIsPresent() should prevent this being called
                  // anyway
  }

  SetConnectionState(SimConnState::kConnecting);
  GS_LOG_MSG(info, "Connecting to SimSocketServer at: " +
                       socket_connect_address_ + ":" + socket_connect_port_);

  try {
    io_context_ = new boost::asio::io_context();

    if (io_context_ == nullptr) {
      GS_LOG_MSG(error,
                 "GsSimSocketInterface could not create a new io_context.");
      return false;
    }

    tcp::resolver resolver(*io_context_);
    GS_LOG_TRACE_MSG(trace, "Connecting to SimSocketServer at address: " +
                                socket_connect_address_ + ":" +
                                socket_connect_port_);
    tcp::resolver::results_type endpoints =
        resolver.resolve(socket_connect_address_, socket_connect_port_);

    // Create the socket if we haven't done so already
    if (socket_ == nullptr) {
      socket_ = new tcp::socket(*io_context_);

      if (socket_ == nullptr) {
        GS_LOG_MSG(error,
                   "GsSimSocketInterface could not create a new socket.");
        return false;
      }
    }

    boost::asio::connect(*socket_, endpoints);

    SetConnectionState(SimConnState::kConnected);
    GS_LOG_MSG(info, "Connected to SimSocketServer at: " +
                         socket_connect_address_ + ":" + socket_connect_port_);

    receiver_thread_ = std::unique_ptr<std::thread>(
        new std::thread(&GsSimSocketInterface::ReceiveSocketData, this));

    // GS_LOG_TRACE_MSG(trace, "Thread was created.  Thread id: " +
    // std::string(receiver_thread_.get()->get_id()) );

    // socket_->set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET,
    // SO_RCVTIMEO>{ 10 });
  } catch (std::exception &e) {
    SetConnectionState(SimConnState::kError, e.what());
    GS_LOG_MSG(error, "Failed TestSimSocketMessage - Error was: " +
                          std::string(e.what()));
    return false;
  }

#ifdef __unix__ // Ignore in Windows environment
  // Give the new thread a moment to get running
  usleep(500);
#endif

  initialized_ = true;

  // Connection just came up – make sure the first heartbeat reports no ball
  // detected.
  GsSimInterface::ResetHeartbeatState();
  GsSimInterface::SendHeartbeat(false);

  // Derived classes will need to deal with any initial messaging after the
  // socket is established.

  return true;
}

void GsSimSocketInterface::ReceiveSocketData() {
  receive_thread_exited_ = false;

  std::array<char, 2001> buf{}; // 2000 bytes payload + null terminator
  boost::system::error_code error;
  std::string received_data_string;

  while (GolfSimGlobals::golf_sim_running_) {
    GS_LOG_TRACE_MSG(trace, "Waiting to receive data from SimSocketserver.");

    size_t len = 0;

    try {
      // IMPORTANT: read at most (size-1) so we can always null-terminate
      len = socket_->read_some(boost::asio::buffer(buf.data(), buf.size() - 1),
                               error);
    } catch (const std::exception &e) {
      GS_LOG_MSG(error, "GsSimSocketInterface::ReceiveSocketData failed to "
                        "read from socket - Error was: " +
                            std::string(e.what()));
      SetConnectionState(SimConnState::kError, e.what());
      receive_thread_exited_ = true;
      return;
    }

    if (error == boost::asio::error::eof) {
      GS_LOG_TRACE_MSG(trace,
                       "GsSimSocketInterface::ReceiveSocketData Received EOF");
      SetConnectionState(SimConnState::kDisconnected, "EOF");
      receive_thread_exited_ = true;
      return;
    }

    if (error) {
      GS_LOG_MSG(error, "Sim socket receive error: " + error.message());
      SetConnectionState(SimConnState::kError, error.message());
      receive_thread_exited_ = true;
      return;
    }

    if (len == 0) {
      GS_LOG_MSG(warning, "Received 0-length message from server.");
      SetConnectionState(SimConnState::kDisconnected, "0-length read");
      receive_thread_exited_ = true;
      return;
    }

    // Null-terminate and build string
    buf[len] = '\0';
    received_data_string.assign(buf.data(), len);

    GS_LOG_TRACE_MSG(trace, "   Read some data (" + std::to_string(len) +
                                " bytes) : " + received_data_string);
    GS_LOG_TRACE_MSG(trace, "Received SimSocket message of: \n" +
                                received_data_string);

    if (!ProcessReceivedData(received_data_string)) {
      GS_LOG_MSG(error, "ProcessReceivedData failed");
      SetConnectionState(SimConnState::kError, "ProcessReceivedData failed");
      receive_thread_exited_ = true;
      return;
    }
  }

  GS_LOG_MSG(info, "GsSimSocketInterface::ReceiveSocketData Exiting");
}

void GsSimSocketInterface::DeInitialize() {
  SetConnectionState(SimConnState::kDisconnected, "DeInitialize()");
  GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::DeInitialize() called.");
  try {

    if (receiver_thread_ != nullptr) {
      /***  TBD - Was locking up
      GS_LOG_TRACE_MSG(trace, "Waiting for join of receiver_thread_.");
      receiver_thread_->join();
      receiver_thread_.release();
      delete receiver_thread_.get();
      */
      GS_LOG_TRACE_MSG(
          trace,
          "GsSimSocketInterface::DeInitialize() killing receive thread.");

#ifdef __unix__ // Ignore in Windows environment
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
  } catch (std::exception &e) {
    GS_LOG_MSG(error,
               "Failed GsSimSocketInterface::DeInitialize() - Error was: " +
                   std::string(e.what()));
  }

  initialized_ = false;
}

int GsSimSocketInterface::SendSimMessage(const std::string &message) {
  size_t write_length = 0;
  boost::system::error_code error;

  if (!socket_) {
    SetConnectionState(SimConnState::kDisconnected,
                       "socket_ was null on write");
    return -1;
  }

  GS_LOG_TRACE_MSG(
      trace, "GsSimSocketInterface::SendSimMessage - Message was: " + message);

  // We don't want to re-enter this while we're processing
  // a received message
  boost::lock_guard<boost::mutex> lock(sim_socket_send_mutex_);

  try {

    write_length = socket_->write_some(boost::asio::buffer(message), error);

    if (error) {
      SetConnectionState(SimConnState::kError, error.message());
      receive_thread_exited_ = true; // forces reconnect path in SendResults
      GS_LOG_MSG(error, "Sim socket write failed: " + error.message());
      return -2;
    }
  } catch (std::exception &e) {
    SetConnectionState(SimConnState::kError, e.what());
    receive_thread_exited_ = true;
    GS_LOG_MSG(error,
               "Failed TestE6Message - Error was: " + std::string(e.what()) +
                   ". Error code was:" + std::to_string(error.value()));
    return -2;
  }

  return write_length;
}

bool GsSimSocketInterface::SendResults(const GsResults &results) {

  if (!initialized_) {
    GS_LOG_MSG(error, "GsSimSocketInterface::SendResults called before the "
                      "interface was intialized.");
    return false;
  }

  if (receive_thread_exited_) {
    GS_LOG_MSG(error, "GsSimSocketInterface::SendResults called before the "
                      "interface was intialized - trying to re-initialize.");
    // If we ended the receive thread, try re-initializing the connection
    DeInitialize();
    if (!Initialize()) {
      GS_LOG_MSG(error, "GsSimSocketInterface::SendResults could not "
                        "re-intialize thew interface.");
      return false;
    }
  }

  GS_LOG_TRACE_MSG(
      trace,
      "Sending GsSimSocketInterface::SendResult results input message:\n" +
          results.Format());

  size_t write_length = -1;

  try {
    static std::array<char, 2000> buf;
    boost::system::error_code error;

    std::string results_msg = GenerateResultsDataToSend(results);

    write_length = SendSimMessage(results_msg);
  } catch (std::exception &e) {
    GS_LOG_MSG(error, "Failed TestSimSocketMessage - Error was: " +
                          std::string(e.what()));
    return false;
  }

  GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::SendResult sent " +
                              std::to_string(write_length) + " bytes.");

  return true;
}

std::string
GsSimSocketInterface::GenerateResultsDataToSend(const GsResults &results) {
  return results.Format();
}

bool GsSimSocketInterface::ProcessReceivedData(
    const std::string received_data) {
  GS_LOG_TRACE_MSG(trace, "GsSimSocketInterface::ProcessReceivedData - No "
                          "Scoket-based Golf Sim connected to Launch Monitor, "
                          "so not doing anything with data.  Data was:\n" +
                              received_data);
  return true;
}

} // namespace golf_sim
#endif

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <memory>
#include <string>
#include <thread>

#include "gs_results.h"
#include "gs_sim_interface.h"

using namespace boost::asio;
using ip::tcp;

// Base class for representing and transferring Golf Sim results over sockets

namespace golf_sim {

enum class SimConnState {
  kDisabled = 0,
  kDisconnected,
  kConnecting,
  kConnected,
  kError
};

class GsSimSocketInterface : public GsSimInterface {

public:
  GsSimSocketInterface();
  virtual ~GsSimSocketInterface();

  // Returns true iff the SimSocket interface is to be used
  static bool InterfaceIsPresent();

  // Must be called before SendResults is called.
  virtual bool Initialize();

  // Deals with, for example, shutting down any socket connection
  virtual void DeInitialize();

  virtual bool SendResults(const GsResults &results);

  virtual void ReceiveSocketData();

  // ---- connection state API ----
  SimConnState GetConnectionState() const {
    return connection_state_.load(std::memory_order_relaxed);
  }

  bool IsConnected() const {
    return GetConnectionState() == SimConnState::kConnected;
  }

  std::string GetLastConnectionError() const {
    boost::lock_guard<boost::mutex> lock(conn_state_mutex_);
    return last_connection_error_;
  }

public:
  std::string socket_connect_address_;
  std::string socket_connect_port_;

protected:
  virtual std::string GenerateResultsDataToSend(const GsResults &results);

  virtual bool ProcessReceivedData(const std::string received_data);

  // Default behavior here is just to send the message to the socket and
  // return the number of bytes written
  virtual int SendSimMessage(const std::string &message);

  // ---- state transition helper + hook ----
  void SetConnectionState(SimConnState s, const std::string &reason = "") {
    SimConnState prev =
        connection_state_.exchange(s, std::memory_order_relaxed);

    {
      boost::lock_guard<boost::mutex> lock(conn_state_mutex_);

      // Clear stale error when we move into healthy-ish states
      if (s == SimConnState::kConnected || s == SimConnState::kConnecting) {
        last_connection_error_.clear();
      }

      // Store reason when provided (works for any state)
      if (!reason.empty()) {
        last_connection_error_ = reason;
      }
    }

    if (prev != s || !reason.empty()) {
      OnConnectionStateChanged(prev, s, reason);
    }
  }

  // Derived classes can override to publish to ActiveMQ / UI / logs
  virtual void OnConnectionStateChanged(SimConnState /*from*/,
                                        SimConnState /*to*/,
                                        const std::string & /*reason*/) {}

protected:
  tcp::socket *socket_ = nullptr;
  boost::asio::io_context *io_context_ = nullptr;

  std::unique_ptr<std::thread> receiver_thread_ = nullptr;

  // TBD - Is this thread safe?
  bool receive_thread_exited_ = false;

  boost::mutex sim_socket_receive_mutex_;
  boost::mutex sim_socket_send_mutex_;

  // ---- state storage ----
  std::atomic<SimConnState> connection_state_{SimConnState::kDisconnected};
  mutable boost::mutex conn_state_mutex_;
  std::string last_connection_error_;
};

} // namespace golf_sim

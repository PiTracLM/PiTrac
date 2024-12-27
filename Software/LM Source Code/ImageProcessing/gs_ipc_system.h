/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// This object handles sending and receiving IPC messages.

#pragma once

#ifdef __unix__  // Ignore in Windows environment


#include <activemq/library/ActiveMQCPP.h>
#include <cms/BytesMessage.h>
#include <cms/BytesMessage.h>


#include "gs_events.h"
#include "gs_message_consumer.h"
#include "gs_message_producer.h"
#include "gs_ipc_message.h"

namespace golf_sim {

	class GolfSimIpcSystem {

	public:

		static const int kIpcLoopIntervalMs = 2000;
		static std::string kWebActiveMQHostAddress;

		static std::string kActiveMQLMIdProperty;

		// Properties (and their potential values) that will be sent within
		// ActiveMQ messages.
		static const std::string kGolfSimMessageTypeTag;
		static const std::string kGolfSimMessageType;
		static const std::string kGolfSimIPCMessageTypeTag;

		static bool DispatchReceivedIpcMessage(const BytesMessage& message);
		static bool SendIpcMessage(const GolfSimIPCMessage& ipc_message);

		static GolfSimIPCMessage* BuildIpcMessageFromBytesMessage(const BytesMessage& active_mq_message);

		static std::unique_ptr<cms::BytesMessage> BuildBytesMessageObjectFromIpcMessage(const GolfSimIPCMessage& ipc_message);

		static bool InitializeIPCSystem();
		static bool ShutdownIPCSystem();

		// The following methods deal with what to do with received IPC messages and are called
		// from the main DispatchReceivedIpcMessage method.
		static bool DispatchRequestForCamera2ImageMessage(const GolfSimIPCMessage& message);
		static bool DispatchCamera2ImageMessage(const GolfSimIPCMessage& message);
		static bool DispatchCamera2PreImageMessage(const GolfSimIPCMessage& message);
		static bool DispatchShutdownMessage(const GolfSimIPCMessage& message);
		static bool DispatchRequestForCamera2TestStillImage(const GolfSimIPCMessage& message);
		static bool DispatchResultsMessage(const GolfSimIPCMessage& message);
		static bool DispatchControlMsgMessage(const GolfSimIPCMessage& message);	


		static bool SimulateCamera2ImageMessage();
	private:
		static GolfSimMessageConsumer* consumer_;
		static GolfSimMessageProducer* producer_;
	};

}

#endif // #ifdef __unix__  // Ignore in Windows environment

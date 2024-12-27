/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

 // Handles the interface to the ActiveMQ system from the producer (i.e., message sending from the LM) side


#pragma once

#ifdef __unix__  // Ignore in Windows environment

#include <activemq/library/ActiveMQCPP.h>
#include <decaf/lang/Thread.h>
#include <decaf/lang/Runnable.h>
#include <decaf/util/concurrent/CountDownLatch.h>
#include <decaf/lang/Integer.h>
#include <decaf/lang/Long.h>
#include <decaf/lang/System.h>
#include <activemq/core/ActiveMQConnectionFactory.h>
#include <activemq/util/Config.h>
#include <cms/Connection.h>
#include <cms/Session.h>
#include <cms/TextMessage.h>
#include <cms/BytesMessage.h>
#include <cms/MapMessage.h>
#include <cms/ExceptionListener.h>
#include <cms/MessageListener.h>

#include "gs_ipc_message.h"
#include "logging_tools.h"


using namespace activemq::core;
using namespace decaf::util::concurrent;
using namespace decaf::util;
using namespace decaf::lang;
using namespace cms;
using namespace std;

namespace golf_sim {

    // TBD - Consider if we want all of this to be static, or allow for multiple listeners?

    class GolfSimMessageProducer : public Runnable {

    private:

        Connection* connection;
        Session* session_;
        Destination* destination;
        MessageProducer* producer_;
        long waitMillis;
        bool useTopic;
        bool sessionTransacted;
        std::string brokerURI;

    private:

        GolfSimMessageProducer(const GolfSimMessageProducer&);
        GolfSimMessageProducer& operator=(const GolfSimMessageProducer&);

    public:

        // Creaters andtarts the listener (consumer) messaging thread.
        // Acts as a factory
        static GolfSimMessageProducer* Initialize(std::string& broker_URI);

        bool Shutdown();

        GolfSimMessageProducer(const std::string& brokerURI, bool useTopic = false, bool sessionTransacted = false);

        virtual ~GolfSimMessageProducer();

        bool SendMessage(BytesMessage* message);

        void close();

        virtual void run();

        // Returns a new BytesMessage that can be used to send
        std::unique_ptr<cms::BytesMessage> getNewBytesMessage();

    private:

        void onException(const CMSException& ex AMQCPP_UNUSED);
        void cleanup();

        Thread* producer_thread_ = nullptr;
    };


}

#endif // #ifdef __unix__  // Ignore in Windows environment

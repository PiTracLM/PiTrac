/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


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
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <memory>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>

#include "gs_globals.h"
#include "gs_options.h"
#include "logging_tools.h"

#include "gs_ipc_message.h"
#include "gs_ipc_system.h"

#include "gs_message_consumer.h"


using namespace activemq::core;
using namespace decaf::util::concurrent;
using namespace decaf::util;
using namespace decaf::lang;
using namespace cms;
using namespace std;

namespace golf_sim {


    GolfSimMessageConsumer::GolfSimMessageConsumer(const std::string& brokerURI, 
                                                    bool useTopic, 
                                                    bool sessionTransacted, 
                                                    int waitMillis) :
        latch(1),
        doneLatch(1),
        connection(nullptr),
        session_(nullptr),
        destination(nullptr),
        consumer_(nullptr),
        waitMillis(waitMillis),
        useTopic(useTopic),
        sessionTransacted(sessionTransacted),
        brokerURI(brokerURI) {
    }

    GolfSimMessageConsumer::~GolfSimMessageConsumer() {
        cleanup();
    }

    void GolfSimMessageConsumer::close() {
        this->cleanup();
    }

    void GolfSimMessageConsumer::waitUntilReady() {
        latch.await();
    }

    void GolfSimMessageConsumer::run() {

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageConsumer::run called.");

        try {

            // Create a ConnectionFactory
            unique_ptr<ConnectionFactory> connectionFactory(
                ConnectionFactory::createCMSConnectionFactory(brokerURI));

            // Create a Connection
            // We may be sending some big images inside messages, so compress if we can
            connection = connectionFactory->createConnection();
            connection->start();
            connection->setExceptionListener(this);

            GS_LOG_TRACE_MSG(trace, "GolfSimMessageConsumer - connection was started.");


            // Create a Session
            if (this->sessionTransacted == true) {
                session_ = connection->createSession(Session::SESSION_TRANSACTED);
            } else {
                session_ = connection->createSession(Session::AUTO_ACKNOWLEDGE);
            }

            // Create the destination (Topic or Queue)
            if (useTopic) {
                destination = session_->createTopic("Golf.Sim");
            } else {
                destination = session_->createQueue("Golf.Sim");
            }

            // We don't want our own messages fed back to us, so exclude them
            std::string system_id_to_exclude;

            if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera1) {
                system_id_to_exclude = "LM_1";
            }
            else {
                system_id_to_exclude = "LM_2";
            }

            std::string selector = GolfSimIpcSystem::kActiveMQLMIdProperty + " <> '" + system_id_to_exclude + "'";

            GS_LOG_TRACE_MSG(trace, "GolfSimMessageConsumer message selector: " + selector);

            // Create a MessageConsumer from the Session to the Topic or Queue
            consumer_ = session_->createConsumer(destination, selector);

            consumer_->setMessageListener(this);

            std::cout.flush();
            std::cerr.flush();

            // Indicate we are ready for messages.
            latch.countDown();

            // Wait while asynchronous messages come in.

            // Loop around, but check every so often to ensure things are still running
            while (GolfSimGlobals::golf_sim_running_) {
                doneLatch.await(waitMillis);
            }

        } catch (CMSException& e) {
            GS_LOG_TRACE_MSG(warning, "GolfSimMessageConsumer::run failed.");
            latch.countDown();
            e.printStackTrace();
        }

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageConsumer::run ended.");
    }



    // Called from the consumer since this class is a registered MessageListener.
    void GolfSimMessageConsumer::onMessage(const Message* message) {

        static int count = 0;

        try {
            count++;
            // TBD - should the bytesMessage be const?
            const BytesMessage* bytesMessage = dynamic_cast<const BytesMessage*> (message);

            if (bytesMessage != NULL) {
                int message_length = bytesMessage->getBodyLength();
                std::string system_id = bytesMessage->getStringProperty(GolfSimIpcSystem::kActiveMQLMIdProperty);

                GS_LOG_TRACE_MSG(trace, "IPC Message Received with bytes size " + std::to_string(message_length) + ", SystemID: " + system_id);

                if (!GolfSimIpcSystem::DispatchReceivedIpcMessage(*bytesMessage)) {
                    GS_LOG_MSG(error, "Could not GolfSimIpcSystem::DispatchReceivedIpcMessage.");
                }
            } else {
                LoggingTools::Warning("Received unexpected type of IPC message.  Ignoring");
            }

        } catch (CMSException& e) {
            e.printStackTrace();
        }

        // Commit all messages.
        if (this->sessionTransacted) {
            session_->commit();
        }

    }

    // If something bad happens you see it here as this class is also been
    // registered as an ExceptionListener with the connection.
    void GolfSimMessageConsumer::onException(const CMSException& ex AMQCPP_UNUSED) {
        GS_LOG_TRACE_MSG(trace, "CMS Exception occurred.  Shutting down client.");
        ex.printStackTrace();
        exit(1);
    }

    void GolfSimMessageConsumer::cleanup() {

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageConsumer::cleanup");

        if (connection != NULL) {
            try {
                connection->close();
            } catch (cms::CMSException& ex) {
                ex.printStackTrace();
            }
        }

        // Destroy resources.
        try {
            if (destination != nullptr) {
                delete destination;
                destination = nullptr;
            }
            if (consumer_ != nullptr) {
                delete consumer_;
                consumer_ = nullptr;
            }
            if (session_ != nullptr) {
                delete session_;
                session_ = nullptr;
            }
            if (connection != nullptr) {
                delete connection;
                connection = nullptr;
            }

        } catch (CMSException& e) {
            e.printStackTrace();
        }
    }

    
    GolfSimMessageConsumer* GolfSimMessageConsumer::Initialize(std::string& broker_URI) {

        GS_LOG_TRACE_MSG(info, "GolfSimMessageConsumer::Initialize called with broker_URI = " + broker_URI);

        //============================================================
        // set to true to use topics instead of queues
        // Note in the code above that this causes createTopic or
        // createQueue to be used in both consumer an producer.
        //============================================================
        bool useTopics = true;
        bool sessionTransacted = false;

        GolfSimMessageConsumer* listener = new GolfSimMessageConsumer(broker_URI, useTopics, sessionTransacted, GolfSimIpcSystem::kIpcLoopIntervalMs);

        if (listener == nullptr) {
            GS_LOG_MSG(error, "could not create an IPC GolfSimMessageConsumer.");
            return nullptr;
        }

        // Start the consumer thread and assign to the new listener instance.
        listener->consumer_thread_ = new Thread(listener);

        if (listener->consumer_thread_ == nullptr) {
            GS_LOG_MSG(error, "Could not create an IPC GolfSimMessageConsumer Thread.");
            return nullptr;
        }

        listener->consumer_thread_->start();

        // Wait for the consumer to indicate that it's ready to go.
        listener->waitUntilReady();

        // Wait for the producer to get started up
        sleep(GolfSimIpcSystem::kIpcLoopIntervalMs / 1000);

        // At this point, the listener/watcher thread will just keep
        // running until something tells it to quit.

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageConsumer::Initialize ready.");

        return listener;
    }

    bool GolfSimMessageConsumer::Shutdown() {
        GS_LOG_TRACE_MSG(trace, "GolfSimMessageConsumer::Shutdown called.");

        // If the consumer's run() function is waiting on the done latch, mark that
        // latch as finished.
        doneLatch.countDown();

        // Wait for the thread loops to have a chance to shutdown
        // TBD - Figure out a cleaner way of doing this.
        sleep(GolfSimIpcSystem::kIpcLoopIntervalMs / 500);

        // Execution will continue here after the listener stops
        // Wait for the threads to complete.
        GS_LOG_TRACE_MSG(trace, "GolfSimMessageConsumer::consumer_thread_ closing.  Waiting for join...");
        if (consumer_thread_ != nullptr) {
            consumer_thread_->join();
        }

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageConsumer::consumer_ closing.");

        if (consumer_ != nullptr) {
            consumer_->close();
        }

        GS_LOG_TRACE_MSG(trace, "Returning from GolfSimMessageConsumer::Shutdown.");

        return true;
    }

}

#endif // #ifdef __unix__  // Ignore in Windows environment

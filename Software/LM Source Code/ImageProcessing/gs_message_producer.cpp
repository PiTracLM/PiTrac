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
#include "gs_ipc_system.h"
#include "logging_tools.h"


#include "gs_message_producer.h"


using namespace activemq::core;
using namespace decaf::util::concurrent;
using namespace decaf::util;
using namespace decaf::lang;
using namespace cms;
using namespace std;

namespace golf_sim {

    GolfSimMessageProducer::GolfSimMessageProducer(const std::string& brokerURI, 
                                                    bool useTopic, 
                                                    bool sessionTransacted) :
        connection(nullptr),
        session_(nullptr),
        destination(nullptr),
        producer_(nullptr),
        useTopic(useTopic),
        sessionTransacted(sessionTransacted),
        brokerURI(brokerURI) 
    {
    }

    GolfSimMessageProducer::~GolfSimMessageProducer() {
        cleanup();
    }

    void GolfSimMessageProducer::close() {
        this->cleanup();
    }

    void GolfSimMessageProducer::run() {

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageProducer::run called.");

        try {

            // Create a ConnectionFactory
            unique_ptr<ConnectionFactory> connectionFactory(
                ConnectionFactory::createCMSConnectionFactory(brokerURI));

            // Create a Connection
            connection = connectionFactory->createConnection();
            connection->start();

            GS_LOG_TRACE_MSG(trace, "GolfSimMessageProducer - connection was started.");

            // Create a Session
            if (this->sessionTransacted) {
                session_ = connection->createSession(Session::SESSION_TRANSACTED);
            }
            else {
                session_ = connection->createSession(Session::AUTO_ACKNOWLEDGE);
            }

            // Create the destination (Topic or Queue)
            if (useTopic) {
                destination = session_->createTopic("Golf.Sim");
            }
            else {
                destination = session_->createQueue("Golf.Sim");
            }

            // Create a MessageProducer from the Session to the Topic or Queue
            producer_ = session_->createProducer(destination);
            producer_->setDeliveryMode(DeliveryMode::NON_PERSISTENT);

            // The producer should be ready to send messagers
        }
        catch (CMSException& e) {
            e.printStackTrace();
        }

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageProducer::run ended.");

    }


    // If something bad happens you see it here as this class is also been
    // registered as an ExceptionListener with the connection.
    void GolfSimMessageProducer::onException(const CMSException& ex AMQCPP_UNUSED) {
        GS_LOG_TRACE_MSG(trace, "CMS Exception occurred.  Shutting down client.");
        ex.printStackTrace();
        exit(1);
    }

    void GolfSimMessageProducer::cleanup() {

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageProducer::cleanup");

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
            if (producer_ != nullptr) {
                delete producer_;
                producer_ = nullptr;
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

    
    GolfSimMessageProducer* GolfSimMessageProducer::Initialize(std::string& broker_URI) {

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageProducer::Initialize called with broker_URI = " + broker_URI);

        //============================================================
        // set to true to use topics instead of queues
        // Note in the code above that this causes createTopic or
        // createQueue to be used in both consumer an producer.
        //============================================================
        bool useTopics = true;
        bool sessionTransacted = false;

        GolfSimMessageProducer* producer = new GolfSimMessageProducer(broker_URI, useTopics, sessionTransacted);

        if (producer == nullptr) {
            GS_LOG_MSG(error, "could not create an IPC GolfSimMessageProducer.");
            return nullptr;
        }

        // Start the consumer thread and assign to the new producer instance.
        producer->producer_thread_ = new Thread(producer);

        if (producer->producer_thread_ == nullptr) {
            GS_LOG_MSG(error, "Could not create an IPC GolfSimMessageProducer Thread.");
            return nullptr;
        }

        producer->producer_thread_->start();

        // At this point, the producer/watcher thread will just keep
        // running until something tells it to quit.

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageProducer::Initialize ready.");

        return producer;
    }

    bool GolfSimMessageProducer::Shutdown() {
        GS_LOG_TRACE_MSG(trace, "GolfSimMessageProducer::Shutdown called.  Waiting for join...");

        // Execution will continue here after the listener stops
        // Wait for the threads to complete.
        if (producer_thread_ != nullptr) {
            producer_thread_->join();
        }

        if (producer_ != nullptr) {
            producer_->close();
        }

        GS_LOG_TRACE_MSG(trace, "Returning from GolfSimMessageProducer::Shutdown.");

        return true;
    };

    std::unique_ptr<cms::BytesMessage> GolfSimMessageProducer::getNewBytesMessage() {

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageProducer::getNewBytesMessage called.");

        if (session_ == nullptr) {
            GS_LOG_TRACE_MSG(trace, "Attempt to getNewBytesMessage before the session was established.");
            return nullptr;
        }

        std::unique_ptr<BytesMessage> new_msg(session_->createBytesMessage());

        return new_msg;
    }

    bool GolfSimMessageProducer::SendMessage(BytesMessage* message) {

        if (message == nullptr) {
            GS_LOG_TRACE_MSG(trace, "Attempt to SendMessage a nullptr message.");
            return false;
        }

        if (producer_ == nullptr) {
            GS_LOG_TRACE_MSG(trace, "Attempt to SendMessage before producer_ was initialized.");
            return false;
        }

        std::string system_id;

        // Ensure we identify who we are so that we can avoid getting our own
        // messages reflected back to su (and chewing up time + bandwidth)
        if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera1) {
            system_id = "LM_1";
        }
        else {
            system_id = "LM_2";
        }

        GS_LOG_TRACE_MSG(trace, "GolfSimMessageProducer system_id: " + system_id);

        message->setStringProperty(GolfSimIpcSystem::kActiveMQLMIdProperty, system_id);

        producer_->send(message);

        return true;
    }
}

#endif // #ifdef __unix__  // Ignore in Windows environment

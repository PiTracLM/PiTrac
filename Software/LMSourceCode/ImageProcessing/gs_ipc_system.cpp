/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#ifdef __unix__  // Ignore in Windows environment

#include "gs_globals.h"
#include "logging_tools.h"

#include "gs_ipc_message.h"
#include "gs_options.h"
#include "gs_config.h"
#include "gs_ipc_system.h"

#include "gs_message_consumer.h"
#include "gs_message_producer.h"


using namespace activemq::core;
using namespace decaf::util::concurrent;
using namespace decaf::util;
using namespace decaf::lang;
using namespace cms;
using namespace std;

namespace golf_sim {

    std::string GolfSimIpcSystem::kWebActiveMQHostAddress = "tcp://10.0.0.41:61616";

    const std::string GolfSimIpcSystem::kGolfSimMessageTypeTag = "Message Type";
    const std::string GolfSimIpcSystem::kGolfSimMessageType = "GolfSimIPCMessage";
    const std::string GolfSimIpcSystem::kGolfSimIPCMessageTypeTag = "IPCMessageType";

    GolfSimMessageConsumer* GolfSimIpcSystem::consumer_ = nullptr;
    GolfSimMessageProducer* GolfSimIpcSystem::producer_ = nullptr;

    std::string GolfSimIpcSystem::kActiveMQLMIdProperty = "LM_System_ID";


    bool GolfSimIpcSystem::InitializeIPCSystem() {

        GolfSimConfiguration::SetConstant("gs_config.ipc_interface.kWebActiveMQHostAddress", kWebActiveMQHostAddress);

        activemq::library::ActiveMQCPP::initializeLibrary();


        // Set the URI to point to the IP Address of your broker.
        // add any optional params to the url to enable things like
        // tightMarshalling or tcp logging etc.  See the CMS web site for
        // a full list of configuration options.
        //
        //  http://activemq.apache.org/cms/
        //
        // Wire Format Options:
        // =========================
        // Use either stomp or openwire, the default ports are different for each
        //
        // Examples:
        //    tcp://127.0.0.1:61616                      default to openwire
        //    tcp://127.0.0.1:61616?wireFormat=openwire  same as above
        //    tcp://127.0.0.1:61613?wireFormat=stomp     use stomp instead
        //
        // SSL:
        // =========================
        // To use SSL you need to specify the location of the trusted Root CA or the
        // certificate for the broker you want to connect to.  Using the Root CA allows
        // you to use failover with multiple servers all using certificates signed by
        // the trusted root.  If using client authentication you also need to specify
        // the location of the client Certificate.
        //
        //     System::setProperty( "decaf.net.ssl.keyStore", "<path>/client.pem" );
        //     System::setProperty( "decaf.net.ssl.keyStorePassword", "password" );
        //     System::setProperty( "decaf.net.ssl.trustStore", "<path>/rootCA.pem" );
        //
        // The you just specify the ssl transport in the URI, for example:
        //
        //     ssl://localhost:61617
        //
        std::string message_broker_host = kWebActiveMQHostAddress;

        if (message_broker_host.empty()) {
            GS_LOG_TRACE_MSG(trace, "GolfSimIpcSystem could not find host address in JSON config file.");
            return false;
        }

        std::string broker_URI = "failover:(" + message_broker_host + ")" + "?useCompression=true&initialReconnectDelay=2000&maxReconnectAttempts=2";
            //        "?wireFormat=openwire"
            //        "&transport.useInactivityMonitor=false"
            //        "&connection.alwaysSyncSend=true"
            //        "&connection.useAsyncSend=true"
            //        "?transport.commandTracingEnabled=true"
            //        "&transport.tcpTracingEnabled=true"
            //        "&wireFormat.tightEncodingEnabled=true"
        
        GS_LOG_TRACE_MSG(trace, "Active-MQ broker_URI is: " + broker_URI);

        // Initialization order probably doesn't matter, but we will initialize the consumer first to 
        // clear out any messages before the producer starts.
        consumer_ = GolfSimMessageConsumer::Initialize(broker_URI);

        if (!consumer_) {
            GS_LOG_TRACE_MSG(trace, "GolfSimIpcSystem could not Initialize consumer");
            return false;
        }

        producer_ = GolfSimMessageProducer::Initialize(broker_URI);

        if (!producer_) {
            GS_LOG_TRACE_MSG(trace, "GolfSimIpcSystem could not Initialize producer");
            return false;
        }

        std::this_thread::yield();

        return true;
    }


    bool GolfSimIpcSystem::ShutdownIPCSystem() {
        GS_LOG_TRACE_MSG(trace, "GolfSimIpcSystem::ShutdownIPC");

        consumer_->Shutdown();
        producer_->Shutdown();

        // TBD - Give other threads a moment to shut down
        sleep(4);

        delete consumer_;
        delete producer_;

        activemq::library::ActiveMQCPP::shutdownLibrary();

        return true;
    }

	bool GolfSimIpcSystem::DispatchReceivedIpcMessage(const BytesMessage& message)  {

        GS_LOG_TRACE_MSG(trace, "DispatchReceivedIpcMessage::Dispatch Received Ipc Message.");

        GolfSimIPCMessage* ipc_message = BuildIpcMessageFromBytesMessage(message);

        if (ipc_message == nullptr) {
            LoggingTools::Warning("Unable to convert ActiveMQ Message to a GolfSimIPCMessage.");
            return false;
        }

        bool result = false;

        GS_LOG_TRACE_MSG(trace, "DispatchReceivedIpcMessage::Dispatch - message type: " + ipc_message->Format());

        switch (ipc_message->GetMessageType()) {
            case GolfSimIPCMessage::IPCMessageType::kUnknown:
            {
                LoggingTools::Warning("Received GolfSimIPCMessage of type kUnknown.");
                break;
            }
            case GolfSimIPCMessage::IPCMessageType::kCamera2Image:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kCamera2Image IPC message.");
                result = DispatchCamera2ImageMessage(*ipc_message);
                break;

            }
            case GolfSimIPCMessage::IPCMessageType::kCamera2ReturnPreImage:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kCamera2PreImage IPC message.");
                result = DispatchCamera2PreImageMessage(*ipc_message);
                break;

            }
            case GolfSimIPCMessage::IPCMessageType::kShutdown:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kShutdown IPC message.");
                result = DispatchShutdownMessage(*ipc_message);
                break;
            }
            case GolfSimIPCMessage::IPCMessageType::kRequestForCamera2Image:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kRequestForCamera2Image IPC message.");
                DispatchRequestForCamera2ImageMessage(*ipc_message);
                break;

            }
            case GolfSimIPCMessage::IPCMessageType::kResults:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kResults IPC message.");
                DispatchResultsMessage(*ipc_message);
                break;

            }
            case GolfSimIPCMessage::IPCMessageType::kControlMessage:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kControlMessage IPC message.");
                DispatchControlMsgMessage(*ipc_message);
                break;
            }
            default:
            {
                GS_LOG_MSG(error, "Could not dispatch unknown IPC message of type " + 
                                            std::to_string((int)ipc_message->GetMessageType()));
                break;
            }
        }

        // We own the new ipc_message, so clean it up here
        delete ipc_message;

        std::this_thread::yield();

		return true;
	}


    bool GolfSimIpcSystem::DispatchShutdownMessage(const GolfSimIPCMessage& message) {

        GS_LOG_TRACE_MSG(trace, "DispatchShutdownMessage Received Ipc Message.");

        // TBD - Not sure if we just want to force the shutdown here or send an event into the
        // FSM and have the FSM do so. Probably better to send the event and get out of this IPC
        // consumer thread callback.

        // This message is telling the system to shutdown and exit
        // Let the FSM deal with the message by entering a related message into the queue
        GolfSimEventElement exitMessageReceived{ new GolfSimEvent::Exit{ } };
        GolfSimEventQueue::QueueEvent(exitMessageReceived);

        return true;
    }

    bool GolfSimIpcSystem::DispatchResultsMessage(const GolfSimIPCMessage& message) {

        // The LM system doesn't currently do anything if it gets a results message.
        // These messages are mostly destined for the PiTrac GUI
        GS_LOG_TRACE_MSG(trace, "DispatchResultsMessage Received Ipc Message.");

        return true;
    }

    bool GolfSimIpcSystem::DispatchControlMsgMessage(const GolfSimIPCMessage& message) {

        GS_LOG_TRACE_MSG(trace, "DispatchControlMsgMessage Received Ipc Message.");

        GolfSimEventElement controlMessageReceived{ new GolfSimEvent::ControlMessage{ message.GetControlMessage().control_type_} };
        GolfSimEventQueue::QueueEvent(controlMessageReceived);
        
        return true;
    }




        bool GolfSimIpcSystem::DispatchRequestForCamera2TestStillImage(const GolfSimIPCMessage& message) {

        GS_LOG_TRACE_MSG(trace, "DispatchRequestForCamera2TestStillImage Received Ipc Message.");

        // This message is telling the camera 2 system to get to  take a one-strobe picture, whereas
        // the camera will be externally triggered from the camera 1 system once the ball appears
        // to have been hit.
        // The main difference between this and the usual camera2 picture request is that the
        // TestStillImage will just take one strobe and immediately save it to a file.

        switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {

            case SystemMode::kCamera1:
            case SystemMode::kCamera1TestStandalone:
            case SystemMode::kCamera2TestStandalone:
                // This message is only for the  camera 2 system.  Ignore it
                break;

            case SystemMode::kCamera2:
            {
                // TBD - Not sure we need this message?
                break;
            }
            default:
            {
                LoggingTools::Warning("GolfSimIpcSystem::DispatchRequestForCamera2ImageMessage found unknown command_line_options_.system_mode_ .");
                return false;

                break;
            }
        }

        return true;
    }

    bool GolfSimIpcSystem::DispatchRequestForCamera2ImageMessage(const GolfSimIPCMessage& message)  {

        GS_LOG_TRACE_MSG(trace, "DispatchRequestForCamera2ImageMessage Received Ipc Message.");

        // This message is telling the camera 2 system to get ready to take a picture, whereas
        // the camera will be externally triggered from the camera 1 system once the ball appears
        // to have been hit.
        
        switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {

            case SystemMode::kCamera1:
                // This message is only for the  camera 2 system.  Ignore it
                break;

            case SystemMode::kCamera1TestStandalone:
            {
                // A request for the camera 2 system to take a triggered picture has been sent.
                // If we are in test mode for camera 1, camera 2 isn't around, so nothing
                // will be done.  Just ignore it here on camera 1.

                break;
            }
            case SystemMode::kCamera2:
            case SystemMode::kCamera2TestStandalone:
            {
                // Let the FSM deal with the message by entering a related message into the queue
                GolfSimEventElement armCamera2MessageReceived{ new GolfSimEvent::ArmCamera2MessageReceived{ } };
                GolfSimEventQueue::QueueEvent(armCamera2MessageReceived);

                break;
            }
            default:
            {
                LoggingTools::Warning("GolfSimIpcSystem::DispatchRequestForCamera2ImageMessage found unknown command_line_options_.system_mode_ .");
                return false;

                break;
            }
        }

        return true;
    }


    bool GolfSimIpcSystem::DispatchCamera2ImageMessage(const GolfSimIPCMessage& message) {

        GS_LOG_TRACE_MSG(trace, "DispatchCamera2ImageMessage received Ipc Message.");

        // Let the FSM deal with the message by entering a related message into the queue

        switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {

            case SystemMode::kCamera2:
            case SystemMode::kCamera2TestStandalone: 
            {

                // This message is only for the camera 1 system.  Ignore it for camera 2

                break;
            }
            case SystemMode::kCamera1TestStandalone:
            case SystemMode::kCamera1:
            {
                // Let the FSM deal with the message by entering a related message (including the image) into the queue
                GolfSimEventElement cam2ImageMessageReceived{ new GolfSimEvent::Camera2ImageReceived{ message.GetImageMat() } };
                GS_LOG_TRACE_MSG(trace, "    QueueEvent: " + cam2ImageMessageReceived.e_->Format());
                GolfSimEventQueue::QueueEvent(cam2ImageMessageReceived);

                break;
            }
            case SystemMode::kTest:
            default:
            {
                LoggingTools::Warning("GolfSimIpcSystem::DispatchCamera2ImageMessage found unknown command_line_options_.system_mode_ .");
                return false;
            }
        }

        return true;
    }

    bool GolfSimIpcSystem::DispatchCamera2PreImageMessage(const GolfSimIPCMessage& message) {

        GS_LOG_TRACE_MSG(trace, "DispatchCamera2PreImageMessage received Ipc Message.");

        // Let the FSM deal with the message by entering a related message into the queue

        switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {

        case SystemMode::kCamera2:
        case SystemMode::kCamera2TestStandalone:
        {

            // This message is only for the camera 1 system.  Ignore it for camera 2

            break;
        }
        case SystemMode::kCamera1TestStandalone:
        case SystemMode::kCamera1:
        {
            // Let the FSM deal with the message by entering a related message (including the image) into the queue
            GolfSimEventElement cam2PreImageMessageReceived{ new GolfSimEvent::Camera2PreImageReceived{ message.GetImageMat() } };
            GS_LOG_TRACE_MSG(trace, "    QueueEvent: " + cam2PreImageMessageReceived.e_->Format());
            GolfSimEventQueue::QueueEvent(cam2PreImageMessageReceived);

            break;
        }
        case SystemMode::kTest:
        default:
        {
            LoggingTools::Warning("GolfSimIpcSystem::DispatchCamera2PreImageMessage found unknown command_line_options_.system_mode_ .");
            return false;
        }
        }

        return true;
    }


    // Caller owns the resulting message.  Returns nullptr if an error.
    GolfSimIPCMessage* GolfSimIpcSystem::BuildIpcMessageFromBytesMessage(const BytesMessage& active_mq_message) {

        GS_LOG_TRACE_MSG(trace, "BuildIpcMessageFromBytesMessage called.");
        GolfSimIPCMessage* ipc_message = nullptr;

        try {
            std::string main_message_type = active_mq_message.getStringProperty(kGolfSimMessageTypeTag);

            if (main_message_type != kGolfSimMessageType) {
                GS_LOG_TRACE_MSG(trace, "BuildIpcMessageFromBytesMessage received unexpected GolfSimMessageType: " + main_message_type);
                return nullptr;
            }

            GolfSimIPCMessage::IPCMessageType ipc_message_type = (GolfSimIPCMessage::IPCMessageType)active_mq_message.getIntProperty(kGolfSimIPCMessageTypeTag);

            if (ipc_message_type == GolfSimIPCMessage::IPCMessageType::kUnknown) {
                return nullptr;
            }

            // We appear to have a valid GolfSimIpcMessage
            GS_LOG_TRACE_MSG(trace, "BuildIpcMessageFromBytesMessage converting Active-MQ message of type " + main_message_type + 
                                        " and message-type " + std::to_string((int)ipc_message_type) + " to GolfSimIpcMessage");
            ipc_message = new GolfSimIPCMessage(ipc_message_type);

            if (ipc_message == nullptr) {
                return nullptr;
            }

            if (ipc_message->GetMessageType() == GolfSimIPCMessage::IPCMessageType::kCamera2Image || 
                ipc_message->GetMessageType() == GolfSimIPCMessage::IPCMessageType::kCamera2ReturnPreImage) {

                GS_LOG_TRACE_MSG(trace, "BuildIpcMessageFromBytesMessage about to UnpackMatData.");
                // The ActiveMQ message's Byte body has the serialized data from which
                // the cv::Mat can be reconstructed.
                char* body_data = (char*)active_mq_message.getBodyBytes();
                ipc_message->UnpackMatData(body_data, active_mq_message.getBodyLength());

                // The caller of getBodyBytes owns the data, so clean it up here
                delete body_data;
            }
            else if (ipc_message->GetMessageType() == GolfSimIPCMessage::IPCMessageType::kResults) {

                GS_LOG_TRACE_MSG(trace, "BuildIpcMessageFromBytesMessage will NOT UnpackMatData for IPCMessageType::kResults.");
                // The ActiveMQ message's Byte body has the serialized data from which
                // the GsIPCResults oiject can be reconstructed.
                char* body_data = (char*)active_mq_message.getBodyBytes();

                /*
                int number_bytes = active_mq_message.getBodyLength();

                msgpack::unpacked unpacked_result_data;
                msgpack::unpack(unpacked_result_data, static_cast<const char*>(body_data), number_bytes);

                auto unpacked_gs_ipc_result = unpacked_result_data.get().as<GsIPCResult>();

                ipc_message->GetResultsForModification() = unpacked_gs_ipc_result;
                */

                // The caller of getBodyBytes owns the data, so clean it up here
                delete body_data;
            }
            else if (ipc_message->GetMessageType() == GolfSimIPCMessage::IPCMessageType::kControlMessage) {

                GS_LOG_TRACE_MSG(trace, "Unpacking data for a IPCMessageType::kControlMessage.");

                // The ActiveMQ message's Byte body has the serialized data from which
                // the GsIPCControlMsg oiject can be reconstructed.
                char* body_data = (char*)active_mq_message.getBodyBytes();

                int number_bytes = active_mq_message.getBodyLength();

                GS_LOG_TRACE_MSG(trace, "Packed IPCMessageType::kControlMessage has length = " + std::to_string(number_bytes));

                msgpack::object_handle oh;

                msgpack::unpack(oh, body_data, number_bytes);

                // Get the packed value(s)
                int control_msg_type;
                oh.get().convert(control_msg_type);

                GS_LOG_TRACE_MSG(trace, "Packed control msg type = " + std::to_string(control_msg_type));

                GsIPCControlMsg &msg = ipc_message->GetControlMessageForModification();
                msg.control_type_ = (GsIPCControlMsgType)control_msg_type;

                GS_LOG_TRACE_MSG(trace, "Unpacked IPCMessageType::kControlMessage - message was: " + ipc_message->GetControlMessage().Format());

                // The caller of getBodyBytes owns the data, so clean it up here
                delete body_data;
            }
        }
        catch (CMSException& e) {
            GS_LOG_TRACE_MSG(trace, "BuildIpcMessageFromBytesMessage received an exception.  Stack trace is:");
            e.printStackTrace();
            return nullptr;
        }
        catch (std::exception& ex) {
            GS_LOG_TRACE_MSG(trace, "Exception! - " + std::string(ex.what()) + ".  Restarting...");
        }

        return ipc_message;
    }


    // Caller owns the resulting message.  Returns nullptr if an error.

    std::unique_ptr<cms::BytesMessage> GolfSimIpcSystem::BuildBytesMessageObjectFromIpcMessage(const GolfSimIPCMessage& ipc_message) {

        GS_LOG_TRACE_MSG(trace, "BuildBytesMessageObjectFromIpcMessage called with IPC message type =" + std::to_string((int)ipc_message.GetMessageType()));

        // Need to ask the producer's session to create the new message for us 
        // (in order to setup some of the messages's internal values correctly).
        std::unique_ptr<cms::BytesMessage> active_mq_message = producer_->getNewBytesMessage();

        if (active_mq_message == nullptr) {
            GS_LOG_MSG(error, "System::BuildBytesMessageObjectFromIpcMessage could not get a new BytesMessage.");
            return nullptr;
        }

        active_mq_message->setStringProperty(kGolfSimMessageTypeTag, kGolfSimMessageType);
        active_mq_message->setIntProperty(kGolfSimIPCMessageTypeTag, ipc_message.GetMessageType());

        size_t image_mat_byte_length = 0;
        unsigned char* data = ipc_message.GetImageMatBytePointer(image_mat_byte_length);

        if ( (ipc_message.GetMessageType() == GolfSimIPCMessage::IPCMessageType::kCamera2Image ||
            ipc_message.GetMessageType() == GolfSimIPCMessage::IPCMessageType::kCamera2ReturnPreImage) &&
                data != nullptr) {

            GS_LOG_TRACE_MSG(trace, "GolfSimIpcSystem::BuildBytesMessageObjectFromIpcMessage has image -- setting body data of length = " + std::to_string(image_mat_byte_length));
            active_mq_message->setBodyBytes(data, image_mat_byte_length);
        }
        else if (ipc_message.GetMessageType() == GolfSimIPCMessage::IPCMessageType::kResults) {

            msgpack::sbuffer serialized_result;

            msgpack::pack(&serialized_result, ipc_message.GetResults());

            GS_LOG_TRACE_MSG(trace, "Sending a result of: " + ipc_message.GetResults().Format());
            GS_LOG_TRACE_MSG(trace, "GolfSimIpcSystem::BuildBytesMessageObjectFromIpcMessage setting body data for GsIPCResults of length = " + 
                            std::to_string(serialized_result.size()));

            active_mq_message->setBodyBytes((unsigned char*)serialized_result.data(), serialized_result.size());
        }

        return active_mq_message;
    }

    bool GolfSimIpcSystem::SendIpcMessage(const GolfSimIPCMessage& ipc_message) {
        GS_LOG_TRACE_MSG(trace, "GolfSimIpcSystem::SendIpcMessage");

        std::unique_ptr<cms::BytesMessage> activeMQ_message = BuildBytesMessageObjectFromIpcMessage(ipc_message);

        if (activeMQ_message == nullptr) {
            GS_LOG_MSG(error, "GolfSimIpcSystem::SendIpcMessage failed to create activeMQ_message from the GolfSimIPCMessage.");
            return false;
        }

        bool result = producer_->SendMessage(activeMQ_message.get());

        std::this_thread::yield();

        return result;
    }

    bool GolfSimIpcSystem::SimulateCamera2ImageMessage() {
        GS_LOG_TRACE_MSG(trace, "GolfSimIpcSystem::SimulateCame");

        // Simulate a returned picture from camera 2 to allow for testing

        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kCamera2Image);

        std::string fname = "test.png";
        cv::Mat img = cv::imread(fname, cv::IMREAD_COLOR);

        if (img.empty()) {
            GS_LOG_TRACE_MSG(trace, "Failed to open file " + fname);
            return false;
        }

        printf("Serializing image in file %s\n", fname.c_str());

        ipc_message.SetImageMat(img);
        GolfSimIpcSystem::SendIpcMessage(ipc_message);

        std::this_thread::yield();
        return true;
    }


}

#endif // #ifdef __unix__  // Ignore in Windows environment

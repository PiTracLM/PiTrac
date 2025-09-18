/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#ifdef __unix__  // Ignore in Windows environment

#include "gs_ipc_system.h"

#include <chrono>
#include <cstring>
#include <random>
#include <thread>

#include "gs_globals.h"
#include "logging_tools.h"
#include "gs_options.h"
#include "gs_config.h"

using namespace std;

namespace golf_sim {

    std::string GolfSimIpcSystem::kZeroMQPublisherEndpoint = "tcp://*:5556";
    std::string GolfSimIpcSystem::kZeroMQSubscriberEndpoint = "tcp://localhost:5556";

    const std::string GolfSimIpcSystem::kGolfSimTopicPrefix = "Golf.Sim";
    const std::string GolfSimIpcSystem::kGolfSimMessageTopic = "Golf.Sim.Message";
    const std::string GolfSimIpcSystem::kGolfSimResultsTopic = "Golf.Sim.Results";
    const std::string GolfSimIpcSystem::kGolfSimControlTopic = "Golf.Sim.Control";

    const std::string GolfSimIpcSystem::kZeroMQSystemIdProperty = "System_ID";
    const std::string GolfSimIpcSystem::kZeroMQMessageTypeProperty = "Message_Type";
    const std::string GolfSimIpcSystem::kZeroMQTimestampProperty = "Timestamp";

    std::unique_ptr<ZeroMQPublisher> GolfSimIpcSystem::publisher_ = nullptr;
    std::unique_ptr<ZeroMQSubscriber> GolfSimIpcSystem::subscriber_ = nullptr;
    std::string GolfSimIpcSystem::system_id_ = "";
    cv::Mat GolfSimIpcSystem::last_received_image_;
    std::mutex GolfSimIpcSystem::system_mutex_;
    std::atomic<bool> GolfSimIpcSystem::initialized_(false);

    bool GolfSimIpcSystem::InitializeIPCSystem() {
        std::lock_guard<std::mutex> lock(system_mutex_);

        if (initialized_.load()) {
            GS_LOG_TRACE_MSG(trace, "ZeroMQ IPC System already initialized");
            return true;
        }

        if (system_id_.empty()) {
            char hostname[256];
            if (gethostname(hostname, sizeof(hostname)) == 0) {
                system_id_ = std::string(hostname) + "_" + std::to_string(getpid());
            } else {
                std::random_device rd;
                std::mt19937 gen(rd());
                system_id_ = "system_" + std::to_string(gen());
            }
        }

        // Check for ZeroMQ endpoint configuration
        std::string config_address;
        GolfSimConfiguration::SetConstant("gs_config.ipc_interface.kZeroMQEndpoint", config_address);
        if (!config_address.empty()) {
            kZeroMQSubscriberEndpoint = config_address;
            size_t port_pos = config_address.find_last_of(':');
            if (port_pos != std::string::npos) {
                std::string port = config_address.substr(port_pos + 1);
                kZeroMQPublisherEndpoint = "tcp://*:" + port;
            }
        }

        GS_LOG_TRACE_MSG(trace, "Initializing ZeroMQ IPC System");
        GS_LOG_TRACE_MSG(trace, "Publisher endpoint: " + kZeroMQPublisherEndpoint);
        GS_LOG_TRACE_MSG(trace, "Subscriber endpoint: " + kZeroMQSubscriberEndpoint);
        GS_LOG_TRACE_MSG(trace, "System ID: " + system_id_);

        try {
            publisher_ = std::make_unique<ZeroMQPublisher>(kZeroMQPublisherEndpoint);
            publisher_->SetHighWaterMark(1000);
            publisher_->SetLinger(1000);

            if (!publisher_->Start()) {
                GS_LOG_TRACE_MSG(error, "Failed to start ZeroMQ publisher");
                return false;
            }

            subscriber_ = std::make_unique<ZeroMQSubscriber>(kZeroMQSubscriberEndpoint);
            subscriber_->SetHighWaterMark(1000);
            subscriber_->SetReceiveTimeout(100);
            subscriber_->SetSystemIdToExclude(system_id_);

            subscriber_->SetMessageHandler(OnMessageReceived);

            subscriber_->Subscribe(kGolfSimTopicPrefix);

            if (!subscriber_->Start()) {
                GS_LOG_TRACE_MSG(error, "Failed to start ZeroMQ subscriber");
                publisher_->Stop();
                return false;
            }

            initialized_ = true;
            GS_LOG_TRACE_MSG(trace, "ZeroMQ IPC System initialized successfully");
            return true;

        } catch (const std::exception& e) {
            GS_LOG_TRACE_MSG(error, "Exception initializing ZeroMQ IPC System: " + std::string(e.what()));
            return false;
        }
    }

    bool GolfSimIpcSystem::ShutdownIPCSystem() {
        std::lock_guard<std::mutex> lock(system_mutex_);

        if (!initialized_.load()) {
            return true;
        }

        GS_LOG_TRACE_MSG(trace, "Shutting down ZeroMQ IPC System");

        if (subscriber_) {
            subscriber_->Stop();
            subscriber_.reset();
        }

        if (publisher_) {
            publisher_->Stop();
            publisher_.reset();
        }

        initialized_ = false;
        GS_LOG_TRACE_MSG(trace, "ZeroMQ IPC System shutdown complete");
        return true;
    }

    void GolfSimIpcSystem::OnMessageReceived(
        const std::string& topic,
        const std::vector<uint8_t>& data,
        const std::map<std::string, std::string>& properties) {

        GS_LOG_TRACE_MSG(trace, "ZeroMQ message received on topic: " + topic);

        auto system_id_it = properties.find(kZeroMQSystemIdProperty);
        if (system_id_it != properties.end() && system_id_it->second == system_id_) {
            GS_LOG_TRACE_MSG(trace, "Ignoring own message");
            return;
        }

        try {
            DispatchReceivedIpcMessage(topic, data, properties);
        } catch (const std::exception& e) {
            GS_LOG_TRACE_MSG(error, "Exception handling ZeroMQ message: " + std::string(e.what()));
        }
    }

    bool GolfSimIpcSystem::DispatchReceivedIpcMessage(
        const std::string& topic,
        const std::vector<uint8_t>& data,
        const std::map<std::string, std::string>& properties) {

        GS_LOG_TRACE_MSG(trace, "Dispatching ZeroMQ IPC message from topic: " + topic);

        GolfSimIPCMessage* ipc_message = BuildIpcMessageFromZeroMQData(data, properties);

        if (ipc_message == nullptr) {
            LoggingTools::Warning("Unable to convert ZeroMQ data to GolfSimIPCMessage");
            return false;
        }

        bool result = false;

        GS_LOG_TRACE_MSG(trace, "Dispatching message type: " + ipc_message->Format());

        switch (ipc_message->GetMessageType()) {
            case GolfSimIPCMessage::IPCMessageType::kUnknown:
            {
                LoggingTools::Warning("Received GolfSimIPCMessage of type kUnknown");
                break;
            }
            case GolfSimIPCMessage::IPCMessageType::kCamera2Image:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kCamera2Image IPC message");
                result = DispatchCamera2ImageMessage(*ipc_message);
                break;
            }
            case GolfSimIPCMessage::IPCMessageType::kCamera2ReturnPreImage:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kCamera2PreImage IPC message");
                result = DispatchCamera2PreImageMessage(*ipc_message);
                break;
            }
            case GolfSimIPCMessage::IPCMessageType::kShutdown:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kShutdown IPC message");
                result = DispatchShutdownMessage(*ipc_message);
                break;
            }
            case GolfSimIPCMessage::IPCMessageType::kRequestForCamera2Image:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kRequestForCamera2Image IPC message");
                result = DispatchRequestForCamera2ImageMessage(*ipc_message);
                break;
            }
            case GolfSimIPCMessage::IPCMessageType::kResults:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kResults IPC message");
                result = DispatchResultsMessage(*ipc_message);
                break;
            }
            case GolfSimIPCMessage::IPCMessageType::kControlMessage:
            {
                GS_LOG_TRACE_MSG(trace, "Dispatching kControlMessage IPC message");
                result = DispatchControlMsgMessage(*ipc_message);
                break;
            }
            default:
            {
                GS_LOG_MSG(error, "Could not dispatch unknown IPC message of type " +
                                  std::to_string((int)ipc_message->GetMessageType()));
                break;
            }
        }

        delete ipc_message;
        std::this_thread::yield();
        return result;
    }

    bool GolfSimIpcSystem::SendIpcMessage(const GolfSimIPCMessage& ipc_message) {
        if (!initialized_.load() || !publisher_) {
            GS_LOG_TRACE_MSG(error, "ZeroMQ IPC System not initialized");
            return false;
        }

        GS_LOG_TRACE_MSG(trace, "Sending ZeroMQ IPC message: " + ipc_message.Format());

        std::string topic;
        std::vector<uint8_t> data;
        std::map<std::string, std::string> properties;

        if (!SerializeIpcMessageToZeroMQ(ipc_message, topic, data, properties)) {
            GS_LOG_TRACE_MSG(error, "Failed to serialize IPC message to ZeroMQ format");
            return false;
        }

        bool result = publisher_->SendMessage(topic, data, properties);
        std::this_thread::yield();
        return result;
    }

    GolfSimIPCMessage* GolfSimIpcSystem::BuildIpcMessageFromZeroMQData(
        const std::vector<uint8_t>& data,
        const std::map<std::string, std::string>& properties) {

        try {
            auto type_it = properties.find(kZeroMQMessageTypeProperty);
            if (type_it == properties.end()) {
                GS_LOG_TRACE_MSG(error, "No message type in ZeroMQ message properties");
                return nullptr;
            }

            int message_type_int = std::stoi(type_it->second);
            auto message_type = static_cast<GolfSimIPCMessage::IPCMessageType>(message_type_int);

            if (message_type == GolfSimIPCMessage::IPCMessageType::kUnknown) {
                return nullptr;
            }

            GolfSimIPCMessage* ipc_message = new GolfSimIPCMessage(message_type);

            if (message_type == GolfSimIPCMessage::IPCMessageType::kCamera2Image ||
                message_type == GolfSimIPCMessage::IPCMessageType::kCamera2ReturnPreImage) {

                msgpack::object_handle oh;
                msgpack::unpack(oh, reinterpret_cast<const char*>(data.data()), data.size());

                ZeroMQImageMessage img_msg;
                oh.get().convert(img_msg);

                cv::Mat image(img_msg.image_rows, img_msg.image_cols, img_msg.image_type,
                             const_cast<uint8_t*>(img_msg.image_data.data()));
                ipc_message->SetImageMat(image);

            } else if (message_type == GolfSimIPCMessage::IPCMessageType::kControlMessage) {

                msgpack::object_handle oh;
                msgpack::unpack(oh, reinterpret_cast<const char*>(data.data()), data.size());

                ZeroMQControlMessage ctrl_msg;
                oh.get().convert(ctrl_msg);

                auto& control_msg = ipc_message->GetControlMessageForModification();
                control_msg.control_type_ = static_cast<GsIPCControlMsgType>(ctrl_msg.control_type);

            } else if (message_type == GolfSimIPCMessage::IPCMessageType::kResults) {

                msgpack::object_handle oh;
                msgpack::unpack(oh, reinterpret_cast<const char*>(data.data()), data.size());

                ZeroMQResultMessage result_msg;
                oh.get().convert(result_msg);

                // TODO: Implement proper result deserialization

            }

            return ipc_message;

        } catch (const std::exception& e) {
            GS_LOG_TRACE_MSG(error, "Exception deserializing ZeroMQ message: " + std::string(e.what()));
            return nullptr;
        }
    }

    bool GolfSimIpcSystem::SerializeIpcMessageToZeroMQ(
        const GolfSimIPCMessage& ipc_message,
        std::string& topic,
        std::vector<uint8_t>& data,
        std::map<std::string, std::string>& properties) {

        try {
            topic = GetTopicForMessageType(ipc_message.GetMessageType());

            properties[kZeroMQSystemIdProperty] = system_id_;
            properties[kZeroMQMessageTypeProperty] = std::to_string(static_cast<int>(ipc_message.GetMessageType()));
            properties[kZeroMQTimestampProperty] = std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            ZeroMQMessageHeader header;
            header.message_type = static_cast<int>(ipc_message.GetMessageType());
            header.timestamp_ms = std::stoll(properties[kZeroMQTimestampProperty]);
            header.system_id = system_id_;

            msgpack::sbuffer buffer;

            if (ipc_message.GetMessageType() == GolfSimIPCMessage::IPCMessageType::kCamera2Image ||
                ipc_message.GetMessageType() == GolfSimIPCMessage::IPCMessageType::kCamera2ReturnPreImage) {

                cv::Mat image = ipc_message.GetImageMat();
                ZeroMQImageMessage img_msg;
                img_msg.header = header;
                img_msg.image_rows = image.rows;
                img_msg.image_cols = image.cols;
                img_msg.image_type = image.type();

                size_t data_size = image.total() * image.elemSize();
                img_msg.image_data.resize(data_size);
                std::memcpy(img_msg.image_data.data(), image.data, data_size);

                msgpack::pack(buffer, img_msg);

            } else if (ipc_message.GetMessageType() == GolfSimIPCMessage::IPCMessageType::kControlMessage) {

                ZeroMQControlMessage ctrl_msg;
                ctrl_msg.header = header;
                ctrl_msg.control_type = static_cast<int>(ipc_message.GetControlMessage().control_type_);

                msgpack::pack(buffer, ctrl_msg);

            } else if (ipc_message.GetMessageType() == GolfSimIPCMessage::IPCMessageType::kResults) {

                ZeroMQResultMessage result_msg;
                result_msg.header = header;

                // TODO: Implement proper result serialization based on GsIPCResult structure
                result_msg.result_data["type"] = "results";

                msgpack::pack(buffer, result_msg);

            } else {

                ZeroMQSimpleMessage simple_msg;
                simple_msg.header = header;

                msgpack::pack(buffer, simple_msg);
            }

            data.resize(buffer.size());
            std::memcpy(data.data(), buffer.data(), buffer.size());

            return true;

        } catch (const std::exception& e) {
            GS_LOG_TRACE_MSG(error, "Exception serializing IPC message: " + std::string(e.what()));
            return false;
        }
    }

    std::string GolfSimIpcSystem::GetTopicForMessageType(GolfSimIPCMessage::IPCMessageType type) {
        switch (type) {
            case GolfSimIPCMessage::IPCMessageType::kResults:
                return kGolfSimResultsTopic;
            case GolfSimIPCMessage::IPCMessageType::kControlMessage:
                return kGolfSimControlTopic;
            default:
                return kGolfSimMessageTopic;
        }
    }

    GolfSimIPCMessage::IPCMessageType GolfSimIpcSystem::GetMessageTypeFromTopic(const std::string& topic) {
        if (topic == kGolfSimResultsTopic) {
            return GolfSimIPCMessage::IPCMessageType::kResults;
        } else if (topic == kGolfSimControlTopic) {
            return GolfSimIPCMessage::IPCMessageType::kControlMessage;
        } else {
            return GolfSimIPCMessage::IPCMessageType::kUnknown;
        }
    }

    void GolfSimIpcSystem::SetSystemId(const std::string& system_id) {
        system_id_ = system_id;
    }

    std::string GolfSimIpcSystem::GetSystemId() {
        return system_id_;
    }

    bool GolfSimIpcSystem::DispatchShutdownMessage(const GolfSimIPCMessage& message) {
        GS_LOG_TRACE_MSG(trace, "DispatchShutdownMessage Received Ipc Message.");

        GolfSimEventElement exitMessageReceived{ new GolfSimEvent::Exit{ } };
        GolfSimEventQueue::QueueEvent(exitMessageReceived);

        return true;
    }

    bool GolfSimIpcSystem::DispatchResultsMessage(const GolfSimIPCMessage& message) {
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

        switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {
            case SystemMode::kCamera1:
            case SystemMode::kCamera1TestStandalone:
            case SystemMode::kCamera2TestStandalone:
                break;

            case SystemMode::kCamera2:
            case SystemMode::kRunCam2ProcessForPi1Processing:
            {
                break;
            }
            default:
            {
                LoggingTools::Warning("GolfSimIpcSystem::DispatchRequestForCamera2TestStillImage found unknown system_mode_");
                return false;
            }
        }

        return true;
    }

    bool GolfSimIpcSystem::DispatchRequestForCamera2ImageMessage(const GolfSimIPCMessage& message) {
        GS_LOG_TRACE_MSG(trace, "DispatchRequestForCamera2ImageMessage Received Ipc Message.");

        switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {
            case SystemMode::kCamera1:
                break;

            case SystemMode::kCamera1TestStandalone:
            {
                break;
            }
            case SystemMode::kCamera2:
            case SystemMode::kCamera2TestStandalone:
            case SystemMode::kRunCam2ProcessForPi1Processing:
            {
                GolfSimEventElement armCamera2MessageReceived{ new GolfSimEvent::ArmCamera2MessageReceived{ } };
                GolfSimEventQueue::QueueEvent(armCamera2MessageReceived);
                break;
            }
            case SystemMode::kCamera1AutoCalibrate:
            case SystemMode::kCamera2AutoCalibrate:
            {
                break;
            }
            default:
            {
                LoggingTools::Warning("GolfSimIpcSystem::DispatchRequestForCamera2ImageMessage found unknown system_mode_");
                return false;
            }
        }

        return true;
    }

    bool GolfSimIpcSystem::DispatchCamera2ImageMessage(const GolfSimIPCMessage& message) {
        GS_LOG_TRACE_MSG(trace, "DispatchCamera2ImageMessage received Ipc Message.");

        if (GolfSimOptions::GetCommandLineOptions().camera_still_mode_ ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1AutoCalibrate ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2AutoCalibrate ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1BallLocation ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2BallLocation) {
            GS_LOG_TRACE_MSG(trace, "In still-picture, locate or AutoCalibrate camera mode. Will save received image.");

            last_received_image_ = message.GetImageMat().clone();
            return true;
        }

        switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {
            case SystemMode::kCamera2:
            case SystemMode::kCamera2TestStandalone:
            {
                break;
            }
            case SystemMode::kCamera1TestStandalone:
            case SystemMode::kCamera1:
            {
                GolfSimEventElement cam2ImageMessageReceived{ new GolfSimEvent::Camera2ImageReceived{ message.GetImageMat() } };
                GS_LOG_TRACE_MSG(trace, "    QueueEvent: " + cam2ImageMessageReceived.e_->Format());
                GolfSimEventQueue::QueueEvent(cam2ImageMessageReceived);
                break;
            }
            case SystemMode::kTest:
            default:
            {
                LoggingTools::Warning("GolfSimIpcSystem::DispatchCamera2ImageMessage found unknown system_mode_");
                return false;
            }
        }

        return true;
    }

    bool GolfSimIpcSystem::DispatchCamera2PreImageMessage(const GolfSimIPCMessage& message) {
        GS_LOG_TRACE_MSG(trace, "DispatchCamera2PreImageMessage received Ipc Message.");

        switch (GolfSimOptions::GetCommandLineOptions().system_mode_) {
            case SystemMode::kCamera2:
            case SystemMode::kCamera2TestStandalone:
            {
                break;
            }
            case SystemMode::kCamera1TestStandalone:
            case SystemMode::kCamera1:
            {
                GolfSimEventElement cam2PreImageMessageReceived{ new GolfSimEvent::Camera2PreImageReceived{ message.GetImageMat() } };
                GS_LOG_TRACE_MSG(trace, "    QueueEvent: " + cam2PreImageMessageReceived.e_->Format());
                GolfSimEventQueue::QueueEvent(cam2PreImageMessageReceived);
                break;
            }
            case SystemMode::kTest:
            default:
            {
                LoggingTools::Warning("GolfSimIpcSystem::DispatchCamera2PreImageMessage found unknown system_mode_");
                return false;
            }
        }

        return true;
    }

    bool GolfSimIpcSystem::SimulateCamera2ImageMessage() {
        GS_LOG_TRACE_MSG(trace, "GolfSimIpcSystem::SimulateCamera2ImageMessage");

        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kCamera2Image);

        std::string fname = "test.png";
        cv::Mat img = cv::imread(fname, cv::IMREAD_COLOR);

        if (img.empty()) {
            GS_LOG_TRACE_MSG(trace, "Failed to open file " + fname);
            return false;
        }

        printf("Serializing image in file %s\n", fname.c_str());

        ipc_message.SetImageMat(img);
        return SendIpcMessage(ipc_message);
    }

} // namespace golf_sim

#endif // #ifdef __unix__
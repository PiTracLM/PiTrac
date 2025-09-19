/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#ifdef __unix__  // Ignore in Windows environment

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>

#include <msgpack.hpp>
#include <opencv2/core.hpp>

#include "zeromq_publisher.h"
#include "zeromq_subscriber.h"
#include "gs_events.h"
#include "gs_ipc_message.h"

namespace golf_sim {

    class GolfSimIpcSystem {
    public:
        static const int kIpcLoopIntervalMs = 2000;
        static std::string kZeroMQPublisherEndpoint;
        static std::string kZeroMQSubscriberEndpoint;

        // Topics for different message types
        static const std::string kGolfSimTopicPrefix;
        static const std::string kGolfSimMessageTopic;
        static const std::string kGolfSimResultsTopic;
        static const std::string kGolfSimControlTopic;

        // Properties for message identification
        static const std::string kZeroMQSystemIdProperty;
        static const std::string kZeroMQMessageTypeProperty;
        static const std::string kZeroMQTimestampProperty;

        static cv::Mat last_received_image_;

        // Main message handling functions
        static bool DispatchReceivedIpcMessage(
            const std::string& topic,
            const std::vector<uint8_t>& data,
            const std::map<std::string, std::string>& properties);

        static bool SendIpcMessage(const GolfSimIPCMessage& ipc_message);

        // Serialization/deserialization helpers
        static std::unique_ptr<GolfSimIPCMessage> BuildIpcMessageFromZeroMQData(
            const std::vector<uint8_t>& data,
            const std::map<std::string, std::string>& properties);

        static bool SerializeIpcMessageToZeroMQ(
            const GolfSimIPCMessage& ipc_message,
            std::string& topic,
            std::vector<uint8_t>& data,
            std::map<std::string, std::string>& properties);

        // System lifecycle
        static bool InitializeIPCSystem();
        static bool ShutdownIPCSystem();

        // Message dispatchers
        static bool DispatchRequestForCamera2ImageMessage(const GolfSimIPCMessage& message);
        static bool DispatchCamera2ImageMessage(const GolfSimIPCMessage& message);
        static bool DispatchCamera2PreImageMessage(const GolfSimIPCMessage& message);
        static bool DispatchShutdownMessage(const GolfSimIPCMessage& message);
        static bool DispatchRequestForCamera2TestStillImage(const GolfSimIPCMessage& message);
        static bool DispatchResultsMessage(const GolfSimIPCMessage& message);
        static bool DispatchControlMsgMessage(const GolfSimIPCMessage& message);

        // Test/simulation methods
        static bool SimulateCamera2ImageMessage();

        // Configuration
        static void SetSystemId(const std::string& system_id);
        static std::string GetSystemId();

    private:
        // ZeroMQ components
        static std::unique_ptr<ZeroMQPublisher> publisher_;
        static std::unique_ptr<ZeroMQSubscriber> subscriber_;

        // System identification
        static std::string system_id_;

        // Message handler for subscriber
        static void OnMessageReceived(
            const std::string& topic,
            const std::vector<uint8_t>& data,
            const std::map<std::string, std::string>& properties);

        // Helper functions for msgpack serialization
        static bool SerializeImageMat(const cv::Mat& image, msgpack::sbuffer& buffer);
        static bool DeserializeImageMat(const char* data, size_t length, cv::Mat& image);

        // Message type to topic mapping
        static std::string GetTopicForMessageType(GolfSimIPCMessage::IPCMessageType type);
        static GolfSimIPCMessage::IPCMessageType GetMessageTypeFromTopic(const std::string& topic);

        // Thread safety
        static std::mutex system_mutex_;
        static std::atomic<bool> initialized_;
    };

    // Msgpack serialization structures for different message types
    struct ZeroMQMessageHeader {
        int message_type;
        int64_t timestamp_ms;
        std::string system_id;

        MSGPACK_DEFINE(message_type, timestamp_ms, system_id);
    };

    struct ZeroMQImageMessage {
        ZeroMQMessageHeader header;
        std::vector<uint8_t> image_data;
        int image_rows;
        int image_cols;
        int image_type;

        MSGPACK_DEFINE(header, image_data, image_rows, image_cols, image_type);
    };

    struct ZeroMQControlMessage {
        ZeroMQMessageHeader header;
        int control_type;

        MSGPACK_DEFINE(header, control_type);
    };

    struct ZeroMQResultMessage {
        ZeroMQMessageHeader header;
        std::map<std::string, std::string> result_data;

        MSGPACK_DEFINE(header, result_data);
    };

    struct ZeroMQSimpleMessage {
        ZeroMQMessageHeader header;

        MSGPACK_DEFINE(header);
    };

} // namespace golf_sim

#endif // #ifdef __unix__
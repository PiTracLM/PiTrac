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

        static const std::string kGolfSimTopicPrefix;
        static const std::string kGolfSimMessageTopic;
        static const std::string kGolfSimResultsTopic;
        static const std::string kGolfSimControlTopic;

        static const std::string kZeroMQSystemIdProperty;
        static const std::string kZeroMQMessageTypeProperty;
        static const std::string kZeroMQTimestampProperty;

        static cv::Mat last_received_image_;

        static bool DispatchReceivedIpcMessage(
            const std::string& topic,
            const std::vector<uint8_t>& data,
            const std::map<std::string, std::string>& properties);

        static bool SendIpcMessage(const GolfSimIPCMessage& ipc_message);

        static GolfSimIPCMessage* BuildIpcMessageFromZeroMQData(
            const std::vector<uint8_t>& data,
            const std::map<std::string, std::string>& properties);

        static bool SerializeIpcMessageToZeroMQ(
            const GolfSimIPCMessage& ipc_message,
            std::string& topic,
            std::vector<uint8_t>& data,
            std::map<std::string, std::string>& properties);

        static bool InitializeIPCSystem();
        static bool ShutdownIPCSystem();

        static bool DispatchRequestForCamera2ImageMessage(const GolfSimIPCMessage& message);
        static bool DispatchCamera2ImageMessage(const GolfSimIPCMessage& message);
        static bool DispatchCamera2PreImageMessage(const GolfSimIPCMessage& message);
        static bool DispatchShutdownMessage(const GolfSimIPCMessage& message);
        static bool DispatchRequestForCamera2TestStillImage(const GolfSimIPCMessage& message);
        static bool DispatchResultsMessage(const GolfSimIPCMessage& message);
        static bool DispatchControlMsgMessage(const GolfSimIPCMessage& message);

        static bool SimulateCamera2ImageMessage();

        static void SetSystemId(const std::string& system_id);
        static std::string GetSystemId();

    private:
        static std::unique_ptr<ZeroMQPublisher> publisher_;
        static std::unique_ptr<ZeroMQSubscriber> subscriber_;

        static std::string system_id_;

        static void OnMessageReceived(
            const std::string& topic,
            const std::vector<uint8_t>& data,
            const std::map<std::string, std::string>& properties);

        static bool SerializeImageMat(const cv::Mat& image, msgpack::sbuffer& buffer);
        static bool DeserializeImageMat(const char* data, size_t length, cv::Mat& image);

        static std::string GetTopicForMessageType(GolfSimIPCMessage::IPCMessageType type);
        static GolfSimIPCMessage::IPCMessageType GetMessageTypeFromTopic(const std::string& topic);

        static std::mutex system_mutex_;
        static std::atomic<bool> initialized_;
    };

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
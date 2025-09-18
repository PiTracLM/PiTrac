#include "zeromq_subscriber.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>

namespace golf_sim {

ZeroMQSubscriber::ZeroMQSubscriber(const std::string& endpoint)
    : endpoint_(endpoint)
    , running_(false)
    , should_stop_(false) {
}

ZeroMQSubscriber::~ZeroMQSubscriber() {
    Stop();
}

bool ZeroMQSubscriber::Start() {
    if (running_.load()) {
        return true;
    }

    should_stop_ = false;

    try {
        context_ = std::make_unique<zmq::context_t>(1);
        subscriber_thread_ = std::thread(&ZeroMQSubscriber::SubscriberThread, this);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        running_ = true;
        std::cout << "ZeroMQ Subscriber started, connecting to " << endpoint_ << std::endl;
        return true;

    } catch (const zmq::error_t& e) {
        std::cerr << "Failed to start ZeroMQ Subscriber: " << e.what() << std::endl;
        return false;
    }
}

void ZeroMQSubscriber::Stop() {
    if (!running_.load()) {
        return;
    }

    should_stop_ = true;

    if (subscriber_thread_.joinable()) {
        subscriber_thread_.join();
    }

    subscriber_.reset();
    context_.reset();

    running_ = false;
    std::cout << "ZeroMQ Subscriber stopped" << std::endl;
}

void ZeroMQSubscriber::Subscribe(const std::string& topic_filter) {
    topic_filters_.push_back(topic_filter);

    if (subscriber_) {
        subscriber_->set(zmq::sockopt::subscribe, topic_filter);
    }
}

void ZeroMQSubscriber::Unsubscribe(const std::string& topic_filter) {
    auto it = std::find(topic_filters_.begin(), topic_filters_.end(), topic_filter);
    if (it != topic_filters_.end()) {
        topic_filters_.erase(it);
    }

    if (subscriber_) {
        subscriber_->set(zmq::sockopt::unsubscribe, topic_filter);
    }
}

void ZeroMQSubscriber::SetMessageHandler(MessageHandler handler) {
    message_handler_ = handler;
}

void ZeroMQSubscriber::SetHighWaterMark(int hwm) {
    high_water_mark_ = hwm;
}

void ZeroMQSubscriber::SetReceiveTimeout(int timeout_ms) {
    receive_timeout_ms_ = timeout_ms;
}

void ZeroMQSubscriber::SetSystemIdToExclude(const std::string& system_id) {
    system_id_to_exclude_ = system_id;
}

std::map<std::string, std::string> ZeroMQSubscriber::ParseProperties(const std::string& json_str) {
    std::map<std::string, std::string> properties;

    if (json_str.size() < 2 || json_str[0] != '{' || json_str[json_str.size()-1] != '}') {
        return properties;
    }

    std::string content = json_str.substr(1, json_str.size() - 2);
    std::istringstream ss(content);
    std::string item;

    while (std::getline(ss, item, ',')) {
        size_t colon_pos = item.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = item.substr(0, colon_pos);
            std::string value = item.substr(colon_pos + 1);

            if (key.size() >= 2 && key[0] == '"' && key[key.size()-1] == '"') {
                key = key.substr(1, key.size() - 2);
            }
            if (value.size() >= 2 && value[0] == '"' && value[value.size()-1] == '"') {
                value = value.substr(1, value.size() - 2);
            }

            properties[key] = value;
        }
    }

    return properties;
}

void ZeroMQSubscriber::SubscriberThread() {
    try {
        subscriber_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::sub);

        subscriber_->set(zmq::sockopt::rcvhwm, high_water_mark_);
        subscriber_->set(zmq::sockopt::rcvtimeo, receive_timeout_ms_);

        subscriber_->connect(endpoint_);

        for (const auto& filter : topic_filters_) {
            subscriber_->set(zmq::sockopt::subscribe, filter);
        }

        if (topic_filters_.empty()) {
            subscriber_->set(zmq::sockopt::subscribe, "");
        }

        std::cout << "ZeroMQ Subscriber connected and listening..." << std::endl;

        while (!should_stop_.load()) {
            try {
                zmq::message_t topic_msg;
                zmq::message_t props_msg;
                zmq::message_t data_msg;

                auto result = subscriber_->recv(topic_msg);
                if (!result || *result == 0) {
                    continue;
                }

                if (!subscriber_->get(zmq::sockopt::rcvmore)) {
                    continue;
                }

                result = subscriber_->recv(props_msg);
                if (!result) {
                    continue;
                }

                if (!subscriber_->get(zmq::sockopt::rcvmore)) {
                    continue;
                }

                result = subscriber_->recv(data_msg);
                if (!result) {
                    continue;
                }

                std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
                std::string props_str(static_cast<char*>(props_msg.data()), props_msg.size());
                std::vector<uint8_t> data(
                    static_cast<uint8_t*>(data_msg.data()),
                    static_cast<uint8_t*>(data_msg.data()) + data_msg.size()
                );

                auto properties = ParseProperties(props_str);

                if (!system_id_to_exclude_.empty()) {
                    auto it = properties.find("LM_System_ID");
                    if (it != properties.end() && it->second == system_id_to_exclude_) {
                        continue;
                    }
                }

                if (message_handler_) {
                    message_handler_(topic, data, properties);
                }

            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN) {
                    std::cerr << "Error receiving message: " << e.what() << std::endl;
                }
            }
        }

    } catch (const zmq::error_t& e) {
        std::cerr << "Subscriber thread error: " << e.what() << std::endl;
    }
}

} // namespace golf_sim
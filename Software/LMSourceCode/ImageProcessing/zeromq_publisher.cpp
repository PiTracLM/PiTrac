#include "zeromq_publisher.h"
#include <chrono>
#include <cstring>
#include <iostream>

namespace golf_sim {

ZeroMQPublisher::ZeroMQPublisher(const std::string& endpoint)
    : endpoint_(endpoint)
    , running_(false)
    , should_stop_(false) {
}

ZeroMQPublisher::~ZeroMQPublisher() {
    Stop();
}

bool ZeroMQPublisher::Start() {
    if (running_.load()) {
        return true;
    }

    should_stop_ = false;

    try {
        context_ = std::make_unique<zmq::context_t>(1);
        publisher_thread_ = std::thread(&ZeroMQPublisher::PublisherThread, this);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        running_ = true;
        std::cout << "ZeroMQ Publisher started on " << endpoint_ << std::endl;
        return true;

    } catch (const zmq::error_t& e) {
        std::cerr << "Failed to start ZeroMQ Publisher: " << e.what() << std::endl;
        return false;
    }
}

void ZeroMQPublisher::Stop() {
    if (!running_.load()) {
        return;
    }

    should_stop_ = true;
    queue_cv_.notify_all();

    if (publisher_thread_.joinable()) {
        publisher_thread_.join();
    }

    publisher_.reset();
    context_.reset();

    running_ = false;
    std::cout << "ZeroMQ Publisher stopped" << std::endl;
}

bool ZeroMQPublisher::SendMessage(const std::string& topic,
                                   const std::vector<uint8_t>& data,
                                   const std::map<std::string, std::string>& properties) {
    if (!running_.load()) {
        std::cerr << "Publisher not running" << std::endl;
        return false;
    }

    Message msg;
    msg.topic = topic;
    msg.data = data;
    msg.properties = properties;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        message_queue_.push(std::move(msg));
    }

    queue_cv_.notify_one();
    return true;
}

bool ZeroMQPublisher::SendMessage(const std::string& topic,
                                   const std::string& data,
                                   const std::map<std::string, std::string>& properties) {
    std::vector<uint8_t> vec_data(data.begin(), data.end());
    return SendMessage(topic, vec_data, properties);
}

void ZeroMQPublisher::SetHighWaterMark(int hwm) {
    high_water_mark_ = hwm;
}

void ZeroMQPublisher::SetLinger(int linger_ms) {
    linger_ms_ = linger_ms;
}

void ZeroMQPublisher::PublisherThread() {
    try {
        publisher_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::pub);

        publisher_->set(zmq::sockopt::sndhwm, high_water_mark_);
        publisher_->set(zmq::sockopt::linger, linger_ms_);

        publisher_->bind(endpoint_);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        while (!should_stop_.load()) {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                              [this] { return !message_queue_.empty() || should_stop_.load(); });

            while (!message_queue_.empty() && !should_stop_.load()) {
                Message msg = std::move(message_queue_.front());
                message_queue_.pop();
                lock.unlock();

                try {
                    zmq::message_t topic_msg(msg.topic.size());
                    std::memcpy(topic_msg.data(), msg.topic.data(), msg.topic.size());
                    publisher_->send(topic_msg, zmq::send_flags::sndmore);

                    std::string props_str = "{";
                    bool first = true;
                    for (const auto& [key, value] : msg.properties) {
                        if (!first) props_str += ",";
                        props_str += "\"" + key + "\":\"" + value + "\"";
                        first = false;
                    }
                    props_str += "}";

                    zmq::message_t props_msg(props_str.size());
                    std::memcpy(props_msg.data(), props_str.data(), props_str.size());
                    publisher_->send(props_msg, zmq::send_flags::sndmore);

                    zmq::message_t data_msg(msg.data.size());
                    std::memcpy(data_msg.data(), msg.data.data(), msg.data.size());
                    publisher_->send(data_msg, zmq::send_flags::none);

                } catch (const zmq::error_t& e) {
                    std::cerr << "Error sending message: " << e.what() << std::endl;
                }

                lock.lock();
            }
        }

    } catch (const zmq::error_t& e) {
        std::cerr << "Publisher thread error: " << e.what() << std::endl;
    }
}
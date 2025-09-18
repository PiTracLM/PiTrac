#ifndef ZEROMQ_SUBSCRIBER_H
#define ZEROMQ_SUBSCRIBER_H

#include <zmq.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include <vector>
#include <map>

namespace golf_sim {

class ZeroMQSubscriber {
public:
    using MessageHandler = std::function<void(
        const std::string& topic,
        const std::vector<uint8_t>& data,
        const std::map<std::string, std::string>& properties
    )>;

    ZeroMQSubscriber(const std::string& endpoint = "tcp://localhost:5556");
    ~ZeroMQSubscriber();

    bool Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }

    void Subscribe(const std::string& topic_filter);
    void Unsubscribe(const std::string& topic_filter);

    void SetMessageHandler(MessageHandler handler);

    void SetHighWaterMark(int hwm);
    void SetReceiveTimeout(int timeout_ms);

    void SetSystemIdToExclude(const std::string& system_id);
    std::string GetSystemIdToExclude() const { return system_id_to_exclude_; }

private:
    void SubscriberThread();

    std::map<std::string, std::string> ParseProperties(const std::string& json_str);

    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> subscriber_;

    std::string endpoint_;

    MessageHandler message_handler_;

    std::thread subscriber_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> should_stop_;

    std::vector<std::string> topic_filters_;

    int high_water_mark_ = 1000;
    int receive_timeout_ms_ = 100;

    std::string system_id_to_exclude_;
};

} // namespace golf_sim

#endif // ZEROMQ_SUBSCRIBER_H
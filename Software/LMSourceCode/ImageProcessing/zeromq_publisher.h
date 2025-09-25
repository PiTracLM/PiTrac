#ifndef ZEROMQ_PUBLISHER_H
#define ZEROMQ_PUBLISHER_H

#include <zmq.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#include <future>

namespace golf_sim {

class ZeroMQPublisher {
public:
    ZeroMQPublisher(const std::string& endpoint = "tcp://*:5556");
    ~ZeroMQPublisher();

    bool Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }

    bool SendMessage(const std::string& topic,
                     const std::vector<uint8_t>& data,
                     const std::map<std::string, std::string>& properties = {});

    bool SendMessage(const std::string& topic,
                     const std::string& data,
                     const std::map<std::string, std::string>& properties = {});

    void SetHighWaterMark(int hwm);
    void SetLinger(int linger_ms);

private:
    struct Message {
        std::string topic;
        std::vector<uint8_t> data;
        std::map<std::string, std::string> properties;
    };

    void PublisherThread();
    bool InitializeSocket();

    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> publisher_;

    std::string endpoint_;

    std::thread publisher_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> should_stop_;

    std::queue<Message> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::mutex socket_mutex_; 

    int high_water_mark_ = 1000;
    int linger_ms_ = 1000;
};

} // namespace golf_sim

#endif // ZEROMQ_PUBLISHER_H
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#ifdef __unix__

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <opencv2/core.hpp>

// Forward declarations — avoid pulling libcamera headers into every TU
class LibcameraJpegApp;

namespace golf_sim {

class GolfSimCamera;

// Runs Camera2 capture in a background thread. The libcamera pipeline
// (OpenCamera + ConfigureViewfinder) is initialized once at start() and
// reused across shots. Only StartCamera/StopCamera cycle per capture.
class Camera2Thread {
public:
    Camera2Thread() = default;
    ~Camera2Thread();

    Camera2Thread(const Camera2Thread&) = delete;
    Camera2Thread& operator=(const Camera2Thread&) = delete;

    void start();
    void stop();
    void arm();
    bool wait_until_ready(int timeout_ms = 10000);

private:
    void run();
    bool init_pipeline();
    void teardown_pipeline();

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool armed_ = false;
    std::atomic<bool> running_{false};

    // Persistent camera pipeline — created once, reused per shot
    std::unique_ptr<LibcameraJpegApp> app_;
    std::unique_ptr<GolfSimCamera> camera_;
    std::atomic<bool> pipeline_ready_{false};
    std::atomic<bool> pipeline_failed_{false};
};

} // namespace golf_sim

#endif // __unix__

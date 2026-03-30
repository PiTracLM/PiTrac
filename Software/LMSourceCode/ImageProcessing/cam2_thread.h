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
#include <opencv2/core.hpp>

namespace golf_sim {

// Runs Camera2 capture in a background thread within the single pitrac_lm process.
// Replaces the old separate Camera2 process + ActiveMQ image exchange.
//
// Lifecycle: start() → [arm() → captures → queues event] loop → stop()
//
// The thread blocks in WaitForCam2Trigger() waiting for the external hardware
// trigger. When triggered, it queues a Camera2ImageReceived event directly
// into the FSM event queue — same event type the old IPC dispatch used.
class Camera2Thread {
public:
    Camera2Thread() = default;
    ~Camera2Thread();

    Camera2Thread(const Camera2Thread&) = delete;
    Camera2Thread& operator=(const Camera2Thread&) = delete;

    void start();
    void stop();

    // Signal Camera2 to begin waiting for the external trigger.
    // Called by Camera1 FSM when the ball has stabilized.
    void arm();

private:
    void run();

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool armed_ = false;
    std::atomic<bool> running_{false};
};

} // namespace golf_sim

#endif // __unix__

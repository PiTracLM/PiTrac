/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#ifdef __unix__

#include "cam2_thread.h"
#include "gs_globals.h"
#include "gs_events.h"
#include "libcamera_interface.h"
#include "logging_tools.h"

namespace golf_sim {

Camera2Thread::~Camera2Thread() {
    stop();
}

void Camera2Thread::start() {
    running_ = true;
    thread_ = std::thread(&Camera2Thread::run, this);
}

void Camera2Thread::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        armed_ = true;
    }
    cv_.notify_one();

    if (thread_.joinable()) {
        thread_.join();
    }
}

void Camera2Thread::arm() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        armed_ = true;
    }
    cv_.notify_one();
    GS_LOG_MSG(info, "Camera2 thread armed for capture");
}

void Camera2Thread::run() {
    GS_LOG_MSG(info, "Camera2 thread started");

    while (running_ && GolfSimGlobals::golf_sim_running_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return armed_; });
            armed_ = false;
        }

        if (!running_ || !GolfSimGlobals::golf_sim_running_) break;

        GS_LOG_MSG(info, "Camera2 waiting for external trigger");

        cv::Mat image;
        if (WaitForCam2Trigger(image)) {
            GS_LOG_MSG(info, "Camera2 captured, queuing image for FSM");
            GolfSimEventElement event{new GolfSimEvent::Camera2ImageReceived{image}};
            GolfSimEventQueue::QueueEvent(event);
        } else {
            GS_LOG_MSG(error, "Camera2 WaitForCam2Trigger failed");
        }
    }

    GS_LOG_MSG(info, "Camera2 thread exiting");
}

} // namespace golf_sim

#endif // __unix__

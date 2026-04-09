/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#ifdef __unix__

#include "cam2_thread.h"
#include "gs_globals.h"
#include "gs_events.h"
#include "gs_options.h"
#include "gs_clubs.h"
#include "gs_camera.h"
#include "camera_hardware.h"
#include "libcamera_interface.h"
#include "logging_tools.h"
#include "still_image_libcamera_app.hpp"
#include "core/rpicam_app.hpp"
#include "core/still_options.hpp"

// Defined in libcamera_jpeg.cpp
bool cam2_run_event_loop(LibcameraJpegApp& app, cv::Mat& returnImg, bool send_priming_pulses = false);

namespace golf_sim {

// Defined in libcamera_interface.cpp
bool SetLibcameraTuningFileEnvVariable(const GolfSimCamera& camera);

Camera2Thread::~Camera2Thread() {
    stop();
}

bool Camera2Thread::init_pipeline() {
    GS_LOG_MSG(info, "Camera2 initializing persistent pipeline");

    camera_ = std::make_unique<GolfSimCamera>();
    camera_->camera_hardware_.init_camera_parameters(
        GsCameraNumber::kGsCamera2,
        GolfSimCamera::kSystemSlot2CameraType,
        GolfSimCamera::kSystemSlot2LensType,
        GolfSimCamera::kSystemSlot2CameraOrientation);

    app_ = std::make_unique<LibcameraJpegApp>();
    StillOptions* options = app_->GetOptions();

    char dummy[] = "DummyExecutableName";
    char* argv[] = {dummy, NULL};
    if (!options->Parse(1, argv)) {
        GS_LOG_MSG(error, "Camera2 failed to parse options");
        return false;
    }

    options->Set().camera = 1;
    options->Set().gain = LibCameraInterface::kCamera2Gain;
    options->Set().contrast = LibCameraInterface::kCamera2Contrast;
    options->Set().saturation = LibCameraInterface::kCamera2Saturation;
    options->Set().immediate = true;
    options->Set().timeout.set("100000s");
    options->Set().nopreview = true;
    options->Set().viewfinder_width = camera_->camera_hardware_.resolution_x_;
    options->Set().viewfinder_height = camera_->camera_hardware_.resolution_y_;
    options->Set().width = camera_->camera_hardware_.resolution_x_;
    options->Set().height = camera_->camera_hardware_.resolution_y_;
    options->Set().shutter.set("11111us");
    options->Set().info_text = "";

    const CameraHardware::CameraModel camera_model = GolfSimCamera::kSystemSlot2CameraType;
    if (camera_model != CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
        options->Set().denoise = "cdn_off";
    } else {
        options->Set().denoise = "auto";
    }

    if (GolfSimCamera::kSystemSlot2CameraOrientation == CameraHardware::CameraOrientation::kUpsideDown) {
        options->Set().transform = libcamera::Transform::VFlip;
    }

    if (!SetLibcameraTuningFileEnvVariable(*camera_)) {
        GS_LOG_MSG(error, "Camera2 failed to set tuning file");
        return false;
    }

    // Open and configure once — this is the expensive part (~500-1200ms)
    app_->OpenCamera();
    uint flags = RPiCamApp::FLAG_STILL_RGB;
    app_->ConfigureViewfinder(flags);

    pipeline_ready_ = true;
    GS_LOG_MSG(info, "Camera2 pipeline ready (OpenCamera + Configure done)");
    return true;
}

void Camera2Thread::teardown_pipeline() {
    if (app_) {
        app_->StopCamera();
        app_->Teardown();
    }
    app_.reset();
    camera_.reset();
    pipeline_ready_ = false;
}

void Camera2Thread::start() {
    running_ = true;
    thread_ = std::thread(&Camera2Thread::run, this);
}

bool Camera2Thread::wait_until_ready(int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!pipeline_ready_.load() && !pipeline_failed_.load()
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (pipeline_failed_.load()) {
        GS_LOG_MSG(error, "Camera2 pipeline initialization failed");
        return false;
    }
    if (!pipeline_ready_.load()) {
        GS_LOG_MSG(error, "Camera2 pipeline did not become ready within " + std::to_string(timeout_ms) + "ms");
        return false;
    }
    GS_LOG_MSG(info, "Camera2 pipeline ready, proceeding with Camera1 setup");
    return true;
}

void Camera2Thread::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        armed_ = true;
    }
    cv_.notify_one();

    // Ensure the global flag is false so cam2_run_event_loop's loop exits
    // after the StopCamera-induced Timeout unblocks Wait().
    GolfSimGlobals::golf_sim_running_ = false;

    // StopCamera cancels in-flight requests but also clears the message
    // queue, so any Timeout it produces may be wiped before Wait() sees it.
    // PostQuit guarantees Wait() unblocks with a Quit message.
    if (app_) {
        app_->StopCamera();
        app_->PostQuit();
    }

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

    if (!init_pipeline()) {
        GS_LOG_MSG(error, "Camera2 pipeline init failed, thread exiting");
        pipeline_failed_ = true;
        return;
    }

    while (running_ && GolfSimGlobals::golf_sim_running_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return armed_; });
            armed_ = false;
        }

        if (!running_ || !GolfSimGlobals::golf_sim_running_) break;

        if (!pipeline_ready_) {
            GS_LOG_MSG(error, "Camera2 pipeline not ready");
            continue;
        }

        // Update gain/contrast if club type changed (takes effect on next StartCamera)
        StillOptions* options = app_->GetOptions();
        if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::kPutter) {
            options->Set().gain = LibCameraInterface::kCamera2PuttingGain;
            options->Set().contrast = LibCameraInterface::kCamera2PuttingContrast;
        } else {
            options->Set().gain = LibCameraInterface::kCamera2Gain;
            options->Set().contrast = LibCameraInterface::kCamera2Contrast;
        }

        GS_LOG_MSG(info, "Camera2 starting capture (StartCamera only — pipeline pre-opened)");

        cv::Mat raw_image;
        if (cam2_run_event_loop(*app_, raw_image)) {
            cv::Mat undistorted = LibCameraInterface::undistort_camera_image(raw_image, *camera_);
            GS_LOG_MSG(info, "Camera2 captured, queuing image for FSM");
            GolfSimEventElement event{new GolfSimEvent::Camera2ImageReceived{undistorted}};
            GolfSimEventQueue::QueueEvent(event);
        } else {
            GS_LOG_MSG(error, "Camera2 capture failed");
        }
    }

    teardown_pipeline();
    GS_LOG_MSG(info, "Camera2 thread exiting");
}

} // namespace golf_sim

#endif // __unix__

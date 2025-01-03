/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// The data structures and methods in this file describe the various states that 
// the system's finite state machine can be in at any given time.
// Certain states have associated state information, such as a golf_ball structure or image.

#pragma once

#ifdef __unix__  // Ignore in Windows environment



#include <chrono>

#include <opencv2/core.hpp>

#include "golf_ball.h"
#include "gs_ipc_result.h"
#include "gs_events.h"


namespace golf_sim {

    // TBD - Put the FSM in a class at some point
    namespace state {

        // The following states are relevant to the camera 1 systtem that is watching 
        // for the ball to be hit.
        struct InitializingCamera1System {
        };

        struct WaitingForBall {
            std::chrono::steady_clock::time_point startTime_;
        };

        struct WaitingForSimulatorArmed {
            std::chrono::steady_clock::time_point startTime_;
        };

        struct WaitingForBallStabilization {
            std::chrono::steady_clock::time_point lastBallAcquisitionTime_;
            std::chrono::steady_clock::time_point startTime_;
            GolfBall cam1_ball_;
            cv::Mat ball_image_;
        };

        struct WaitingForBallHit {
            std::chrono::steady_clock::time_point  startTime_;
            GolfBall cam1_ball_;
            cv::Mat ball_image_;
            cv::Mat camera2_pre_image_;
        };

        struct BallHitNowWaitingForCam2Image {
            GolfBall cam1_ball_;
            cv::Mat ball_image_;
            cv::Mat camera2_pre_image_;
        };

        struct WaitingForCamera2PreImage {
            std::chrono::steady_clock::time_point  startTime_;
            GolfBall cam1_ball_;
            cv::Mat ball_image_;
        };


        // The following states are relevant to the camera 2 system.  That system
        // sets up the camera for external triggering and waits for it to be
        // triggered by the camera 1 system.
        struct InitializingCamera2System {
        };

        struct WaitingForCameraArmMessage {
            std::chrono::steady_clock::time_point startTime_;
        };

        struct WaitingForCameraTrigger {
            std::chrono::steady_clock::time_point startTime_;
        };

        struct Exiting {
        };

    }

    // Must contain each of the above states
    using GolfSimState = std::variant<  state::InitializingCamera1System,
                                        state::Exiting,
                                        state::WaitingForSimulatorArmed,
                                        state::WaitingForBall,
                                        state::WaitingForBallStabilization,
                                        state::WaitingForBallHit,
                                        state::WaitingForCamera2PreImage,
                                        state::BallHitNowWaitingForCam2Image,
                                        state::InitializingCamera2System,
                                        state::WaitingForCameraArmMessage,
                                        state::WaitingForCameraTrigger
                                    >;

    // Send an Active-MQ message to any listeners, such as the PiTrac GUI
    // Such messages can be states like "WaitingForBallStabilization"
    bool SendIPCStatusMessage(GsIPCResultType& message_type);
    void SendIPCErrorStatusMessage(const std::string& error_message);

    bool TestFsm();

    // The main system-loop.  It processes incoming events, changing states accordingly
    // This function is really the brains of the operation.
    bool RunGolfSimFsm(const GolfSimState& starting_state);

    // Control messages are external messages coming to the system.  
    // Currently driver/putter changes are the only such messages.
    bool ProcessControlMessageEvent(GolfSimEvent::ControlMessage &event);

    bool PerformSystemStartupTasks();
    bool PerformSystemShutdownTasks();

    // Signal to (for example), any threads that the FSM is going to be shutdown soon.
    void StartFsmShutdown();

}
	
#endif //#ifdef __unix__  // Ignore in Windows environment

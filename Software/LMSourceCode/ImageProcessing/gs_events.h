/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// This module defines the events, event queue, and associated processing for the various
// types of events that occur within the launch monitor system.
// These events largely drive the transitions in the system's finite state machine
// These events are separate from IPC events that deal with external messaging.

#pragma once

#ifdef __unix__  // Ignore in Windows environment


#include <boost/thread/thread.hpp>
#include <boost/lockfree/queue.hpp>

#include "blocking_queue.h"

#include <opencv2/core.hpp>

#include "golf_ball.h"
#include "gs_ipc_control_msg.h"

namespace golf_sim {

    class GolfSimEventBase {
    public:
        GolfSimEventBase() {};
        virtual ~GolfSimEventBase() {};

        virtual std::string Format() { return "GolfSimEventBase - Should have been overridden"; };
    };

    namespace GolfSimEvent {

        class EventLoopTick : public GolfSimEventBase
        {
        public:
            EventLoopTick() {};
            ~EventLoopTick() {};

            virtual std::string Format() override { return "EventLoopTick"; };
        };

        class BeginWatchingForBallHit : public GolfSimEventBase
        {
        public:
            BeginWatchingForBallHit() {};
            ~BeginWatchingForBallHit() {};

            virtual std::string Format() override { return "BeginWatchingForBallHit"; };
        };

        class BeginWaitingForBallPlaced : public GolfSimEventBase
        {
        public:
            BeginWaitingForBallPlaced() {};
            ~BeginWaitingForBallPlaced() {};

            virtual std::string Format() override { return "BeginWaitingForBallPlaced"; };
        };

        class CheckForBallStable : public GolfSimEventBase
        {
        public:
            CheckForBallStable() {};
            ~CheckForBallStable() {};

            virtual std::string Format() override { return "CheckForBallStable"; };
        };

        // The previously-located ball will be held in the stabilizing state class
        class BallStabilized : public GolfSimEventBase
        {
        public:
            BallStabilized(GolfBall& ball) { ball_ = ball; };
            ~BallStabilized() {};

            virtual std::string Format() override { return "BallStabilized"; };

            GolfBall ball_;
        };

        class BallHit : public GolfSimEventBase
        {
        public:
            BallHit(GolfBall& ball, cv::Mat& ball_hit_image) { ball_ = ball; ball_hit_image_ = ball_hit_image; };
            ~BallHit() {};

            virtual std::string Format() override { return "BallHit"; };

            GolfBall ball_; cv::Mat ball_hit_image_;
        };

        class ControlMessage : public GolfSimEventBase
        {
        public:
            ControlMessage(const GsIPCControlMsgType& message_type) { message_type_ = message_type; };
            ~ControlMessage() {};

            virtual std::string Format() override { return "ControlMessage - " + GsIPCControlMsg::FormatControlMessageType(message_type_); };

            GsIPCControlMsgType message_type_;
        };

        class BeginWaitingForSimulatorArmed : public GolfSimEventBase
        {
        public:
            BeginWaitingForSimulatorArmed() {};
            ~BeginWaitingForSimulatorArmed() {};

            virtual std::string Format() override { return "BeginWaitingForSimulatorArmed"; };
        };

        class SimulatorIsArmed : public GolfSimEventBase
        {
        public:
            SimulatorIsArmed() {};
            ~SimulatorIsArmed() {};

            virtual std::string Format() override { return "SimulatorIsArmed"; };
        };

        class CheckForCam2ImageReceived : public GolfSimEventBase
        {
        public:
            CheckForCam2ImageReceived() {};
            ~CheckForCam2ImageReceived() {};

            virtual std::string Format() override { return "CheckForCam2ImageReceived"; };
        };

        // TBD - this error event isn't really handled properly yet
        class FoundMultipleBalls : public GolfSimEventBase
        {
        public:
            FoundMultipleBalls() {};
            ~FoundMultipleBalls() {};

            virtual std::string Format() override { return "FoundMultipleBalls"; };

            unsigned int numberBallsFound = 0;
        };

        class Camera2ImageReceived : public GolfSimEventBase
        {
        public:
            Camera2ImageReceived(const cv::Mat& ball_hit_image) { ball_flight_image_ = ball_hit_image; };
            ~Camera2ImageReceived() {};

            virtual std::string Format() override { return "Camera2ImageReceived"; };

            const cv::Mat& GetBallFlightImage() const { return ball_flight_image_; };

        private:
            cv::Mat ball_flight_image_;
        };

        class Camera2PreImageReceived : public GolfSimEventBase
        {
        public:
            Camera2PreImageReceived(const cv::Mat& ball_pre_image) { ball_pre_image_ = ball_pre_image; };
            ~Camera2PreImageReceived() {};

            virtual std::string Format() override { return "Camera2PreImageReceived"; };

            const cv::Mat& GetBallFlightPreImage() const { return ball_pre_image_; };

        private:
            cv::Mat ball_pre_image_;
        };

        // The camera1 system has determined that the ball is ready to be hit.
        // The camera2 system should be ready to take a picture and send it
        // back to the other system when the camera2 is triggered.
        class ArmCamera2MessageReceived : public GolfSimEventBase
        {
        public:
            ArmCamera2MessageReceived(){ };
            ~ArmCamera2MessageReceived() {};

            virtual std::string Format() override { return "ArmCamera2MessageReceived"; };

            // TBD - Not sure if the camera1 system will send any additional information
        };

        // The camera2 has been triggered and a picture of the ball in flight has been taken. 
        class Camera2Triggered : public GolfSimEventBase
        {
        public:
            Camera2Triggered(cv::Mat &ball_flight_image) { ball_flight_image_ = ball_flight_image; };
            ~Camera2Triggered() {};

            virtual std::string Format() override { return "Camera2Triggered"; };

            const cv::Mat& GetBallFlightImage() const { return ball_flight_image_; };

        private:
            cv::Mat ball_flight_image_;
        };
       
        // Reset the FSM to the initializing state
        class Restart : public GolfSimEventBase
        { 
        public:
            Restart() {};
            ~Restart() {};

            virtual std::string Format() override { return "Restart"; };

        };

        class Exit : public GolfSimEventBase
        {
        public:
            Exit() {};
            ~Exit() {};

            virtual std::string Format() override { return "Exit"; };

        };

    }

    using PossibleEvent = std::variant< GolfSimEvent::EventLoopTick, 
                                        GolfSimEvent::BeginWaitingForSimulatorArmed,
                                        GolfSimEvent::SimulatorIsArmed,
                                        GolfSimEvent::BeginWaitingForBallPlaced,
                                        GolfSimEvent::CheckForBallStable, 
                                        GolfSimEvent::BallStabilized, 
                                        GolfSimEvent::BallHit,
                                        GolfSimEvent::ControlMessage,
                                        GolfSimEvent::BeginWatchingForBallHit,
                                        GolfSimEvent::FoundMultipleBalls,
                                        GolfSimEvent::ArmCamera2MessageReceived,
                                        GolfSimEvent::Camera2Triggered,
                                        GolfSimEvent::CheckForCam2ImageReceived,
                                        GolfSimEvent::Camera2ImageReceived,
                                        GolfSimEvent::Camera2PreImageReceived,
                                        GolfSimEvent::Exit,
                                        GolfSimEvent::Restart>;

    
    struct GolfSimEventElement {
        /*
        GolfSimEventElement(const GolfSimEventElement& rhs) { e_ = rhs.e_; };
        // GolfSimEventElement(const PossibleEvent& e) { e_ = e; };
        GolfSimEventElement() {
            GolfSimEvent::Restart r;
            e_ = r;
        };
        ~GolfSimEventElement();
        */

        // TBD - Do we need a std::move constructor and assignment here?  Will that work with a variant?

        GolfSimEventBase* e_;

        // TBD - Might add, for example, queue time
    };

    class GolfSimEventQueue {

    public:
        static const int kMaxQueueSize = 20;

        static bool QueueEvent(GolfSimEventElement& event);

        // Caller is responsible for deleting the event element's event pointer
        static bool DeQueueEvent(GolfSimEventElement& event, unsigned int time_out_ms = 0);

        // Cast a specific derived event type into the PossibleEvent variant type
        // TBD - Seems really clunky - how to improve?
        static PossibleEvent ConvertEventToPossibleEvent(GolfSimEventBase* event);

        static bool EventIsShutdownEvent(GolfSimEventBase* event);

        static bool EventIsControlEvent(GolfSimEventBase* event);

        // Not thread safe
        static int GetQueueLength();

        // static boost::lockfree::queue<GolfSimEventElement, boost::lockfree::capacity<kMaxQueueSize>> queue_;
        static queue<GolfSimEventElement> queue_;

        static int queue_size_;
    };
}

#endif // #ifdef __unix__  // Ignore in Windows environment

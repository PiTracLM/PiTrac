/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// This module defines the events, event queue, and assocaited processing for the various
// types of events that occur in the launch monitor system.

#include "gs_events.h"
#include "logging_tools.h" 

#ifdef __unix__  // Ignore in Windows environment


namespace golf_sim {

	// boost::lockfree::queue<GolfSimEventElement, boost::lockfree::capacity<GolfSimEventQueue::kMaxQueueSize>> GolfSimEventQueue::queue_;
    queue<GolfSimEventElement> GolfSimEventQueue::queue_(GolfSimEventQueue::kMaxQueueSize);
    int GolfSimEventQueue::queue_size_ = 0;

	bool GolfSimEventQueue::QueueEvent(GolfSimEventElement& event) {
		queue_.push(std::move(event));
        queue_size_++;
        return true;
	}

    int GolfSimEventQueue::GetQueueLength() {
        return queue_size_;
    }

	bool GolfSimEventQueue::DeQueueEvent(GolfSimEventElement& event, unsigned int time_out_ms) {
        bool status = queue_.pop(event, time_out_ms);
        if (status) {
            queue_size_--;
        }
        return status;
    }

    bool GolfSimEventQueue::EventIsShutdownEvent(GolfSimEventBase* event) {
        return (dynamic_cast<GolfSimEvent::Exit*>(event) != nullptr);
    }

    bool GolfSimEventQueue::EventIsControlEvent(GolfSimEventBase* event) {
        return (dynamic_cast<GolfSimEvent::ControlMessage*>(event) != nullptr);
    }

    // Down-cast a specific derived event type into the PossibleEvent variant type
    // TBD - Seems really clunky - how to improve?
    PossibleEvent GolfSimEventQueue::ConvertEventToPossibleEvent(GolfSimEventBase *event) {
        PossibleEvent possible_event;

        GolfSimEvent::BeginWaitingForBallPlaced* beginWaitingForBallPlaced = nullptr;
        GolfSimEvent::SimulatorIsArmed* simulatorIsArmed = nullptr;
        GolfSimEvent::BeginWaitingForSimulatorArmed* beginWaitingForSimulatorArmed = nullptr;
        GolfSimEvent::EventLoopTick* eventLoopTick = nullptr;
        GolfSimEvent::BallStabilized* ballStabilized = nullptr;
        GolfSimEvent::BallHit* ballHit = nullptr;
        GolfSimEvent::ControlMessage* controlMessage = nullptr;
        GolfSimEvent::FoundMultipleBalls* foundMultipleBalls = nullptr;
        GolfSimEvent::Camera2ImageReceived* cam2ImageReceived = nullptr;
        GolfSimEvent::Camera2PreImageReceived* cam2PreImageReceived = nullptr;
        GolfSimEvent::Restart* restart = nullptr;
        GolfSimEvent::CheckForBallStable* checkForBallStable = nullptr;
        GolfSimEvent::BeginWatchingForBallHit* beginWatchingForBallHit = nullptr;
        GolfSimEvent::Exit* exit = nullptr;
        GolfSimEvent::CheckForCam2ImageReceived* checkForCam2ImageReceived = nullptr;
        GolfSimEvent::Camera2Triggered* cam2Triggered = nullptr;
        GolfSimEvent::ArmCamera2MessageReceived* armCamera2MessageReceived = nullptr;


        if ((beginWaitingForSimulatorArmed = dynamic_cast<GolfSimEvent::BeginWaitingForSimulatorArmed*>(event))) {
            possible_event = *beginWaitingForSimulatorArmed;
        }
        else if ((simulatorIsArmed = dynamic_cast<GolfSimEvent::SimulatorIsArmed*>(event))) {
            possible_event = *simulatorIsArmed;
        }
        else if ((beginWaitingForBallPlaced = dynamic_cast<GolfSimEvent::BeginWaitingForBallPlaced*>(event))) {
            possible_event = *beginWaitingForBallPlaced;
        }
        else if ((eventLoopTick = dynamic_cast<GolfSimEvent::EventLoopTick*>(event))) {
            possible_event = *eventLoopTick;
        }
        else if ((ballStabilized = dynamic_cast<GolfSimEvent::BallStabilized*>(event))) {
            possible_event = *ballStabilized;
        }
        else if ((ballHit = dynamic_cast<GolfSimEvent::BallHit*>(event))) {
            possible_event = *ballHit;
        }
        else if ((controlMessage = dynamic_cast<GolfSimEvent::ControlMessage*>(event))) {
            possible_event = *controlMessage;
        }
        else if ((beginWatchingForBallHit = dynamic_cast<GolfSimEvent::BeginWatchingForBallHit*>(event))) {
            possible_event = *beginWatchingForBallHit;
        }
        else if ((foundMultipleBalls = dynamic_cast<GolfSimEvent::FoundMultipleBalls*>(event))) {
            possible_event = *foundMultipleBalls;
        }
        else if ((cam2Triggered = dynamic_cast<GolfSimEvent::Camera2Triggered*>(event))) {
            possible_event = *cam2Triggered;
        }
        else if ((cam2ImageReceived = dynamic_cast<GolfSimEvent::Camera2ImageReceived*>(event))) {
            possible_event = *cam2ImageReceived;
        }
        else if ((cam2PreImageReceived = dynamic_cast<GolfSimEvent::Camera2PreImageReceived*>(event))) {
            possible_event = *cam2PreImageReceived;
        }
        else if ((checkForCam2ImageReceived = dynamic_cast<GolfSimEvent::CheckForCam2ImageReceived*>(event))) {
            possible_event = *checkForCam2ImageReceived;
        }
        else if ((armCamera2MessageReceived = dynamic_cast<GolfSimEvent::ArmCamera2MessageReceived*>(event))) {
            possible_event = *armCamera2MessageReceived;
        }
        else if ((restart = dynamic_cast<GolfSimEvent::Restart*>(event))) {
            possible_event = *restart;
        }
        else if ((exit = dynamic_cast<GolfSimEvent::Exit*>(event))) {
            possible_event = *exit;
        }
        else if ((checkForBallStable = dynamic_cast<GolfSimEvent::CheckForBallStable*>(event))) {
            possible_event = *checkForBallStable;
        }
        else {
            GS_LOG_MSG(error, "ConvertEventToPossibleEvent could not cast to PossibleEvent variant type");
        }

        return possible_event;
    }

}

#endif // #ifdef __unix__  // Ignore in Windows environment

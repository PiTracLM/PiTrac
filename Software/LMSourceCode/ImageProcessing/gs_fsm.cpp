/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */



#ifdef __unix__  // Ignore in Windows environment


#include <variant>
#include <thread>
#include "gs_format_lib.h"
#include <iostream>
#include <signal.h>
#include <sys/signalfd.h>
#include <poll.h>

#include "logging_tools.h"
#include "worker_thread.h"
#include "gs_ipc_message.h"
#include "gs_options.h"
#include "golf_ball.h"
#include "gs_camera.h"
#include "gs_events.h"
#include "gs_config.h"
#include "ball_image_proc.h"
#include "gs_ipc_system.h"
#include "gs_ui_system.h"
#include "gs_sim_interface.h"
#include "pulse_strobe.h"
#include "libcamera_interface.h"

#include "gs_fsm.h"


namespace golf_sim {

    static int signal_received;
    static void default_signal_handler(int signal_number)
    {
        GS_LOG_TRACE_MSG(trace, "GolfSim Received Signal:" + std::to_string(signal_number) + ".");

        signal_received = signal_number;
        GolfSimGlobals::golf_sim_running_ = false;
    }

    static long kMaxCam2ImageReceivedTimeMs = 4000;

    const int kWaitForBallPauseMs = 500;
    const int kEventLoopPauseMs = 5000;
    const int kBallStabilizationTime = 1; // seconds

    // This is where the strobed-ball image will be put so that the web interface can display it
    std::string kWebServerCamera2Image;
    std::string kWebServerLastTeedBallImage;

    namespace helper {
        template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
    }

    TimedCallbackThread* BallStabilizationCheckTimerThread = nullptr;
    TimedCallbackThread* ReceivedCam2ImageCheckTimerThread = nullptr;


    void queueBallStabilizationCheck() {

        GS_LOG_TRACE_MSG(trace, "Queueing CheckForBallStableEvent.");

        if (GolfSimGlobals::golf_sim_running_) {
            GolfSimEventElement CheckForBallStableEvent{ new GolfSimEvent::CheckForBallStable{ } };
            GolfSimEventQueue::QueueEvent(CheckForBallStableEvent);
        }
        else {
            GS_LOG_TRACE_MSG(trace, "Not Queueing CheckForBallStableEvent - System Shutting down.");
        }

        // No need to restart timer.  The ball stabilizing state will do so if appropriate.
    }

    void setupBallStabilizationCheckTimer() {

        GS_LOG_TRACE_MSG(trace, "setupBallStabilizationCheckTimer.");
        // queueBallStabilizationCheck();

        if (BallStabilizationCheckTimerThread == nullptr) {
            BallStabilizationCheckTimerThread = new TimedCallbackThread("BallStabilizationCheckTimerThread", kBallStabilizationTime * 1000, queueBallStabilizationCheck);
            BallStabilizationCheckTimerThread->CreateThread();
        }
    }


    void queueCam2ImageReceivedCheck() {

        GS_LOG_TRACE_MSG(trace, "Queueing CheckForCam2ImageReceived.");

        if (GolfSimGlobals::golf_sim_running_) {
            GolfSimEventElement checkForCam2ImageReceivedEvent{ new GolfSimEvent::CheckForCam2ImageReceived{ } };
            GolfSimEventQueue::QueueEvent(checkForCam2ImageReceivedEvent);
        }
        else {
            GS_LOG_TRACE_MSG(trace, "Not Queueing CheckForBallStableEvent - System Shutting down.");
        }

        // No need to restart timer.  The rest of the system will do so if appropriate.
    }

    void setupCam2ImageReceivedCheckTimer() {

        GS_LOG_TRACE_MSG(trace, "setupCam2ImageReceivedCheckTimer - Setting call back for " + std::to_string(kMaxCam2ImageReceivedTimeMs) + " milliseconds.");

        if (ReceivedCam2ImageCheckTimerThread == nullptr) {
            ReceivedCam2ImageCheckTimerThread = new TimedCallbackThread("setupCam2ImageReceivedCheckTimerThread", kMaxCam2ImageReceivedTimeMs * 1000, queueCam2ImageReceivedCheck);
            ReceivedCam2ImageCheckTimerThread->CreateThread();
        }
    }



    /*********** InitializingCamera1System  ************/

    GolfSimState onEvent(const state::InitializingCamera1System& initializing,
        const GolfSimEvent::Restart& restart) {
        GS_LOG_MSG(debug, "GolfSim state transition: Initializing - Received Restart - Next state WaitingForSimArmed or WaitingForBall. ");

        // Let the monitor interface know what's happening
        GsUISystem::SendIPCStatusMessage(GsIPCResultType::kInitializing);

        // If we're already armed, just start waiting for a ball to appear.
        if (GsSimInterface::GetAllSystemsArmed()) {
            GolfSimEventElement beginWaitingForBallPlacedEvent{ new GolfSimEvent::BeginWaitingForBallPlaced{ } };
            GolfSimEventQueue::QueueEvent(beginWaitingForBallPlacedEvent);

            return state::WaitingForBall{ std::chrono::steady_clock::now(), false /* have not sent the waiting-for-ball IPC message yet */ };
        }

        GolfSimEventElement beginWaitingForSimulatorArmedEvent{ new GolfSimEvent::BeginWaitingForSimulatorArmed{ } };
        GolfSimEventQueue::QueueEvent(beginWaitingForSimulatorArmedEvent);

        return state::WaitingForSimulatorArmed{ std::chrono::steady_clock::now() };

    }


    /************ WaitingForBall ***********/

    GolfSimState onEvent(const state::WaitingForBall& waitingForBallState,
        const GolfSimEvent::CheckForCam2ImageReceived& checkForCam2ImageReceived) {
        GS_LOG_MSG(debug, "GolfSim state transition: WaitingForBall - Received CheckForCam2ImageReceived ");

        // We have cycled back to waiting for a ball to show up, and we received a 
        // reminder to check to see if we received an image from the cam2 system.
        // But if we got here, we already DID receive it (most likely).  
        // So just ignore.
        // TBD - a little sloppy - why not just get rid of the late-breaking event?

        return state::WaitingForBall{ std::chrono::steady_clock::now(), false /* have not sent the waiting-for-ball IPC message yet */ };
    }

    GolfSimState onEvent(const state::WaitingForBall& waitingForBallState,
        const GolfSimEvent::BeginWaitingForBallPlaced& beginWaitingForBallPlacedEvent) {
        GS_LOG_MSG(trace, "State: WaitingForBall - Received BeginWaitingForBallPlacedEvent - Now waiting for ball to show up.");

        // Let the monitor interface know what's happening
        // TBD - see if we need to move this back to the initializating state
        if (!waitingForBallState.already_sent_waiting_ipc_message) {
            GsUISystem::SendIPCStatusMessage(GsIPCResultType::kWaitingForBallToAppear);
        }

        // This check will be called repeatedly by re-queuing events.
        // That way, we can process other, asynchronous, events like button presses and such as we
        // continue to wait to see a ball.

        // Otherwise, check for the ball.  The check SHOULD yield to other threads
        cv::Mat img;
        GolfBall ball;

        bool found = CheckForBall(ball, img);

        if (img.empty()) {
            GS_LOG_MSG(warning, "CheckForBall() return image was empty - ignoring.");
        }

        if (found) {

            if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1Calibrate ||
                GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2Calibrate) {

                // Queue a restart state change just to ensure we don't do anything
                // else before the shutdown
                GolfSimEventElement restartEvent{ new GolfSimEvent::Restart{ } };
                GolfSimEventQueue::QueueEvent(restartEvent);

                StartFsmShutdown();
            }

            std::chrono::steady_clock::time_point lastBallAcquisitionTime = std::chrono::steady_clock::now();

            // Schedule the timer for a determined (short) time in the future.  When the timer goes off, an
            // CheckForBallStable event will be injected
            // Create a thread that will queue an event-loop queueBallStabilizationCheck in the future
            setupBallStabilizationCheckTimer();

            // Let the monitor interface know what's happening
            GsUISystem::SendIPCStatusMessage(GsIPCResultType::kPausingForBallStabilization);

            return state::WaitingForBallStabilization{ lastBallAcquisitionTime, std::chrono::steady_clock::now(), ball, img };
        }


        // The ball was NOT found.  Report it and then get back into the event loop so 
        // we can check again asap.
        if (GolfSimOptions::GetCommandLineOptions().artifact_save_level_ == ArtifactSaveLevel::kAll) {
            LoggingTools::LogImage("", img, std::vector < cv::Point >{}, true, "log_last_no_ball_img");
        }

        // Create an image that the monitor can show the player in order to see where the LM is looking
        // for the ball, if there is a search area set.
        // TBD - Figure out a clearer way to pass back the search-area information

        if (ball.search_area_radius_ > 0.01) {
            cv::Scalar c1(255, 255, 255);
            cv::circle(img, cv::Point(ball.search_area_center_[0], ball.search_area_center_[1]), ball.search_area_radius_, c1, 2);
        }

        // Update what it is the LM 'sees' when looking for th teed ball to allow users to see
        // if they need to reposition that ball.
        GsUISystem::SaveWebserverImage(GsUISystem::kWebServerBallSearchAreaImage, img, true);

        // Queue up another event to get back here (after processing any other waiting events)
        GolfSimEventElement newBeginWaitingForBallPlacedEvent{ new GolfSimEvent::BeginWaitingForBallPlaced{ } };
        GolfSimEventQueue::QueueEvent(newBeginWaitingForBallPlacedEvent);

        return state::WaitingForBall{ std::chrono::steady_clock::now(), true /* already_sent_waiting_ipc_message */};
    }


    /*********** WaitingForBallStabilization ************/

    GolfSimState onEvent(const state::WaitingForBallStabilization& waitingForBallStabilization,
        const GolfSimEvent::CheckForBallStable& CheckForBallStableEvent) {

        GS_LOG_MSG(debug, "GolfSim state transition: WaitingForBallStabilization - Received CheckForBallStableEvent ");

        GolfBall ball;
        cv::Mat img;

        bool found = CheckForBall(ball, img);
        // LoggingTools::LogImage("", img, std::vector < cv::Point >{}, true, "log_last_ball_2bcompared2_still.png");


        // We were called by a timer in a separate thread.  Clean up that thread
        if (BallStabilizationCheckTimerThread != nullptr) {
            BallStabilizationCheckTimerThread->ExitThread();
            delete BallStabilizationCheckTimerThread;
            BallStabilizationCheckTimerThread = nullptr;
        }

        bool ballMoved = true;

        // If the ball hasn't been found, then whether the ball moved is moot
        if (found) {
            ballMoved = ball.CheckIfBallMoved(waitingForBallStabilization.cam1_ball_, 10 /* max center move pixels */, 6 /* % radius change */);
        }
        else {
            GS_LOG_MSG(info, "=============== Ball Lost Before Stabilizing - Will look for ball again.");
        }

        // TBD - Perform state transition processing here

        // If the ball moved, start over by finding it again
        if (!found || ballMoved) {
            GS_LOG_MSG(info, "=============== Ball Moved (or was lost) Before Stabilizing - Will look for ball again.");

            // This event will cause the WaitingForBall state to begin waiting for the ball to appear teed up again
            GolfSimEventElement beginWaitingForBallPlaced{ new GolfSimEvent::BeginWaitingForBallPlaced{ } };
            GolfSimEventQueue::QueueEvent(beginWaitingForBallPlaced);

            return state::WaitingForBall{ std::chrono::steady_clock::now(), false /* send the ball-waiting message again*/};
        }

        // The ball has stabilized.  Now we just have to wait for the ball to be hit
        GS_LOG_MSG(info, "=============== Ball Stabilized - Let's Play Golf!  (Waiting for hit)\n\n");

        // Whatever happens, this is a new shot with a new shot number
        GsSimInterface::IncrementShotCounter();

        // Let the second camera know to be ready for a ball hit
        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kRequestForCamera2Image);
        GolfSimIpcSystem::SendIpcMessage(ipc_message);

        // The sending of the priming pulses will include a trigger to make the camera2
        // take a pre-image.  That will in turn send an event to the camera1 system that 
        // will eventually set up the WaitingForBallHit state.
        bool use_fast_speed = (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::GsClubType::kDriver);
        if (!PulseStrobe::SendCameraPrimingPulses(use_fast_speed)) {
            GS_LOG_MSG(error, "FAILED to PulseStrobe::SendCameraPrimingPulses");
        }

        // Log the pertinent images for debugging & analysis
        // Add a center dot to the first image to analyze camera off-center
        // LoggingTools::LogImage("", img, std::vector < cv::Point >{ cv::Vec2i(1456 / 2, 1088 / 2) }, true, "log_view_last_ball_found_img.png");

        if (GolfSimOptions::GetCommandLineOptions().artifact_save_level_ != ArtifactSaveLevel::kNoArtifacts) {
            if (GolfSimCamera::kLogDiagnosticImagesToUniqueFiles) {
                // Save a unique version of the webserver image into a directory that will not get
                // over-written.  A unique timestamp will be added to the file name
                LoggingTools::LogImage(kWebServerLastTeedBallImage + "_Shot_" + std::to_string(GsSimInterface::GetShotCounter()) + "_", img, std::vector < cv::Point >{});
            }

            // In any case, save the image with a non-unique name that will be overwritten on the next show, but that the GUI
            // will be able to depend on the name of.
            LoggingTools::LogImageWithCircles("", img, std::vector < GsCircle >{ball.ball_circle_}, true, kWebServerLastTeedBallImage + ".png");
        }

        // TBD - Not sure this is necessary if the Java servlet is smart enough
        // to figure out what it needs to display
        GsUISystem::ClearWebserverImages();

        // TBD - Probably remove.  Pre-image subtraction was an idea that never panned out as well as we'd hoped.
        if (GolfSimCamera::kUsePreImageSubtraction) {
            return state::WaitingForCamera2PreImage{ std::chrono::steady_clock::now(), ball, img };
        }
        else {
            // This even will cause the waitingForBallHit state to begin watching for the hit
            GolfSimEventElement beginWatchingForBallHit{ new GolfSimEvent::BeginWatchingForBallHit{ } };
            GolfSimEventQueue::QueueEvent(beginWatchingForBallHit);

            cv::Mat empty_mat;
            return state::WaitingForBallHit{ std::chrono::steady_clock::now(),
                                             waitingForBallStabilization.cam1_ball_,
                                             waitingForBallStabilization.ball_image_,
                                             empty_mat };

        }
    }


    /*********** WaitingForCamera2PreImage ************/
    GolfSimState onEvent(const state::WaitingForCamera2PreImage& waitingForCamera2PreImage,
        const GolfSimEvent::Camera2PreImageReceived& camera2PreImageReceived) {
        GS_LOG_MSG(debug, "GolfSim state transition: WaitingForCamera2PreImage - Received Camera2PreImageReceived.");

        // This even will cause the waitingForBallHit state to begin watching for the hit
        GolfSimEventElement beginWatchingForBallHit{ new GolfSimEvent::BeginWatchingForBallHit{ } };
        GolfSimEventQueue::QueueEvent(beginWatchingForBallHit);

        return state::WaitingForBallHit{ std::chrono::steady_clock::now(),
                                         waitingForCamera2PreImage.cam1_ball_,
                                         waitingForCamera2PreImage.ball_image_,
                                         camera2PreImageReceived.GetBallFlightPreImage() };
    }

    

    /*********** WaitingForSimulatorArmed ************/

        GolfSimState onEvent(const state::WaitingForSimulatorArmed& waitingForSimulatorArmed,
            const GolfSimEvent::BeginWaitingForSimulatorArmed& beginWaitingForSimulatorArmed) {
        GS_LOG_MSG(debug, "GolfSim state transition: WaitingForSimulatorArmed - Received SimulatorArmed.");

        // Let the monitor interface know what's happening so it can alert the user
        GsUISystem::SendIPCStatusMessage(GsIPCResultType::kWaitingForSimulatorArmed);

        // Wait a moment so that we're not spinning too much
        sleep(1);

        if (GsSimInterface::GetAllSystemsArmed()) {
            GolfSimEventElement beginWaitingForBallPlacedEvent{ new GolfSimEvent::BeginWaitingForBallPlaced{ } };
            GolfSimEventQueue::QueueEvent(beginWaitingForBallPlacedEvent);

            return state::WaitingForBall{ std::chrono::steady_clock::now(), false /* have not sent the waiting-for-ball IPC message yet */};
        }

        // Otherwise, keep in waiting state
        GolfSimEventElement nextBeginWaitingForSimulatorArmed{ new GolfSimEvent::BeginWaitingForSimulatorArmed{ } };
        GolfSimEventQueue::QueueEvent(nextBeginWaitingForSimulatorArmed);

        return state::WaitingForSimulatorArmed{ std::chrono::steady_clock::now() };
    }

    // TBD - Not certain we are going to use this state
    GolfSimState onEvent(const state::WaitingForSimulatorArmed& waitingForSimulatorArmed,
        const GolfSimEvent::SimulatorIsArmed& simulatorArmed) {
        GS_LOG_MSG(debug, "GolfSim state transition: WaitingForSimulatorArmed - Received SimulatorArmed.");

        // Let the monitor interface know what's happening so it can alert the user
        GsUISystem::SendIPCStatusMessage(GsIPCResultType::kWaitingForSimulatorArmed);

        // The simulator is now armed.
        // The following will cause the waitingForBall state to begin watching for the ball
        GolfSimEventElement beginWaitingForSimulatorArmed{ new GolfSimEvent::BeginWaitingForSimulatorArmed{ } };
        GolfSimEventQueue::QueueEvent(beginWaitingForSimulatorArmed);

        return state::WaitingForBall{ std::chrono::steady_clock::now(), false /* have not sent the waiting-for-ball IPC message yet */ };
    }


    /*********** WaitingForBallHit ************/

    GolfSimState onEvent(const state::WaitingForBallHit& waitingForBallHit,
        const GolfSimEvent::BeginWatchingForBallHit& beginWatchingForBallHit) {
        GS_LOG_MSG(debug, "GolfSim state transition: WaitingForBallHit - Received BeginWatchingForBallHit.");

        // TBD - Figure out a better way to time this.  Need to give camera2 a moment to get ready to
        // receive and process the priming pulses and also probably the ready-to-play message
        sleep(1);

        cv::Mat image;  // Not sure if actually needed

        bool ball_hit = false;

        // Let the monitor interface know what's happening
        GsUISystem::SendIPCStatusMessage(GsIPCResultType::kBallPlacedAndReadyForHit);

        if (!WatchForHitAndTrigger(waitingForBallHit.cam1_ball_, image, ball_hit)) {
            GS_LOG_MSG(error, "Failed to WatchForHitAndTrigger.  Restarting GolfSim FSM.");
            GolfSimEventElement restartEvent{ new GolfSimEvent::Restart{ } };
            GolfSimEventQueue::QueueEvent(restartEvent);
            return state::InitializingCamera1System{};
        }

        // TBD - Consider case where we did NOT get a ball hit indication for some reason
        GS_LOG_MSG(info, "============= BALL HIT ===============\n");

        // Make sure we do something sensible if we don't receive an image from the camera 2
        // system in a reasonable amount of time.
        // TBD - How to turn the check off when we DO get an image?
        setupCam2ImageReceivedCheckTimer();

        // Start waiting for the camera 2 image to returned. 
        // TBD - Should probably start timer to make sure we get an image soon.
        return state::BallHitNowWaitingForCam2Image{ waitingForBallHit.cam1_ball_, waitingForBallHit.ball_image_, waitingForBallHit.camera2_pre_image_ };
    }

    GolfSimState onEvent(const state::WaitingForBallHit& waitingForBallHit,
        const GolfSimEvent::BallHit& ballHit) {
        GS_LOG_MSG(debug, "GolfSim state transition: WaitingForBallHit - Received BallHit ");

        // TBD - Perform state transition processing here

        return state::BallHitNowWaitingForCam2Image{ waitingForBallHit.cam1_ball_, waitingForBallHit.ball_image_, waitingForBallHit.camera2_pre_image_ };
    }

    /*********** BallHitNowWaitingForCam2Image ************/

    GolfSimState onEvent(const state::BallHitNowWaitingForCam2Image& BallHitNowWaitingForCam2Image,
        const GolfSimEvent::Camera2ImageReceived& cam2ImageReceived) {
        GS_LOG_MSG(debug, "GolfSim state transition: BallHitNowWaitingForCam2Image - Received Camera2ImageReceived ");

        // TBD - Perform state transition processing here
        // Most importantly, all of the hit analysis!

        const cv::Mat& cam2_mat = cam2ImageReceived.GetBallFlightImage();

        GolfBall result_ball;
        cv::Vec3d rotation_results;
        cv::Mat exposures_image;
        std::vector<GolfBall> exposure_balls;

        if (!GolfSimCamera::ProcessReceivedCam2Image(BallHitNowWaitingForCam2Image.ball_image_,
                                                    cam2_mat,
                                                    BallHitNowWaitingForCam2Image.camera2_pre_image_,
                                                    result_ball,
                                                    rotation_results,
                                                    exposures_image,
                                                    exposure_balls)) {
            GS_LOG_MSG(error, "GolfSim FSM could not ProcessReceivedCam2Image.");
#ifdef __unix__ 
            // Give the webserver UI something to show the user
            GsUISystem::SaveWebserverImage(GsUISystem::kWebServerErrorExposuresImage, cam2_mat);
#endif

            GsUISystem::SendIPCErrorStatusMessage("GolfSim FSM could not ProcessReceivedCam2Image.");

            GS_LOG_MSG(info, "BALL_HIT_CSV, " + std::to_string(GsSimInterface::GetShotCounter()) + ", (carry - Error), (Total - Error), (Side Dest - Error), (Smash Factor - Error), (Club Speed - Error), "
                + std::to_string(0) + ", "
                + std::to_string(0) + ", "
                + std::to_string(0) + ", "
                + std::to_string(0) + ", "
                + std::to_string(0)
                + ", (Descent Angle-Error), (Apex-Error), (Flight Time-Error), (Type-Error)"
            );

        }
        else {

            GS_LOG_TRACE_MSG(trace, "Received and processed cam2ImageReceived.  Now sending Results to any connected Golf Simulator");
            GsResults results(result_ball);


            // Get the result to the golf simulator ASAP
            if (!GsSimInterface::SendResultsToGolfSims(results)) {
                GS_LOG_MSG(error, "GolfSim FSM could not SendResultsToGolfSim.");
            }

            GS_LOG_TRACE_MSG(trace, "Received and processed cam2ImageReceived.  Now sending an IPC Results Message:");

            std::string s;

            float velocity_time_period = (float)result_ball.time_between_ball_positions_for_velocity_uS_ / 1000.0;
            auto velocity_time_period_string = GS_FORMATLIB_FORMAT("{: <6.2f}", velocity_time_period);
            s = " Time between chosen images for velocity calculation: " + velocity_time_period_string + " ms.";

            GsUISystem::SendIPCHitMessage(result_ball, s);

#ifdef __unix__ 
            if (exposures_image.empty()) {
                GS_LOG_MSG(warning, "Exposures_image from ProcessReceivedCamera2 was empty.");
            }
            GsUISystem::SaveWebserverImage(GsUISystem::kWebServerResultBallExposureCandidates,
                exposures_image, exposure_balls);
#endif

        }

        // Setup to go through the whole sequence again
        GolfSimEventElement beginWaitingForBallPlacedEvent{ new GolfSimEvent::BeginWaitingForBallPlaced{ } };
        GolfSimEventQueue::QueueEvent(beginWaitingForBallPlacedEvent);


        return state::WaitingForBall{ std::chrono::steady_clock::now(), false /* have not sent the waiting-for-ball IPC message yet */ };
    }

    GolfSimState onEvent(const state::BallHitNowWaitingForCam2Image& BallHitNowWaitingForCam2Image,
        const GolfSimEvent::CheckForCam2ImageReceived& checkForCam2ImageReceived) {
        GS_LOG_MSG(debug, "GolfSim state transition: BallHitNowWaitingForCam2Image - Received CheckForCam2ImageReceived - Will restart ");

        GS_LOG_MSG(error, "BallHitNowWaitingForCam2Image - Timed out waiting for Cam2Image.  Restarting... ");

        GolfSimEventElement restartEvent{ new GolfSimEvent::Restart{ } };
        GolfSimEventQueue::QueueEvent(restartEvent);

        return state::InitializingCamera1System{};
    }


    GolfSimState onEvent(const auto& state, const GolfSimEvent::EventLoopTick& eventLoopTick) {
        GS_LOG_MSG(debug, "Got an eventLoopTick.  Ignoring");

        // TBD - At some point, may want to check to make sure we're not shutting down or
        // perform some statistics updating, or etc.

        return state;
    }


    /*********** InitializingCamera2System  ************/

    GolfSimState onEvent(const state::InitializingCamera2System& initializing,
        const GolfSimEvent::Restart& restart) {
        GS_LOG_MSG(debug, "GolfSim state transition: InitializingCamera2System - Received Restart - Next state WaitingForCameraArmMessage. ");

        if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1TestStandalone ||
            GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2TestStandalone) {
            // for now, we will just fake the camera2 arm message
            GolfSimEventElement armCamera2MessageReceived{ new GolfSimEvent::ArmCamera2MessageReceived{ } };
            GolfSimEventQueue::QueueEvent(armCamera2MessageReceived);
        }

        return state::WaitingForCameraArmMessage{ };
    }


    GolfSimState onEvent(const state::WaitingForCameraArmMessage& initializing,
        const GolfSimEvent::ArmCamera2MessageReceived& armCamera2MessageReceived) {
        GS_LOG_MSG(debug, "GolfSim state transition: WaitingForCameraArmMessage - Received ArmCamera2MessageReceived - WAITING FOR EXTERNAL TRIGGER - Next state InitializingCamera2System. ");

        // Prepare for and start waiting for the camera to receive an external trigger
        // and take a picture.

        cv::Mat image;  // Not sure if actually needed

        GS_LOG_TRACE_MSG(trace, "\n===========================\nGolfSim:  Cam2 System - Waiting for ball.\n");
        if (!WaitForCam2Trigger(image)) {
            GS_LOG_MSG(error, "Failed to WaitForCam2Trigger.");
        }

        GS_LOG_TRACE_MSG(trace, "WaitForCam2Trigger returned with image. ");

        // Send the image back to the cam1 system
        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kCamera2Image);
        ipc_message.SetImageMat(image);
        GolfSimIpcSystem::SendIpcMessage(ipc_message);

        // Save the image for later analysis
        if (GolfSimOptions::GetCommandLineOptions().artifact_save_level_ != ArtifactSaveLevel::kNoArtifacts) {
            if (GolfSimCamera::kLogDiagnosticImagesToUniqueFiles) {
                // Save a unique version of the webserver image into a directory that will not get
                // over-written.  A unique timestamp will be added to the file name
                // The camera2 system isn't sending message to the simulator system, so we need to update 
                // the shot counter here manually
                GsSimInterface::IncrementShotCounter();
                LoggingTools::LogImage(kWebServerCamera2Image + "_Shot_" + std::to_string(GsSimInterface::GetShotCounter()) + "_", image, std::vector < cv::Point >{});
            }

            // In any case, also save the image with a non-unique name that will be overwritten on the next show, but that the GUI
            // will be able to depend on the name of.

            // REMOVE THE TRACE - TBD
            GS_LOG_TRACE_MSG(trace, "Image saving using GolfSimOptions::GetCommandLineOptions().base_image_logging_dir_ = " + GolfSimOptions::GetCommandLineOptions().base_image_logging_dir_);
            LoggingTools::LogImage("", image, std::vector < cv::Point >{}, true, kWebServerCamera2Image);
        }

        // Get a restart queued up to start all over
        GolfSimEventElement restartEvent{ new GolfSimEvent::Restart{ } };
        GolfSimEventQueue::QueueEvent(restartEvent);

        return state::InitializingCamera2System{ };
    }


    /*********** Exit state/event ************/
/* TBD
    GolfSimState onEvent(const auto&,
        const GolfSimEvent::Exit& exit) {

        // The state transition when an exit is received
        // is the same for any current state
        GS_LOG_TRACE_MSG(trace, "GolfSim state transition: <Any State> - Received Exit - Exiting. ");

        StartFsmShutdown();

        return state::Exiting{ };
    }

    GolfSimState onEvent(const state::Exiting& exiting,
        const auto&) {

        // The state transition when exiting
        // is the same for any event - it's ignored.
        GS_LOG_TRACE_MSG(trace, "GolfSim state transition: Exiting - Received <Any event> - Still Exiting. ");
        return state::Exiting{ };
    }
*/

/*********** Invalid state/event ************/

    GolfSimState onEvent(const auto&, const auto&) {
        GS_LOG_TRACE_MSG(warning, "Unsupported state transition. Resetting ");
        throw std::logic_error{ "Unsupported state transition" };
    }


    class GolfSimStateMachine {

    public:
        void restartSim(const GolfSimState& starting_state) {
            state_ = starting_state;
        }

        void processEvent(const PossibleEvent& event) {
            state_ = std::visit(
                helper::overload{
                    [](const auto& state, const auto& evt) {
                        return onEvent(state, evt);
                    }
                },
                state_, event);

            reportCurrentState();
        }

        void reportCurrentState() {
            GS_LOG_TRACE_MSG(trace, "Current state is: ");
            std::visit(
                helper::overload{
                            [](const state::InitializingCamera1System& initializing) {
                            GS_LOG_TRACE_MSG(trace, "Initializing.");
                            },
                            [](const state::Exiting& exiting) {
                            GS_LOG_TRACE_MSG(trace, "Exiting.");
                            },
                            [](const state::WaitingForBall& ballPlaced) {
                            GS_LOG_TRACE_MSG(trace, "BallPlaced.");
                            },
                            [](const state::WaitingForBallStabilization& waitingForBallStabilization) {
                            GS_LOG_TRACE_MSG(trace, "WaitingForBallStabilization.");
                            },
                            [](const state::BallHitNowWaitingForCam2Image& ballHitNowWaitingForCam2Image) {
                            GS_LOG_TRACE_MSG(trace, "BallHitNowWaitingForCam2Image.");
                            },
                            [](const state::WaitingForBallHit& ballHit) {
                            GS_LOG_TRACE_MSG(trace, "WaitingForBallHit.");
                            },
                            [](const state::WaitingForCamera2PreImage& waitingForCamera2PreImage) {
                            GS_LOG_TRACE_MSG(trace, "WaitingForCamera2PreImage.");
                            },
                            [](const state::WaitingForSimulatorArmed& waitingForSimulatorArmed) {
                            GS_LOG_TRACE_MSG(trace, "WaitingForSimulatorArmed.");
                            },
                            [](const state::InitializingCamera2System& initializingCamera1System) {
                            GS_LOG_TRACE_MSG(trace, "InitializingCamera2System.");
                            },
                            [](const state::WaitingForCameraArmMessage& waitingForCameraArmMessage) {
                            GS_LOG_TRACE_MSG(trace, "WaitingForCameraArmMessage.");
                            },
                            [](const state::WaitingForCameraTrigger& waitingForCameraTrigger) {
                            GS_LOG_TRACE_MSG(trace, "WaitingForCameraTrigger.");
                            }
                },
                state_);
        }

    private:
        GolfSimState state_;
    };


    void StartFsmShutdown() {
        GolfSimGlobals::golf_sim_running_ = false;
    }


    bool ProcessControlMessageEvent(GolfSimEvent::ControlMessage &control_message) {

        GsIPCControlMsgType message_type = control_message.message_type_;

        GS_LOG_TRACE_MSG(trace, "Processing ControlMessage of type: " + GsIPCControlMsg::FormatControlMessageType(message_type));

        if (message_type == GsIPCControlMsgType::kClubChangeToPutter) {
            GolfSimClubs::SetCurrentClubType(GolfSimClubs::GsClubType::kPutter);
        }
        else if (message_type == GsIPCControlMsgType::kClubChangeToDriver) {
            GolfSimClubs::SetCurrentClubType(GolfSimClubs::GsClubType::kDriver);
        }
        else {
            GS_LOG_MSG(error, "Received ControlMessage event with unknown message type.");
        }

        return true;
    }


    bool RunGolfSimFsm( const GolfSimState& starting_state ) {
        GS_LOG_TRACE_MSG(trace, "RunGolfSimFsm");

        // Catch Ctrl-C and such to get out of the FSM when necessary.
        signal(SIGUSR1, default_signal_handler);
        signal(SIGUSR2, default_signal_handler);
        signal(SIGINT, default_signal_handler);

        // TBD - Is this the right place to create the IPC stuff
        if ( !PerformSystemStartupTasks() ) {
            GS_LOG_MSG(error, "Failed to InitializeIPCSystem.");
            return false;
        }

        GolfSimConfiguration::SetConstant("gs_config.ipc_interface.kMaxCam2ImageReceivedTimeMs", kMaxCam2ImageReceivedTimeMs);

        GolfSimConfiguration::SetConstant("gs_config.user_interface.kWebServerCamera2Image", kWebServerCamera2Image);
        GolfSimConfiguration::SetConstant("gs_config.user_interface.kWebServerLastTeedBallImage", kWebServerLastTeedBallImage);        
        
        GolfSimStateMachine golfSim;

        // Start the golfSim will start in the Initializing state
        // Note that we need to queue a restart event to 
        // get the Initializing state to complete and transition to the next, active state.
        golfSim.restartSim(starting_state);

        // Schedule the event loop timer for the first time.  Otherwise, it might
        // never start the timing 'tick' loop.

        GolfSimEventElement restartEvent{ new GolfSimEvent::Restart{ } };
        GolfSimEventQueue::QueueEvent(restartEvent);

        // If in immediate still-picture mode, also queue up a simulated
        // ArmCamera2MessageReceived so that the system immediately starts
        // waiting for a picture.
        if (GolfSimOptions::GetCommandLineOptions().camera_still_mode_) {
            GolfSimEventElement armCamera2MessageReceived{ new GolfSimEvent::ArmCamera2MessageReceived{ } };
            GolfSimEventQueue::QueueEvent(armCamera2MessageReceived);
        }

        while (GolfSimGlobals::golf_sim_running_) {

            GS_LOG_TRACE_MSG(trace, "Looking for event...");

            // This loop will block until there is an event
            GolfSimEventElement eventElement;

            GS_LOG_TRACE_MSG(trace, "       Event Queue size = " + std::to_string(GolfSimEventQueue::GetQueueLength()) );

            // Only wait for a bit
            bool event_present = GolfSimEventQueue::DeQueueEvent(eventElement, kEventLoopPauseMs);
       
            if (!event_present) {
                continue;
            }

            if (eventElement.e_ == nullptr) {
                GS_LOG_MSG(error, "Received eventElement with null event.");
                continue;
            }

            GS_LOG_TRACE_MSG(trace, "       Received event: " + eventElement.e_->Format());
            // At least one event is waiting - process it
            try {
                PossibleEvent e = GolfSimEventQueue::ConvertEventToPossibleEvent(eventElement.e_);

                // If we have been asked to shutdown, set the flag to stop this loop processing
                if (GolfSimEventQueue::EventIsShutdownEvent(eventElement.e_)) {
                    GS_LOG_TRACE_MSG(trace, "----------- Shutting Down - Received Exit Event -------------");
                    GolfSimGlobals::golf_sim_running_ = false;
                }
                else if (GolfSimEventQueue::EventIsControlEvent(eventElement.e_)) {
                    GS_LOG_TRACE_MSG(trace, "----------- Received Control Event -------------");

                    GolfSimEvent::ControlMessage* control_message = dynamic_cast<GolfSimEvent::ControlMessage*>(eventElement.e_);
                    
                    if (control_message == nullptr) {
                        GS_LOG_MSG(error, "Could not get ControlMessage event.");
                        continue;
                    }

                    if (!ProcessControlMessageEvent(*control_message)) {
                        GS_LOG_MSG(error, "Could not ProcessControlMessageEvent.");
                        continue;
                    }
                }
                else {
                    // Let the FSM handle the event
                    golfSim.processEvent(e);
                }

                // TBD - Use shared_ptr and unique_ptr
                delete eventElement.e_;
            }
            catch (std::exception& ex) {
                GS_LOG_TRACE_MSG(trace, "Exception! - " + std::string(ex.what()) + ".  Restarting...");
                state::InitializingCamera1System state;
                golfSim.restartSim(state);
            }

            // If there is another event, we won't pause before processing it in the next loop
        }

        GS_LOG_TRACE_MSG(trace, "Shutting down system...");

        PerformSystemShutdownTasks();

        GS_LOG_TRACE_MSG(trace, "Exiting eventLoop");

        return true;
    }


    // TBD - this is pretty much defunct - consider deleting
    bool TestFsm() {
        
        // Queue up a series of test events to test with

        GolfSimEventElement restartEvent{ new GolfSimEvent::Restart{ } };
        GolfSimEventQueue::QueueEvent(restartEvent);

        GolfBall ball;

        GolfSimEventElement beginWaitingForBallPlacedEvent{ new GolfSimEvent::BeginWaitingForBallPlaced{ } };
        GolfSimEventQueue::QueueEvent(beginWaitingForBallPlacedEvent);

        GolfSimEventElement ballStabilizedEvent{ new GolfSimEvent::BallStabilized( ball ) };
        GolfSimEventQueue::QueueEvent(ballStabilizedEvent);

        cv::Mat dummyImg;

        GolfSimEventElement ballHitEvent{ new GolfSimEvent::BallHit( ball, dummyImg ) };
        GolfSimEventQueue::QueueEvent(ballHitEvent);

        GolfSimEventElement cam2ImageReceived{ new GolfSimEvent::Camera2ImageReceived(dummyImg) };
        GolfSimEventQueue::QueueEvent(cam2ImageReceived);

        return true;
    }

    bool PerformSystemShutdownTasks() {

        GS_LOG_TRACE_MSG(trace, "PerformSystemShutdownTasks");
        // This may have already been set false, but do so here just in case to
        // ensure that any running loops drop out
        GolfSimGlobals::golf_sim_running_ = false;
        // Allow other things that might be checking the running flag to do so
        std::this_thread::yield();

        // Clean up any threads that still exist
        // TBD - This is not thread-safe - move to a central function with a lock
        if (BallStabilizationCheckTimerThread != nullptr) {
            GS_LOG_TRACE_MSG(trace, "Shutting down BallStabilizationCheckTimerThread");
            BallStabilizationCheckTimerThread->ExitThread();
            delete BallStabilizationCheckTimerThread;
            BallStabilizationCheckTimerThread = nullptr;
        }
        if (ReceivedCam2ImageCheckTimerThread != nullptr) {
            GS_LOG_TRACE_MSG(trace, "Shutting down ReceivedCam2ImageCheckTimerThread");
            ReceivedCam2ImageCheckTimerThread->ExitThread();
            delete ReceivedCam2ImageCheckTimerThread;
            ReceivedCam2ImageCheckTimerThread = nullptr;
        }

        std::this_thread::yield();

        // Only the camera1 system deals with the simulator interfaces
        if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera1) {
            GsSimInterface::DeInitializeSims();
        }

        GS_LOG_TRACE_MSG(trace, "Shutting down IPC System");
        GolfSimIpcSystem::ShutdownIPCSystem();

        if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1 ||
               GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1TestStandalone) {
            PulseStrobe::DeinitGPIOSystem();
        }
        return true;
    }

    bool PerformSystemStartupTasks() {

        GS_LOG_TRACE_MSG(trace, "PerformSystemStartupTasks");

        SystemMode mode = GolfSimOptions::GetCommandLineOptions().system_mode_;
        
        // Skip camera initialization for test modes that don't need hardware
        // These modes use test images or simulate functionality without cameras
        bool skip_camera = (mode == SystemMode::kTest ||
                           mode == SystemMode::kTestSpin ||
                           mode == SystemMode::kTestExternalSimMessage ||
                           mode == SystemMode::kTestGSProServer ||
                           mode == SystemMode::kAutomatedTesting);

        if (!skip_camera) {
            // Setup the Pi Camera to be internally or externally triggered as appropriate
            if (!PerformCameraSystemStartup() ) {
                GS_LOG_MSG(error, "Failed to PerformCameraSystemStartup.");
                return false;
            }
        } else {
            GS_LOG_MSG(info, "Skipping camera initialization for test mode: " + std::to_string(mode));
        }

        if (!GolfSimIpcSystem::InitializeIPCSystem()) {
            GS_LOG_MSG(error, "Failed to InitializeIPCSystem.");
            return false;
        }

        // Give the IPC system time to set up before trying to send any messages
        sleep(1);
        
        GsUISystem::SendIPCStatusMessage(GsIPCResultType::kInitializing);

        if (!PulseStrobe::InitGPIOSystem(default_signal_handler)) {
            GS_LOG_MSG(error, "Failed to InitGPIOSystem.");
            return false;
        }

        // Only the camera1 system deals with the simulator interfaces
        if (GolfSimOptions::GetCommandLineOptions().GetCameraNumber() == GsCameraNumber::kGsCamera1) {
            if (!GsSimInterface::InitializeSims()) {
                GS_LOG_MSG(error, "Failed to Initialize the Golf Simulator Interface.");
                return false;
            }
        }

        // Driver is as good a default as any if not other indication  
        bool kStartInPuttingMode = false;
        GolfSimConfiguration::SetConstant("gs_config.modes.kStartInPuttingMode", kStartInPuttingMode);
        
        if (kStartInPuttingMode) {
            GS_LOG_MSG(info, "Starting in Putting Mode.");
            GolfSimClubs::SetCurrentClubType(GolfSimClubs::GsClubType::kPutter);
        }
        else {
            GolfSimClubs::SetCurrentClubType(GolfSimClubs::GsClubType::kDriver);
        }

        // Give the other threads a chance to get going
        std::this_thread::yield();

        return true;
    }



} // GolfSim namespace

#endif // #ifdef __unix__  // Ignore in Windows environment


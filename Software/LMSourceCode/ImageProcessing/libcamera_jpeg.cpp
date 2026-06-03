/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#ifdef __unix__  // Ignore in Windows environment

#include <chrono>
#include <signal.h>
#include <sys/stat.h>
#include <thread>

#include "core/rpicam_encoder.hpp"
#include "encoder/encoder.hpp"
#include "output/output.hpp"

#include <opencv2/core/cvdef.h>
#include <opencv2/highgui.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/range/adaptor/reversed.hpp>

// Attempt to remove boost bind warning by including the correct bind
// header before boot inserts the wrong one.
#include <boost/bind/bind.hpp>

#include <sys/signalfd.h>
#include <poll.h>


#include "gs_globals.h"
#include "gs_config.h"
#include "gs_camera.h"
#include "libcamera_interface.h"
#include "logging_tools.h"
#include "ball_watcher.h"
#include "pulse_strobe.h"
#include "core/rpicam_app.hpp"
#include "core/still_options.hpp"

#include "image/image.hpp"

#include "still_image_libcamera_app.hpp"

using namespace std::placeholders;
using libcamera::Stream;
namespace gs = golf_sim;

void SetImx296TriggerModeViaI2C(int mode) {
	if (mode != 0 && mode != 1) {
		GS_LOG_MSG(error, "Invalid trigger mode: " + std::to_string(mode) + " (must be 0 or 1)");
		return;
	}
	const std::string cmd = "$PITRAC_ROOT/ImageProcessing/CameraTools/imx296_trigger 4 " + std::to_string(mode);
	int rc = system(cmd.c_str());
	if (rc != 0) {
		GS_LOG_MSG(warning, "imx296_trigger 4 " + std::to_string(mode) + " failed (rc=" + std::to_string(rc) + ")");
		return;
	}
	GS_LOG_TRACE_MSG(trace, "Set IMX296 trigger mode via I2C: " + std::to_string(mode));
}

enum FlightCameraState {
	kUninitialized,
	kWaitingForFirstPrimingPulseGroup,
	kWaitingForFirstPrimingTimeEnd,
	kWaitingForPreImageTrigger,
	kWaitingForPreImageFlush,
	kWaitingForSecondPrimingPulseGroup,
	kWaitingForSecondPrimingTimeEnd,
	kWaitingForFinalImageTrigger,
	kWaitingForFinalImageFlush,
	kFinalImageReceived
};


void SetExternalTrigger(bool& flag) {

	GS_LOG_TRACE_MSG(trace, "SetExternalTrigger - flag = " + std::to_string((int)flag));

	const gs::CameraHardware::CameraModel  camera_model = gs::GolfSimCamera::kSystemSlot2CameraType;

	// This will take a moment to complete, so the waiting time to deal with it is dealt with elsehwere in the code
	if (!flag && camera_model == gs::CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {

		flag = true;

		std::string trigger_mode_command = "$PITRAC_ROOT/ImageProcessing/CameraTools/imx296_trigger 4 1";

		GS_LOG_TRACE_MSG(trace, "ball_flight_camera_event_loop - Camera 2 trigger_mode_command = " + trigger_mode_command);
		int command_result = system(trigger_mode_command.c_str());

		if (command_result != 0) {
			GS_LOG_TRACE_MSG(trace, "system(trigger_mode_command) failed.");
			return;
		}
	}
}

// In Pico autonomous mode the strike is a single, self-fired XTR trigger -- there
// is no legacy priming train. The PiGS/InnoMaker IMX296 reads out empty,
// non-integrating frames for its first several external triggers after trigger
// mode is (re)enabled; legacy mode hides this by firing-and-ignoring a burst of
// priming pulses before the real shot. Without that warm-up the lone strike frame
// comes back all-zero. Warm the sensor the same way: pulse cam2 XTR (no strobe,
// via the Pico's CAM_PULSE) a handful of times and drain the readout frames so the
// real strike lands on a sensor that is actually integrating light.
static void PicoWarmUpSensor(LibcameraJpegApp& app, int pulses, long pulse_width_us) {
	// Fire the XTR pulses on a timer, exactly like the legacy priming train
	// (SendCameraPrimingPulses). NEVER block on a per-pulse frame: a cold IMX296 emits no
	// frame until it has had several quick external triggers, so the old wait-per-pulse
	// (app.Wait() here) deadlocked on the very first pulse when the sensor was cold -- one
	// CAM_PULSE, then a 60s hang. DrainMessages() is just msg_queue_.Clear(), so it drops
	// the warm-up readouts and frees their buffers without blocking.
	const long inter_pulse_us =
		(long)(1000000.0 / gs::PulseStrobe::kPrimingPulseFPS) - pulse_width_us;
	for (int i = 0; i < pulses && gs::GolfSimGlobals::golf_sim_running_; ++i) {
		gs::PulseStrobe::SendOnOffPulse(pulse_width_us);  // -> Pico CAM_PULSE: XTR low/high, no strobe
		if (inter_pulse_us > 0) {
			std::this_thread::sleep_for(std::chrono::microseconds(inter_pulse_us));
		}
		app.DrainMessages();  // non-blocking: drop the readout frame, keep the buffer pool free
	}
	// Settle: the final pulse's readout is still in flight, and in a lit room it is NOT black
	// -- the mean<1 discard guard won't catch it, so the armed wait would grab it as the
	// strike (capturing a pre-strike frame, which the FSM then drops -- leaving cam2's one-shot
	// capture spent and nothing to catch the real hit). Drain across a short window so every
	// trailing warm-up frame is gone; in trigger mode nothing new can arrive until the real
	// strike's XTR, so this can't eat the actual shot.
	for (int s = 0; s < 4 && gs::GolfSimGlobals::golf_sim_running_; ++s) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		app.DrainMessages();
	}
	GS_LOG_TRACE_MSG(trace, "Pico warm-up complete (" + std::to_string(pulses) + " XTR pulses on a timer; pipeline settled).");
}

// Run the triggered capture event loop on an already-opened camera.
// The camera must have been opened and configured before calling this.
// Calls StartCamera at entry and StopCamera when the final image arrives.
bool cam2_run_event_loop(LibcameraJpegApp& app, cv::Mat& returnImg, bool send_priming_pulses)
{
	struct TriggerModeResetGuard {
		~TriggerModeResetGuard() { SetImx296TriggerModeViaI2C(0); }
	} trigger_reset_guard;

	gs::PulseStrobe::cam2_ready_for_final_trigger_.store(false);

	// Drop any stale message a prior cancelled capture left in the queue before we start.
	// A cam2 timeout that fires just after a frame was already captured runs cancel_capture
	// (StopCamera + PostQuit) while this worker is parked in cv_.wait -- not app.Wait() --
	// so that Quit is never consumed; it would otherwise be the first thing this fresh
	// capture's Wait() returns, failing the shot and cascading into the next one. The camera
	// is stopped here, so there is no legitimate in-flight message to lose.
	app.DrainMessages();

	// Pico mode: enter external-trigger mode BEFORE StartCamera. The standby-toggle that
	// commits it only takes cleanly while stopped -- doing it mid-stream stalls the sensor ~14s.
	const bool pico_mode = gs::PulseStrobe::IsPicoActive();
	const gs::CameraHardware::CameraModel camera_model = gs::GolfSimCamera::kSystemSlot2CameraType;
	if (pico_mode && camera_model == gs::CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
		SetImx296TriggerModeViaI2C(1);
	}

	app.StartCamera();
	GS_LOG_TRACE_MSG(trace, "cam2_run_event_loop: camera started, waiting for triggers");


	auto start_time = std::chrono::high_resolution_clock::now();

	// This should be slightly more time than it takes to get all of the timing pulses
	long kQuiesceTimeMs = (gs::PulseStrobe::kNumberPrimingPulses ) * (1000 / gs::PulseStrobe::kPrimingPulseFPS) + 10;

	// If appropriate, add the time we allow to setup external trigginer for the InnoMaker cameras
	if (camera_model == gs::CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
		kQuiesceTimeMs += gs::PulseStrobe::kPauseToSetUpInnoMakerExternalTriggerMilliseconds;
	}

	GS_LOG_TRACE_MSG(trace, "kQuiesceTimeMs to wait for the priming pulses to arrive = " + std::to_string(kQuiesceTimeMs) + " milliseconds");


	// Set the starting time to now, even though we will override it when the first trigger is received
	std::chrono::steady_clock::time_point timeOfFirstTrigger = std::chrono::steady_clock::now();

	// These flags manage state while the sequence of external shutter pulses are
	// processed. In Pico autonomous mode the armed Pico fires a single GP15
	// trigger coincident with the strobe -- there is no priming train to
	// quiesce through, so jump straight to the final-image state and capture
	// the one frame the Pico produces.
	FlightCameraState state = pico_mode ? kWaitingForFinalImageTrigger : kWaitingForFirstPrimingPulseGroup;

	// Check here, once, to see if we are going to expect to produce a pre-image for later subtraction
	golf_sim::GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kUsePreImageSubtraction", 
												golf_sim::GolfSimCamera::kUsePreImageSubtraction);


    // Legacy priming path only -- see the kWaitingForFirstPrimingPulseGroup case.
	bool innomaker_first_external_trigger_is_set = false;

	// Legacy path only. Pico mode already committed trigger before StartCamera; toggling
	// the standby mid-stream is what stalled the sensor.
	if (!pico_mode) {
		bool dummy = false;
		SetExternalTrigger(dummy);
	}

	// Send priming pulses + trigger in a background thread so the event loop
	// can process the resulting hardware trigger events as they arrive.
	// Skipped in normal mode -- the FSM handles pulse timing externally.
	std::thread pulse_sender;
	if (send_priming_pulses) {
		GS_LOG_TRACE_MSG(trace, "Sending priming pulses from event loop");
		pulse_sender = std::thread([&app]() {
			try {
				if (!gs::PulseStrobe::SendCameraPrimingPulses(true)) {
					GS_LOG_MSG(error, "Failed to send priming pulses.");
					app.PostQuit();
					return;
				}
				// Give the event loop time to transition before the capture trigger
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
				if (!gs::PulseStrobe::SendExternalTrigger()) {
					GS_LOG_MSG(error, "Failed to send external trigger.");
					app.PostQuit();
					return;
				}
			} catch (const std::exception& e) {
				GS_LOG_MSG(error, "Pulse sender exception: " + std::string(e.what()));
				app.PostQuit();
			} catch (...) {
				GS_LOG_MSG(error, "Pulse sender unknown exception.");
				app.PostQuit();
			}
		});
	}

	struct PulseThreadGuard {
		std::thread& t;
		~PulseThreadGuard() { if (t.joinable()) t.join(); }
	} pulse_guard{pulse_sender};

	bool return_status = true;

	if (pico_mode) {
		// The IMX296 reads out empty frames for its first several triggers after trigger
		// mode is enabled. Burn through them with a short XTR warm-up so the real strike
		// lands on a sensor that's integrating light. Floor so a zeroed config can't skip it.
		int warm_up_pulses = gs::PulseStrobe::kNumberPrimingPulses;
		if (warm_up_pulses < 6) {
			warm_up_pulses = 6;
		}
		PicoWarmUpSensor(app, warm_up_pulses, /*pulse_width_us=*/200);

		// Drop the warm-up frames so the armed wait starts on the real strike.
		app.DrainMessages();

		// Now let gs_fsm arm the Pico (it waits on this flag).
		gs::PulseStrobe::cam2_ready_for_final_trigger_.store(true);
		GS_LOG_TRACE_MSG(trace, "cam2_run_event_loop: Pico mode -- trigger committed + sensor warmed, armed for a single autonomous trigger.");
	}

	for (;state != kFinalImageReceived;)
	{
		if (!gs::GolfSimGlobals::golf_sim_running_  || return_status == false) {
			return_status = false;
			break;
		}

		// Get the next message from the camera system
		RPiCamApp::Msg msg = app.Wait();
		if (msg.type == RPiCamApp::MsgType::Timeout)
		{
			if (pico_mode) {
				// Pico single-pulse mode: a timeout means the FSM cancelled the wait
				// (no strike / shot over). Tear down cleanly and let the run loop
				// re-arm. Restarting the camera here (the legacy recovery) churns
				// StopCamera+Configure+StartCamera against the FSM restart and aborts
				// the process (std::terminate from a joinable thread torn down mid-reconfigure).
				GS_LOG_TRACE_MSG(trace, "cam2 Pico mode: timeout/cancel -- exiting capture loop cleanly.");
				app.StopCamera();
				return_status = false;
				break;
			}
			GS_LOG_MSG(error, "ERROR: Device timeout detected, attempting a restart!!!");
			app.StopCamera();
			uint flags = RPiCamApp::FLAG_STILL_RGB;
			app.ConfigureViewfinder(flags);
			app.StartCamera();
			continue;
		}

		if (msg.type == RPiCamApp::MsgType::Quit) {
			GS_LOG_TRACE_MSG(trace, "Received Quit message.");
			return_status = false;
			break;
		}
		else if (msg.type != RPiCamApp::MsgType::RequestComplete) {
			GS_LOG_MSG(error, "Unrecognised camera message type, aborting event loop.");
			return_status = false;
			break;
		}
		else {
			// GS_LOG_TRACE_MSG(trace, "RECEIVED libcamera-app message-------");
		}


		// MJLMODIFIED - Here, we're going to ignore any triggered frames received
		// for a period of time to make sure that the device is ready
		// to receive the 'real' trigger pulse.
		// 
		// The background on this is that the Pi GS camera appears to require at least
		// a few XTR trigger pulsees in order to be ready to actually take a picture
		//
		switch (state) {

		case kWaitingForFinalImageTrigger: {

			// A leftover warm-up / cold trigger reads out all-zero (mean ~0). Discard it
			// and wait for the strobe-lit strike, whose flashes lift the mean off zero.
			if (pico_mode) {
				Stream* probe_stream = app.ViewfinderStream();
				if (probe_stream != nullptr) {
					StreamInfo probe_info = app.GetStreamInfo(probe_stream);
					CompletedRequestPtr& probe_payload = std::get<CompletedRequestPtr>(msg.payload);
					libcamera::FrameBuffer* probe_buffer = probe_payload->buffers[probe_stream];
					BufferReadSync probe_r(&app, probe_buffer);
					const std::vector<libcamera::Span<uint8_t>> probe_mem = probe_r.Get();
					uint32_t* probe_image = (uint32_t*)probe_mem[0].data();
					if (probe_image != nullptr) {
						cv::Mat probe_frame(probe_info.height, probe_info.width, CV_8UC3, probe_image, probe_info.stride);
						cv::Mat ch0;
						cv::extractChannel(probe_frame, ch0, 0);
						const double frame_mean = cv::mean(ch0)[0];
						GS_LOG_TRACE_MSG(trace, "Pico: cam2 armed-wait frame mean = " + std::to_string(frame_mean));
						if (frame_mean < 1.0) {
							GS_LOG_TRACE_MSG(trace, "Pico: discarding non-strobed frame; waiting for the strobe-lit strike.");
							break;  // stay in kWaitingForFinalImageTrigger and wait for the next frame
						}
					}
				}
				// Fall through: this is the strobe-lit strike -- capture it below.
			}

			GS_LOG_TRACE_MSG(trace, "Received Final Image Trigger - capturing strobed image.");
			app.StopCamera();

			Stream* stream = app.ViewfinderStream();

			if (stream == nullptr) {
				GS_LOG_MSG(error, "Got a null stream");
				return_status = false;
				state = kFinalImageReceived;
				break;
			}

			StreamInfo info = app.GetStreamInfo(stream);

			CompletedRequestPtr& payload = std::get<CompletedRequestPtr>(msg.payload);
			libcamera::FrameBuffer *buffer = payload->buffers[stream];
			BufferReadSync r(&app, buffer);

			const std::vector<libcamera::Span<uint8_t>> mem = r.Get();

			uint32_t* image = (uint32_t*)mem[0].data();

			if (image == nullptr) {
				GS_LOG_MSG(error, "Got a null image");
				return_status = false;
				state = kFinalImageReceived;
				break;
			}

			GS_LOG_TRACE_MSG(trace, "About to create Mat frame.  Info.height, width = " + std::to_string(info.height) +
								", " + std::to_string(info.width) + ". Stride = " + std::to_string(info.stride));

			cv::Mat frame = cv::Mat(info.height, info.width, CV_8UC3, image, info.stride);

			GS_LOG_TRACE_MSG(trace, "Created Mat frame");

			returnImg = frame.clone();

			GS_LOG_TRACE_MSG(trace, "Returning (Final, Strobed) Viewfinder captured image");

			return_status = true;

			state = kFinalImageReceived;
			break;
		}

		case kWaitingForFinalImageFlush: {

			GS_LOG_TRACE_MSG(trace, "Flushing Final Strobed Image");
			app.StopCamera();

			Stream* stream = app.ViewfinderStream();

			if (stream == nullptr) {
				GS_LOG_MSG(error, "Got a null stream");
				return_status = false;
				state = kFinalImageReceived;
				break;
			}

			StreamInfo info = app.GetStreamInfo(stream);

			CompletedRequestPtr& payload = std::get<CompletedRequestPtr>(msg.payload);
			libcamera::FrameBuffer *buffer = payload->buffers[stream];
			BufferReadSync r(&app, buffer);

			const std::vector<libcamera::Span<uint8_t>> mem = r.Get();

			uint32_t* image = (uint32_t*)mem[0].data();

			if (image == nullptr) {
				GS_LOG_MSG(error, "Got a null image");
				return_status = false;
				state = kFinalImageReceived;
				break;
			}

			GS_LOG_TRACE_MSG(trace, "About to create Mat frame in kWaitingForFinalImageFlush.  Info.height, width = " + std::to_string(info.height) + 
								", " + std::to_string(info.width) + ". Stride = " + std::to_string(info.stride));


			cv::Mat frame = cv::Mat(info.height, info.width, CV_8UC3, image, info.stride);

			GS_LOG_TRACE_MSG(trace, "Created Mat frame");

			// Save the image in memory
			returnImg = frame.clone();

			// THE FOLLOWING CREATES A SEGMENTATION FAULT: returnImg = cv::Mat(info.height, info.width, CV_8UC3, image, info.stride);
			// So, that's why the frame is being cloned.
			GS_LOG_TRACE_MSG(trace, "Returning (Final, Strobed) Viewfinder captured image");
			// golf_sim::LoggingTools::LogImage("", returnImg, std::vector < cv::Point >{}, true, "Cam2_Strobed_Image.png");

			return_status = true;

			state = kFinalImageReceived;
			break;
		}


		case kUninitialized:
		case kFinalImageReceived: {
			GS_LOG_TRACE_MSG(trace, "Invalid state transition.  Aborting.");
			return_status = false;
			break;
		}

		case kWaitingForFirstPrimingPulseGroup: {
			// Start the countdown timer.  During this time, we will just receive and
			// ignore the priminmg pulses
			timeOfFirstTrigger = std::chrono::steady_clock::now();
			GS_LOG_TRACE_MSG(trace, "Received first (priming) trigger of first priming group.  Ignoring it.");

			// Create a completed request to make sure that the buffer(s) get re-used.
			CompletedRequestPtr& completed_request = std::get<CompletedRequestPtr>(msg.payload);

			// (Re)set external triggering if we have not already done so
			SetExternalTrigger(innomaker_first_external_trigger_is_set);

			state = kWaitingForFirstPrimingTimeEnd;
			break;
		}

		case kWaitingForFirstPrimingTimeEnd: {
			// This is not the first trigger
			GS_LOG_TRACE_MSG(trace, "Received priming trigger.");
			// We have been waiting for some time to get ready for the 'real' trigger after
			// having received one or more priming triggers.  Get ready to take the real
			// picture if we have waited long enough.
			auto timeOfCurrentTrigger = std::chrono::steady_clock::now();
			auto timeLapsed = std::chrono::duration_cast<std::chrono::milliseconds>(timeOfCurrentTrigger - timeOfFirstTrigger).count();

			GS_LOG_TRACE_MSG(trace, "		Time since last trigger: " + std::to_string(timeLapsed) + " ms.");

			// Create a completed request to make sure that the buffer(s) get re-used.
			CompletedRequestPtr& completed_request = std::get<CompletedRequestPtr>(msg.payload);

			if (timeLapsed < kQuiesceTimeMs) {
				GS_LOG_TRACE_MSG(trace, "Ignoring trigger - still quiescing...");
				state = kWaitingForFirstPrimingTimeEnd;
			}
			else {
				// We've waited long enough for priming pulses
				if (!golf_sim::GolfSimCamera::kUsePreImageSubtraction) {

					if (!golf_sim::GolfSimCamera::kCameraRequiresFlushPulse) {
						// If no flush is required, jump straight to the final state
						GS_LOG_TRACE_MSG(trace, "Priming period complete.  Ready for Final Image Trigger.");
						state = kWaitingForFinalImageFlush;
					}
					else {
						GS_LOG_TRACE_MSG(trace, "Priming period complete.  Ready for Final Image Trigger (before flush).");
						state = kWaitingForFinalImageTrigger;
					}
					gs::PulseStrobe::cam2_ready_for_final_trigger_.store(true);
				}
				else {
					GS_LOG_TRACE_MSG(trace, "Priming period complete.  Ready for Pre-image Trigger.");
					state = kWaitingForPreImageTrigger;
				}
			}
			break;
		}

		case kWaitingForPreImageTrigger: {
			if (!app.ViewfinderStream())
			{
				GS_LOG_TRACE_MSG(trace, "Received non-viewfinder stream. Aborting");
				return_status = false;
				app.StopCamera();
				break;
			}

			GS_LOG_TRACE_MSG(trace, "Received Pre-Image Trigger - Image will be de-queued after next (flush) trigger.");

			state = kWaitingForPreImageFlush;
			break;
		}

		case kWaitingForPreImageFlush: {
			GS_LOG_TRACE_MSG(trace, "Received Pre-Image Flush.  Saving current image");

			/*** TBD - REMOVE - DEPRECATED - This never worked 
			Stream* stream = app.ViewfinderStream();
			StreamInfo info = app.GetStreamInfo(stream);
			CompletedRequestPtr& payload = std::get<CompletedRequestPtr>(msg.payload);
                        libcamera::FrameBuffer *buffer = payload->buffers[stream];
                        BufferReadSync r(&app, buffer);

                        const std::vector<libcamera::Span<uint8_t>> mem = r.Get();

                        uint32_t* image = (uint32_t*)mem[0].data();

                        cv::Mat frame = cv::Mat(info.height, info.width, CV_8UC3, image, info.stride);

                        // Save the image in memory
                        cv::Mat pre_image = frame.clone();

                        golf_sim::LibCameraInterface::SendCamera2PreImage(pre_image);
			***/

			// TBD - If using second priming group, use state = kWaitingForSecondPrimingPulseGroup;
			state = kWaitingForFinalImageTrigger;
			gs::PulseStrobe::cam2_ready_for_final_trigger_.store(true);
			break;
		}

		// This state is not curently used.  Instead, the system goes directly from the pre-message
		// flush to waiting for the final image trigger
		case kWaitingForSecondPrimingPulseGroup: {
			timeOfFirstTrigger = std::chrono::steady_clock::now();
			GS_LOG_TRACE_MSG(trace, "Received first (priming) trigger of SECOND priming group.  Ignoring it.");
			state = kWaitingForSecondPrimingTimeEnd;
			break;
		}

		// This state is not curently used.  Instead, the system goes directly from the pre-message
		// flush to waiting for the final image trigger
		case kWaitingForSecondPrimingTimeEnd: {
			// This is not the first trigger
			GS_LOG_TRACE_MSG(trace, "Received priming trigger for SECOND priming group.");
			// We have been waiting for some time to get ready for the 'real' trigger after
			// having received one or more priming triggers.  Get ready to take the real
			// picture if we have waited long enough.
			auto timeOfCurrentTrigger = std::chrono::steady_clock::now();
			auto timeLapsed = std::chrono::duration_cast<std::chrono::milliseconds>(timeOfCurrentTrigger - timeOfFirstTrigger).count();

			GS_LOG_TRACE_MSG(trace, "		Time since last trigger: " + std::to_string(timeLapsed) + " ms.");

			// It takes less time to quiesce for the second set of priming pulses
			if (timeLapsed < kQuiesceTimeMs / 2) {
				GS_LOG_TRACE_MSG(trace, "		Ignoring trigger - still quiescing...");

				// Create a completed request to make sure that the buffer(s) get re-used.
				CompletedRequestPtr& completed_request = std::get<CompletedRequestPtr>(msg.payload);

				state = kWaitingForSecondPrimingTimeEnd;
			}
			else {
				GS_LOG_TRACE_MSG(trace, "		Priming period complete.  Ready for Trigger.");
				state = kWaitingForFinalImageTrigger;
				gs::PulseStrobe::cam2_ready_for_final_trigger_.store(true);
			}
			break;
		}

		} // switching on state
	} // for loop

	GS_LOG_TRACE_MSG(trace, "cam2_run_event_loop ended.");

	return return_status;
}

// Full pipeline open + capture + close. Used by WaitForCam2Trigger in still-picture mode.
bool ball_flight_camera_event_loop(LibcameraJpegApp& app, cv::Mat& returnImg)
{
	app.OpenCamera();
	uint flags = RPiCamApp::FLAG_STILL_RGB;
	app.ConfigureViewfinder(flags);
	bool result = cam2_run_event_loop(app, returnImg, true);
	return result;
}

	// The main event loop for the camera 1 system.

	bool still_image_event_loop(LibcameraJpegApp& app, cv::Mat& returnImg)
	{
		GS_LOG_TRACE_MSG(trace, "still_image_event_loop");

		StillOptions* options = app.GetOptions();
		
		libcamera::logSetLevel("*", "ERROR"); 
		RPiCamApp::verbosity = 0;

		options->Set().no_raw = true;  // See https://forums.raspberrypi.com/viewtopic.php?t=369927

		app.StartCamera();
		GS_LOG_TRACE_MSG(trace, "Camera started.");
		auto start_time = std::chrono::high_resolution_clock::now();

		for (;;)
		{
			if (!gs::GolfSimGlobals::golf_sim_running_) {
				app.StopCamera(); // stop complains if encoder very slow to close
				return false;
			}


			RPiCamApp::Msg msg = app.Wait();

			// TBD - REMOVE - Otherwise will slow things down
			// GS_LOG_MSG(trace, "   Received camera message: " + std::to_string((int)msg.type));

			if (msg.type == RPiCamApp::MsgType::Timeout)
			{
				GS_LOG_MSG(error, "ERROR: Device timeout detected, attempting a restart.");
				GS_LOG_MSG(error, "		Check to make sure the .yaml file in use by libcamera has a long timeout set, for example,  \"camera_timeout_value_ms\": 86400000,  in the appropriate file.");
				GS_LOG_MSG(error, "			On a Pi 4, check both /usr/local/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml and /usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml");
				GS_LOG_MSG(error, "			On a Pi 5, check both /usr/local/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml and /usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml");
				app.StopCamera();
				app.StartCamera();
				continue;
			}
			if (msg.type == RPiCamApp::MsgType::Quit) {
				GS_LOG_TRACE_MSG(trace, "Received Quit message in still_image_event_loop.");
				app.StopCamera();
				return false;
			}
			else if (msg.type != RPiCamApp::MsgType::RequestComplete) {
				GS_LOG_MSG(error, "Unrecognised camera message type in still_image_event_loop, aborting.");
				app.StopCamera();
				return false;
			}

			// In viewfinder mode, simply run until the timeout. When that happens, switch to
			// capture mode.
			if (app.ViewfinderStream())
			{
				GS_LOG_TRACE_MSG(trace, "still_image_event_loop received msg -- in viewfinder.");

				auto now = std::chrono::high_resolution_clock::now();
				if (options->Set().timeout && now - start_time > std::chrono::milliseconds(options->Set().timeout.get<std::chrono::milliseconds>()))
				{
					GS_LOG_TRACE_MSG(warning, "still_image_event_loop timed out. -- in viewfinder.");
					app.StopCamera();
					app.Teardown();

					uint flags = RPiCamApp::FLAG_STILL_RGB;
					app.ConfigureStill(flags);

					app.StartCamera();
				}
				else
				{
					CompletedRequestPtr& completed_request = std::get<CompletedRequestPtr>(msg.payload);
					app.ShowPreview(completed_request, app.ViewfinderStream());
				}
			}
			// In still capture mode, save a jpeg and quit.
			else if (app.StillStream())
			{
				app.StopCamera();
				GS_LOG_TRACE_MSG(trace, "Still capture image received");


				Stream* stream = app.StillStream();
				StreamInfo info = app.GetStreamInfo(stream);

				unsigned int h = info.height, w = info.width, stride = info.stride;
				GS_LOG_TRACE_MSG(trace, "Still image (width, height) = (" + std::to_string(w) + "," + std::to_string(h) + ") Stride = " + std::to_string(stride));

				CompletedRequestPtr& payload = std::get<CompletedRequestPtr>(msg.payload);
                libcamera::FrameBuffer *buffer = payload->buffers[stream];

                BufferReadSync r(&app, buffer);

                const std::vector<libcamera::Span<uint8_t>> mem = r.Get();

                uint32_t* image = (uint32_t*)mem[0].data();

                cv::Mat frame = cv::Mat(info.height, info.width, CV_8UC3, image, info.stride);

				// Save the image in memory
				returnImg = frame.clone();

				return true;
			}
		}
}




#endif // #ifdef __unix__  // Ignore in Windows environment

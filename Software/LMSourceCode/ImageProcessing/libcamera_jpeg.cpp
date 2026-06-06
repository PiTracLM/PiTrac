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

// Pico autonomous mode warms cam2 the same way the legacy priming train does: fire-and-ignore
// a burst of XTR pulses (no strobe, via the Pico's CAM_PULSE) before the real shot, so the
// IMX296 is past its cold-start empty frames. This ONLY sends the pulses on a timer -- it runs
// in a background thread while the event loop ignores the resulting frames for the quiesce
// window (exactly like legacy's kWaitingForFirstPrimingTimeEnd). Nothing is drained or
// inspected here.
static void PicoFireWarmUpPulses(int pulses, long pulse_width_us) {
	const long inter_pulse_us =
		(long)(1000000.0 / gs::PulseStrobe::kPrimingPulseFPS) - pulse_width_us;
	for (int i = 0; i < pulses && gs::GolfSimGlobals::golf_sim_running_; ++i) {
		gs::PulseStrobe::SendOnOffPulse(pulse_width_us);  // -> Pico CAM_PULSE: XTR low/high, no strobe
		if (inter_pulse_us > 0) {
			std::this_thread::sleep_for(std::chrono::microseconds(inter_pulse_us));
		}
	}
	GS_LOG_TRACE_MSG(trace, "Pico warm-up pulses fired (" + std::to_string(pulses) + " XTR on a timer).");
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

	// cam2's external trigger is set AFTER StartCamera, on the first frame (see the loop) --
	// EXACTLY like main/legacy. main runs imx296_trigger while the stream is already live (its
	// comment: "needs to be called AFTER the camera has already started up") and lets the ~1s
	// InnoMaker pause baked into the quiesce window commit it. Setting it BEFORE StartCamera (the
	// old pico path, commit 1da2ac5) doesn't stick -- StartCamera re-inits the sensor and cam2
	// free-runs at 30fps. So: don't set it here.
	const bool pico_mode = gs::PulseStrobe::IsPicoActive();
	const gs::CameraHardware::CameraModel camera_model = gs::GolfSimCamera::kSystemSlot2CameraType;

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

	// Set the InnoMaker trigger right after StartCamera -- main does this for ALL modes (its
	// SetExternalTrigger right after the stream starts), then re-commits it again on the first
	// frame (the loop below does that for Pico). This first set doesn't fully commit (the stream
	// only just started), so a free-running first frame still arrives and the loop's re-commit
	// lands it -- exactly like main's double-set. No (!pico_mode) gate now: SetExternalTrigger
	// no-ops for non-InnoMaker cameras, so it's safe in every mode.
	{
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

	// Pico mode mirrors main: on the FIRST frame (sensor now streaming) we set the external
	// trigger and start the background warm-up/priming pulses; the loop then IGNORES every frame
	// for the quiesce window (kQuiesceTimeMs already includes the ~1s InnoMaker commit pause),
	// and the fire-count gate takes the real strike. Trigger is set there, after StartCamera --
	// before StartCamera doesn't stick and cam2 free-runs.
	std::thread pico_warmup_thread;
	std::chrono::steady_clock::time_point pico_quiesce_start{};
	bool pico_trigger_set = false;
	bool pico_quiesce_done = false;
	uint64_t pico_event_baseline = 0;
	// Fire warm-up pulses spanning the WHOLE quiesce window + margin, so a triggered frame still
	// arrives AFTER the window closes and the time-quiesce can complete. main gets this for free
	// (its priming train spans ~1.8s via the InnoMaker commit pause); our 12-pulse ~800ms train
	// stopped short of the 1302ms window, so the quiesce never fired and cam2 blocked until the
	// strike -- the 93s hang. CAM_PULSE runs on core1 (no DSP/audio impact), so more is safe.
	const long pico_inter_pulse_ms = 1000 / gs::PulseStrobe::kPrimingPulseFPS;  // ~66ms at 15fps
	int pico_warm_up_pulses = (int)((kQuiesceTimeMs + 400) / pico_inter_pulse_ms) + 1;
	if (pico_warm_up_pulses < gs::PulseStrobe::kNumberPrimingPulses) {
		pico_warm_up_pulses = gs::PulseStrobe::kNumberPrimingPulses;
	}
	if (pico_mode) {
		// set-1 (SetExternalTrigger above) already engaged the trigger, so cam2 is in trigger
		// mode and NO frame arrives on its own. Fire the warm-up/priming XTR pulses NOW -- like
		// main fires its priming train from a separate thread -- so triggered frames flow and the
		// loop can quiesce. Without this the loop blocks forever on a first frame trigger mode
		// never produces (which is why the FSM timed out after 15s and armed cold).
		pico_warmup_thread = std::thread(PicoFireWarmUpPulses, pico_warm_up_pulses, /*pulse_width_us=*/200);
	}
	struct PicoWarmupThreadGuard {
		std::thread& t;
		~PicoWarmupThreadGuard() { if (t.joinable()) t.join(); }
	} pico_warmup_guard{pico_warmup_thread};

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

			// Pico mode, FIRST frame: the sensor is now streaming, so set the InnoMaker external
			// trigger the SAME way main does -- HERE, after StartCamera, NOT before it (before
			// doesn't stick and cam2 free-runs). Then start the background warm-up/priming pulses.
			// Ignore this last pre-trigger frame.
			if (pico_mode && !pico_trigger_set) {
				// First triggered (warm-up) frame: re-commit the trigger, like main's second
				// SetExternalTrigger on the first frame. Warm-up pulses are already firing
				// (started before the loop). Ignore this frame.
				pico_trigger_set = true;
				if (camera_model == gs::CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
					SetImx296TriggerModeViaI2C(1);
				}
				GS_LOG_TRACE_MSG(trace, "Pico: first frame -- trigger re-committed (main-style set-2).");
				CompletedRequestPtr& first_discard = std::get<CompletedRequestPtr>(msg.payload);
				(void)first_discard;
				break;
			}

			// Pico mode: replicate the legacy time-quiesce (kWaitingForFirstPrimingTimeEnd).
			// Ignore EVERY frame received during the quiesce window -- these are the background
			// warm-up pulses' readouts -- then take the first frame AFTER the window as the real
			// strike. No brightness/frame-mean check: the warm-up pulses stop well before the
			// window ends, so in trigger mode nothing new arrives until the Pico's autonomous
			// strike fires cam2 XTR.
			if (pico_mode && !pico_quiesce_done) {
				if (pico_quiesce_start == std::chrono::steady_clock::time_point{}) {
					// First warm-up readout -- start the quiesce clock here (legacy's
					// timeOfFirstTrigger), so the window absorbs the sensor's cold-start delay.
					pico_quiesce_start = std::chrono::steady_clock::now();
				}
				const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - pico_quiesce_start).count();
				if (elapsed_ms < kQuiesceTimeMs) {
					CompletedRequestPtr& discard = std::get<CompletedRequestPtr>(msg.payload);
					(void)discard;  // warm-up readout -- ignore it, keep quiescing
					break;
				}
				// Quiesce window elapsed: cam2 is warm and the pipeline is quiet.
				pico_quiesce_done = true;

				if (send_priming_pulses) {
					// Still-picture / calibration capture: there is no FSM to arm the
					// Pico and no acoustic strike to fire it, so fire it ourselves to
					// produce one strobed frame. First join the warm-up thread and drain
					// its trailing CAM_PULSE (XTR-only) readouts so the fire-count gate
					// grabs the strobed frame, not a leftover dark warm-up frame -- with
					// the sensor in external-trigger mode, the only frame after the drain
					// is the one our fire triggers.
					if (pico_warmup_thread.joinable()) {
						pico_warmup_thread.join();
					}
					constexpr long kPicoStillSettleMs = 150;  // > one warm-up frame period + readout
					std::this_thread::sleep_for(std::chrono::milliseconds(kPicoStillSettleMs));
					app.DrainMessages();
					pico_event_baseline = gs::PulseStrobe::PicoEventCount();
					gs::PulseStrobe::cam2_ready_for_final_trigger_.store(true);
					if (!gs::PulseStrobe::FirePicoForStill()) {
						GS_LOG_MSG(error, "Pico still-capture fire failed -- cam2 will not trigger.");
						return_status = false;
						state = kFinalImageReceived;
						break;
					}
					GS_LOG_MSG(info, "Pico: quiesce complete -- fired for still/calibration (fire-count baseline " +
						std::to_string(pico_event_baseline) + ").");
				} else {
					// Live shot: let the FSM arm the Pico (it waits on this flag); the
					// real acoustic strike fires it and the NEXT frame is the strike.
					pico_event_baseline = gs::PulseStrobe::PicoEventCount();
					gs::PulseStrobe::cam2_ready_for_final_trigger_.store(true);
					GS_LOG_TRACE_MSG(trace, "Pico: quiesce complete -- cam2 armed (fire-count baseline " +
						std::to_string(pico_event_baseline) + "), waiting for the Pico to fire.");
				}
				break;
			}

			// Capture ONLY when the Pico has actually fired. Its strike advances event_count
			// (the firmware emits an EVENT + bumps the counter on every fire, once the strobe
			// train drains). Any frame whose fire-count is still the arming baseline is a
			// cold-start warm-up straggler, NOT the strike -- ignore it and keep waiting. This
			// is the real strike signal, not a brightness or timing guess.
			if (pico_mode) {
				const uint64_t fire_count = gs::PulseStrobe::PicoEventCount();
				if (fire_count <= pico_event_baseline) {
					GS_LOG_TRACE_MSG(trace, "Pico: pre-fire frame (count " + std::to_string(fire_count) +
						" == baseline " + std::to_string(pico_event_baseline) + ") -- ignoring, waiting for the strike.");
					CompletedRequestPtr& discard = std::get<CompletedRequestPtr>(msg.payload);
					(void)discard;
					break;
				}
				GS_LOG_MSG(info, "Pico: strike fired (count " + std::to_string(fire_count) + " > baseline " +
					std::to_string(pico_event_baseline) + ") -- capturing the strobed image.");
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

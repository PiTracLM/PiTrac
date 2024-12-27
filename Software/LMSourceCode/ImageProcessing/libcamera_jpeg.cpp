/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#ifdef __unix__  // Ignore in Windows environment

#include <chrono>
#include <signal.h>
#include <sys/stat.h>

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
#include "core/rpicam_app.hpp"
#include "core/still_options.hpp"

#include "image/image.hpp"

#include "still_image_libcamera_app.hpp"

using namespace std::placeholders;
using libcamera::Stream;
namespace gs = golf_sim;

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

// The main event loop for the the externally-triggered camera.

bool ball_flight_camera_event_loop(LibcameraJpegApp& app, cv::Mat& returnImg)
{
	GS_LOG_TRACE_MSG(trace, "ball_flight_camera_event_loop started.  Waiting for external trigger....");

	// MJLMODs BELOW

	StillOptions const* options = app.GetOptions();

	app.OpenCamera();

	GS_LOG_TRACE_MSG(trace, "ball_flight_camera_event_loop started.  Opened Camera....");


	uint flags = RPiCamApp::FLAG_STILL_RGB;
	app.ConfigureViewfinder(flags);

	app.StartCamera();

	GS_LOG_TRACE_MSG(trace, "ball_flight_camera_event_loop started.  Started Camera....");


	auto start_time = std::chrono::high_resolution_clock::now();

	const long kQuiesceTimeMs = 2000; 

	// Set the starting time to now, even though we will override it when the first trigger is received
	std::chrono::steady_clock::time_point timeOfFirstTrigger = std::chrono::steady_clock::now();

	// These flags manage state while the sequence of external shutter pulses are
	// processed.
	FlightCameraState state = kWaitingForFirstPrimingPulseGroup;

	// Check here, once, to see if we are going to expect to produce a pre-image for later subtraction
	golf_sim::GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kUsePreImageSubtraction", 
												golf_sim::GolfSimCamera::kUsePreImageSubtraction);

	bool return_status = true;

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
			GS_LOG_MSG(error, "ERROR: Device timeout detected, attempting a restart!!!");
			app.StopCamera();
			uint flags = RPiCamApp::FLAG_STILL_RGB;
			app.ConfigureViewfinder(flags);
			app.StartCamera();
			continue;
		}

		if (msg.type == RPiCamApp::MsgType::Quit) {
			GS_LOG_TRACE_MSG(trace, "Received Quit message.");
			return true;
		}
		else if (msg.type != RPiCamApp::MsgType::RequestComplete)
			throw std::runtime_error("Unrecognised message!");
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

			GS_LOG_TRACE_MSG(trace, "Received Final Image Trigger - Image will be de-queued after next (flush) trigger.");
			state = kWaitingForFinalImageFlush;
			break;
		}

		case kWaitingForFinalImageFlush: {

			GS_LOG_TRACE_MSG(trace, "Flushing Final Strobed Image");
			app.StopCamera();

			Stream* stream = app.ViewfinderStream();

			if (stream == nullptr) {
				GS_LOG_MSG(error, "Got a null stream");

				return false;
			}

			StreamInfo info = app.GetStreamInfo(stream);

			CompletedRequestPtr& payload = std::get<CompletedRequestPtr>(msg.payload);
			libcamera::FrameBuffer *buffer = payload->buffers[stream];
			BufferReadSync r(&app, buffer);

			const std::vector<libcamera::Span<uint8_t>> mem = r.Get();

			uint32_t* image = (uint32_t*)mem[0].data();

			if (image == nullptr) {
				GS_LOG_MSG(error, "Got a null image");
				
				return false;
			}
                        /*

                        const libcamera::Request::BufferMap& buffers = payload->buffers;

                        // Create an OpenCV Mat object from the payload

                        libcamera::Span<uint8_t> buffer = r.Get()[0];
                        uint32_t* image = (uint32_t*)buffer.data();
                        */

			GS_LOG_TRACE_MSG(trace, "About to create Mat frame.  Info.height, width = " + std::to_string(info.height) + 
								", " + std::to_string(info.width) + ". Stride = " + std::to_string(info.stride));

			// TBD - Need to figure out how to get this picture to be in color again!!
			cv::Mat frame = cv::Mat(info.height, info.width, CV_8UC3, image, info.stride);
			// cv::Mat frame = cv::Mat(info.height, info.width, CV_8U, image, info.stride);

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

			if (timeLapsed < kQuiesceTimeMs) {
				GS_LOG_TRACE_MSG(trace, "Ignoring trigger - still quiescing...");

				// Create a completed request to make sure that the buffer(s) get re-used.
				CompletedRequestPtr& completed_request = std::get<CompletedRequestPtr>(msg.payload);

				state = kWaitingForFirstPrimingTimeEnd;
			}
			else {

				if (!golf_sim::GolfSimCamera::kUsePreImageSubtraction) {

					if (!golf_sim::GolfSimCamera::kCameraRequiresFlushPulse) {
						// If no flush is required, jump straight to the final state
						GS_LOG_TRACE_MSG(trace, "Priming period complete.  Ready for Final Image Trigger and Flush.");
						state = kWaitingForFinalImageFlush;
					}
					else {
						GS_LOG_TRACE_MSG(trace, "Priming period complete.  Ready for Final Image Trigger (before flush).");
						state = kWaitingForFinalImageTrigger;
					}
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

			// TBD - If using second priming group, use state = kWaitingForSecondPrimingPulseGroup;
			state = kWaitingForFinalImageTrigger;
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
			}
			break;
		}

		} // switching on state
	} // for loop

	GS_LOG_TRACE_MSG(trace, "ball_flight_camera_event_loop ended.  Return final image.");

	return return_status;
}

	// The main event loop for the camera 1 system.

	bool still_image_event_loop(LibcameraJpegApp& app, cv::Mat& returnImg)
	{
		GS_LOG_TRACE_MSG(trace, "still_image_event_loop");

		StillOptions* options = app.GetOptions();
		
		libcamera::logSetLevel("*", "ERROR"); 
		RPiCamApp::verbosity = 0;

		options->no_raw = true;  // See https://forums.raspberrypi.com/viewtopic.php?t=369927

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
			if (msg.type == RPiCamApp::MsgType::Timeout)
			{
				GS_LOG_MSG(error, "ERROR: Device timeout detected, attempting a restart!!!");
				app.StopCamera();
				app.StartCamera();
				continue;
			}
			if (msg.type == RPiCamApp::MsgType::Quit)
				return false;
			else if (msg.type != RPiCamApp::MsgType::RequestComplete)
				throw std::runtime_error("unrecognised message!");

			// In viewfinder mode, simply run until the timeout. When that happens, switch to
			// capture mode.
			if (app.ViewfinderStream())
			{
				GS_LOG_TRACE_MSG(trace, "still_image_event_loop received msg -- in viewfinder.");

				auto now = std::chrono::high_resolution_clock::now();
				if (options->timeout && now - start_time > std::chrono::milliseconds(options->timeout.get<std::chrono::milliseconds>()))
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

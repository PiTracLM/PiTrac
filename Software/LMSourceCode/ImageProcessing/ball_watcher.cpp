/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#ifdef __unix__  // Ignore in Windows environment

#include <chrono>
#include <signal.h>
#include <sys/stat.h>
#include <sched.h>
#include <pthread.h>

#include "core/rpicam_encoder.hpp"
#include "encoder/encoder.hpp"
#include "output/output.hpp"

#include <opencv2/core/cvdef.h>
#include <opencv2/highgui.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "motion_detect.h"

#include "logging_tools.h"
#include "gs_globals.h"

namespace gs = golf_sim;


#include <sys/signalfd.h>
#include <poll.h>


#include "ball_watcher.h"

using namespace std::placeholders;

namespace golf_sim {


static int get_colourspace_flags(std::string const &codec)
{
	GS_LOG_TRACE_MSG(trace, "get_colourspace_flags - codec is: " + codec);

	if (codec == "mjpeg" || codec == "yuv420")
		return RPiCamEncoder::FLAG_VIDEO_JPEG_COLOURSPACE;
	else
		return RPiCamEncoder::FLAG_VIDEO_NONE;
}

// The main event loop for the application.

bool ball_watcher_event_loop(RPiCamEncoder &app, bool & motion_detected)
{
	// Elevate to real-time priority for the trigger-critical motion detection loop.
	// Prevents the kernel from preempting us for 1-4ms during normal scheduling.
	// Requires CAP_SYS_NICE (granted via AmbientCapabilities in the systemd service).
	struct sched_param sp = {};
	sp.sched_priority = 80;
	if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
		GS_LOG_MSG(warning, "Could not set SCHED_FIFO — trigger latency may have jitter. Grant CAP_SYS_NICE to pitrac_lm.");
	}

	VideoOptions const *options = app.GetOptions();
	std::unique_ptr<Output> output = std::unique_ptr<Output>(Output::Create(options));
	app.SetEncodeOutputReadyCallback(std::bind(&Output::OutputReady, output.get(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
	app.SetMetadataReadyCallback(std::bind(&Output::MetadataReady, output.get(), std::placeholders::_1));

	app.OpenCamera();

	app.ConfigureVideo(get_colourspace_flags(options->Get().codec));
	GS_LOG_TRACE_MSG(trace, "ball_watcher_event_loop - starting encoder.");
	app.StartEncoder();
	app.StartCamera();

	// Instead of using the dynamical link4ed-library approach used by lrpiocam apps, 
	// we will just manually create a mottion_detect object

	MotionDetectStage motion_detect_stage(&app);

	// Setup the same elements of the stage that rpicam apps would otherwise do dynamically.

	boost::property_tree::ptree empty_params;
	motion_detect_stage.Read(empty_params);
	motion_detect_stage.Configure();


	auto start_time = std::chrono::high_resolution_clock::now();

	pollfd p[1] = { { STDIN_FILENO, POLLIN, 0 } };

	motion_detected = false;

	for (unsigned int count = 0; ; count++)
	{
		if (!gs::GolfSimGlobals::golf_sim_running_) {
			app.StopCamera();
			app.StopEncoder();
			sp.sched_priority = 0;
			pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
			return false;
		}


		RPiCamEncoder::Msg msg = app.Wait();
		if (msg.type == RPiCamApp::MsgType::Timeout)
		{
			GS_LOG_MSG(error, "ERROR: Device timeout detected, attempting a restart!!!");
			app.StopCamera();
			app.StartCamera();
			continue;
		}

		if (msg.type == RPiCamEncoder::MsgType::Quit) {
			GS_LOG_TRACE_MSG(trace, "Received Quit message in ball_watcher_event_loop.");
			app.StopCamera();
			app.StopEncoder();
			sp.sched_priority = 0;
			pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
			return motion_detected;
		}
		else if (msg.type != RPiCamEncoder::MsgType::RequestComplete) {
			GS_LOG_MSG(error, "Unrecognised camera message type in ball_watcher_event_loop, aborting.");
			app.StopCamera();
			app.StopEncoder();
			sp.sched_priority = 0;
			pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
			return false;
		}

		CompletedRequestPtr &completed_request = std::get<CompletedRequestPtr>(msg.payload);

		// Motion detection FIRST — this is the latency-critical path.
		// EncodeBuffer is deferred until after we check for motion.
		bool result = motion_detect_stage.Process(completed_request);

		bool mdResult = false;
		int getStatus = completed_request->post_process_metadata.Get("motion_detect.result", mdResult);
		if (getStatus == 0) {
			if (mdResult) {
				// Trigger already fired inside Process() — stop immediately
				app.StopCamera();
				app.StopEncoder();
				motion_detected = true;
				// Drop RT priority before returning to normal processing
				sp.sched_priority = 0;
				pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
				return true;
			}
			else {
				// std::cout << "****** motion stopped ********* " << std::endl;
			}
		}
		else {
			// std::cout << "WARNING:  Could not find motion_detect.result." << std::endl;
		}

		// Encode after motion check — keeps encoding out of the trigger-critical path
		app.EncodeBuffer(completed_request, app.VideoStream());
	}

	return true;
}

}

#endif // #ifdef __unix__  // Ignore in Windows environment

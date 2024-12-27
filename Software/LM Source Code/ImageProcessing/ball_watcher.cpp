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

	VideoOptions const *options = app.GetOptions();
	std::unique_ptr<Output> output = std::unique_ptr<Output>(Output::Create(options));
	app.SetEncodeOutputReadyCallback(std::bind(&Output::OutputReady, output.get(), _1, _2, _3, _4));
	app.SetMetadataReadyCallback(std::bind(&Output::MetadataReady, output.get(), _1));

	app.OpenCamera();

	app.ConfigureVideo(get_colourspace_flags(options->codec));
	GS_LOG_TRACE_MSG(trace, "ball_watcher_event_loop - starting encoder.");
	app.StartEncoder();
	app.StartCamera();
	auto start_time = std::chrono::high_resolution_clock::now();

	pollfd p[1] = { { STDIN_FILENO, POLLIN, 0 } };

	motion_detected = false;

	for (unsigned int count = 0; ; count++)
	{
		if (!gs::GolfSimGlobals::golf_sim_running_) {
			app.StopCamera(); // stop complains if encoder very slow to close
			app.StopEncoder();
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
		if (msg.type == RPiCamEncoder::MsgType::Quit)
			return motion_detected;
		else if (msg.type != RPiCamEncoder::MsgType::RequestComplete)
			throw std::runtime_error("unrecognised message!");

		/*
		auto now = std::chrono::high_resolution_clock::now();
		bool timeout = !options->frames && options->timeout &&
					   (now - start_time > std::chrono::milliseconds(options->timeout));
		bool frameout = options->frames && count >= options->frames;

		if (timeout || frameout)
		{
			if (timeout)
				LOG(1, "Halting: reached timeout of " << options->timeout << " milliseconds.");
			app.StopCamera(); // stop complains if encoder very slow to close
			app.StopEncoder();
			return motion_detected;
		}
		*/

		CompletedRequestPtr &completed_request = std::get<CompletedRequestPtr>(msg.payload);
		app.EncodeBuffer(completed_request, app.VideoStream());

		bool mdResult = false;
		int getStatus = completed_request->post_process_metadata.Get("motion_detect.result", mdResult);
		if (getStatus == 0) {
			if (mdResult) {
				app.StopCamera(); // stop complains if encoder very slow to close
				app.StopEncoder();
				motion_detected = true;
				
				// TBD - for now, once we have motion, get out immediately
				return true;
			}
			else {
				// std::cout << "****** motion stopped ********* " << std::endl;
			}
		}
		else {
			// std::cout << "WARNING:  Could not find motion_detect.result." << std::endl;
		}
	}

	return true;
}

}

#endif // #ifdef __unix__  // Ignore in Windows environment

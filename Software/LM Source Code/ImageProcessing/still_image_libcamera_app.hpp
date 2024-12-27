/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

 // This is the LM's version of the libcamera libcamera-jpeg utility code.
 // It contains an event loop that processes events from the libcamera sub-system.
 // 
 // This code is modelled closely on the libcamera still image application processing code.
 // 
 // See the libcamera documentation for more details.


#pragma once

#ifdef __unix__  // Ignore in Windows environment


#include "core/still_options.hpp"
#include "core/rpicam_app.hpp"
#include "encoder/encoder.hpp"

#include <opencv2/photo.hpp>
#include <opencv2/core/cvdef.h>


class LibcameraJpegApp : public RPiCamApp
{
public:
	LibcameraJpegApp()
		: RPiCamApp(std::make_unique<StillOptions>())
	{
	}

	StillOptions* GetOptions() const
	{
		return static_cast<StillOptions*>(options_.get());
	}
};

// The main event loops for the camera 1 and 2 systems
bool still_image_event_loop(LibcameraJpegApp& app, cv::Mat& returnImg);

bool ball_flight_camera_event_loop(LibcameraJpegApp& app, cv::Mat& returnImg);

#endif // #ifdef __unix__  // Ignore in Windows environment

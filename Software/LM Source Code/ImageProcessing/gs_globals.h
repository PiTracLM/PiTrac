/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <string>
#include <opencv2/core.hpp>


namespace golf_sim {	

	using GsCircle = cv::Vec3f;    // Certain openCV calls require Vec3fs, even though we'd probably prefer Vec3d. Probably overkill, but saves the need for a lot of explicit type conversions
	using GsEllipse = cv::RotatedRect;
	// Note that the GsColorTriplet may need to go negative to handle loop-around situations for the hue in an HSV value.  A Vec3b would not work for that, so we use a Scalar
	using GsColorTriplet = cv::Scalar; // signed
	using GsColor = int;

	const short kX_index = 0;
	const short kY_index = 1;
	const short kZ_index = 2;

	struct GolfSimGlobals {

		// Can be set false to stop the event loops and other async processes
		// Certain processes may not check this very frequently, so may take
		// some time to shutdown even after this is set false.
		static bool golf_sim_running_;

	};

	// Enable compiling and testing (without cameras) on Windows platform
#ifdef __unix__   
	const std::string GolfSimPlatform = "Unix";
#elif defined(_WIN32) || defined(WIN32) 
	const std::string GolfSimPlatform = "Windows";
#endif
};
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <string>
#include <opencv2/core.hpp>

namespace golf_sim {

	using GsCircle = cv::Vec3f;
	// Note that the GsColorTriplet may need to go negative to handle loop-around situations for the hue in an HSV value.  A Vec3b would not work for that
	using GsColorTriplet = cv::Scalar;  // cv::Vec3b;
	using GsColor = uchar;


#ifdef __unix__   
	const std::string GolfSimPlatform = "Unix";
#elif defined(_WIN32) || defined(WIN32) 
	const std::string GolfSimPlatform = "Windows";
#endif
};
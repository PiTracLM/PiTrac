/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include "ball_watcher_image_buffer.h"

namespace golf_sim {

	class GolfSimClubData {

	public:

		// Setup constants and anything else
		static bool Configure();

		// Create a video of the club strike, detect club face information,
		// perform analysis, etc.
		static bool ProcessClubStrikeData(boost::circular_buffer<RecentFrameInfo>& frame_info);

		static bool CreateClubStrikeVideo(boost::circular_buffer<RecentFrameInfo>& frame_info);


	public:

		// True if the system has been configured to gather club data (e.g., pre- and post-strike
		// images.  See club_data section of the .json configuration file.
		static bool kGatherClubData;

		// These define the area that the camera sensor will be cropped to
		// in order to allow for gathering images of the club strike.  This
		// cropping is much larger than just what is needed to watch the ball.
		// These values will likely result in a much slower FPT for the camera
		// that we would want when just watching the ball to see when it moves,
		// but the trade off is getting the club images.
		// Must call Configure() before these will be set.
		static uint kClubImageWidthPixels;
		static uint kClubImageHeightPixels;

		// The fully-qualified output directory
		static std::string kClubImageOutputDir;

		static uint kNumberFramesToSaveBeforeHit;
		static uint kNumberFramesToSaveAfterHit;

		// TBD - This is experimental - we are trying to shorten the usual
		// camera 1 shutter time and increase gain to reduce blur when we are attempting
		// to gather club strike images.
		static float kClubImageCameraGain;
		static float kClubImageShutterSpeedMultiplier;

	};

}


/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#include <boost/circular_buffer.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "logging_tools.h"
#include "gs_options.h"
#include "gs_config.h"

#include "gs_club_data.h"


namespace golf_sim {

	bool GolfSimClubData::kGatherClubData = false;

	// These define the area that the camera sensor will be cropped to
	// in order to allow for gathering images of the club strike.  This
	// cropping is much larger than just what is needed to watch the ball.
	// These values will likely result in a much slower FPT for the camera
	// that we would want when just watching the ball to see when it moves,
	// but the trade off is getting the club images.
	// Must call Configure() before these will be set.
	 uint GolfSimClubData::kClubImageWidthPixels = 200;
	 uint GolfSimClubData::kClubImageHeightPixels = 150;

	// The fully-qualified output directory
	 std::string GolfSimClubData::kClubImageOutputDir;

	 uint GolfSimClubData::kNumberFramesToSaveBeforeHit = 4;
	 uint GolfSimClubData::kNumberFramesToSaveAfterHit = 4;

	 float GolfSimClubData::kClubImageCameraGain = 30.0;
	 float GolfSimClubData::kClubImageShutterSpeedMultiplier = 0.4;


	bool GolfSimClubData::Configure() {
		GS_LOG_TRACE_MSG(trace, "GolfSimClubData::Configure");

		GolfSimConfiguration::SetConstant("gs_config.club_data.kEnableClubImages", kGatherClubData);

		if (kGatherClubData) {
			GolfSimConfiguration::SetConstant("gs_config.club_data.kEnableClubImages", kClubImageOutputDir);
			GolfSimConfiguration::SetConstant("gs_config.club_data.kNumberFramesToSaveBeforeHit", kNumberFramesToSaveBeforeHit);
			GolfSimConfiguration::SetConstant("gs_config.club_data.kNumberFramesToSaveAfterHit", kNumberFramesToSaveAfterHit);
			GolfSimConfiguration::SetConstant("gs_config.club_data.kClubImageWidthPixels", kClubImageWidthPixels);
			GolfSimConfiguration::SetConstant("gs_config.club_data.kClubImageHeightPixels", kClubImageHeightPixels);
			GolfSimConfiguration::SetConstant("gs_config.club_data.kClubImageCameraGain", kClubImageCameraGain);
			GolfSimConfiguration::SetConstant("gs_config.club_data.kClubImageShutterSpeedMultiplier", kClubImageShutterSpeedMultiplier);
		}

		// Not too much can go wrong so far
		return true;
	}

	bool GolfSimClubData::ProcessClubStrikeData(boost::circular_buffer<RecentFrameInfo>& frame_info) {
		GS_LOG_TRACE_MSG(trace, "GolfSimClubData::ProcessClubStrikeData.");

		if (!kGatherClubData) {
			GS_LOG_TRACE_MSG(trace, "Not gathering club data.");
			return true;
		}

		if (!CreateClubStrikeVideo(frame_info)) {
			GS_LOG_TRACE_MSG(warning, "GolfSimClubData::CreateClubStrikeVideo failed.");
			return false;
		}

		// TBD - Perform analysis

		return true;
	}


	bool GolfSimClubData::CreateClubStrikeVideo(boost::circular_buffer<RecentFrameInfo>& frame_info) {
		GS_LOG_TRACE_MSG(trace, "GolfSimClubData::CreateClubStrikeVideo with " + std::to_string(frame_info.size()) + " frames.");

		if (!kGatherClubData) {
			GS_LOG_TRACE_MSG(warning, "GolfSimClubData::CreateClubStrikeVideo called, but kGatherClubData was not set to true. Cannot generate video.");
			return false;
		}

		// TBD - For now, just dump the frame images to the output directory

		int frame_index = 0;

		for (auto& it : frame_info) {

			cv::Mat& next_frame_mat = it.mat;

			std::string frame_number = std::to_string(frame_index);
			frame_number = std::string(3 /* zeros */ - frame_number.length(), '0') + frame_number;

			std::string frame_image_name = "Club_Frame_" + frame_number + ".png";

			GS_LOG_TRACE_MSG(trace, "Frame rate = " + std::to_string(it.frameRate));

			if (next_frame_mat.empty()) {
				GS_LOG_TRACE_MSG(warning, "GolfSimClubData::CreateClubStrikeVideo -- " + frame_image_name + " was empty.");
			}
			else {
				LoggingTools::LogImage("", next_frame_mat, std::vector < cv::Point >{}, true, frame_image_name);
			}

			frame_index++;
		}


		std::string unique_time_tag = LoggingTools::GetUniqueLogName();
		std::string make_movie_command = "ffmpeg -framerate 2 -pattern_type glob -i '" + LoggingTools::kBaseImageLoggingDir + 
				"Club*.png' -c:v libx264 -pix_fmt yuv420p " +  LoggingTools::kBaseImageLoggingDir + "ClubStrike_" + unique_time_tag + ".mp4";

		GS_LOG_TRACE_MSG(info, "CreateClubStrikeVideo video creation command is: " + make_movie_command);

		int cmdResult = system(make_movie_command.c_str());

		if (cmdResult != 0) {
			GS_LOG_TRACE_MSG(warning, "CreateClubStrikeVideo video creation failed.");
			return false;
		}

		return true;
	}

};

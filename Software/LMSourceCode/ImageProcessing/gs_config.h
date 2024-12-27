/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// This class provides an interface to the .json configuration file that is used to set
// various constants in the system.  It is also responsible for reading many of those
// constant values early as the system initializes.

#pragma once

#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <opencv2/core.hpp>

#include "gs_results.h"

namespace golf_sim {

	class GolfSimConfiguration {

	public:

		enum PiModel {
			kRPi5,
			kRPi4,
			kRPiUnknown
		};

		static bool Initialize(const std::string& configuration_filename = "gs_config.json");

		static PiModel GetPiModel();

		// Reads any values that need to be initialized early, such as static members of
		// classes that won't otherwise have a good place to be otherwise initialized because,
		// e.g., there's not constructur that will be called.
		static bool ReadValues();

		static bool PropertyExists(const std::string& value_tag);

		static void SetConstant(const std::string& value_tag, bool& constant_value);
		static void SetConstant(const std::string& value_tag, int& constant_value);
		static void SetConstant(const std::string& value_tag, long& constant_value);
		static void SetConstant(const std::string& value_tag, unsigned int& constant_value);
		static void SetConstant(const std::string& value_tag, float& constant_value);
		static void SetConstant(const std::string& value_tag, double& constant_value);
		static void SetConstant(const std::string& value_tag, std::string& constant_value);
		static void SetConstant(const std::string& tag_name, cv::Vec3d& vec);
		static void SetConstant(const std::string& tag_name, cv::Vec2d& vec);
		static void SetConstant(const std::string& tag_name, std::vector<cv::Vec3d>& vec);
		static void SetConstant(const std::string& tag_name, std::vector<float>& vec);
		static void SetConstant(const std::string& tag_name, cv::Mat& matrix);


		static bool ReadShotInjectionData(std::vector<GsResults>& shots,
								   int& kInterShotInjectionPauseSeconds);

	protected:

		static boost::property_tree::ptree configuration_root_;
	};

}

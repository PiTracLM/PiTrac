/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#ifdef __unix__  // Ignore in Windows environment
#endif
#include <boost/foreach.hpp>
#include <cstdlib>
#include <memory>
#include <fstream>
#include <string>
#include <sstream>
#include "gs_http_client.h"
#include "logging_tools.h"
#include "gs_camera.h"
#include "gs_ui_system.h"
#include "gs_config.h"
#include "gs_options.h"

// Having to set the constants in this way creates more entanglement than we'd like.  TBD - Re-architect
#include "libcamera_interface.h"



namespace golf_sim {

	boost::property_tree::ptree GolfSimConfiguration::configuration_root_;

	GolfSimConfiguration::EnclosureType GolfSimConfiguration::kEnclosureVersion = GolfSimConfiguration::EnclosureType::kEnclosureVersion_Unknown;


static std::string GetConfigString(const std::string& tag_name) {
	std::string value;
	GolfSimConfiguration::SetConstant(tag_name, value);
	return value;
}

static void PopulateOptionsFromConfig() {
	GolfSimOptions& options = GolfSimOptions::GetCommandLineOptions();

	auto apply_enum = [&options](const std::string& tag_name, bool (GolfSimOptions::*setter)(const std::string&)) {
		std::string value = GetConfigString(tag_name);
		if (!value.empty() && !(options.*setter)(value)) {
			GS_LOG_MSG(warning, "Ignoring unrecognized value for " + tag_name + ": " + value);
		}
	};
	apply_enum("gs_config.player.kGolferOrientation", &GolfSimOptions::SetGolferOrientation);
	apply_enum("logging.level", &GolfSimOptions::SetLoggingLevel);
	apply_enum("gs_config.logging.kArtifactSaveLevel", &GolfSimOptions::SetArtifactSaveLevel);

	GolfSimConfiguration::SetConstant("gs_config.debug.kShowDebugImages", options.show_images_);
	GolfSimConfiguration::SetConstant("gs_config.debug.kWaitForKeyOnImages", options.wait_for_key_on_images_);
	GolfSimConfiguration::SetConstant("gs_config.player.kUsePracticeBalls", options.practice_ball_);
	GolfSimConfiguration::SetConstant("gs_config.cameras.kCamera1SearchCenterX", options.search_center_x_);
	GolfSimConfiguration::SetConstant("gs_config.cameras.kCamera1SearchCenterY", options.search_center_y_);
}

	bool GolfSimConfiguration::Initialize() {

		std::string config_body;
#ifdef __unix__
		config_body = GsHttpClient::FetchConfig();
#endif

		if (config_body.empty()) {
			GS_LOG_MSG(error, "GolfSimConfiguration::Initialize failed: no config. The web server must be running to serve /api/internal/config.");
			return false;
		}

		try {
			std::istringstream config_stream(config_body);
			boost::property_tree::read_json(config_stream, configuration_root_);
		}
		catch (std::exception const& e)
		{
			GS_LOG_MSG(error, "GolfSimConfiguration::Initialize failed. ERROR: *** " + std::string(e.what()) + " ***");
			return false;
		}

		// Read any values that we want to set early, here at initialization
		if (!ReadValues()) {
			return false;
		}

		PopulateOptionsFromConfig();

		return true;
	}

	// Helper function to safely get environment variable as std::string
	std::string GolfSimConfiguration::safe_getenv(const std::string& varname) {
		char* buffer = nullptr;
		size_t sz = 0;
#ifndef __unix__  // _dupenv_s does not exist yet in standard unix environment

		if (_dupenv_s(&buffer, &sz, varname.c_str()) == 0) {
			if (buffer != nullptr) {
			std::string value(buffer);
			free(buffer);
			return value;
			}
		}
#else
		char *value = getenv(varname.c_str());
		if (value != nullptr) {
			std::string result = getenv(varname.c_str());
			return result;
		}

#endif
		return std::string();
	}


	bool GolfSimConfiguration::ReadShotInjectionData(std::vector<GsResults>& shots,
													 int & kInterShotInjectionPauseSeconds) {
		try {
			SetConstant("gs_config.testing.kInterShotInjectionPauseSeconds", kInterShotInjectionPauseSeconds);

			// Retrirve as many shots as are defined in the json file
			boost::property_tree::ptree shots_json = configuration_root_.get_child("gs_config.testing.test_shots_to_inject");

			int shot_number = 1;
			for (boost::property_tree::ptree::iterator iter = shots_json.begin(); iter != shots_json.end(); iter++) {
			// for (boost::property_tree::ptree& shot_section : shots_json) {
				GsResults result;
				result.shot_number_ = shot_number;
				shot_number++;

				result.speed_mph_ = iter->second.get<float>("Speed", 0);
				result.hla_deg_ = iter->second.get<float>("HLA", 0);
				result.vla_deg_ = iter->second.get<float>("VLA", 0);
				result.back_spin_rpm_ = iter->second.get<int>("BackSpin", 0);
				result.side_spin_rpm_ = iter->second.get<int>("SideSpin", 0);
				result.club_type_ = GolfSimClubs::GsClubType::kNotSelected;

				shots.push_back(result);
			}
		}
		catch (std::exception const& e)
		{
			GS_LOG_MSG(error, "GolfSimConfiguration::ReadShotInjectionData failed. ERROR: *** " + std::string(e.what()) + " ***");
			return false;
		}
		return true;
	}

	// Returns the valiue of the environment variable PITRAC_ROOT
	std::string GolfSimConfiguration::GetPiTracRootPath() {

		std::string pi_trac_root = safe_getenv("PITRAC_ROOT");

		if (pi_trac_root.empty()) {
			GS_LOG_MSG(warning, "Environment variable PITRAC_ROOT is not set.");
		}
		else {
			GS_LOG_MSG(trace, "Environment variable PITRAC_ROOT is: " + pi_trac_root + ".");
		}
		return pi_trac_root;
	}

GolfSimConfiguration::PiModel GolfSimConfiguration::GetPiModel() {
     GolfSimConfiguration::PiModel pi_model = kRPi5; // default fallback

    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;

    while (std::getline(cpuinfo, line)) {
        // Check for 'Model' field
        if (line.find("Model") != std::string::npos) {
            if (line.find("Raspberry Pi 4") != std::string::npos) {
                pi_model = kRPi4;
            } else if (line.find("Raspberry Pi 5") != std::string::npos) {
                pi_model = kRPi5;
            }
            break;
        }

    }

    return pi_model;
}


bool GolfSimConfiguration::ReadValues() {

	// Many constants are read in the modules that own those constants.  But some are better 
	// initialized here, early, before those modules may get to be initialized (if they even
	// have an initialization).

	SetConstant("gs_config.physical_constants.kBallRadiusMeters", GolfBall::kBallRadiusMeters);

	SetConstant("gs_config.cameras.kCamera1PositionsFromExpectedBallMeters", GolfSimCamera::kCamera1PositionsFromExpectedBallMeters);
	SetConstant("gs_config.cameras.kCamera2PositionsFromExpectedBallMeters", GolfSimCamera::kCamera2PositionsFromExpectedBallMeters);
	SetConstant("gs_config.cameras.kCamera2OffsetFromCamera1OriginMeters", GolfSimCamera::kCamera2OffsetFromCamera1OriginMeters);


	int enclosure_type = 2;
	GolfSimConfiguration::SetConstant("gs_config.system.kEnclosureVersion", enclosure_type);
	kEnclosureVersion = (GolfSimConfiguration::EnclosureType)enclosure_type;

#ifdef __unix__  // Ignore in Windows environment

	SetConstant("gs_config.user_interface.kWebServerResultBallExposureCandidates",GsUISystem::kWebServerResultBallExposureCandidates);
	SetConstant("gs_config.user_interface.kWebServerResultSpinBall1Image", GsUISystem::kWebServerResultSpinBall1Image);
	SetConstant("gs_config.user_interface.kWebServerResultSpinBall2Image", GsUISystem::kWebServerResultSpinBall2Image);
	SetConstant("gs_config.user_interface.kWebServerResultBallRotatedByBestAngles", GsUISystem::kWebServerResultBallRotatedByBestAngles);
	SetConstant("gs_config.user_interface.kWebServerErrorExposuresImage", GsUISystem::kWebServerErrorExposuresImage);
	SetConstant("gs_config.user_interface.kWebServerBallSearchAreaImage", GsUISystem::kWebServerBallSearchAreaImage);
	
	SetConstant("gs_config.image_capture.kMaxWatchingCropWidth", LibCameraInterface::kMaxWatchingCropWidth);
	SetConstant("gs_config.image_capture.kMaxWatchingCropHeight", LibCameraInterface::kMaxWatchingCropHeight);
	SetConstant("gs_config.cameras.kCamera1Gain", LibCameraInterface::kCamera1Gain);
	SetConstant("gs_config.cameras.kCamera1Saturation", LibCameraInterface::kCamera1Saturation);
	SetConstant("gs_config.cameras.kCamera1HighFPSGain", LibCameraInterface::kCamera1HighFPSGain);
	SetConstant("gs_config.cameras.kCamera1Contrast", LibCameraInterface::kCamera1Contrast);
	SetConstant("gs_config.cameras.kCamera2Gain", LibCameraInterface::kCamera2Gain);
	SetConstant("gs_config.cameras.kCamera2Saturation", LibCameraInterface::kCamera2Saturation);

	// Let the command-line gain parameter override the .json config file parameter 
	// TBD - May want to have separate gain options?
	if (GolfSimOptions::GetCommandLineOptions().camera_gain_ > 0.0) {
		GS_LOG_MSG(info, "Overriding camera gains with value: " + std::to_string(GolfSimOptions::GetCommandLineOptions().camera_gain_) + ".");
		LibCameraInterface::kCamera1Gain = GolfSimOptions::GetCommandLineOptions().camera_gain_;
		LibCameraInterface::kCamera2Gain = GolfSimOptions::GetCommandLineOptions().camera_gain_;
	}

	SetConstant("gs_config.cameras.kCamera2CalibrateOrLocationGain", LibCameraInterface::kCamera2CalibrateOrLocationGain);	
	SetConstant("gs_config.cameras.kCamera2ComparisonGain", LibCameraInterface::kCamera2ComparisonGain);
	SetConstant("gs_config.cameras.kCamera2Contrast", LibCameraInterface::kCamera2Contrast);
	SetConstant("gs_config.cameras.kCamera2PuttingGain", LibCameraInterface::kCamera2PuttingGain);
	SetConstant("gs_config.cameras.kCamera2PuttingContrast", LibCameraInterface::kCamera2PuttingContrast);
	SetConstant("gs_config.cameras.kCamera1StillShutterTimeuS", LibCameraInterface::kCamera1StillShutterTimeuS);
	SetConstant("gs_config.cameras.kCamera2StillShutterTimeuS", LibCameraInterface::kCamera2StillShutterTimeuS);

	// The web server share directory isn't really a value we want to use from the .json configuration
	// file anymore, but for now, let's allow it as a fall-back to the command line
	if (!GolfSimOptions::GetCommandLineOptions().web_server_share_dir_.empty()) {
		GsUISystem::kWebServerShareDirectory = GolfSimOptions::GetCommandLineOptions().web_server_share_dir_;
	}
	else {
		// Attempt to get the image logging directory from the .json config file
		SetConstant("gs_config.ipc_interface.kWebServerShareDirectory", GsUISystem::kWebServerShareDirectory);
	}

	// If the configuration file forgot to add a "/" at the end of the logging directory, we should add it here ourselves
	if (!GsUISystem::kWebServerShareDirectory.empty() && GsUISystem::kWebServerShareDirectory.back() != '/') {
		GsUISystem::kWebServerShareDirectory += '/';
	}

#endif

	std::string slot1_type = GetConfigString("cameras.slot1.type");
	if (!slot1_type.empty()) {
		GolfSimCamera::kSystemSlot1CameraType = CameraHardware::string_to_camera_model(slot1_type);
	}

	std::string slot2_type = GetConfigString("cameras.slot2.type");
	if (slot2_type.empty()) {
		GS_LOG_MSG(error, "cameras.slot2.type must be set. Exiting.");
		return false;
	}
	GolfSimCamera::kSystemSlot2CameraType = CameraHardware::string_to_camera_model(slot2_type);

	std::string slot1_lens = GetConfigString("cameras.slot1.lens");
	if (!slot1_lens.empty()) {
		GolfSimCamera::kSystemSlot1LensType = CameraHardware::string_to_lens_type(slot1_lens);
	}

	std::string slot2_lens = GetConfigString("cameras.slot2.lens");
	if (slot2_lens.empty()) {
		GS_LOG_MSG(error, "cameras.slot2.lens must be set. Exiting.");
		return false;
	}
	GolfSimCamera::kSystemSlot2LensType = CameraHardware::string_to_lens_type(slot2_lens);

	std::string slot1_orientation = GetConfigString("cameras.slot1.orientation");
	if (!slot1_orientation.empty()) {
		GolfSimCamera::kSystemSlot1CameraOrientation = CameraHardware::string_to_camera_orientation(slot1_orientation);
	}

	std::string slot2_orientation = GetConfigString("cameras.slot2.orientation");
	if (slot2_orientation.empty()) {
		GS_LOG_MSG(error, "cameras.slot2.orientation must be set. Exiting.");
		return false;
	}
	GolfSimCamera::kSystemSlot2CameraOrientation = CameraHardware::string_to_camera_orientation(slot2_orientation);


	return true;
}

	bool GolfSimConfiguration::PropertyExists(const std::string& value_tag) {
		// int count = configuration_root_.count(value_tag);
		boost::optional<std::string> v = configuration_root_.get_optional<std::string>(value_tag);

		return ((bool)v);
	}


	template <typename T>
	static void ReadScalar(const boost::property_tree::ptree& root, const std::string& tag_name, T& constant_value) {
		if (auto v = root.get_optional<T>(tag_name)) {
			constant_value = *v;
		} else {
			GS_LOG_MSG(warning, "No config value for " + tag_name + ", keeping default");
		}
	}

	void GolfSimConfiguration::SetConstant(const std::string& tag_name, bool& constant_value) { ReadScalar(configuration_root_, tag_name, constant_value); }
	void GolfSimConfiguration::SetConstant(const std::string& tag_name, int& constant_value) { ReadScalar(configuration_root_, tag_name, constant_value); }
	void GolfSimConfiguration::SetConstant(const std::string& tag_name, long& constant_value) { ReadScalar(configuration_root_, tag_name, constant_value); }
	void GolfSimConfiguration::SetConstant(const std::string& tag_name, unsigned int& constant_value) { ReadScalar(configuration_root_, tag_name, constant_value); }
	void GolfSimConfiguration::SetConstant(const std::string& tag_name, float& constant_value) { ReadScalar(configuration_root_, tag_name, constant_value); }
	void GolfSimConfiguration::SetConstant(const std::string& tag_name, double& constant_value) { ReadScalar(configuration_root_, tag_name, constant_value); }
	void GolfSimConfiguration::SetConstant(const std::string& tag_name, std::string& constant_value) { ReadScalar(configuration_root_, tag_name, constant_value); }

	 void GolfSimConfiguration::SetConstant(const std::string& tag_name, cv::Vec3d& vec) {
		 try {
			 int i = 0;
			 for (boost::property_tree::ptree::value_type& element : configuration_root_.get_child(tag_name)) {
				 // vec[i] = std::stod(element.second.data());
				 vec[i] = element.second.get_value<double>();
				 i++;
			 }
		 }
		 catch (std::exception const& e)
		 {
			 GS_LOG_MSG(error, "GolfSimConfiguration::SetConstant failed. ERROR: *** " + std::string(e.what()) + " ***");
		 }
	 }

	 void GolfSimConfiguration::SetConstant(const std::string& tag_name, cv::Vec3f& vec) {
		 try {
			 int i = 0;
			 for (boost::property_tree::ptree::value_type& element : configuration_root_.get_child(tag_name)) {
				 // vec[i] = std::stod(element.second.data());
				 vec[i] = element.second.get_value<float>();
				 i++;
			 }
		 }
		 catch (std::exception const& e)
		 {
			 GS_LOG_MSG(error, "GolfSimConfiguration::SetConstant failed. ERROR: *** " + std::string(e.what()) + " ***");
		 }
	 }

	 void GolfSimConfiguration::SetConstant(const std::string& tag_name, cv::Vec2d& vec) {
		 try {
			 int i = 0;
			 for (boost::property_tree::ptree::value_type& element : configuration_root_.get_child(tag_name)) {
				 // vec[i] = std::stod(element.second.data());
				 vec[i] = element.second.get_value<double>();
				 i++;
			 }
		 }
		 catch (std::exception const& e)
		 {
			 GS_LOG_MSG(error, "GolfSimConfiguration::SetConstant failed. ERROR: *** " + std::string(e.what()) + " ***");
		 }
	 }

	 void GolfSimConfiguration::SetConstant(const std::string& tag_name, std::vector<float>& vec) {
		 try {
			 int i = 0;
			 for (boost::property_tree::ptree::value_type& element : configuration_root_.get_child(tag_name)) {
				 vec.push_back( element.second.get_value<float>() );
				 i++;
			 }
		 }
		 catch (std::exception const& e)
		 {
			 GS_LOG_MSG(error, "GolfSimConfiguration::SetConstant failed. ERROR: *** " + std::string(e.what()) + " ***");
		 }
	 }

	 void GolfSimConfiguration::SetConstant(const std::string& tag_name, std::vector<cv::Vec3d>& matrix) {
		 try {
			 int x = 0;
			 for (boost::property_tree::ptree::value_type& row : configuration_root_.get_child(tag_name))
			 {
				 int y = 0;
				 for (boost::property_tree::ptree::value_type& cell : row.second)
				 {
					 matrix[x][y] = cell.second.get_value<double>();
					 y++;
				 }
				 x++;
			 }
		 }
		 catch (std::exception const& e)
		 {
			 GS_LOG_MSG(error, "GolfSimConfiguration::SetConstant failed. ERROR: *** " + std::string(e.what()) + " ***");
		 }
	 }

	 void GolfSimConfiguration::SetConstant(const std::string& tag_name, cv::Mat& matrix) {
		 bool is_1D = (matrix.rows == 1);

		 try {
			 if (is_1D) {
				 int i = 0;
				 for (boost::property_tree::ptree::value_type& element : configuration_root_.get_child(tag_name)) {
					 matrix.at<double>(0, i) = element.second.get_value<double>();
					 i++;
				 }
			 }
			 else {
				 int x = 0;
				 for (boost::property_tree::ptree::value_type& row : configuration_root_.get_child(tag_name))
				 {
					 int y = 0;
					 for (boost::property_tree::ptree::value_type& cell : row.second)
					 {
						 matrix.at<double>(x, y) = cell.second.get_value<double>();
						 y++;
					 }
					 x++;
				 }
			 }
		 }
		 catch (std::exception const& e)
		 {
			 GS_LOG_MSG(error, "GolfSimConfiguration::SetConstant failed. ERROR: *** " + std::string(e.what()) + " ***");
		 }
	 }

} // namespace golf_sim

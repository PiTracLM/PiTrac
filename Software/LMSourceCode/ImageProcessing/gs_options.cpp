/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

 // The LM's command-line processing module.

#include "gs_options.h"
#include "logging_tools.h"

namespace golf_sim {



GolfSimOptions GolfSimOptions::the_command_line_options_;

GolfSimOptions& GolfSimOptions::GetCommandLineOptions() {
	return the_command_line_options_;
}

GsCameraNumber GolfSimOptions::GetCameraNumber() {
	GsCameraNumber camera_number;

	if (system_mode_ == kCamera1 || 
		system_mode_ == kCamera1TestStandalone ||
		system_mode_ == kTest ||
		system_mode_ == kCamera1Calibrate ||
		system_mode_ == kCamera1AutoCalibrate ||
		system_mode_ == kCamera1BallLocation ) {
		camera_number = GsCameraNumber::kGsCamera1;
	} 
	else {
		camera_number = GsCameraNumber::kGsCamera2;
	}

	return camera_number;
}

bool GolfSimOptions::Parse(int argc, char *argv[])
{
	using namespace boost::program_options;

	variables_map vm;
	// Read options from the command line
	store(parse_command_line(argc, argv, options_), vm);
	notify(vm);

	// Read options from a file if specified
	std::ifstream ifs(command_line_file_.c_str());
	if (ifs)
	{
		store(parse_config_file(ifs, options_), vm);
		notify(vm);
	}


	std::map<std::string, int> mode_table =
	{	{ "test", SystemMode::kTest },
		{ "camera1", SystemMode::kCamera1 },
		{ "camera1_test_standalone", SystemMode::kCamera1TestStandalone },
		{ "camera1Calibrate", SystemMode::kCamera1Calibrate },
		{ "camera2Calibrate", SystemMode::kCamera2Calibrate },
		{ "test_spin", SystemMode::kTestSpin },
		{ "camera1_ball_location", SystemMode::kCamera1BallLocation },
		{ "camera2_ball_location", SystemMode::kCamera2BallLocation },
		{ "test_sim_message", SystemMode::kTestExternalSimMessage },
		{ "test_gspro_server", SystemMode::kTestGSProServer },
		{ "automated_testing", SystemMode::kAutomatedTesting },
		{ "camera1AutoCalibrate", SystemMode::kCamera1AutoCalibrate },
		{ "camera2AutoCalibrate", SystemMode::kCamera2AutoCalibrate },
	};
	if (mode_table.count(system_mode_string_) == 0)
		throw std::runtime_error("Invalid system_mode: " + system_mode_string_);
	system_mode_ = (SystemMode)mode_table[system_mode_string_];


	if (help_)
	{
		std::cout << options_;
		return true;
	}

	if (version_)
	{
		std::cout << "GolfSim build version: TBD" << std::endl;
		return false;
	}


	return true;
}

bool GolfSimOptions::SetArtifactSaveLevel(const std::string& level)
{
	static const std::map<std::string, ArtifactSaveLevel> table =
	{	{ "none", ArtifactSaveLevel::kNoArtifacts },
		{ "final_results_only", ArtifactSaveLevel::kFinalResultsOnly },
		{ "all", ArtifactSaveLevel::kAll },
	};
	auto it = table.find(level);
	if (it == table.end())
		return false;
	artifact_save_level_string_ = level;
	artifact_save_level_ = it->second;
	return true;
}

bool GolfSimOptions::SetLoggingLevel(const std::string& level)
{
	static const std::map<std::string, LoggingLevel> table =
	{	{ "trace", LoggingLevel::kTrace },
		{ "debug", LoggingLevel::kDebug },
		{ "info", LoggingLevel::kInfo },
		{ "warn", LoggingLevel::kWarn },
		{ "error", LoggingLevel::kError },
		{ "none", LoggingLevel::kNone },
	};
	auto it = table.find(level);
	if (it == table.end())
		return false;
	logging_level_string_ = level;
	logging_level_ = it->second;
	return true;
}

bool GolfSimOptions::SetGolferOrientation(const std::string& orientation)
{
	static const std::map<std::string, GolferOrientation> table =
	{	{ "right_handed", GolferOrientation::kRightHanded },
		{ "left_handed", GolferOrientation::kLeftHanded },
	};
	auto it = table.find(orientation);
	if (it == table.end())
		return false;
	golfer_orientation_string_ = orientation;
	golfer_orientation_ = it->second;
	return true;
}

void GolfSimOptions::Print() const
{
	std::cout << "Options:" << std::endl;
	std::cout << "    system_mode: " << system_mode_string_ << std::endl;
	std::cout << "    logging_level: " << logging_level_string_ << std::endl;
	std::cout << "    artifact_save_level: " << artifact_save_level_string_ << std::endl;
	std::cout << "    shutdown: " << std::to_string(shutdown_) << std::endl;
	std::cout << "    cam_still_mode: " << std::to_string(camera_still_mode_) << std::endl;
	std::cout << "    lm_comparison_mode: " << std::to_string(lm_comparison_mode_) << std::endl;	
	std::cout << "    send_test_results: " << std::to_string(send_test_results_) << std::endl;
	if (!output_filename_.empty())
		std::cout << "    output_filename: " << output_filename_ << std::endl;
	if (!base_image_logging_dir_.empty())
		std::cout << "    base_image_logging_dir_: " << base_image_logging_dir_ << std::endl;
	if (!web_server_share_dir_.empty())
			std::cout << "    web_server_share_dir: " << web_server_share_dir_ << std::endl;
	if (!e6_host_address_.empty())
		std::cout << "    e6_host_address: " << e6_host_address_ << std::endl;
	if (!gspro_host_address_.empty())
		std::cout << "    gspro_host_address: " << gspro_host_address_ << std::endl;
	std::cout << "    web_server_port: " << std::to_string(web_server_port_) << std::endl;
	std::cout << "    pulse_test: " << std::to_string(perform_pulse_test_) << std::endl;
	std::cout << "    golfer_orientation: " << golfer_orientation_string_ << std::endl;
	std::cout << "    practice_ball: " << std::to_string(practice_ball_) << std::endl;
	std::cout << "    wait_keys: " << std::to_string(wait_for_key_on_images_) << std::endl;
	
	std::cout << "    show_images: " << std::to_string(show_images_) << std::endl;
	std::cout << "    use_non_IR_camera: " << std::to_string(use_non_IR_camera_) << std::endl;
	if (!command_line_file_.empty())
		std::cout << "    config file: " << command_line_file_ << std::endl;
	if (search_center_x_ > 0)
		std::cout << "    search_center_x: " << std::to_string(search_center_x_) << std::endl;
	if (search_center_y_ > 0)
		std::cout << "    search_center_y: " << std::to_string(search_center_y_) << std::endl;
	if (camera_gain_ > 0.0001)
		std::cout << "    camera_gain (will override .json file settings): " << std::to_string(camera_gain_) << std::endl;

}

} // namespace golf_sim

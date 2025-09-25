/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// The LM's command-line processing module.

#pragma once

#include <fstream>
#include <iostream>
#include <optional>

#include <boost/program_options.hpp>

namespace golf_sim {

	enum SystemMode {
		kTest = 0,			// Just run unit tests.  Generally used when compiled in Windows
		kCamera1 = 1,		// The mode when PiTrac is running normally and processing hits.  This mode is only used by the Pi 1/Camera 1 half of the system.
		kCamera2 = 2,		// The mode when PiTrac is running normally and processing hits.  This mode is only used by the Pi 2/Camera 2 half of the system.
		kCamera1TestStandalone = 3,
		kCamera2TestStandalone = 4,
		kCamera1Calibrate = 5,
		kCamera2Calibrate = 6,
		kTestSpin = 7,
		kCamera1BallLocation = 8,
		kCamera2BallLocation = 9,
		kTestExternalSimMessage = 10,
		kTestGSProServer = 11,
		kAutomatedTesting = 12,
		kCamera1AutoCalibrate = 13,
		kCamera2AutoCalibrate = 14,
		kRunCam2ProcessForPi1Processing = 15,  // This is for when a process is running on camera 2 for the purpose of auto-calibration or taking pictures for ball location
		kCamera2OnePulseOnly = 16,
	};

	enum LoggingLevel {
		kTrace = 0,      // Just run unit tests
		kDebug = 1,
		kInfo = 2,
		kWarn = 3,
		kError = 4,
		kNone = 5
	};

	enum ArtifactSaveLevel {
		kNoArtifacts = 0,
		kFinalResultsOnly = 1,	// produces images, but only at a few higher-level points in the processing
		kAll = 2				// May slow the system to a crawl, with over a dozen large intermediate files being written
	};

	// TBD - More of a place-holder.  Not implemented yet.
	enum GolferOrientation {
		kRightHanded = 0,
		kLeftHanded = 1
	};

	// TBD - Some question as to whether this should be defined here instead in GolfSimCamera class?
	// Note that the cameras are given enumerated values to match the name (e.g., 1, 2, etc.)
	enum GsCameraNumber {
		kGsCamera1 = 1,
		kGsCamera2 = 2
	};

	struct GolfSimOptions
	{
		GolfSimOptions()
		{
			using namespace boost::program_options;
			// clang-format off
			options_.add_options()
				("help,h", value<bool>(&help_)->default_value(false)->implicit_value(true),
					"Print this help message")
				("version", value<bool>(&version_)->default_value(false)->implicit_value(true),
					"Displays the build version number")
				("golfer_orientation", value<std::string>(&golfer_orientation_string_)->default_value("right_handed"),
					"Set the golfer's handed-ness (right_handed, left_handed)")
				("system_mode", value<std::string>(&system_mode_string_)->default_value("test"),
					"Set the system's operating mode (test, camera1, camera2, camera1Calibrate, camera2Calibrate, camera1_test_standalone, camera2_test_standalone, test_spin, camera1_ball_location, camera2_ball_location, test_gspro_message, test_gspro_server, automated_testing, camera1AutoCalibrate, camera2AutoCalibrate, runCam2ProcessForPi1Processing, camera2_one_pulse_only)")
				("logging_level", value<std::string>(&logging_level_string_)->default_value("warn"),
					"Set the system's logging level (trace, debug, info, warn, error, none)")
				("artifact_save_level", value<std::string>(&artifact_save_level_string_)->default_value("final_results_only"),
					"Set the system's level of saving artifact images to files (none, final_results_only, all)")
				("shutdown", value<bool>(&shutdown_)->default_value(false)->implicit_value(true),
					"Instructs any GolfSim instance connected to the ActiveMQ broker to shutdown")
				("cam_still_mode", value<bool>(&camera_still_mode_)->default_value(false)->implicit_value(true),
					"Take a single camera2 still picture (using one strobe flash) and exit")
				("lm_comparison_mode", value<bool>(&lm_comparison_mode_)->default_value(false)->implicit_value(true),
					"Configure for operating in another infrared-based LM environment")
				("send_test_results", value<bool>(&send_test_results_)->default_value(false)->implicit_value(true),
					"Send a single IPC results message (e.g., for testing) and exit")
				("skip_wait_armed", value<bool>(&skip_wait_armed_)->default_value(false)->implicit_value(true),
					"Skip waiting for simulator armed state (for hardware-less testing)")
				("output_filename", value<std::string>(&output_filename_)->default_value("out.png"),
					"Write any still picture to the specified filename")
				("pulse_test", value<bool>(&perform_pulse_test_)->default_value(false)->implicit_value(true),
					"Continually sends strobe and shutter signals")
				("practice_ball", value<bool>(&practice_ball_)->default_value(false)->implicit_value(true),
					"Configure system to expect a lightweight, soft, practice ball")
				("wait_keys", value<bool>(&wait_for_key_on_images_)->default_value(false)->implicit_value(true),
					"0 = Don't wait for a key press after showing each debug image, 1 = Do wait")
				("run_single_pi", value<bool>(&run_single_pi_)->default_value(false)->implicit_value(true),
					"0 =  System will run on separate Pi's, each with its own camer.  1 = This system will run both cameras on a single Pi.")
				("show_images", value<bool>(&show_images_)->default_value(false)->implicit_value(true),
					"0 = Don't show any debug/trace images in windows on the screen, 1 = Do")
				("use_non_IR_camera", value<bool>(&use_non_IR_camera_)->default_value(false)->implicit_value(true),
					"1 = The camera in use by this system is not an IR camera (and will likely need less gain)")
				("search_center_x", value<unsigned int>(&search_center_x_)->default_value(0),
					"Set the x coordinate of the center of the ball-search circle")
				("search_center_y", value<unsigned int>(&search_center_y_)->default_value(0),
					"Set the y coordinate of the center of the ball-search circle")
				("simulate_found_ball", value<bool>(&simulate_found_ball_)->default_value(false)->implicit_value(true),
					"Causes camera1 system to act as though a ball was found even if none is present.")
				("camera_gain", value<double>(&camera_gain_)->default_value(0.0),
					"Amount of gain for taking pictures")
				("base_image_logging_dir", value<std::string>(&base_image_logging_dir_)->default_value(""),
					"Specify the full path (with an ending '/') where diagnostic images are to be written. Default is: <empty>")
				("web_server_share_dir", value<std::string>(&web_server_share_dir_)->default_value(""),
					"Specify the full path (with an ending '/') where diagnostic images are to be written. Default  is: <empty>")
				("e6_host_address", value<std::string>(&e6_host_address_)->default_value(""),
					"Specify the name or IP address of the host PC that is running the E6 simulator.  Default is: <empty string>, indicating no TruGolf sim is connected.")
				("gspro_host_address", value<std::string>(&gspro_host_address_)->default_value(""),
					"Specify the name or IP address of the host PC that is running the GSPro simulator.  Default is: <empty string>, indicating no GSPro sim is connected.")
				("config_file", value<std::string>(&config_file_)->default_value("golf_sim_config.json"),
					"Specify the filename with the JSON configuration.  Default is: golf_sim_config.json")
				("cmd_file,cmd", value<std::string>(&command_line_file_)->implicit_value("config.txt"),
					"Read the options from a file. If no filename is specified, default to config.txt. "
					"In case of duplicate options, the ones provided on the command line will be used. "
					"Note that the config file must only contain the long form options.")

				;
			// clang-format on
		}

		virtual ~GolfSimOptions() {}

		static GolfSimOptions& GetCommandLineOptions();

		// Returns 1 for camera-1-based modes and 2 for 2-based modes
		GsCameraNumber GetCameraNumber();

		bool help_;
		bool version_;
		bool shutdown_;
		bool camera_still_mode_;
		bool lm_comparison_mode_;
		bool send_test_results_;
		bool skip_wait_armed_;
		bool practice_ball_;
		bool perform_pulse_test_;
		bool use_non_IR_camera_;
		bool simulate_found_ball_;
		std::string output_filename_;
		std::string system_mode_string_;
		std::string artifact_save_level_string_;
		std::string logging_level_string_;
		std::string command_line_file_;
		std::string base_image_logging_dir_;
		std::string web_server_share_dir_;
		std::string e6_host_address_;
		std::string gspro_host_address_;
		std::string config_file_;
		std::string golfer_orientation_string_;
		SystemMode system_mode_;
		LoggingLevel logging_level_;
		ArtifactSaveLevel artifact_save_level_;
		GolferOrientation golfer_orientation_;
		bool wait_for_key_on_images_;
		bool run_single_pi_;
		bool show_images_;
		unsigned int search_center_x_ = 0;
		unsigned int search_center_y_ = 0;
		double camera_gain_ = 0.0;   // 1.0 might seem more appropriate, but we want to be able to see if this is set or not

		virtual bool Parse(int argc, char* argv[]);
		virtual void Print() const;

	protected:
		boost::program_options::options_description options_;
		static GolfSimOptions the_command_line_options_;
	};
}

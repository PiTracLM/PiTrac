/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// This is the main interface between the LM and the underlying libcamera system.

#pragma once

#ifdef __unix__  // Ignore in Windows environment

#include "core/rpicam_app.hpp"
#include "core/still_options.hpp"

#include <opencv2/core.hpp>

#include "golf_ball.h"
#include "gs_camera.h"
#include "gs_options.h"

#include "still_image_libcamera_app.hpp"



namespace golf_sim {

	// TBD - Put in a struct or class sometime

	class LibCameraInterface {
	public:

		enum CropConfiguration {
			kCropUnknown,
			kFullScreen,
			kCropped
		};

		enum CameraConfiguration {
			kNotConfigured,
			kStillPicture,
			kHighSpeedWatching,
			kExternallyStrobed
		};

		static cv::Mat undistort_camera_image(const cv::Mat& img, GsCameraNumber camera_number, CameraHardware::CameraModel cameraModel);
		static bool SendCamera2PreImage(const cv::Mat& raw_image);

		static uint kMaxWatchingCropWidth;
		static uint kMaxWatchingCropHeight;
		static double kCamera1Gain;  // 0.0 to TBD??
		static double kCamera1Contrast; // 0.0 to 32.0
		static double kCamera2Gain;  // 0.0 to TBD??
		static double kCamera2ComparisonGain;  // 0.0 to TBD??
		static double kCamera2CalibrateOrLocationGain;
		static double kCamera2StrobedEnvironmentGain;
		static double kCamera2Contrast; // 0.0 to 32.0
		static double kCamera2PuttingGain;  // 0.0 to TBD??
		static double kCamera2PuttingContrast; // 0.0 to 32.0
		static std::string kCameraMotionDetectSettings;

		static long kCamera1StillShutterTimeuS;
		static long kCamera2StillShutterTimeuS;

		static CropConfiguration camera_crop_configuration_;
		static cv::Vec2i current_watch_resolution_;

		// The first (0th) element in the array is for camera1, the second for camera2
		static CameraConfiguration libcamera_configuration_[];
		static LibcameraJpegApp* libcamera_app_[];
	};

	bool CheckForBall(GolfBall& ball, cv::Mat& return_image);

	bool WatchForBallMovement(GolfSimCamera& c, const GolfBall& ball, bool & motion_detected);

	// Uses media-ctl to setup a cropping mode to allow for high FPS.  Requires GS camera.
	bool ConfigCameraForCropping(GolfBall ball1, GolfSimCamera& c, cv::Vec2i& watchResolution);

	std::string GetCmdLineForMediaCtlCropping(cv::Vec2i croppedHW, cv::Vec2i cropOffsetXY);

	bool RetrieveCameraInfo(cv::Vec2i& resolution, uint& frameRate, bool restartCamera = false);

	LibcameraJpegApp* ConfigureForLibcameraStill(GsCameraNumber camera_number);
	bool DeConfigureForLibcameraStill(GsCameraNumber camera_number);

	bool TakeLibcameraStill(cv::Mat& return_image);

	bool WatchForHitAndTrigger(const GolfBall& ball, cv::Mat& return_image, bool& motion_detected);

	bool ConfigCameraForCropping(const GolfSimCamera& c);

	bool WaitForCam2Trigger(cv::Mat& return_image);

	bool PerformCameraSystemStartup();

	static void SetLibCameraLoggingOff();


}

#endif // #ifdef __unix__  // Ignore in Windows environment

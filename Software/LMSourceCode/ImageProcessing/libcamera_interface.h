/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// This is the main interface between the LM and the underlying libcamera system.

#pragma once

#ifdef __unix__  // Ignore in Windows environment

#include "core/rpicam_app.hpp"
#include "core/rpicam_encoder.hpp"
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

		// The Camera 1 operates in a cropped mode only when watching the teed-up ball for a 
		// hit.  Otherwise, while watching for the ball to be teed up in the first place, the
		// camera operates at full resolution.
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

		static cv::Mat undistort_camera_image(const cv::Mat& img, const GolfSimCamera& camera);
		static bool SendCamera2PreImage(const cv::Mat& raw_image);

		static uint kMaxWatchingCropWidth;
		static uint kMaxWatchingCropHeight;
		static double kCamera1Gain;  // 0.0 to TBD??
		static double kCamera1Saturation;
		static double kCamera1HighFPSGain;  // 15.0 to TBD??
		static double kCamera1Contrast; // 0.0 to 32.0
		static double kCamera2Gain;  // 0.0 to TBD??
		static double kCamera2Saturation;
		static double kCamera2ComparisonGain;  // 0.0 to TBD??
		static double kCamera2CalibrateOrLocationGain;
		static double kCamera2StrobedEnvironmentGain;
		static double kCamera2Contrast; // 0.0 to 32.0
		static double kCamera2PuttingGain;  // 0.0 to TBD??
		static double kCamera2PuttingContrast; // 0.0 to 32.0
		static std::string kCameraMotionDetectSettings;

		static long kCamera1StillShutterTimeuS;
		static long kCamera2StillShutterTimeuS;

		// Once the cropped rectange is determined (usually around the center of the ball)
		// These offsets can further move that cropping area
		static int kCroppedImagePixelOffsetLeft;
		static int kCroppedImagePixelOffsetUp;


		static CropConfiguration camera_crop_configuration_;
		static cv::Vec2i current_watch_resolution_;
		static cv::Vec2i current_watch_offset_;


		// The first (0th) element in the array is for camera1, the second for camera2
		static CameraConfiguration libcamera_configuration_[];
		static LibcameraJpegApp* libcamera_app_[];

		// True (or set to a non-negative number) if we've already figured out the media and device number for the camera;
		static bool camera_location_found_;
		static int previously_found_media_number_;
		static int previously_found_device_number_;
	};

	bool TakeRawPicture(const GolfSimCamera& camera, cv::Mat& img);

	// Takes a picture and then tries to find the ball
	bool CheckForBall(GolfBall& ball, cv::Mat& return_image);

	// Configures the camera and the rest of the system to sit in a tight loop, waiting for the
	// ball to move.  Blocks until movement or some other event that causes the loop to stop
	// Returns whether or not motion was detected
	// Lower-level methods in the loop will try to trigger the external shutter of the camera 2
	// as soon as possible after motion has been detected.
	bool WatchForBallMovement(GolfSimCamera& camera, const GolfBall& ball, bool & motion_detected);

	// Do everything necessary to get the system ready to use a tightly-cropped camera video
	// mode (in order to allow high FPS)
	bool ConfigCameraForCropping(GolfBall ball, GolfSimCamera& camera, RPiCamEncoder& app);

	// Uses media-ctl to setup a cropping mode to allow for high FPS.  Requires GS camera.
	bool SendCameraCroppingCommand(const GolfSimCamera &camera, cv::Vec2i& cropping_window_size, cv::Vec2i& cropping_window_offset);

	// Sets up the rpicam-app-based post-processing pipeline so that the motion-detection stage knows 
	// how to analyze the cropped image
	bool ConfigurePostProcessing(const cv::Vec2i& roi_size, const cv::Vec2i& roi_offset);

	// Sets up a libcamera encoder with options necessary for a high FPS video loop in a cropped part of
	// the camera sensor.
	bool ConfigureLibCameraOptions(const GolfSimCamera& camera, RPiCamEncoder& app, const cv::Vec2i& cropping_window_size, uint cropped_frame_rate_fps_fps);

	std::string GetCmdLineForMediaCtlCropping(const GolfSimCamera &camera_number, cv::Vec2i croppedHW, cv::Vec2i cropOffsetXY);

	// Determine where the camera is, such as /dev/media2 at device 6
	bool DiscoverCameraLocation(const GsCameraNumber camera_number, int& media_number, int& device_number);

	bool RetrieveCameraInfo(const GsCameraNumber camera_number, cv::Vec2i& resolution, uint& frameRate, bool restartCamera = false);

	LibcameraJpegApp* ConfigureForLibcameraStill(const GolfSimCamera& camera);
	bool DeConfigureForLibcameraStill(const GsCameraNumber camera_number);

	bool TakeLibcameraStill(const GolfSimCamera& camera, cv::Mat& return_image);

	bool WatchForHitAndTrigger(const GolfBall& ball, cv::Mat& return_image, bool& motion_detected);

	// TBD - REMOVE bool ConfigCameraForCropping(const GolfSimCamera& c);

	bool WaitForCam2Trigger(cv::Mat& return_image);

	bool PerformCameraSystemStartup();

	static void SetLibCameraLoggingOff();


}

#endif // #ifdef __unix__  // Ignore in Windows environment

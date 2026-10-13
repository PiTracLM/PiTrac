/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include <algorithm>
#include <bitset>

#include "gs_options.h"
#include "ball_image_proc.h"
#include "pulse_strobe.h"
#include "gs_ui_system.h"
#include "gs_config.h"
#include "gs_clubs.h"

#include "libcamera_interface.h"

#include "gs_camera.h"
#include "gs_calibration.h"
#include "gs_http_client.h"


namespace golf_sim {

    GolfSimCalibration::CalibrationRigType GolfSimCalibration::kCalibrationRigType = GolfSimCalibration::CalibrationRigType::kCalibrationRigTypeUnknown;

    cv::Vec3d GolfSimCalibration::kFinalAutoCalibrationBallPositionFromCameraMeters;

    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCam1Meters;
    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCam2Meters;


    // If kCalibrationRigType is custom, then these constants will hold the position of the ball from each camera
    // for that custom rig.

    cv::Vec3d GolfSimCalibration::kCustomCalibrationRigPositionFromCamera1;
    cv::Vec3d GolfSimCalibration::kCustomCalibrationRigPositionFromCamera2;

    // These next two pairs of constants hold the ball position from each camera for the two supported
    // standard calibration rig types.

    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCam1MetersForStraightOutCamerasV2Enclosure;
    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCam2MetersForStraightOutCamerasV2Enclosure;

    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCam1MetersForSkewedCamerasV2Enclosure;
    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCam2MetersForSkewedCamerasV2Enclosure;

    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCam1MetersForStraightOutCamerasV3Enclosure;
    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCam2MetersForStraightOutCamerasV3Enclosure;

    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCam1MetersForSkewedCamerasV3Enclosure;
    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCam2MetersForSkewedCamerasV3Enclosure;


    // Number of pictures to average when determining focal length.  Because the focal length can tend
    // to bounce around a bit due to small variations in ball detection, averaging multiple pictures can help.

    int GolfSimCalibration::kNumberPicturesForFocalLengthAverage = 6;

    int GolfSimCalibration::kNumberOfCalibrationFailuresToTolerate = 4;


    GolfSimCalibration::GolfSimCalibration() {

        // TBD - Probably shouldn't be doing all of this in the constructor, but downstream
		// consumers of these values can check for initialization, so it should be OK for now.

        GS_LOG_TRACE_MSG(trace, "GolfSimCalibration reading constants from JSON file.");

        GolfSimConfiguration::SetConstant("gs_config.calibration.kNumberPicturesForFocalLengthAverage", kNumberPicturesForFocalLengthAverage);

        int rig_type = 0;
        GolfSimConfiguration::SetConstant("gs_config.calibration.kCalibrationRigType", rig_type);
        kCalibrationRigType = (GolfSimCalibration::CalibrationRigType)rig_type;

        GolfSimConfiguration::SetConstant("gs_config.calibration.kNumberOfCalibrationFailuresToTolerate", kNumberOfCalibrationFailuresToTolerate);

        GolfSimConfiguration::SetConstant("gs_config.calibration.kCustomCalibrationRigPositionFromCamera1", kCustomCalibrationRigPositionFromCamera1);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kCustomCalibrationRigPositionFromCamera2", kCustomCalibrationRigPositionFromCamera2);

        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBallPositionFromCam1MetersForStraightOutCamerasV2Enclosure", kAutoCalibrationBallPositionFromCam1MetersForStraightOutCamerasV2Enclosure);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBallPositionFromCam2MetersForStraightOutCamerasV2Enclosure", kAutoCalibrationBallPositionFromCam2MetersForStraightOutCamerasV2Enclosure);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBallPositionFromCam1MetersForSkewedCamerasV2Enclosure", kAutoCalibrationBallPositionFromCam1MetersForSkewedCamerasV2Enclosure);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBallPositionFromCam2MetersForSkewedCamerasV2Enclosure", kAutoCalibrationBallPositionFromCam2MetersForSkewedCamerasV2Enclosure);

        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBallPositionFromCam1MetersForStraightOutCamerasV3Enclosure", kAutoCalibrationBallPositionFromCam1MetersForStraightOutCamerasV3Enclosure);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBallPositionFromCam2MetersForStraightOutCamerasV3Enclosure", kAutoCalibrationBallPositionFromCam2MetersForStraightOutCamerasV3Enclosure);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBallPositionFromCam1MetersForSkewedCamerasV3Enclosure", kAutoCalibrationBallPositionFromCam1MetersForSkewedCamerasV3Enclosure);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBallPositionFromCam2MetersForSkewedCamerasV3Enclosure", kAutoCalibrationBallPositionFromCam2MetersForSkewedCamerasV3Enclosure);
    }

    GolfSimCalibration::~GolfSimCalibration() {
    }
    
    bool GolfSimCalibration::RetrieveAutoCalibrationConstants(const GsCameraNumber camera_number) {

        GS_LOG_TRACE_MSG(trace, "RetrieveAutoCalibrationConstants called with camera number = " + std::to_string(camera_number));
        GS_LOG_TRACE_MSG(trace, "RetrieveAutoCalibrationConstants using kCalibrationRigType = " + std::to_string(kCalibrationRigType));
        GS_LOG_TRACE_MSG(trace, "RetrieveAutoCalibrationConstants using kEnclosureVersion = " + std::to_string(GolfSimConfiguration::kEnclosureVersion));

        // Set the ball position based on the rig and enclosure type
        // These constants should already have been set by the constructor

        switch (kCalibrationRigType) {
        case CalibrationRigType::kStraightForwardCameras:
            kAutoCalibrationBallPositionFromCam1Meters = (GolfSimConfiguration::kEnclosureVersion == GolfSimConfiguration::EnclosureType::kEnclosureVersion_2) ? kAutoCalibrationBallPositionFromCam1MetersForStraightOutCamerasV2Enclosure : kAutoCalibrationBallPositionFromCam1MetersForStraightOutCamerasV3Enclosure;
            kAutoCalibrationBallPositionFromCam2Meters = (GolfSimConfiguration::kEnclosureVersion == GolfSimConfiguration::EnclosureType::kEnclosureVersion_2) ? kAutoCalibrationBallPositionFromCam2MetersForStraightOutCamerasV2Enclosure : kAutoCalibrationBallPositionFromCam2MetersForStraightOutCamerasV3Enclosure;
            break;

        case CalibrationRigType::kSkewedCamera1:
            kAutoCalibrationBallPositionFromCam1Meters = (GolfSimConfiguration::kEnclosureVersion == GolfSimConfiguration::EnclosureType::kEnclosureVersion_2) ? kAutoCalibrationBallPositionFromCam1MetersForSkewedCamerasV2Enclosure : kAutoCalibrationBallPositionFromCam1MetersForSkewedCamerasV3Enclosure;
            kAutoCalibrationBallPositionFromCam2Meters = (GolfSimConfiguration::kEnclosureVersion == GolfSimConfiguration::EnclosureType::kEnclosureVersion_2) ? kAutoCalibrationBallPositionFromCam2MetersForSkewedCamerasV2Enclosure : kAutoCalibrationBallPositionFromCam2MetersForSkewedCamerasV3Enclosure;
            break;

        case kSCustomRig:
            // Use custom values
            kAutoCalibrationBallPositionFromCam1Meters = kCustomCalibrationRigPositionFromCamera1;
            kAutoCalibrationBallPositionFromCam2Meters = kCustomCalibrationRigPositionFromCamera2;
            break;

        case CalibrationRigType::kCalibrationRigTypeUnknown:
        default:
            GS_LOG_TRACE_MSG(error, "GolfSimCalibration: Unknown calibration rig type.  Cannot set auto-calibration ball positions.");
			return false;
            break;
        }

		// Now set the final ball position based on the camera number
        kFinalAutoCalibrationBallPositionFromCameraMeters = (camera_number == GsCameraNumber::kGsCamera1) ? 
            kAutoCalibrationBallPositionFromCam1Meters : kAutoCalibrationBallPositionFromCam2Meters;

        GS_LOG_TRACE_MSG(trace, "kFinalAutoCalibrationBallPositionFromCameraMeters (x,y,z) distances to ball: " + LoggingTools::FormatVec3f(kFinalAutoCalibrationBallPositionFromCameraMeters));

        return true;
    }

    double GolfSimCalibration::DetermineFocalLengthForAutoCalibration(const cv::Mat& color_image, const GolfSimCamera& camera, GolfBall &ball) {
        GS_LOG_TRACE_MSG(trace, "DetermineFocalLengthUsingAutoCalibration called");

        // Find the ball in the image

        cv::Rect nullROI;
        std::vector<GolfBall> return_balls;
        BallImageProc* ip = BallImageProc::get_ball_image_processor();

        // The search mode depends on the camera we are calibrating.  The camera2 pictures will be more like that
        // of typical strobed (ball in flight) pictures.  
        // TBD - Still not sure this is the best mode?  Seems like kStrobed is best for camera 2 calibration
        BallImageProc::BallSearchMode search_mode = (camera.camera_hardware_.camera_number_ == 1) ? BallImageProc::BallSearchMode::kFindPlacedBall : BallImageProc::BallSearchMode::kStrobed;

        bool result = ip->GetBall(color_image, ball, return_balls, nullROI, search_mode);

        if (!result || return_balls.empty()) {
            GS_LOG_MSG(error, "GetBall() failed to get a ball.  Consider enabling Show Debug Images (gs_config.debug.kShowDebugImages) to determine why no ball was found.");
            return -1.0;
        }

        ball = return_balls[0];

        // Because we are auto-calibrating, we know the exact distance from the ball to the lens
        double distance_direct_to_ball = CvUtils::GetDistance(kFinalAutoCalibrationBallPositionFromCameraMeters);

        if (distance_direct_to_ball <= 0.0001) {
            LoggingTools::Warning("DetermineFocalLengthForAutoCalibration called without setting the kFinalAutoCalibrationBallPositionFromCameraMeters values.");
            return -1.0;
        }

        double measured_radius_pixels = ball.ball_circle_[2];

        if (measured_radius_pixels < 1) {
            GS_LOG_MSG(error, "DetermineFocalLengthForAutoCalibration() failed to get a ball with a non-zero radius.");
            return -1.0;
        }

        double calibrated_focal_length = GolfSimCamera::computeFocalDistanceFromBallData(camera, measured_radius_pixels, distance_direct_to_ball);
        GS_LOG_MSG(info, "Calibrated focal length for distance " + std::to_string(distance_direct_to_ball) + " and Radius: " + std::to_string(measured_radius_pixels) +
            " mm is " + std::to_string(calibrated_focal_length) + ".");

        return calibrated_focal_length;
    }

    bool GolfSimCalibration::DetermineCameraAngles(const cv::Mat& color_image, const GolfSimCamera& camera, cv::Vec2d& camera_angles) {

        GS_LOG_TRACE_MSG(trace, "DetermineCameraAngles called");

        if (color_image.empty()) {
            GS_LOG_MSG(error, "DetermineCameraAngles received empty color_image.");
            return false;
        }

        // Find the ball in the image

        GolfBall ball;
        cv::Rect nullROI;
        std::vector<GolfBall> return_balls;
        BallImageProc* ip = BallImageProc::get_ball_image_processor();

        // The search mode depends on the camera we are calibrating.  The camera2 pictures will be more like that
        // of typical strobed (ball in flight) pictures.  
        // TBD - Still not sure this is the best mode?
        BallImageProc::BallSearchMode search_mode = (camera.camera_hardware_.camera_number_ == 1) ? BallImageProc::BallSearchMode::kFindPlacedBall : BallImageProc::BallSearchMode::kStrobed;

        bool result = ip->GetBall(color_image, ball, return_balls, nullROI, search_mode);

        if (!result || return_balls.empty()) {
            GS_LOG_MSG(error, "GetBall() failed to get a ball.");
            return false;
        }

        ball = return_balls[0];

        if (kFinalAutoCalibrationBallPositionFromCameraMeters[2] <= 0.0) {
            GS_LOG_MSG(error, "DetermineCameraAngles called without kFinalAutoCalibrationBallPositionFromCameraMeters constants being set.");
            return false;
        }

        // The ball is seen along the ray through its pixel, and its real position is known.  The camera's
        // pan and tilt are whatever turn that ray onto the direction of the known position.
        // Positive X angle is counter-clockwise looking down on the camera/ball from above
        // Positive Y angle is tilted up
        if (!ball_geometry::SolveCameraAngles(GolfSimCamera::GetLens(camera.camera_hardware_), ball.x(), ball.y(),
                                              kFinalAutoCalibrationBallPositionFromCameraMeters, camera_angles)) {
            GS_LOG_MSG(error, "GolfSimCalibration::DetermineCameraAngles could not find camera angles that place the ball at its known position.");
            return false;
        }

        const double kMaxReasonableAngle = 45.0;
        if (std::abs(camera_angles[0]) > kMaxReasonableAngle || std::abs(camera_angles[1]) > kMaxReasonableAngle) {
            GS_LOG_MSG(error, "GolfSimCalibration::DetermineCameraAngles computed invalid camera angles: " +
                std::to_string(camera_angles[0]) + ", " + std::to_string(camera_angles[1]) +
                " degrees. Angles must be within +/- " + std::to_string(kMaxReasonableAngle) +
                " degrees. Rejecting calibration.");
            return false;
        }

        GS_LOG_TRACE_MSG(trace, "GolfSimCalibration::DetermineCameraAngles computed angles to the camera of: " +
            std::to_string(camera_angles[0]) + ", " +
            std::to_string(camera_angles[1]));

        return true;
    }

    bool GolfSimCalibration::AutoCalibrateCamera(GsCameraNumber camera_number) {

        GS_LOG_TRACE_MSG(trace, "AutoCalibrateCamera called with camera number = " + std::to_string(camera_number));

        if (!RetrieveAutoCalibrationConstants(camera_number)) {
            GS_LOG_MSG(error, "Could not RetrieveAutoCalibrationConstants.");
            return false;
        }

        // We will need a camera for context
        const CameraHardware::CameraModel  camera_model = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1CameraType : GolfSimCamera::kSystemSlot2CameraType;
        GS_LOG_TRACE_MSG(trace, "AutoCalibrateCamera called with camera model = " + std::to_string(camera_model));
        const CameraHardware::LensType  camera_lens_type = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1LensType : GolfSimCamera::kSystemSlot2LensType;
        GS_LOG_TRACE_MSG(trace, "AutoCalibrateCamera called with camera lens type = " + std::to_string(camera_lens_type));
		const CameraHardware::CameraOrientation camera_orientation = (camera_number == GsCameraNumber::kGsCamera1) ? GolfSimCamera::kSystemSlot1CameraOrientation : GolfSimCamera::kSystemSlot2CameraOrientation;
        GS_LOG_TRACE_MSG(trace, "AutoCalibrateCamera called with camera orientation = " + std::to_string(camera_orientation));

        GolfSimCamera camera;
        // Use the default focal length for the camera, as the focal length is one parameter
        // that this function is being called to re-set
        camera.camera_hardware_.init_camera_parameters(camera_number, camera_model, camera_lens_type, camera_orientation, true /* Use default, not .json focal-length*/);

        cv::Mat color_image;

        // Now that we have the correct camera, determine the focal length

        double average_focal_length = 0.0;
#ifdef __unix__  
        // In the "real" Pi environment, the focal length computation can vary from image to image,
        // so we will try multiple times and average the results
        int number_attempts = 10;

	if (kNumberPicturesForFocalLengthAverage > 0) {
        number_attempts = kNumberPicturesForFocalLengthAverage;
	}
#else
        // It's the same canned picture in the non-Pi environment, so no need to do averaging
        const int number_attempts = 1;
#endif
        int number_samples = 0;

        GolfBall ball;


        BallImageProc* ip = BallImageProc::get_ball_image_processor();

        if (ip == nullptr) {
            GS_LOG_MSG(error, "Could not get_ball_image_processor().");
            return false;
        }

        GS_LOG_TRACE_MSG(trace, "Expected (x,y,z) distances to ball: " + LoggingTools::FormatVec3f(kFinalAutoCalibrationBallPositionFromCameraMeters));

        double distance_direct_to_ball = CvUtils::GetDistance(kFinalAutoCalibrationBallPositionFromCameraMeters);
            
        if (distance_direct_to_ball <= 0.0) {
            GS_LOG_MSG(error, "Could not calculate a valid distance_direct_to_ball.");
            return false;
        }
            
        GS_LOG_TRACE_MSG(trace, "Expected distance_direct_to_ball is: " + std::to_string(distance_direct_to_ball));

        // Because we know the exact distance to the ball, the expected radius ranges
        // could be pretty tight.  However--and especially if we are using the AI-based ball ID, the ball identification
		// will probably work pretty well even with a wider range.  And a wider range will generally create fewer
        // problems.
        double expectedRadius = GolfSimCamera::GetExpectedBallRadiusPixels(camera.camera_hardware_, camera.camera_hardware_.resolution_x_, distance_direct_to_ball);

        const double kMaxReasonableRadius = 1000.0;
        if (expectedRadius <= 0.0 || expectedRadius > kMaxReasonableRadius) {
            GS_LOG_MSG(error, "GolfSimCalibration::AutoCalibrateCamera computed invalid expected ball radius: " +
                std::to_string(expectedRadius) + " pixels. Must be positive and less than " +
                std::to_string(kMaxReasonableRadius) + " pixels. Rejecting calibration.");
            return false;
        }

        // The problem with calculating the min/max ball radii using a multiplicative ratio,
        // is that for smaller expected radii, the range ended up too small.
        ip->min_ball_radius_ = std::max(0, (int)expectedRadius - GolfSimCamera::kMinRadiusOffset);
        ip->max_ball_radius_ = (int)expectedRadius + GolfSimCamera::kMaxRadiusOffset;

        if (ip->max_ball_radius_ <= 0 || ip->max_ball_radius_ > static_cast<int>(kMaxReasonableRadius)) {
            GS_LOG_MSG(error, "GolfSimCalibration::AutoCalibrateCamera computed invalid max_ball_radius: " +
                std::to_string(ip->max_ball_radius_) + " pixels. This would cause detection failures. Rejecting calibration.");
            return false;
        }

        GS_LOG_TRACE_MSG(trace, "Min/Max expected ball radii are: " + std::to_string(ip->min_ball_radius_) + " / " + std::to_string(ip->max_ball_radius_));

        GS_LOG_TRACE_MSG(trace, "Determining focal length for auto-calibration. Will average " + std::to_string(number_attempts) + " samples.");

        // Focal length can be touchy because of small changes in the perceived radius of the ball due to small changes in, for example, lighting
        // Find an average focal length
        int number_failures = 0;

        for (int i = 0; i < number_attempts; i++) {

            if (!GolfSimCamera::TakeStillPicture(camera, color_image)) {
                GS_LOG_MSG(error, "FAILED to TakeStillPicture");
                return false;
            }

            LoggingTools::LogImage("", color_image, std::vector < cv::Point >{}, true, "Focal_Length_Autocalibration_Input_Image_" + std::to_string(i) + ".png");

            // This code will take the place of determining the angles by hand measurements
            // At this point, we don't know at what angles the camera we're calibrating is oriented.
            // We cannot determine this without determining the focal length, so do that first and
            // then use is to determine the angles.
            
            GolfBall ball;
            double focal_length = DetermineFocalLengthForAutoCalibration(color_image, camera, ball);

            if (focal_length < 0.0) {

		        number_failures++;	

	   	        if (number_failures > kNumberOfCalibrationFailuresToTolerate) {
                	GS_LOG_MSG(error, "Could not DetermineFocalLengthForAutoCalibration -- Too many failures - giving up.  Check the input pictures for more information.");
                    return false;
		        }
		        else {
                	GS_LOG_MSG(warning, "Could not DetermineFocalLengthForAutoCalibration -- trying again.");
                    i--;
                    continue;
		        }
            }

            cv::Mat final_result_image = color_image.clone();
            LoggingTools::DrawCircleOutlineAndCenter(final_result_image, ball.ball_circle_, "Ball");

            // The intermediate image is useful to see if the circles are being identified accurately
            LoggingTools::LogImage("", final_result_image, std::vector < cv::Point >{}, true, "Focal_Length_Autocalibration_Results_Image_" + std::to_string(i) + ".png");


            number_samples++;

            average_focal_length += focal_length;
            std::string calibration_results_message = "Next Sampled Focal Length = " + std::to_string(focal_length) + ".";
            GS_LOG_MSG(info, calibration_results_message);
        }

        if (number_samples == 0) {
            GS_LOG_MSG(error, "GolfSimCalibration::AutoCalibrateCamera failed: All focal length samples failed. Unable to determine focal length.");
            return false;
        }

        average_focal_length /= number_samples;
        GS_LOG_MSG(info, "====>  Average Focal Length = " + std::to_string(average_focal_length) + ".");

        const double kMinFocalLength = 2.0;
        const double kMaxFocalLength = 50.0;
        if (average_focal_length < kMinFocalLength || average_focal_length > kMaxFocalLength) {
            GS_LOG_MSG(error, "GolfSimCalibration::AutoCalibrateCamera computed invalid focal length: " +
                std::to_string(average_focal_length) + " mm. Valid range is " +
                std::to_string(kMinFocalLength) + " to " + std::to_string(kMaxFocalLength) +
                " mm for typical camera lenses. Rejecting calibration.");
            return false;
        }

        // Re-set the camera_hardware object's focal length to reflect the real-world focal length we just determined.
        camera.camera_hardware_.focal_length_ = (float)average_focal_length;
            
        // Save the last image we captured to allow for review/QC.
        LoggingTools::LogImage("", color_image, std::vector < cv::Point >{}, true, "Base Autocalibration Image.png");

        // Also reset the expected radius numbers based on the (hopefully improved) focal length
        expectedRadius = GolfSimCamera::GetExpectedBallRadiusPixelsUsingKnownFocalLength(camera.camera_hardware_, color_image.cols, distance_direct_to_ball);
        ip->min_ball_radius_ = int(expectedRadius * 0.9);
        ip->max_ball_radius_ = int(expectedRadius * 1.1);


        GS_LOG_TRACE_MSG(trace, "Narrowed min/max expected ball radii (based on computed focal length) are: " + std::to_string(ip->min_ball_radius_) + " / " + std::to_string(ip->max_ball_radius_));

        cv::Vec2d camera_angles;

		// We assume the prior picture taken was ok for ball identification,
		// so no need to have a retry loop here.

        // Use the last-taken image to determine at what angle the ball is to the bore-line of the camera's center
        if (!DetermineCameraAngles(color_image, camera, camera_angles)) {
            GS_LOG_MSG(error, "Could not DetermineCameraAngles.");
            return false;
        }

        const bool is_camera1 = camera_number == GsCameraNumber::kGsCamera1;
        std::string focal_length_tag_name = is_camera1 ? "gs_config.cameras.kCamera1FocalLength" : "gs_config.cameras.kCamera2FocalLength";
        std::string camera_angles_tag_name = is_camera1 ? "gs_config.cameras.kCamera1Angles" : "gs_config.cameras.kCamera2Angles";

#ifdef __unix__
        std::vector<double> angles_vector = {camera_angles[0], camera_angles[1]};
        return GsHttpClient::UpdateCalibration(focal_length_tag_name, average_focal_length) &&
               GsHttpClient::UpdateCalibration(camera_angles_tag_name, angles_vector);
#else
        return false;
#endif
    }

}



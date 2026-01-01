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
#include "gs_web_api.h"


namespace golf_sim {

    GolfSimCalibration::CalibrationRigType GolfSimCalibration::kCalibrationRigType = GolfSimCalibration::CalibrationRigType::kCalibrationRigTypeUnknown;

    cv::Vec3d GolfSimCalibration::kFinalAutoCalibrationBallPositionFromCameraMeters;

    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCamera1Meters;
    cv::Vec3d GolfSimCalibration::kAutoCalibrationBallPositionFromCamera2Meters;


    // If kCalibrationRigType is custom, then these constants will hold the position of the ball from each camera
    // for that custom rig.

    cv::Vec3d GolfSimCalibration::kCustomCalibrationRigPositionFromCamera1;
    cv::Vec3d GolfSimCalibration::kCustomCalibrationRigPositionFromCamera2;

    // These next two pairs of constants hold the ball position from each camera for the two supported
    // standard calibration rig types.

    cv::Vec3d GolfSimCalibration::kAutoCalibrationBaselineBallPositionFromCamera1MetersForStraightOutCameras;
    cv::Vec3d GolfSimCalibration::kAutoCalibrationBaselineBallPositionFromCamera2MetersForStraightOutCameras;

    cv::Vec3d GolfSimCalibration::kAutoCalibrationBaselineBallPositionFromCamera1MetersForSkewedCameras;
    cv::Vec3d GolfSimCalibration::kAutoCalibrationBaselineBallPositionFromCamera2MetersForSkewedCameras;

    // Number of pictures to average when determining focal length.  Because the focal length can tend
    // to bounce around a bit due to small variations in ball detection, averaging multiple pictures can help.

    int GolfSimCalibration::kNumberPicturesForFocalLengthAverage = 5;

    int GolfSimCalibration::kNumberOfCalibrationFailuresToTolerate = 2;


    GolfSimCalibration::GolfSimCalibration() {

        // TBD - Probably shouldn't be doing all of this in the constructor, but downstream
		// consumers of these values can check for initialization, so it should be OK for now.

        GS_LOG_TRACE_MSG(trace, "GolfSimCalibration reading constants from JSON file.");

        GolfSimConfiguration::SetConstant("gs_config.calibration.kNumberPicturesForFocalLengthAverage", kNumberPicturesForFocalLengthAverage);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kNumberOfCalibrationFailuresToTolerate", kNumberOfCalibrationFailuresToTolerate);

        GolfSimConfiguration::SetConstant("gs_config.calibration.kCustomCalibrationRigPositionFromCamera1", kCustomCalibrationRigPositionFromCamera1);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kCustomCalibrationRigPositionFromCamera2", kCustomCalibrationRigPositionFromCamera2);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBaselineBallPositionFromCamera1MetersForStraightOutCameras", kAutoCalibrationBaselineBallPositionFromCamera1MetersForStraightOutCameras);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBaselineBallPositionFromCamera2MetersForStraightOutCameras", kAutoCalibrationBaselineBallPositionFromCamera2MetersForStraightOutCameras);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBaselineBallPositionFromCamera1MetersForSkewedCameras", kAutoCalibrationBaselineBallPositionFromCamera1MetersForSkewedCameras);
        GolfSimConfiguration::SetConstant("gs_config.calibration.kAutoCalibrationBaselineBallPositionFromCamera2MetersForSkewedCameras", kAutoCalibrationBaselineBallPositionFromCamera2MetersForSkewedCameras);
        int rig_type = 0;
        GolfSimConfiguration::SetConstant("gs_config.calibration.kCalibrationRigType", rig_type); 
        kCalibrationRigType = (GolfSimCalibration::CalibrationRigType)rig_type;
    }

    GolfSimCalibration::~GolfSimCalibration() {
    }

    bool GolfSimCalibration::RetrieveAutoCalibrationConstants(const GsCameraNumber camera_number) {

        GS_LOG_TRACE_MSG(trace, "RetrieveAutoCalibrationConstants called with camera number = " + std::to_string(camera_number));
        GS_LOG_TRACE_MSG(trace, "RetrieveAutoCalibrationConstants using kCalibrationRigType = " + std::to_string(kCalibrationRigType));

        // Set the ball position based on the rig type
        // These constants should already have been set by the constructor
        switch (kCalibrationRigType) {
        case CalibrationRigType::kStraightForwardCameras:
            kAutoCalibrationBallPositionFromCamera1Meters = kAutoCalibrationBaselineBallPositionFromCamera1MetersForStraightOutCameras;
            kAutoCalibrationBallPositionFromCamera2Meters = kAutoCalibrationBaselineBallPositionFromCamera2MetersForStraightOutCameras;
            break;

        case CalibrationRigType::kSkewedCamera1:
            kAutoCalibrationBallPositionFromCamera1Meters = kAutoCalibrationBaselineBallPositionFromCamera1MetersForSkewedCameras;
            kAutoCalibrationBallPositionFromCamera2Meters = kAutoCalibrationBaselineBallPositionFromCamera2MetersForSkewedCameras;
            break;

        case kSCustomRig:
            // Use custom values
            kAutoCalibrationBallPositionFromCamera1Meters = kCustomCalibrationRigPositionFromCamera1;
            kAutoCalibrationBallPositionFromCamera2Meters = kCustomCalibrationRigPositionFromCamera2;
            break;

        case CalibrationRigType::kCalibrationRigTypeUnknown:
        default:
            GS_LOG_TRACE_MSG(error, "GolfSimCalibration: Unknown calibration rig type.  Cannot set auto-calibration ball positions.");
			return false;
            break;
        }

        kFinalAutoCalibrationBallPositionFromCameraMeters = (camera_number == GsCameraNumber::kGsCamera1) ? 
                            kAutoCalibrationBallPositionFromCamera1Meters : kAutoCalibrationBallPositionFromCamera2Meters;

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
            GS_LOG_MSG(error, "GetBall() failed to get a ball.  Consider setting  --show_images=1  in order to determine why no ball was found.");
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

        // First calculate the distances as if the camera was facing straight ahead toward 
        // the ball flight plane.  
        // The Z-distance in this scenario would measure as if the image plane is orthogonal to the camera's
        // bore.  The image plane would only actually go through the ball's center if the
        // ball was in the exact center of the image.

        double xFromCameraCenter = ball.x() - std::round(camera.camera_hardware_.resolution_x_ / 2.0);
        double yFromCameraCenter = ball.y() - std::round(camera.camera_hardware_.resolution_y_ / 2.0);

        cv::Vec3d camera_perspective_distances;

        double distance_direct_to_ball = CvUtils::GetDistance(kFinalAutoCalibrationBallPositionFromCameraMeters);

        if (distance_direct_to_ball <= 0.0001) {
            LoggingTools::Warning("DetermineFocalLengthForAutoCalibration called without setting the kFinalAutoCalibrationBallPositionFromCameraMeters values.");
            return false;
        }

        if (kFinalAutoCalibrationBallPositionFromCameraMeters[2] < 0.0) {
            GS_LOG_MSG(error, "DetermineCameraAngles called without kFinalAutoCalibrationBallPositionFromCameraMeters constants being set.");
            return false;
        }

        // We have the direct-to-ball-PLANE distance - it is already in real-world meters.   
        // However, we do not have the exact direct-to-ball distance due to the fact the lens will slightly
        // enlarge objects that are actually further away the camera.

        // Use the direct-to-ball-plane distance as the direct-to-ball distance to calculate the offset of the ball from center.
        double xDistanceFromCamCenter = GolfSimCamera::convertXDistanceToMeters(camera, distance_direct_to_ball, xFromCameraCenter);
        camera_perspective_distances[0] = xDistanceFromCamCenter;  // X distance, negative means to the left of the camera

        double yDistanceFromCamCenter = GolfSimCamera::convertYDistanceToMeters(camera, distance_direct_to_ball, yFromCameraCenter);

        camera_perspective_distances[1] = -yDistanceFromCamCenter;  // Y distance, positive is upward (smaller Y values)

        camera_perspective_distances[2] = kFinalAutoCalibrationBallPositionFromCameraMeters[2];

        GS_LOG_TRACE_MSG(trace, "GolfSimCalibration::DetermineCameraAngles computed camera_perspective_distances of: " +
            std::to_string(camera_perspective_distances[0]) + ", " +
            std::to_string(camera_perspective_distances[1]));

        // Determine the angles from the center-bore of the camera at which the ball exists to the ball

        // Angles in this section are taken using a ray that extends out from the camera
        // Positive X angle is counter-clockwise looking down on the camera/ball from above
        // Positive Y angle is looking up from level to the ball
        double x_angle_degrees_of_ball_camera_perspective = -CvUtils::RadiansToDegrees(atan(camera_perspective_distances[0] / distance_direct_to_ball));
        double y_angle_degrees_of_ball_camera_perspective = CvUtils::RadiansToDegrees(atan(camera_perspective_distances[1] / distance_direct_to_ball));

        GS_LOG_TRACE_MSG(trace, "GolfSimCalibration::DetermineCameraAngles computed angles to ball from center-bore of camera of: " +
            std::to_string(x_angle_degrees_of_ball_camera_perspective) + ", " +
            std::to_string(y_angle_degrees_of_ball_camera_perspective));

        // Determine the angles at which the camera would be if the ball were centered (in other
        // words, the angle of the ball from the center of the lens if the camera was
        // pointing straight out).

        double x_angle_degrees_of_ball_lm_perspective = -CvUtils::RadiansToDegrees(atan(kFinalAutoCalibrationBallPositionFromCameraMeters[0] / kFinalAutoCalibrationBallPositionFromCameraMeters[2]));
            
        // Need to calculate the adjacent (tan x = opposite/adjacent) distance by using the known x and z distances) to determine the y angle
        double horizontal_distance_to_ball_vertical_axis = sqrt(pow(kFinalAutoCalibrationBallPositionFromCameraMeters[0], 2) + pow(kFinalAutoCalibrationBallPositionFromCameraMeters[2], 2) );
        double y_angle_degrees_of_ball_lm_perspective = CvUtils::RadiansToDegrees(atan(kFinalAutoCalibrationBallPositionFromCameraMeters[1] / horizontal_distance_to_ball_vertical_axis));

        GS_LOG_TRACE_MSG(trace, "GolfSimCalibration::DetermineCameraAngles computed angles to ball from the perspective of the LM (from the center of the camera lens if the camera was pointing straight out): " +
            std::to_string(x_angle_degrees_of_ball_lm_perspective) + ", " +
            std::to_string(y_angle_degrees_of_ball_lm_perspective));

        // The difference (if any) will be the angle of the ball from the camera
        camera_angles[0] = x_angle_degrees_of_ball_lm_perspective - x_angle_degrees_of_ball_camera_perspective;
        camera_angles[1] = y_angle_degrees_of_ball_lm_perspective - y_angle_degrees_of_ball_camera_perspective;

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
        GS_LOG_MSG(info, "====>  Average Focal Length = " + std::to_string(average_focal_length) + ". Will set this value into the gs_config.json file.");

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

        // Now save the values out to a .json file

        std::string camera_number_string = std::to_string(camera_number);
            
        std::string focal_length_tag_name = "gs_config.cameras.kCamera" + camera_number_string + "FocalLength";
        std::string camera_angles_tag_name = "gs_config.cameras.kCamera" + camera_number_string + "Angles";

        GolfSimConfiguration::SetTreeValue(focal_length_tag_name, average_focal_length);
        GolfSimConfiguration::SetTreeValue(camera_angles_tag_name, camera_angles);
            
        WebApi::UpdateCalibration(focal_length_tag_name, average_focal_length);
            
        std::vector<double> angles_vector = {camera_angles[0], camera_angles[1]};
        WebApi::UpdateCalibration(camera_angles_tag_name, angles_vector);

        std::string config_file_name = "golf_sim_config.json";

        if (!GolfSimOptions::GetCommandLineOptions().config_file_.empty()) {

            config_file_name = GolfSimOptions::GetCommandLineOptions().config_file_;
        }

        // Add only to the tail of the file name to ensure that any prefixed path will remain valid
        std::string backup_json_file_name = config_file_name + "_BACKUP_" + LoggingTools::GetUniqueLogName() + ".json";

        GS_LOG_TRACE_MSG(info, "Saving current golf_sim_config.json file to filename = " + backup_json_file_name);

#ifdef __unix__  
        std::string cp_command = "cp " + config_file_name + " " + backup_json_file_name;
#else
        std::string cp_command = "copy " + config_file_name + " " + backup_json_file_name;
#endif
        int command_result = system(cp_command.c_str());

        if (command_result != 0) {
            GS_LOG_TRACE_MSG(trace, "system(cp_command) failed. Could not backup existing golf_sim_config.json file.  Exiting");
            return false;
        }

        // NOTE - we will overwrite the original config file
        std::string results_tree_file_name = config_file_name;

        if (!GolfSimConfiguration::WriteTreeToFile(results_tree_file_name)) {
            GS_LOG_MSG(error, "Could not WriteTreeToFile(" + results_tree_file_name + ").");
            return false;
        }

        return true;
    }

}



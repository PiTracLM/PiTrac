/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

/*
    The golf-sim-camera module operates the hardware camera and deals with tasks
    The module generally interfaces to the rest of the system by taking images as input
    and by producing golf_ball objects as output.

    An important function of this class is to identify a set of potential golf balls (circles)
    from a strobed image that may include multiple, possibly-overlapping golf balls.
    See U.S. Patent Application No. 18/428,191 for more details.
*/

#include <string>
#include "camera_hardware.h"
#include "cv_utils.h"
#include "golf_ball.h"
#include "gs_globals.h"
#include "logging_tools.h"

namespace golf_sim {

    class GolfSimCalibration {
       public:
        // Note that the skewed camera rig sets up the ball for the teed ball cameara to be
        // rotated away from the centerline in order to give the ball-movement detection more time
        // to detect movement.

        enum CalibrationRigType {
            kStraightForwardCameras = 1,
            kSkewedCamera1 = 2,
            kSCustomRig = 3,
            kCalibrationRigTypeUnknown
        };

        static CalibrationRigType kCalibrationRigType;

        // These next two constants hold the finally-selected ball position from each camera for the
        // auto-calibration after we figure out which rig and enclosure type we are using.
        static cv::Vec3d kAutoCalibrationBallPositionFromCam1Meters;
        static cv::Vec3d kAutoCalibrationBallPositionFromCam2Meters;

        // If kCalibrationRigType is custom, then these constants will hold the position of the ball
        // from each camera for that custom rig.

        static cv::Vec3d kCustomCalibrationRigPositionFromCamera1;
        static cv::Vec3d kCustomCalibrationRigPositionFromCamera2;

        // These next two pairs of constants hold the ball position from each camera for the two
        // supported standard calibration rig types.

        static cv::Vec3d kAutoCalibrationBallPositionFromCam1MetersForStraightOutCamerasV2Enclosure;
        static cv::Vec3d kAutoCalibrationBallPositionFromCam2MetersForStraightOutCamerasV2Enclosure;

        static cv::Vec3d kAutoCalibrationBallPositionFromCam1MetersForSkewedCamerasV2Enclosure;
        static cv::Vec3d kAutoCalibrationBallPositionFromCam2MetersForSkewedCamerasV2Enclosure;

        static cv::Vec3d kAutoCalibrationBallPositionFromCam1MetersForStraightOutCamerasV3Enclosure;
        static cv::Vec3d kAutoCalibrationBallPositionFromCam2MetersForStraightOutCamerasV3Enclosure;

        static cv::Vec3d kAutoCalibrationBallPositionFromCam1MetersForSkewedCamerasV3Enclosure;
        static cv::Vec3d kAutoCalibrationBallPositionFromCam2MetersForSkewedCamerasV3Enclosure;

        // Number of pictures to average when determining focal length.  Because the focal length
        // can tend to bounce around a bit due to small variations in ball detection, averaging
        // multiple pictures can help.

        static int kNumberPicturesForFocalLengthAverage;

        // The ball-detection algorithm can sometimes fail to find the ball in an image.  We will
        // let that occur a few times before giving up on the calibration process.
        static int kNumberOfCalibrationFailuresToTolerate;

        // Used internally during calibration
        static cv::Vec3d kFinalAutoCalibrationBallPositionFromCameraMeters;

       public:
        GolfSimCalibration();

        ~GolfSimCalibration();

        static bool AutoCalibrateCamera(GsCameraNumber camera_number);

        static bool RetrieveAutoCalibrationConstants(GsCameraNumber camera_number);

        static bool DetermineCameraAngles(const cv::Mat& color_image, const GolfSimCamera& camera,
                                          cv::Vec2d& camera_angles);

        // Returns -1.0 on error, otherwise a positive focal length (e.g., 6.3)
        // The ball is the ball that the focal length was determined from
        static double DetermineFocalLengthForAutoCalibration(const cv::Mat& color_image,
                                                             const GolfSimCamera& camera,
                                                             GolfBall& ball);
    };
}  // namespace golf_sim

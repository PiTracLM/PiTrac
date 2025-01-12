/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#ifdef __unix__  // Ignore in Windows environment

#include "logging_tools.h"

#include "gs_ipc_result.h"
#include "gs_options.h"
#include "gs_clubs.h"
#include "gs_ui_system.h"
#include "gs_sim_interface.h"
#include "gs_camera.h"

namespace golf_sim {

    std::string GsUISystem::kWebServerShareDirectory;
    std::string GsUISystem::kWebServerResultBallExposureCandidates;
    std::string GsUISystem::kWebServerResultSpinBall1Image;
    std::string GsUISystem::kWebServerResultSpinBall2Image;
    std::string GsUISystem::kWebServerResultBallRotatedByBestAngles;
    std::string GsUISystem::kWebServerErrorExposuresImage;
    std::string GsUISystem::kWebServerBallSearchAreaImage;


    void GsUISystem::SendIPCErrorStatusMessage(const std::string& error_message) {

        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kResults);
        GsIPCResult& error_result = ipc_message.GetResultsForModification();

        error_result.result_type_ = GsIPCResultType::kError;

        if (LoggingTools::current_error_root_cause_ != "") {
            error_result.message_ = LoggingTools::current_error_root_cause_;
            // We've effectively consumed the root cause error, so reset it to empty for 
            // any future errors
            LoggingTools::current_error_root_cause_ = "";
        }
        else {
            error_result.message_ = error_message;
        }

        error_result.log_messages_ = LoggingTools::GetRecentLogMessages();

        GS_LOG_TRACE_MSG(trace, "FSM is sending an Error-Type IPC Results Message:" + error_result.Format());

        GolfSimIpcSystem::SendIpcMessage(ipc_message);
    }


    bool GsUISystem::SendIPCStatusMessage(const GsIPCResultType message_type, const std::string& custom_message) {

        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kResults);
        GsIPCResult& results = ipc_message.GetResultsForModification();
        results.club_type_ = GolfSimClubs::GetCurrentClubType();

        results.result_type_ = message_type;

        switch (message_type) {
        case GsIPCResultType::kInitializing:
            results.message_ = "Version 0.0X.  System Mode: " + std::to_string(GolfSimOptions::GetCommandLineOptions().system_mode_);
            break;

        case GsIPCResultType::kWaitingForBallToAppear:
            if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1Calibrate ||
                GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2Calibrate)
            {
                results.message_ = "Waiting for ball to be teed up at " + std::to_string(GolfSimCamera::kCamera1CalibrationDistanceToBall) + "cm in order to perform calibration.";
            }
            else {
                results.message_ = "Waiting for ball to be teed up.";
            }
            break;

        case GsIPCResultType::kPausingForBallStabilization:
            results.message_ = "Ball teed.  Confirming ball is stable.";
            break;

        case GsIPCResultType::kWaitingForSimulatorArmed:
            results.message_ = "Waiting on the simulator to be armed (ready to accept a shot).";
            break;

        case GsIPCResultType::kMultipleBallsPresent:
            results.message_ = "Multiple balls present.";
            break;

        case GsIPCResultType::kBallPlacedAndReadyForHit:
            results.message_ = "Ball placed - Let's Golf!";
            break;

        case GsIPCResultType::kHit:
            results.message_ = "Ball hit - waiting for Results.";
            break;

        case GsIPCResultType::kCalibrationResults:
            results.message_ = "Returning Camera Calibration Results - see message.";
            break;

        default:
            GS_LOG_TRACE_MSG(trace, "SendIPCStatusMessage received unknown GsIPCResultType : " + std::to_string((int)message_type));
            return false;
            break;
        }

        if (!custom_message.empty()) {
            results.message_ = custom_message;
        }

        GS_LOG_TRACE_MSG(trace, "FSM is sending an IPC Results Message: " + results.Format());

        GolfSimIpcSystem::SendIpcMessage(ipc_message);

        return true;
    }

    void GsUISystem::SendIPCHitMessage(const GolfBall& result_ball, const std::string& secondary_message) {
        GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kResults);

        GsIPCResult& results = ipc_message.GetResultsForModification();

        results.result_type_ = GsIPCResultType::kHit;
        results.speed_mpers_ = result_ball.velocity_;
        results.carry_meters_ = 100 + rand() % 150;
        results.launch_angle_deg_ = result_ball.angles_ball_perspective_[1];
        results.side_angle_deg_ = result_ball.angles_ball_perspective_[0];
        results.back_spin_rpm_ = result_ball.rotation_speeds_RPM_[2];
        results.side_spin_rpm_ = result_ball.rotation_speeds_RPM_[0];
        results.confidence_ = 5;  // TBD - Set from analysis
        results.message_ = "Ball Hit - Results returned." + secondary_message;

        GS_LOG_MSG(info, "BALL_HIT_CSV, " + std::to_string(GsSimInterface::GetShotCounter()) + ", (carry - NA), (Total - NA), (Side Dest - NA), (Smash Factor - NA), (Club Speed - NA), "
            + std::to_string(CvUtils::MetersPerSecondToMPH(results.speed_mpers_)) + ", "
            + std::to_string(results.back_spin_rpm_) + ", "
            + std::to_string(results.side_spin_rpm_) + ", "
            + std::to_string(results.launch_angle_deg_) + ", "
            + std::to_string(results.side_angle_deg_) 
            + ", (Descent Angle-NA), (Apex-NA), (Flight Time-NA), (Type-NA)"
            );

        GolfSimIpcSystem::SendIpcMessage(ipc_message);
    }


    bool GsUISystem::SaveWebserverImage(const std::string& input_file_name,
                                        const cv::Mat& img,
                                        bool suppress_diagnostic_saving) {

        GS_LOG_MSG(trace, "GsUISystem::SaveWebserverImage called with file name = " + input_file_name);

        if (img.empty()) {
            GS_LOG_MSG(warning, "GsUISystem::SaveWebserverImage was empty - ignoring.");
            return false;
        }

        std::string file_name(input_file_name);

        if (GolfSimCamera::kLogDiagnosticImagesToUniqueFiles  && !suppress_diagnostic_saving) {

            // Save a unique version of the webserver image into a directory that will not get
            // over-written.  A unque timestamp will be added to the file name
            std::string fname(file_name);

            LoggingTools::LogImage(file_name + "_Shot_" + std::to_string(GsSimInterface::GetShotCounter()) + "_", img, std::vector < cv::Point >{});
        }

        if (!GolfSimCamera::kLogWebserverImagesToFile) {
            return true;
        }

        if (file_name.find(".png") == std::string::npos) {
            file_name += ".png";
        }


        // The kWebServerShareDirectory is already setup to have a trailing "/"
        std::string fname = kWebServerShareDirectory + file_name;

        try {
            if (cv::imwrite(fname, img)) {
                GS_LOG_TRACE_MSG(trace, "Logged image to file: " + fname);
            }
            else {
                GS_LOG_MSG(warning, "GsUISystem::SaveWebserverImage - could not save to file name: " + fname);
            }
        }
        catch (std::exception& ex) {
            GS_LOG_TRACE_MSG(warning, "Exception! - failed to imwrite with fname = " + fname);
        }

        return true;
    }


    bool GsUISystem::SaveWebserverImage(const std::string& file_name,
                                        const cv::Mat& img,
                                        const std::vector<GolfBall>& balls,
                                        bool suppress_diagnostic_saving) {

        if (!GolfSimCamera::kLogWebserverImagesToFile) {
            return true;
        }

        cv::Mat ball_image = img.clone();

        // Show the final candidates for 
        for (size_t i = 0; i < balls.size(); i++) {
            const GolfBall& b = balls[i];
            const GsCircle& c = b.ball_circle_;

            std::string label = std::to_string(i);
            LoggingTools::DrawCircleOutlineAndCenter(ball_image, c, label);
        }

        return SaveWebserverImage(file_name, ball_image, suppress_diagnostic_saving);
    }

    void GsUISystem::ClearWebserverImages() {
        // The kWebServerShareDirectory is already setup to have a trailing "/"
        std::string command = "rm -f " + kWebServerShareDirectory + "*.png";

        int cmdResult = system(command.c_str());

        if (cmdResult != 0) {
            GS_LOG_TRACE_MSG(trace, "system(" + command + ") failed.");
        }
    }

}

#endif // #ifdef __unix__  // Ignore in Windows environment

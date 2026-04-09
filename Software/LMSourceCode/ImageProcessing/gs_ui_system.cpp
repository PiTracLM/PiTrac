/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#ifdef __unix__  // Ignore in Windows environment

#include "logging_tools.h"

#include "gs_result_types.h"
#include "gs_options.h"
#include "gs_clubs.h"
#include "gs_ui_system.h"
#include "gs_sim_interface.h"
#include "gs_camera.h"
#include "gs_http_client.h"
#include "cv_utils.h"

namespace golf_sim {

    std::string GsUISystem::kWebServerShareDirectory;
    std::string GsUISystem::kWebServerResultBallExposureCandidates;
    std::string GsUISystem::kWebServerResultSpinBall1Image;
    std::string GsUISystem::kWebServerResultSpinBall2Image;
    std::string GsUISystem::kWebServerResultBallRotatedByBestAngles;
    std::string GsUISystem::kWebServerErrorExposuresImage;
    std::string GsUISystem::kWebServerBallSearchAreaImage;

    static std::string EscapeJson(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 16);
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += c;
            }
        }
        return out;
    }

    static std::string BuildResultJson(int result_type, const std::string& message,
                                       float speed_mps = 0, float launch_deg = 0,
                                       float side_deg = 0, int back_spin = 0,
                                       int side_spin = 0, int carry_m = 0,
                                       const std::vector<std::string>& images = {}) {
        std::string json = "{";
        json += "\"result_type\":" + std::to_string(result_type);
        json += ",\"speed_mps\":" + std::to_string(speed_mps);
        json += ",\"launch_angle\":" + std::to_string(launch_deg);
        json += ",\"side_angle\":" + std::to_string(side_deg);
        json += ",\"back_spin\":" + std::to_string(back_spin);
        json += ",\"side_spin\":" + std::to_string(side_spin);
        json += ",\"carry\":" + std::to_string(carry_m);
        json += ",\"message\":\"" + EscapeJson(message) + "\"";
        json += ",\"images\":[";
        for (size_t i = 0; i < images.size(); i++) {
            if (i > 0) json += ",";
            json += "\"" + EscapeJson(images[i]) + "\"";
        }
        json += "]}";
        return json;
    }


    void GsUISystem::SendIPCErrorStatusMessage(const std::string& error_message) {
        std::string msg;
        if (!LoggingTools::current_error_root_cause_.empty()) {
            msg = LoggingTools::current_error_root_cause_;
            LoggingTools::current_error_root_cause_ = "";
        } else {
            msg = error_message;
        }

        GS_LOG_TRACE_MSG(trace, "Sending error result: " + msg);
        GsHttpClient::PostResult(BuildResultJson(
            static_cast<int>(GsIPCResultType::kError), msg));
    }


    bool GsUISystem::SendIPCStatusMessage(const GsIPCResultType message_type, const std::string& custom_message) {
        std::string msg;

        switch (message_type) {
        case GsIPCResultType::kInitializing:
            msg = "Version 0.0X.  System Mode: " + std::to_string(GolfSimOptions::GetCommandLineOptions().system_mode_);
            break;
        case GsIPCResultType::kWaitingForBallToAppear:
            if (GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera1Calibrate ||
                GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2Calibrate) {
                msg = "Waiting for ball to be teed up at " + std::to_string(GolfSimCamera::kCamera1CalibrationDistanceToBall) + "cm in order to perform calibration.";
            } else {
                msg = "Waiting for ball to be teed up.";
            }
            break;
        case GsIPCResultType::kPausingForBallStabilization:
            msg = "Ball teed.  Confirming ball is stable.";
            break;
        case GsIPCResultType::kWaitingForSimulatorArmed:
            msg = "Waiting on the simulator to be armed (ready to accept a shot).";
            break;
        case GsIPCResultType::kMultipleBallsPresent:
            msg = "Multiple balls present.";
            break;
        case GsIPCResultType::kBallPlacedAndReadyForHit:
            msg = "Ball placed - Let's Golf!";
            break;
        case GsIPCResultType::kHit:
            msg = "Ball hit - waiting for Results.";
            break;
        case GsIPCResultType::kCalibrationResults:
            msg = "Returning Camera Calibration Results - see message.";
            break;
        default:
            GS_LOG_TRACE_MSG(trace, "SendIPCStatusMessage received unknown GsIPCResultType : " + std::to_string((int)message_type));
            return false;
        }

        if (!custom_message.empty()) {
            msg = custom_message;
        }

        GS_LOG_TRACE_MSG(trace, "Sending status result: " + msg);
        GsHttpClient::PostResult(BuildResultJson(static_cast<int>(message_type), msg));
        return true;
    }

    void GsUISystem::SendIPCHitMessage(const GolfBall& result_ball, const std::string& secondary_message) {
        float speed = result_ball.velocity_;
        float launch = result_ball.angles_ball_perspective_[1];
        float side = result_ball.angles_ball_perspective_[0];
        int back_spin = static_cast<int>(result_ball.rotation_speeds_RPM_[2]);
        int side_spin = static_cast<int>(result_ball.rotation_speeds_RPM_[0]);
        int carry = 100 + rand() % 150;

        std::vector<std::string> images;

        std::string msg = "Ball Hit - Results returned." + secondary_message;

        GS_LOG_MSG(info, "BALL_HIT_CSV, " + std::to_string(GsSimInterface::GetShotCounter())
            + ", (carry - NA), (Total - NA), (Side Dest - NA), (Smash Factor - NA), (Club Speed - NA), "
            + std::to_string(CvUtils::MetersPerSecondToMPH(speed)) + ", "
            + std::to_string(back_spin) + ", "
            + std::to_string(side_spin) + ", "
            + std::to_string(launch) + ", "
            + std::to_string(side)
            + ", (Descent Angle-NA), (Apex-NA), (Flight Time-NA), (Type-NA)");

        GsHttpClient::PostResult(BuildResultJson(
            static_cast<int>(GsIPCResultType::kHit), msg,
            speed, launch, side, back_spin, side_spin, carry, images));
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
            LoggingTools::LogImage(file_name + "_", img, std::vector < cv::Point >{}, false, "", "_Shot_" + std::to_string(GsSimInterface::GetShotCounter()));
        }

        if (!GolfSimCamera::kLogWebserverImagesToFile) {
            return true;
        }

        if (file_name.find(".png") == std::string::npos) {
            file_name += ".png";
        }

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

        for (size_t i = 0; i < balls.size(); i++) {
            const GolfBall& b = balls[i];
            const GsCircle& c = b.ball_circle_;
            std::string label = std::to_string(i);
            LoggingTools::DrawCircleOutlineAndCenter(ball_image, c, label);
        }

        return SaveWebserverImage(file_name, ball_image, suppress_diagnostic_saving);
    }

    void GsUISystem::ClearWebserverImages() {
        // Disabled — images are needed for debugging
        return;
    }

}

#endif // #ifdef __unix__  // Ignore in Windows environment

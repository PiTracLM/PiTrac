/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Main class for communicating with the system's web-based GUI.

#pragma once

#ifdef __unix__  // Ignore in Windows environment


#include <gs_ipc_system.h>

#include "logging_tools.h"
#include "golf_ball.h"


// The primary object for communications to the Golf Sim user interface

namespace golf_sim {

    class GsUISystem {

    public:

        static std::string kWebServerShareDirectory;
        static std::string kWebServerResultBallExposureCandidates;
        static std::string kWebServerResultSpinBall1Image;
        static std::string kWebServerResultSpinBall2Image;
        static std::string kWebServerResultBallRotatedByBestAngles;
        static std::string kWebServerErrorExposuresImage;
        static std::string kWebServerBallSearchAreaImage;
        

        static void SendIPCErrorStatusMessage(const std::string& error_message);

        static bool SendIPCStatusMessage(const GsIPCResultType message_type, const std::string& custom_message = "");

        static void SendIPCHitMessage(const GolfBall& result_ball, const std::string& secondary_message = "");

        // Save the image into the shared web-server directory so that the web-based 
        // golf-sim user interface can access it.  
        // Also save a uniquely-named copy to the usual images directory unless suppressed.

        static bool SaveWebserverImage(const std::string& file_name, const cv::Mat& img, bool suppress_diagnostic_saving = false);
        static bool SaveWebserverImage(const std::string& file_name, const cv::Mat& img, const std::vector<GolfBall>& balls, bool suppress_diagnostic_saving = false);

        static void ClearWebserverImages();
    };

}


#endif // #ifdef __unix__  // Ignore in Windows environment

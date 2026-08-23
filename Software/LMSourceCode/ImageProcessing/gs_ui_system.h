/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Main class for communicating with the system's web-based GUI.

#pragma once

#ifdef __unix__  // Ignore in Windows environment


#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "logging_tools.h"
#include "golf_ball.h"
#include "gs_result_types.h"


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

        // Per-shot image bookkeeping.  shot_id is an epoch-ms id minted once per shot
        // (lazily, when the first image of the shot is saved) and reset when the shot's
        // result is flushed to the web server.  Image paths are relative to the share dir.
        static long current_shot_id_;
        static std::vector<std::string> current_shot_image_paths_;
        static std::mutex shot_images_mutex_;

        // Returns "shots/<shot_id>/<file_name>", minting the shot id if not already set.
        static std::string CurrentShotRelativePath(const std::string& file_name);

        // Clears the current shot id and accumulated image paths so the next shot starts
        // fresh.  Used when a shot is abandoned (timeout/trigger failure) without a result POST.
        static void ResetCurrentShot();


        static void SendIPCErrorStatusMessage(const std::string& error_message);

        static bool SendIPCStatusMessage(const GsIPCResultType message_type, const std::string& custom_message = "");

        static void SendIPCHitMessage(const GolfBall& result_ball, const std::string& secondary_message = "");

        // Whether the web server's sims are ready for a shot, as of its last reply.
        static bool SimArmed();

        // Save the image into the shared web-server directory so that the web-based 
        // golf-sim user interface can access it.  
        // Also save a uniquely-named copy to the usual images directory unless suppressed.

        static bool SaveWebserverImage(const std::string& file_name, const cv::Mat& img, bool suppress_diagnostic_saving = false);
        static bool SaveWebserverImage(const std::string& file_name, const cv::Mat& img, const std::vector<GolfBall>& balls, bool suppress_diagnostic_saving = false);

        static void ClearWebserverImages();

    private:
        // Posts to the web server and applies the armed state and club in its reply.
        static void PostResult(const std::string& json);

        static std::atomic<bool> sim_armed_;
    };

}


#endif // #ifdef __unix__  // Ignore in Windows environment

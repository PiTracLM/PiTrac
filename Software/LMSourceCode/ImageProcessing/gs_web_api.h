/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <string>
#include <vector>

namespace golf_sim {

class WebApi {
public:
    // Send calibration update to web server
    // Returns true if successful, false otherwise
    static bool UpdateCalibration(const std::string& key, double value);
    static bool UpdateCalibration(const std::string& key, const std::vector<double>& values);
    
    // Check if web server is available
    static bool IsWebServerAvailable();
    
private:
    // Get web server URL from environment or use default
    static std::string GetWebServerUrl();
    
    // Execute curl command and return response
    static bool ExecuteCurl(const std::string& url, const std::string& method, 
                           const std::string& payload, std::string& response);
    
    // Format value as JSON
    static std::string FormatAsJson(double value);
    static std::string FormatAsJson(const std::vector<double>& values);
    
    // Default web server URL
    static constexpr const char* kDefaultWebServerUrl = "http://localhost:8080";
};

} // namespace golf_sim
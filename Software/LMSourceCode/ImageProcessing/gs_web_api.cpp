/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "gs_web_api.h"
#include "logging_tools.h"
#include <cstdlib>
#include <sstream>
#include <iomanip>

namespace golf_sim {

bool WebApi::UpdateCalibration(const std::string& key, double value) {
    std::string url = GetWebServerUrl() + "/api/config/" + key;
    std::string payload = "{\"value\": " + FormatAsJson(value) + "}";
    std::string response;
    
    bool success = ExecuteCurl(url, "PUT", payload, response);
    
    if (success) {
        GS_LOG_MSG(info, "Successfully updated calibration: " + key + " = " + std::to_string(value));
    } else {
        GS_LOG_MSG(warn, "Failed to update calibration via web API: " + key + 
                   ". Web server may not be running. Calibration saved locally to golf_sim_config.json");
    }
    
    return success;
}

bool WebApi::UpdateCalibration(const std::string& key, const std::vector<double>& values) {
    std::string url = GetWebServerUrl() + "/api/config/" + key;
    std::string payload = "{\"value\": " + FormatAsJson(values) + "}";
    std::string response;
    
    bool success = ExecuteCurl(url, "PUT", payload, response);
    
    if (success) {
        GS_LOG_MSG(info, "Successfully updated calibration array: " + key);
    } else {
        GS_LOG_MSG(warn, "Failed to update calibration via web API: " + key + 
                   ". Web server may not be running. Calibration saved locally to golf_sim_config.json");
    }
    
    return success;
}

bool WebApi::IsWebServerAvailable() {
    std::string url = GetWebServerUrl() + "/health";
    std::string response;
    
    return ExecuteCurl(url, "GET", "", response);
}

std::string WebApi::GetWebServerUrl() {
    const char* env_url = std::getenv("PITRAC_WEB_SERVER_URL");
    if (env_url != nullptr) {
        return std::string(env_url);
    }
    return kDefaultWebServerUrl;
}

bool WebApi::ExecuteCurl(const std::string& url, const std::string& method, 
                         const std::string& payload, std::string& response) {
    std::stringstream cmd;
    
    // Build curl command with timeout and silent mode
    cmd << "curl -s -m 2 -X " << method;
    
    if (!payload.empty()) {
        cmd << " -H 'Content-Type: application/json'";
        cmd << " -d '" << payload << "'";
    }
    
    cmd << " '" << url << "' 2>/dev/null";
    
    // Execute curl command
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return false;
    }
    
    // Read response
    char buffer[128];
    response.clear();
    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr) {
            response += buffer;
        }
    }
    
    int exit_code = pclose(pipe);
    
    // Check if curl succeeded (exit code 0)
    return exit_code == 0 && !response.empty();
}

std::string WebApi::FormatAsJson(double value) {
    std::stringstream ss;
    ss << std::setprecision(10) << value;
    return ss.str();
}

std::string WebApi::FormatAsJson(const std::vector<double>& values) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << std::setprecision(10) << values[i];
    }
    ss << "]";
    return ss.str();
}

} // namespace golf_sim
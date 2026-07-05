/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#ifdef __unix__

#include <string>
#include <vector>

namespace golf_sim {

// HTTP client for the Python web server. Shot result posts are fire-and-forget:
// failures are logged but don't block the shot cycle.
class GsHttpClient {
public:
    static void Init(const std::string& host = "localhost", int port = 8080);
    static std::string FetchConfig();  // GET /api/internal/config; returns body or "" on failure
    static void PostResult(const std::string& json_body);
    static void PostImageReady(const std::string& filename);
    static bool PutJson(const std::string& path, const std::string& json_body);
    static bool UpdateCalibration(const std::string& key, double value);
    static bool UpdateCalibration(const std::string& key, const std::vector<double>& values);

private:
    static bool PutConfigValue(const std::string& key, const std::string& json_value);
    static std::string FormatAsJson(double value);
    static std::string FormatAsJson(const std::vector<double>& values);

    static std::string host_;
    static int port_;
};

} // namespace golf_sim

#endif

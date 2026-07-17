/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#ifdef __unix__

#include <iomanip>
#include <sstream>

#include "gs_http_client.h"
#include "logging_tools.h"
#include "httplib.h"

namespace golf_sim {

std::string GsHttpClient::host_ = "localhost";
int GsHttpClient::port_ = 8080;

void GsHttpClient::Init(const std::string& host, int port) {
    host_ = host;
    port_ = port;
}

std::string GsHttpClient::FetchConfig() {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(2);
        cli.set_read_timeout(5);

        auto res = cli.Get("/api/internal/config");

        if (!res || res->status != 200) {
            GS_LOG_MSG(error, "Config fetch failed (status " +
                std::to_string(res ? res->status : 0) + ")");
            return "";
        }
        return res->body;
    } catch (const std::exception& e) {
        GS_LOG_MSG(error, "Config fetch exception: " + std::string(e.what()));
        return "";
    }
}

void GsHttpClient::PostResult(const std::string& json_body) {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(1);
        cli.set_read_timeout(1);

        auto res = cli.Post("/api/internal/shot-result", json_body, "application/json");

        if (!res) {
            GS_LOG_MSG(warning, "HTTP POST to web server failed (no response)");
        } else if (res->status != 200) {
            GS_LOG_MSG(warning, "HTTP POST returned status " + std::to_string(res->status));
        }
    } catch (const std::exception& e) {
        GS_LOG_MSG(warning, "HTTP POST exception: " + std::string(e.what()));
    }
}

void GsHttpClient::PostImageReady(const std::string& filename) {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(1);
        cli.set_read_timeout(1);

        std::string json = "{\"filename\":\"" + filename + "\"}";
        auto res = cli.Post("/api/internal/image-ready", json, "application/json");

        if (!res) {
            GS_LOG_MSG(warning, "HTTP POST image-ready failed (no response)");
        }
    } catch (const std::exception& e) {
        GS_LOG_MSG(warning, "HTTP POST image-ready exception: " + std::string(e.what()));
    }
}

bool GsHttpClient::PutJson(const std::string& path, const std::string& json_body) {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(2);
        cli.set_read_timeout(5);

        auto res = cli.Put(path, json_body, "application/json");
        if (!res) {
            GS_LOG_MSG(error, "HTTP PUT " + path + " failed (no response)");
            return false;
        }
        if (res->status != 200) {
            GS_LOG_MSG(error, "HTTP PUT " + path + " returned " + std::to_string(res->status) + ": " + res->body);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        GS_LOG_MSG(warning, "HTTP PUT exception: " + std::string(e.what()));
        return false;
    }
}

bool GsHttpClient::PutConfigValue(const std::string& key, const std::string& json_value) {
    if (!PutJson("/api/config/" + key, "{\"value\": " + json_value + "}")) {
        GS_LOG_MSG(error, "Failed to save calibration value " + key + " to the web server.");
        return false;
    }
    GS_LOG_MSG(info, "Saved calibration: " + key + " = " + json_value);
    return true;
}

bool GsHttpClient::UpdateCalibration(const std::string& key, double value) {
    return PutConfigValue(key, FormatAsJson(value));
}

bool GsHttpClient::UpdateCalibration(const std::string& key, const std::vector<double>& values) {
    return PutConfigValue(key, FormatAsJson(values));
}

std::string GsHttpClient::FormatAsJson(double value) {
    std::stringstream ss;
    ss << std::setprecision(10) << value;
    return ss.str();
}

std::string GsHttpClient::FormatAsJson(const std::vector<double>& values) {
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

#endif

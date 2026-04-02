/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#ifdef __unix__

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

} // namespace golf_sim

#endif

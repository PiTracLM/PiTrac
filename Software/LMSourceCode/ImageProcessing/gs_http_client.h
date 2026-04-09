/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#ifdef __unix__

#include <string>

namespace golf_sim {

// Lightweight HTTP client for posting shot results to the Python web server.
// Fire-and-forget — failures are logged but don't block the shot cycle.
class GsHttpClient {
public:
    static void Init(const std::string& host = "localhost", int port = 8080);
    static void PostResult(const std::string& json_body);
    static void PostImageReady(const std::string& filename);

private:
    static std::string host_;
    static int port_;
};

} // namespace golf_sim

#endif

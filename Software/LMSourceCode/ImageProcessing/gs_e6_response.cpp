/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// "TruGolf Simulators" and other marks such as E6 may be trademarked by TruGolf, Inc.
// The PiTrac project is not endorsed, sponsored by or associated with TrueGolf products or services.


#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "gs_format_lib.h"

#ifdef __unix__  // Ignore in Windows environment
#include <openssl/sha.h>
#endif

#include "logging_tools.h"
#include "gs_config.h"
#include "gs_events.h"
#include "gs_ipc_control_msg.h"

#include "gs_e6_interface.h"
#include "gs_e6_response.h"
#include "gs_e6_results.h"


namespace golf_sim {

    GsE6Response::GsE6Response() {
    }

    GsE6Response::~GsE6Response() {
    }


    bool GsE6Response::ProcessSimCommand(boost::property_tree::ptree& pt,
                                        std::string& e6_response_string) {

	return true;

    }

    bool GsE6Response::ParseJson(const std::string& e6_json_string) {
        GS_LOG_MSG(error, "GsE6Response::ParseJson should not be called.  Call ProcessJson instead.");
        return false;
    }

    bool GsE6Response::ProcessJson(const std::string& e6_json_string,
                                   std::string& e6_response_string) {

        return true;
    }

    std::string GsE6Response::Format() const {
        std::string s;

        return s;
    }

}

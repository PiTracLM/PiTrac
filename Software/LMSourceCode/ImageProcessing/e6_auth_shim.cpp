/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// C entry point that answers the E6 authentication challenge for the web
// server's E6 sim (sims/e6_auth.py).
// The real headers for the symbols stubbed below pull in OpenCV, so they are
// declared here with the same signatures instead.

#include <cstring>
#include <regex>
#include <sstream>
#include <string>

#include <boost/algorithm/string/replace.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "gs_e6_response.h"

namespace golf_sim {

    // Only reached for Arm, Disarm and PlayerDataModified, which the web server
    // handles itself and never passes in.
    class GsSimInterface {
    public:
        enum GolfSimulatorType { kNone = 0, kGSPro = 1, kE6 = 2 };
        static GsSimInterface* GetSimInterfaceByType(GolfSimulatorType sim_type);
    };

    GsSimInterface* GsSimInterface::GetSimInterfaceByType(GolfSimulatorType) {
        return nullptr;
    }

    enum class GsIPCControlMsgType { kUnknown = 0, kClubChangeToPutter = 1, kClubChangeToDriver = 2 };

    class GsIPCControlMsg {
    public:
        static std::string FormatControlMessageType(const GsIPCControlMsgType t);
    };

    std::string GsIPCControlMsg::FormatControlMessageType(const GsIPCControlMsgType) {
        return "";
    }

    class GolfSimEventElement;

    class GolfSimEventQueue {
    public:
        static bool QueueEvent(GolfSimEventElement& event);
    };

    bool GolfSimEventQueue::QueueEvent(GolfSimEventElement&) {
        return false;
    }

    class GsResults {
    public:
        static std::string GenerateStringFromJsonTree(const boost::property_tree::ptree& root);
    };

    // Same as gs_results.cpp
    std::string GsResults::GenerateStringFromJsonTree(const boost::property_tree::ptree& root) {
        std::stringstream ss;
        boost::property_tree::write_json(ss, root);

        std::regex reg("\\\"([+-]?[0-9]+\\.{0,1}[0-9]*)\\\"");
        std::string result = std::regex_replace(ss.str(), reg, "$1");

        boost::replace_all(result, "\"true\"", "true");
        boost::replace_all(result, "\"false\"", "false");
        boost::replace_all(result, "\"APIversion\": 1,", "\"APIversion\": \"1\",");

        return result;
    }
}

namespace {
    // GsE6Response logs every message at trace, which would fill the web server's journal
    const bool kLogFilterSet = [] {
        boost::log::core::get()->set_filter(boost::log::trivial::severity >= boost::log::trivial::warning);
        return true;
    }();
}

// Only challenge messages reach GsE6Response: Arm and Disarm dereference the
// null GetSimInterfaceByType stub, which would kill the web server process.
static bool IsChallenge(const char* json) {
    boost::property_tree::ptree pt;
    std::istringstream ss(json);
    boost::property_tree::read_json(ss, pt);
    const std::string type = pt.get<std::string>("Type", "");
    return (type == "Handshake" || type == "Challenge") && pt.count("Challenge") > 0;
}

// sims/e6_auth.py depends on these codes: reply length, 0 for no reply,
// -1 rejected, -2 reply plus terminator larger than cap.
extern "C" int e6_process(const char* json, char* out, size_t cap) {
    std::string reply;
    try {
        if (!IsChallenge(json)) {
            return -1;
        }
        golf_sim::GsE6Response response;
        if (!response.ProcessJson(json, reply)) {
            return -1;
        }
    }
    catch (...) {
        return -1;
    }
    if (reply.size() + 1 > cap) {
        return -2;
    }
    std::memcpy(out, reply.c_str(), reply.size() + 1);
    return static_cast<int>(reply.size());
}

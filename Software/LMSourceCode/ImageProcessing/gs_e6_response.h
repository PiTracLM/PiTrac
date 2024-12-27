/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// "TruGolf Simulators" and other marks such as E6 may be trademarked by TruGolf, Inc.
// The PiTrac project is not endorsed, sponsored by or associated with TrueGolf products or services.

#pragma once

// Represents a response (message) being received by the system from an
// extern E6 golf simualator.

namespace golf_sim {

    class GsE6Response {

    public:


        enum PlayerHandedness {
            kRightHanded = 0,
            kLeftHanded = 1
        };

        enum PlayerClub {
            kDriver = 0,
            kPutter = 1
        };

    public:
        GsE6Response();
        virtual ~GsE6Response();
        virtual std::string Format() const;

        // If e6_response_string is not empty, it should be sent back to the 
        // E6 system's socket by the caller        
        bool ProcessJson(const std::string& e6_json_string,
            std::string& e6_response_string);

        // NOTE - Should NOT be called.  This isn't used in the E6 system
        bool ParseJson(const std::string& gspro_json_string);

        std::string message_ = "Not Set";
        PlayerHandedness player_handed_ = PlayerHandedness::kRightHanded;
        PlayerClub player_club_ = PlayerClub::kDriver;

    protected:

        // Each 'ProcessXXX' method will return a response string, which
        // if not empty, should be sent back to the E6 system's socket by
        // the caller
        bool ProcessAuthentication(boost::property_tree::ptree& pt, std::string& e6_response_string);
        bool ProcessChallenge(boost::property_tree::ptree& pt, std::string& e6_response_string);
        bool ProcessSimCommand(boost::property_tree::ptree& pt, std::string& e6_response_string);
        bool ProcessPing(boost::property_tree::ptree& pt, std::string& e6_response_string);
        bool ProcessArm(boost::property_tree::ptree& pt, std::string& e6_response_string);
        bool ProcessDisarm(boost::property_tree::ptree& pt, std::string& e6_response_string);
        bool ProcessShotComplete(boost::property_tree::ptree& pt, std::string& e6_response_string);
        bool ProcessAck(boost::property_tree::ptree& pt, std::string& e6_response_string);
        bool ProcessWarning(boost::property_tree::ptree& pt, std::string& e6_response_string);
        bool ProcessError(boost::property_tree::ptree& pt, std::string& e6_response_string);

        std::string GetKey();
        std::string GetID();
        std::string GenerateSHA256String(const std::string& s);

    };

}

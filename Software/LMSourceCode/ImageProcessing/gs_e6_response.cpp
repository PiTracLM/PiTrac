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

#include "obfuscate.h"


namespace golf_sim {

    GsE6Response::GsE6Response() {
    }

    GsE6Response::~GsE6Response() {
    }


    bool GsE6Response::ProcessAuthentication(boost::property_tree::ptree& pt,
                                            std::string& e6_response_string) {

        std::string success = pt.get<std::string>("Success", "");

        if (success != "true") {
            GS_LOG_MSG(warning, "GsE6Response::ProcessAuthentication received non-true success: " + success);
            return false;
        }

        return true;
    }

    std::string GsE6Response::GetKey() {
    	// This is referred to as the Secret Key in the E6 documentation

        // This is the test/developer key - not the official key for PiTrac
        const std::string str(AY_OBFUSCATE("kIvRILMEqHaPPylcAoOWsjKxhTRbxqWURg5iD0Nbilmt7KZ8"));
        // This is the official key for PiTrac
        // TBD const std::string str(AY_OBFUSCATE("2TUSzbAUfKRfcjcMzfoV1qdiixjnzi95HfqR77bieLYCT4aJ"));
        return str;
    }

    std::string GsE6Response::GetID() {
    	// This is referred to as the Developer ID in the E6 documentation
        // This is the test/developer key - not the official key for PiTrac
        const std::string str( AY_OBFUSCATE("3A1D3CBD-9FAB-4328-91E6-C97F7FC29DC2") );
        // This is the official key for PiTrac
        // TBD const std::string str(AY_OBFUSCATE("5D00A3F8-8546-4481-B07F-4237DF0F43B7"));

        return str;
    }

    std::string GsE6Response::GenerateSHA256String(const std::string& s) {

        std::string final_return_hash_string;

#ifdef __unix__  // Ignore in Windows environment

        // Create a SHA256 object.
        SHA256_CTX ctx;
        SHA256_Init(&ctx);

        // Update the SHA256 object with the byte array.
        SHA256_Update(&ctx, s.c_str(), s.length());

        // Compute the hash.
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_Final(hash, &ctx);

        char buffer[20];

        // Print the hash.
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            sprintf(buffer, "%02x", hash[i]);

            auto next_byte_str = std::string(buffer); // std::format("%02X", (int)hash[i]);

            final_return_hash_string += next_byte_str;
        }
#endif
        return final_return_hash_string;
    }

    bool GsE6Response::ProcessChallenge(boost::property_tree::ptree& pt,
                                        std::string& e6_response_string) {

        std::string challenge_from_e6 = pt.get<std::string>("Challenge", "");

        std::string response_challenge;
        std::string hash;

        hash = GenerateSHA256String(challenge_from_e6 + GetKey());

        // Generate the challenge reply

        boost::property_tree::ptree root;

        // Root-level values
        root.put("Type", "Challenge");
        root.put("Developer", GetID());
        root.put("Hash", hash);

        e6_response_string = GsResults::GenerateStringFromJsonTree(root);

        if (e6_response_string == "") {
            GS_LOG_MSG(warning, "E6Results::Format() returning empty string.");
            return false;
        }

        return true;
    }

    bool GsE6Response::ProcessPing(boost::property_tree::ptree& pt,
        std::string& e6_response_string) {
        e6_response_string = "{\"Type\":\"Pong\"}";
        return true;
    }

    bool GsE6Response::ProcessAck(boost::property_tree::ptree& pt,
                                  std::string& e6_response_string) {

        std::string details = pt.get<std::string>("Details", "");

        GS_LOG_TRACE_MSG(trace, "E6Results - received an ACK message. Details were: " + details);

        e6_response_string = "";
        return true;
    }

    bool GsE6Response::ProcessWarning(boost::property_tree::ptree& pt,
                                      std::string& e6_response_string) {

        std::string details = pt.get<std::string>("Details", "");

        GS_LOG_MSG(warning, "E6Results - received a Warning message. Details were: " + details);

        e6_response_string = "";
        return true;
    }

    bool GsE6Response::ProcessError(boost::property_tree::ptree& pt,
                                    std::string& e6_response_string) {

        std::string details = pt.get<std::string>("Details", "");

        GS_LOG_MSG(error, "E6Results - received an Error message. Details were: " + details);

        e6_response_string = "";
        return true;
    }

    bool GsE6Response::ProcessShotComplete(boost::property_tree::ptree& pt,
                                           std::string& e6_response_string) {
        // TBD - Do nothing for now
        e6_response_string = "";
        return true;
    }

    bool GsE6Response::ProcessArm(boost::property_tree::ptree& pt,
                                  std::string& e6_response_string) {

        // No response is necessary, just need to tell the LM E6 interface that the 
        // E6 system is now armed and ready for a shot
	//
#ifdef __unix__  // Ignore in Windows environment

	    GsSimInterface* e6_interface = GsSimInterface::GetSimInterfaceByType(GsSimInterface::GolfSimulatorType::kE6);

	    if (e6_interface == nullptr) {
	    	GS_LOG_MSG(error, "GsE6Response::ProcessArm could not find the E6 interface");
	    }

	    e6_interface->SetSimSystemArmed(true);
#endif
        e6_response_string = "";
        return true;
    }

    bool GsE6Response::ProcessDisarm(boost::property_tree::ptree& pt,
                                     std::string& e6_response_string) {
        // No response is necessary, just need to tell the LM E6 interface that the 
        // E6 system is no longer armed and not ready for a shot message
#ifdef __unix__  // Ignore in Windows environment
        GsSimInterface* e6_interface = GsSimInterface::GetSimInterfaceByType(GsSimInterface::GolfSimulatorType::kE6);

        if (e6_interface == nullptr) {
            GS_LOG_MSG(error, "GsE6Response::ProcessArm could not find the E6 interface");
        }
        
        e6_interface->SetSimSystemArmed(false);
#endif
        e6_response_string = "";
        return true;
    }

    bool GsE6Response::ProcessSimCommand(boost::property_tree::ptree& pt,
                                        std::string& e6_response_string) {

        std::string subtype = pt.get<std::string>("SubType", "");

        if (subtype == "Ping") {
            return ProcessPing(pt, e6_response_string);
        }
        else if (subtype == "Arm") {
            return ProcessArm(pt, e6_response_string);
        }
        else if (subtype == "Disarm") {
            return ProcessDisarm(pt, e6_response_string);
        }
        else if (subtype == "EnvironmentDataModified") {
            // TBD - Not doing anything yet
        }
        else if (subtype == "PlayerDataModified") {

            std::string handedness_str;
            std::string club_str;

            boost::optional< boost::property_tree::ptree& > child = pt.get_child_optional("Details");
            if (!child) {
                GS_LOG_MSG(warning, "E6Response::ProcessSimCommand - No player information was provided.");
            }
            else {
                handedness_str = pt.get<std::string>("Details.Handedness", "");
                club_str = pt.get<std::string>("Details.ClubType", "");
            }

            GS_LOG_MSG(info, "E6Response::ProcessSimCommand - Club = " + club_str + ", Handedness = " + handedness_str);

#ifdef __unix__  // Ignore in Windows environment

            if (club_str != "") {
                GsIPCControlMsgType club_instruction;

                if (club_str == "Putter") {
                    club_instruction = GsIPCControlMsgType::kClubChangeToPutter;
                }
                else {
                    club_instruction = GsIPCControlMsgType::kClubChangeToDriver;
                }

                // Send the instruction to switch clubs to the main FSM
                GolfSimEventElement control_message{ new GolfSimEvent::ControlMessage{ club_instruction } };
                GolfSimEventQueue::QueueEvent(control_message);
            }
#endif

            e6_response_string = "";
            return true;
        }
        else {
            GS_LOG_MSG(warning, "GsE6Response::ParseJson - received unknown 'SubType' tag: " + subtype);
            return false;
        }

        return true;
    }

    bool GsE6Response::ParseJson(const std::string& e6_json_string) {
        GS_LOG_MSG(error, "GsE6Response::ParseJson should not be called.  Call ProcessJson instead.");
        return false;
    }

    bool GsE6Response::ProcessJson(const std::string& e6_json_string,
                                   std::string& e6_response_string) {

        int return_code = 0;
        std::string message_str;
        std::string handedness_str;
        std::string club_str;

        std::stringstream ss;
        ss << e6_json_string;

        bool return_status = false;

        try {
            boost::property_tree::ptree pt;
            boost::property_tree::read_json(ss, pt);

            GS_LOG_TRACE_MSG(trace, "GsE6Response::ProcessJson message.");

            message_str = pt.get<std::string>("Type", "");

            if (message_str == "") {
                GS_LOG_MSG(warning, "GsE6Response::ParseJson - Did not find 'Type' tag.");
                return false;
            }

            if (message_str == "Handshake") {
                GS_LOG_TRACE_MSG(trace, "GsE6Response::ParseJson - received Handshake");
                // Should be able to do nothing - a challenge message should be following.
                // That's what the documentation says.  But IRL, the handshake appears to
                // hold the challenge information, so process it that way.
                ProcessChallenge(pt, e6_response_string);
            }
            else if (message_str == "Challenge") {
                GS_LOG_TRACE_MSG(trace, "GsE6Response::ParseJson - received Challenge");
                ProcessChallenge(pt, e6_response_string);
            }
            else if (message_str == "Authentication") {
                GS_LOG_TRACE_MSG(trace, "GsE6Response::ParseJson - received Authentication");
                ProcessAuthentication(pt, e6_response_string);
            }
            else if (message_str == "SimCommand") {
                GS_LOG_TRACE_MSG(trace, "GsE6Response::ParseJson - received SimCommand");
                ProcessSimCommand(pt, e6_response_string);
            }
            else if (message_str == "ACK") {
                GS_LOG_TRACE_MSG(trace, "GsE6Response::ParseJson - received ACK");
                ProcessAck(pt, e6_response_string);
            }
            else if (message_str == "Warning") {
                GS_LOG_TRACE_MSG(trace, "GsE6Response::ParseJson - received Warning");
                ProcessWarning(pt, e6_response_string);
            }
            else if (message_str == "ShotError") {
                GS_LOG_TRACE_MSG(trace, "GsE6Response::ParseJson - received Error");
                ProcessError(pt, e6_response_string);
            }
            else if (message_str == "ShotComplete") {
                GS_LOG_TRACE_MSG(trace, "GsE6Response::ParseJson - received ShotComplete");
                ProcessShotComplete(pt, e6_response_string);
            }
            else {
                GS_LOG_MSG(warning, "GsE6Response::ParseJson - received unknown 'Type' tag: " + message_str);
                return false;
            }
        }
        catch (std::exception const& e)
        {
            // For now, return true even if we failed - TBD - need to figure out what garbage at end means
            // from the boost library
            GS_LOG_MSG(error, "GsE6Response::ParseJson failed to parse E6 response: " + std::string(e.what()));
            return true;
        }

        GS_LOG_TRACE_MSG(trace, "GsE6Response::ProcessJson completed.");


        return true;
    }

    std::string GsE6Response::Format() const {
        std::string s;

        std::string handed_str = (player_handed_ == PlayerHandedness::kLeftHanded) ? "LH" : "RH";
        std::string club_str = (player_club_ == PlayerClub::kDriver) ? "Driver" : "Putter";

        // TBD - REMOVE - s += "Return Code: " + std::to_string(return_code_) + ".";
        s += " Message: " + message_ + "\n";
        s += " Player.Handed: " + handed_str;
        s += " Player.Club: " + club_str;

        return s;
    }

}

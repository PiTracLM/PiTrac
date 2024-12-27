/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once


// Class for representing, parsing and transferring Golf Sim responses from GsPro

namespace golf_sim {

    class GsGSProResponse {

    public:
        enum ReturnCode {
            kShotReceivedSuccessfully = 200,
            kPlayerInformation = 201,
            k501Failure = 501,
            kShotOtherFailure = 599
        };

        enum PlayerHandedness {
            kRightHanded = 0,
            kLeftHanded = 1
        };

        enum PlayerClub {
            kDriver = 0,
            kPutter = 1
        };

    public:
        GsGSProResponse();
        bool ParseJson(const std::string& gspro_json_string);
        virtual ~GsGSProResponse();
        virtual std::string Format() const;

        ReturnCode return_code_ = ReturnCode::kShotReceivedSuccessfully;
        std::string message_ = "Not Set";
        PlayerHandedness player_handed_ = PlayerHandedness::kRightHanded;
        PlayerClub player_club_ = PlayerClub::kDriver;

    };

}

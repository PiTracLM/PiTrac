/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "logging_tools.h"
#include "gs_config.h"
#include "gs_result_types.h"
#include "gs_http_client.h"
#include "gs_result_types.h"
#include "gs_clubs.h"


namespace golf_sim {

	GolfSimClubs::GsClubType GolfSimClubs::current_club_ = GolfSimClubs::kNotSelected;

	GolfSimClubs::GsClubType GolfSimClubs::GetCurrentClubType() {

		return current_club_;
	}

	void GolfSimClubs::SetCurrentClubType(GsClubType club_type) {
		current_club_ = club_type;

		GS_LOG_MSG(info, "Club type set to " + std::string((club_type == GolfSimClubs::GsClubType::kPutter) ? "Putter" : "Driver"));

		// Notify the GUI, and possibly any attached Golf Sims about the change
		// TBD - We need a new type of message.
		// For now, just send a zero-results message with the
		// new driver setting.

#ifdef __unix__
		std::string club_name = (club_type == GolfSimClubs::GsClubType::kPutter) ? "Putter" : "Driver";
		std::string json = "{\"result_type\":" + std::to_string(static_cast<int>(GsIPCResultType::kHit))
			+ ",\"message\":\"Club type was set to " + club_name + "\""
			+ ",\"speed_mps\":0,\"launch_angle\":0,\"side_angle\":0"
			+ ",\"back_spin\":0,\"side_spin\":0,\"carry\":0,\"images\":[]}";
		GsHttpClient::PostResult(json);
#endif
	}


}

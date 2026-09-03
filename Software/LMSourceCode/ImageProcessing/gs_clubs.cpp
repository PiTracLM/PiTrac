/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "logging_tools.h"
#include "gs_config.h"
#include "gs_result_types.h"
#include "gs_ui_system.h"
#include "gs_clubs.h"


namespace golf_sim {

	GolfSimClubs::GsClubType GolfSimClubs::current_club_ = GolfSimClubs::kNotSelected;

	GolfSimClubs::GsClubType GolfSimClubs::GetCurrentClubType() {

		return current_club_;
	}

	void GolfSimClubs::SetCurrentClubType(GsClubType club_type) {
		current_club_ = club_type;

		std::string club_name = (club_type == GolfSimClubs::GsClubType::kPutter) ? "Putter" : "Driver";
		GS_LOG_MSG(info, "Club type set to " + club_name);

#ifdef __unix__
		GsUISystem::SendIPCStatusMessage(GsIPCResultType::kControlMessage, "Club type was set to " + club_name);
#endif
	}


}

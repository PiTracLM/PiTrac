/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "logging_tools.h"
#include "gs_config.h"
#include "gs_ipc_result.h"
#include "gs_ipc_system.h"
#include "gs_ipc_message.h"
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

#ifdef __unix__  // Ignore in Windows environment
		GolfSimIPCMessage ipc_message(GolfSimIPCMessage::IPCMessageType::kResults);
		GsIPCResult& ipc_results = ipc_message.GetResultsForModification();

		// Really should be kControlMessage, but the GUI is not processing that yet
		ipc_results.result_type_ = GsIPCResultType::kHit;  
		ipc_results.club_type_ = club_type;
		ipc_results.message_ = "Club type was set";

		GS_LOG_TRACE_MSG(trace, "Sending Club Change to LM GUI.");
		if (!GolfSimIpcSystem::SendIpcMessage(ipc_message)) {
			GS_LOG_MSG(warning, "Failed to SendResultsToGolfSim to the LM GUI.");
		}
#endif
	}


}

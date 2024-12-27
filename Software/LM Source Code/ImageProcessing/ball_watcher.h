/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#ifdef __unix__  // Ignore in Windows environment

#include "core/rpicam_encoder.hpp"
#include "encoder/encoder.hpp"

namespace golf_sim {

	// The main event loop 
	// Returns true if function ran as expected, and without error
	// motion_detected will be set true only if motion was successfully detected.
	bool ball_watcher_event_loop(RPiCamEncoder &app, bool& motion_detected);

}

#endif // #ifdef __unix__  // Ignore in Windows environment

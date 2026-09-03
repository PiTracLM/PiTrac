/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

// TThis is basically just a state flag for now.  It's used
// to determine whether the system is putting or not.

namespace golf_sim {

	class GolfSimClubs {

	public:

		enum GsClubType	{
			kNotSelected = 0,
			kDriver = 1,
			kIron = 2,
			kPutter = 3
		};

		static GsClubType current_club_;

		static GsClubType GetCurrentClubType();
		static void SetCurrentClubType(GsClubType club_type);

	};

}

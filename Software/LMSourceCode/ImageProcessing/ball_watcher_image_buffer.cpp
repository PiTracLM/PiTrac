/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */



#ifdef __unix__  // Ignore in Windows environment


#include "ball_watcher_image_buffer.h"

	// Global queue to hold the last <n> frames before motion is detected in the frame
	// WARNING - NOT THREAD SAFE ON ITS OWN
	boost::circular_buffer<golf_sim::RecentFrameInfo> golf_sim::RecentFrames(10);

#endif // #ifdef __unix__  // Ignore in Windows environment


/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Hides whatever format library we're using

#ifdef __unix__  // Ignore in Windows environment
#include <fmt/core.h>
#define GS_FORMATLIB_FORMAT fmt::format
#else
#include <format>
#define GS_FORMATLIB_FORMAT std::format
#endif // __unix__

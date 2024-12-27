/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include "gs_results.h"


// Class for representing and transferring Golf Sim results to GSPro

namespace golf_sim {

    class GsGSProResults : public GsResults {

    public:
        GsGSProResults();
        GsGSProResults(const GolfBall& ball);
        GsGSProResults(const GsResults& results);
        virtual ~GsGSProResults();
        virtual std::string Format() const;

    };

}

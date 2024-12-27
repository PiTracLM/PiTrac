/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include "gs_results.h"

// "TruGolf Simulators" and other marks such as E6 may be trademarked by TruGolf, Inc.
// The PiTrac project is not endorsed, sponsored by or associated with TrueGolf products or services.



// Base class for representing and transferring Golf Sim results

namespace golf_sim {

    class GsE6Results : public GsResults {

    public:
        GsE6Results();
        GsE6Results(const GolfBall& ball);
        GsE6Results(const GsResults& results);
        virtual ~GsE6Results();
        // Will enforce range limits
        virtual std::string Format() const;

    };

}

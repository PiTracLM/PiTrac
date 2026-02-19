/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Representation of one or more parameters of a golf shot, such as speed, launch angle, spin, etc.
// Can be useful for things like specifying a particular aspect of a shot result to use (or ignore) when averaging ball flight results.


#pragma once

#include <string>


// Base class for representing and transferring Golf Sim results

namespace golf_sim {

    class GsShotParameters {

    public:

        // We'll implement the parameters as an 8- bit field
        // kShotParameterAll is treated separately, but should
        // behave like all the relevant bits are set.
        enum ShotParameter {
            kShotParameterNone,
            kBallVelocity,
            kHLA,
            kVLA,
            kShotParameterAll
        };  

        GsShotParameters();
        GsShotParameters(const ShotParameter& parameter);
        ~GsShotParameters();
        virtual std::string Format() const;

        // Sets the specified parameter as present (value==true) or not present. 
		// Any other parameters are not affected.  Setting kShotParameterAll to true 
        // or false will set all parameters to present or not present, respectively.
        void SetParameter(const ShotParameter& parameter, bool value);

        // Returns true if the parameter is present in this set of shot parameters.
        bool ParameterIsPresent(const ShotParameter& parameter) const;


    protected:
        void SetInternalParameter(const ShotParameter& parameter, bool is_present);

        // Had originally used a bit-field for this, but it was actually less clear 
		// than just having separate boolean members for each parameter
        bool velocity_is_set_ = false;
        bool HLA_is_set_ = false;
        bool VLA_is_set_ = false;
    };

}

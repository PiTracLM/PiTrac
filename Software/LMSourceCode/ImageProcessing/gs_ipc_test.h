/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#ifdef __unix__  // Ignore in Windows environment


#include <msgpack.hpp>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/dnn/all_layers.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>

#include "logging_tools.h"


// The primary object for communications to the Golf Sim user interface

namespace golf_sim {

    enum class GsIPCTestType { 
        kUnknownTest = 0, 
        kBallLocation = 1 };

    // This class is designed to compartmentalize the details of (De)serializing
    // cv::Mat objects.
    class GsIPCTest {

    public:

        GsIPCTest();
        virtual ~GsIPCTest();

        // Returns a string representation of this result
        std::string Format() const;

        std::string FormatResultType(const GsIPCTestType t) const;

    public:
        GsIPCTestType test_type_ = GsIPCTestType::kUnknownTest;
        double ball_distance_x_cm_ = 0;
        double ball_distance_y_cm_ = 0;
        double ball_distance_z_cm_ = 0;

        MSGPACK_DEFINE(test_type_,
                       ball_distance_x_cm_,
                       ball_distance_y_cm_,
                       ball_distance_z_cm_
                        );

    };

}
// This needs to be placed outside the namespace
MSGPACK_ADD_ENUM(golf_sim::GsIPCTestType);


#endif // #ifdef __unix__  // Ignore in Windows environment

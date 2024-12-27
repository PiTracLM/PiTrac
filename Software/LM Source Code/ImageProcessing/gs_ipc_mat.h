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




namespace golf_sim {

    // This class is designed to compartmentalize the details of (De)serializing
    // cv::Mat objects.
    class GsIPCMat {

        struct GsIPCMatHolder {
            std::vector<uchar> matrix;
            int rows = 0;
            int cols = 0;
            int type = 0;
            MSGPACK_DEFINE(matrix, rows, cols, type);
        };

    public:
        GsIPCMat();
        virtual ~GsIPCMat();

        void SetAndPackMat(cv::Mat& mat);

        const msgpack::sbuffer& GetSerializedMat() const;

        // Retrieves the image from the internal msgpack buffer
        cv::Mat GetImageMat() const;

        // Takes the external data pointer (which must have been serialized by this 
        // class) and unpacks that data into the internal serialized serialized_image_.
        // The resulting cv::Mat can then be retrieved by calling GetImageMat();
        // Useful when a serialized GsIPCMat has been received from, e.g., an
        // ActiveMQ message consumer.
        // Returns true if successful, false otherwise.
        bool UnpackMatData(char* data, size_t length);

    private:
        GsIPCMatHolder mat_holder_;

        // Will hold the serialized mat
        msgpack::sbuffer serialized_image_;
    };

}

#endif // #ifdef __unix__  // Ignore in Windows environment

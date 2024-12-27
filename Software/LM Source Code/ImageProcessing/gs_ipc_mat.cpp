/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#ifdef __unix__  // Ignore in Windows environment

#include "logging_tools.h"

#include "gs_ipc_mat.h"

namespace golf_sim {

    GsIPCMat::GsIPCMat() {
    }

    GsIPCMat::~GsIPCMat() {
    }

    void GsIPCMat::SetAndPackMat(cv::Mat& mat) {

        mat_holder_.matrix = std::vector<uchar>(mat.data, mat.data + (mat.rows * mat.cols * mat.channels()));
        mat_holder_.rows = mat.rows;
        mat_holder_.cols = mat.cols;
        mat_holder_.type = mat.type();

        GS_LOG_TRACE_MSG(trace, "GsIPCMat::SetAndPackMat called with row/cols/type = " + std::to_string(mat_holder_.rows) + "/" + std::to_string(mat_holder_.cols) + "/" + std::to_string(mat_holder_.type) + ".");

        /* pack/serialize the data using msgpack */
        msgpack::pack(&serialized_image_, mat_holder_);
    }

    const msgpack::sbuffer& GsIPCMat::GetSerializedMat() const {
        return serialized_image_;
    }

    cv::Mat GsIPCMat::GetImageMat() const {
        cv::Mat emptyMat;

        if (serialized_image_.size() == 0 || serialized_image_.data() == nullptr) {
            GS_LOG_TRACE_MSG(trace, "GsIPCMat::GetImageMat called, but no serialized_image data exists!");
            return emptyMat;
        }
        msgpack::unpacked unpacked_mat_data;
        msgpack::unpack(unpacked_mat_data,
            static_cast<const char*>(serialized_image_.data()),
            serialized_image_.size());

        auto unpacked_mat = unpacked_mat_data.get().as<GsIPCMatHolder>();

        /* return back the vector<uchar> to cv::Mat image and show it  */
        cv::Mat mat(unpacked_mat.rows, unpacked_mat.cols,
            unpacked_mat.type, unpacked_mat.matrix.data() );

        // Have to clone the Mat, as it is going away when the function exits
        return mat.clone();
    }


    bool GsIPCMat::UnpackMatData(char* data, size_t length) {
        if (data == nullptr || length == 0) {
            return false;
        }

        GS_LOG_TRACE_MSG(trace, "GsIPCMat::UnpackMatData - (re)writing serialized_image_");
        serialized_image_.write(data, length);

        return true;
    }

}

#endif // #ifdef __unix__  // Ignore in Windows environment

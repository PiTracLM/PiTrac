/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#ifdef __unix__  // Ignore in Windows environment

#include "logging_tools.h"

#include "gs_ipc_message.h"


namespace golf_sim {

    GolfSimIPCMessage::GolfSimIPCMessage(IPCMessageType message_type) {
        message_type_ = message_type;
    }

    GolfSimIPCMessage::~GolfSimIPCMessage() {

    }

    std::string GolfSimIPCMessage::Format() {
        std::string s = "GolfSimIPCMessage::Format() - message_type = " + std::to_string(message_type_) + ". Size = " + std::to_string(getSize()) + ".";

        return s;
    }

    void GolfSimIPCMessage::SetMessageType(IPCMessageType& message_type) {
        message_type_ = message_type;
    }

    GolfSimIPCMessage::IPCMessageType GolfSimIPCMessage::GetMessageType() const {
        return message_type_;
    }

    void GolfSimIPCMessage::SetImageMat(cv::Mat& mat) {
        ipc_mat_.SetAndPackMat(mat);
    }

    cv::Mat GolfSimIPCMessage::GetImageMat() const {
        return ipc_mat_.GetImageMat();
    }

    unsigned char* GolfSimIPCMessage::GetImageMatBytePointer(size_t& image_mat_byte_length) const {
        image_mat_byte_length = ipc_mat_.GetSerializedMat().size();
        return (unsigned char *)ipc_mat_.GetSerializedMat().data();
    }

    bool GolfSimIPCMessage::UnpackMatData(char* data, size_t length) {
        if (data == nullptr || length == 0) {
            return false;
        }

        return ipc_mat_.UnpackMatData(data,length);
    }


}

#endif // #ifdef __unix__  // Ignore in Windows environment

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Main base class for any inter-process messages used by the system.

#pragma once

#ifdef __unix__  // Ignore in Windows environment

#include <opencv2/core.hpp>
#include <vector>
#include <string>
#include <cstdint>

#include "logging_tools.h"

#include "gs_ipc_mat.h"
#include "gs_ipc_result.h"
#include "gs_ipc_control_msg.h"

using namespace std;

namespace golf_sim {

    // A class that embodies various types of IPC messages that the golf sim uses.
    // Some of the elements of the class may or may not be used or applicable
    // depending on the IPCMessageType.  For example, for kRequestForCamera2Image messages,
    // the contained Mat object (and related accessors) is not used.
    class GolfSimIPCMessage {

    public:

        enum IPCMessageType {
            kUnknown = 0,
            kRequestForCamera2Image = 1, // Sent by the Pi 1 system to signal the Pi 2 system that Pi 1 is going to expect a picture
            kCamera2Image = 2,    // Sent by the Pi 2 system when it takes a picture
            kRequestForCamera2TestStillImage = 3,
            kResults = 4,   // The result of the current system's operation, such as a ball hit
            kShutdown = 5,  // Tells the system to shutdown and exit
            kCamera2ReturnPreImage = 6,  // Picture of the 'hit' area before the ball is actually hit
            kControlMessage = 7    // These are messages coming to the LM from outside
        };


        GolfSimIPCMessage(IPCMessageType message_type = IPCMessageType::kUnknown);
        virtual ~GolfSimIPCMessage();

        // Returns a human-readable description of the message
        virtual std::string Format();

        void SetMessageType(IPCMessageType &message_type);
        IPCMessageType GetMessageType() const;

        // A serialized copy of the Mat will be made and stored in the message
        // See setters/getters below
        void SetImageMat(cv::Mat& mat);

        // A mat object will be (re)constructed from a serialized version stored in the message
        cv::Mat GetImageMat() const;

        // Returns a pointer to the serialized mat object, and returns the
        // length via image_mat_byte_length
        unsigned char * GetImageMatBytePointer(size_t & image_mat_byte_length) const;

        // Takes the data and unpacks it into the cv::Mat for this object.
        bool UnpackMatData(char* data, size_t length);

        const GsIPCResult& GetResults() const { return ipc_result_; };
        GsIPCResult& GetResultsForModification() { return ipc_result_; };

        const GsIPCControlMsg& GetControlMessage() const { return ipc_control_message_; };
        GsIPCControlMsg& GetControlMessageForModification() { return ipc_control_message_; };

    private:
        IPCMessageType message_type_ = IPCMessageType::kUnknown;

        GsIPCMat ipc_mat_;
        GsIPCResult ipc_result_;
        GsIPCControlMsg ipc_control_message_;
    };

}

#endif // #ifdef __unix__  // Ignore in Windows environment

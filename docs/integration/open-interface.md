---
title: Open Interface Specification
layout: default
nav_order: 2
parent: Integration
---

# PiTrac's Open Interface (POI)

{: .warning }
**TBD - THIS DOCUMENT IS UNDER CONSTRUCTION**

This document describes the details of the interface PiTrac provides to allow other systems to receive shot data from PiTrac and to accept certain control instructions from outside (such as golf club selection.  The interface is also used to support control and synchronization messages between the two Pi-based computers that comprise PiTrac.  The web-based PiTrac GUI is an example of a client for the POI messages.  The GUI consumes shot data messages and displays them to the user.

The inter-PiTrac messages may be expected to continue to change, at least at this early point in the system’s development.  However, it is hoped that the messages that are sent outside of PiTrac, such as shot data, will remain fairly constant.

POI is built on the Apache [ActiveMQ](https://activemq.apache.org/) open source multi-protocol messaging platform.  Byte-by-byte encoding is based on the [MsgPack](https://msgpack.org/index.html) standard to eliminate typical encoding issues such as big-endian/little-endian, etc.  MsgPack is supported in over a dozen programming languages and across most popular operating systems.  As such, almost anything should be able to communicate with PiTrac with a minimum of code.

ActiveMQ essentially provides a payload (along with a payload type) and the required message-brokering, routing, subscriptions, tracking, etc. for that payload.  The payload is then formatted in a platform-independent manner by the MsgPack standard. 

Although this document presents various constants and formats for the POI, the most authoritative source of this information is to be found in the C++ code of PiTrac, including the following files, and especially within the macros called MSGPACK\_DEFINE:

* gs\_ipc\_message.\*  
* gs\_ipc\_result.\*  
* gs\_ip\_control\_msg.\*  
* gs\_clubs.\*

The above IPC (inter-process communication) classes provide both C++ representations of the IPC messages, as well as methods to help serialize and de-serialize the MsgPack-formatted payloads of the Active MQ messages into these classes.

When this document refers to types such as String or Float, those are to be understood as MsgPack types.

**POI Outputs:**

Each ActiveMQ IPC message sent to/from PiTrac has a particular message type that allows the receiver to determine how to de-serialize and parse the payload of the message.  The potential types and their meanings are described below:

| Value | Message Type                | Notes |
|------:|-----------------------------|-------|
| 0     | `Unknown`                   | Essentially an error – should not occur |
| 1     | `RequestForCamera2Image`    | Sent by the Pi 1 system to signal the Pi 2 system to be ready to take a picture. This also signals to the Pi 2 that the Pi 1 system is going to expect a picture in a `Camera2Image`–type message, and that the Pi 1 will be sending an external trigger to the Pi 2 camera. |
| 2     | `Camera2Image`              | Sent by the Pi 2 system when it takes a picture. The message will include the picture itself. The picture is an OpenCV Mat object packed as a MsgPack serialized data type. See `gpc_ipc_mat.*` in the PiTrac C++ source code. |
| 3     | `RequestForCamera2TestStillImage` | Reserved for testing modes. |
| 4     | `Results`                   | The result of the current system's operation, such as a ball hit |
| 5     | `Shutdown`                  | Tells the PiTrac system to shutdown and exit |
| 6     | `Camera2ReturnPreImage`     | Picture of the “hit” area before the ball is actually hit. Can be used for, e.g., subtractive filtering |
| 7     | `ControlMessage`            | A control message such as a club selection |


| Element | Field Name                 | MsgPack Type       | Notes |
|-------:|-----------------------------|--------------------|-------|
| 0      | `Carry_meters` (\*)         | Integer OR Float   | Not currently computed. The carry is calculated by external golf sims like GSPro. |
| 1      | `speed_mpers` (\*)          | Integer OR Float   | Ball speed in MPH |
| 2      | `launch_angle_deg` (\*)     | Integer OR Float   | Positive degrees are from the ground up to the line of flight. Must check type before decoding, due to an apparent issue in MsgPack for float values that are integer-like (e.g., 123.0). |
| 3      | `side_angle_deg` (\*)       | Integer OR Float   | Must check type before decoding. A positive side angle number means the ball will land to the right of the target, while a negative number means it will land to the left. |
| 4      | `back_spin_rpm` (\*)        | Integer            | Generally positive. The spin that occurs from, e.g., a chip shot. |
| 5      | `side_spin_rpm` (\*)        | Integer            | Negative is left spin (counter-clockwise from above ball). |
| 6      | `confidence` (\*)           | Integer            | A value between 0 and 10 — 10 means results are as confident as the system can be; 0 means no confidence and likely an error. |
| 7      | `club_type` (\*)            | Integer            | `kNotSelected` = 0, `kDriver` = 1, `kIron` = 2, `kPutter` = 3 |
| 8      | `result_type`               | Integer            | See Result Types below |
| 9      | `message`                   | String             | Can be `NilValue` |
| 10     | `log_messages`              | Array of Strings   | Array and each string can be `NilValue` |


The elements with asterisks and highlighted in green, above, are only set when the result\_type \= kHit (= 6).

In the IPC Result message, the Integer result type can be any one of the following:

| Integer Value | Value                  | Notes |
|--------------:|------------------------|-------|
| 1             | `Initializing`         | TBD |
| 2             | `WaitingForBallToAppear` |  |
| 3             | `PausingForBallStabilization` |  |
| 4             | `MultipleBallsPresent` |  |
| 5             | `BallPlacedAndReadyForHit` |  |
| 6             | `Hit`                  |  |
| 7             | `Error`                |  |
| 8             | `CalibrationResults`   |  |
| 9             | `ControlMessage`       |  |



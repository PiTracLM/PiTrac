---
title: Glossary
layout: default
nav_order: 100
description: Technical terms and definitions used throughout PiTrac documentation, based on terms found in the existing documentation.
keywords: PiTrac glossary, technical terms, golf launch monitor terminology
---

# Glossary

This glossary defines technical terms used throughout the PiTrac documentation. All definitions are based on explanations found within the existing documentation.

## A

**ActiveMQ**
: Open source multi-protocol messaging platform used by PiTrac for inter-process communication. Provides message brokering, routing, subscriptions, and tracking between PiTrac components.

**Auto-calibration**
: PiTrac feature that automatically determines focal length and camera angles empirically using a physical calibration rig with golf balls at known positions.

## B

**Ball Speed**
: One of the three primary measurements provided by PiTrac - the velocity of the golf ball as measured by the launch monitor system.

## C

**Camera Calibration**
: Process of determining camera angles, focal lengths, and distortion correction matrices needed for PiTrac to accurately locate balls in 3D space.

**Calibration Rig**
: Physical device that positions golf balls at known, fixed distances and angles from PiTrac cameras for the auto-calibration process.

## D

**De-distortion**
: Process of correcting fish-eye-like lens distortions using matrices calculated during camera calibration. Particularly evident around image edges.

**Distortion Matrix**
: Mathematical matrices used by PiTrac to correct lens distortions in camera images during calibration and operation.

## E

**E6/TruGolf**
: Golf simulator software that PiTrac can interface with. Uses port 2483 for communication.

**Extrinsic Calibration**
: Camera calibration process that determines camera position and orientation in 3D space relative to the scene.

## F

**Flight Camera**
: Camera 2 in the PiTrac system - points straight ahead to capture the ball in flight after being hit.

**Focal Length**
: Camera parameter that determines the field of view and magnification. Calculated during camera calibration process.

## G

**Global Shutter Camera**
: Type of camera sensor that captures the entire image simultaneously, avoiding rolling shutter artifacts. Required for PiTrac's high-speed ball tracking.

**GsPro**
: Golf simulator software that PiTrac can interface with for golf course simulation.

## H

**Hough Transform**
: OpenCV algorithm used by PiTrac for circle detection in strobed ball images. Can be sensitive and may require tuning for reliable operation.

**HSA (Horizontal Side Angle)**
: One of the launch angle measurements provided by PiTrac, indicating the ball's horizontal trajectory direction.

## I

**Intrinsic Calibration**
: Camera calibration process that determines internal camera parameters like focal length and lens distortion characteristics.

## L

**Launch Angle**
: Ball trajectory angle measurements provided by PiTrac, including both vertical and horizontal components.

**libcamera**
: Open-source camera stack and framework used by PiTrac for camera control on Raspberry Pi systems.

## M

**MsgPack**
: Platform-independent message serialization standard used by PiTrac's Open Interface for encoding data payloads.

## O

**OpenCV**
: Computer vision library used by PiTrac for image processing, including circle detection via Hough transforms.

**Open Interface (POI)**
: PiTrac's messaging interface built on ActiveMQ and MsgPack that allows third-party applications to communicate with the launch monitor.

## S

**Spin Rate**
: Measurement of golf ball rotation in three axes provided by PiTrac's analysis system.

**Strobe Light**
: Infrared LED lighting system that provides short, high-intensity pulses to illuminate the golf ball for high-speed imaging.

**Strobed Image**
: Photograph taken during strobe light pulse showing multiple ball positions for motion analysis.

## T

**Tee Camera**
: Camera 1 in the PiTrac system - angled to watch for the initial ball hit and teed-up ball position.

**TomEE**
: Apache TomEE application server used by PiTrac for web interface hosting on port 8080.

## V

**VSA (Vertical Side Angle)**
: One of the launch angle measurements provided by PiTrac, indicating the ball's vertical trajectory angle.

---

*All terms and definitions in this glossary are derived from explanations found within the existing PiTrac documentation.*
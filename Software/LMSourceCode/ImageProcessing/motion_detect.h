/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Interface to the ball-motion-detection module.
// The module is structured as a rpicam-apps-style post-processing stage,
// so there's a lot of uncessary code compared to what we would have if
// this was all completely designed from scatch.


#pragma once

#include "core/rpicam_app.hpp"

#include "post_processing_stages/post_processing_stage.hpp"


using Stream = libcamera::Stream;

class MotionDetectStage : public PostProcessingStage
{
public:
	MotionDetectStage(RPiCamApp* app) : PostProcessingStage(app) {}

	char const* Name() const override;

	void Read(boost::property_tree::ptree const& params) override;

	void Configure() override;

	bool Process(CompletedRequestPtr& completed_request) override;


	// In the Config, dimensions are given as fractions of the image size.
	struct Config
	{
		bool use_incoming_configuration = false;
		float roi_x, roi_y;
		float roi_width, roi_height;
		int hskip, vskip;
		float difference_m;
		int difference_c;
		float region_threshold;
		float max_region_threshold;
		int frame_period;
		bool verbose;
		bool showroi;
	};

	// This is the current configuration of the MotionDetectStage
	Config config_;

	// This structure can be set externally before the video-processing
	// starts so that the motion detector does not have to get its configuration
	// from the motion_detect.json file that the rpicam-apps framework would
	// otherwise use.  It must be called before the video-processing loop
	// begins.
	static Config incoming_configuration;

private:
	Stream* stream_;
	// Here we convert the dimensions to pixel locations in the image, as if subsampled
	// by hskip and vskip.
	uint roi_x_, roi_y_;
	uint roi_width_, roi_height_;
	uint region_threshold_;
	uint max_region_threshold_;
	std::vector<uint8_t> previous_frame_;
	bool first_time_;
	bool motion_detected_;
	uint postMotionFramesToCapture_;
	std::mutex mutex_;

	// If true, the Processing will no longer spend time looking for differences between
	// frames.  This accommodates post-club-strike image processing.
	bool detectionPaused_;
};

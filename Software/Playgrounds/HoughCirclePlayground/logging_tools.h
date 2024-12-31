/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <boost/shared_ptr.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/attributes/named_scope.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>

#include <string>

#include "gs_globals.h"


namespace golf_sim {

struct LoggingTools
{
	static constexpr std::string_view kDefaultSaveFileName = "out.png";

	static bool show_intermediate_images_;
	static bool logging_is_initialized_;
	static bool logging_tool_wait_for_keypress_;

	static void InitLogging();

	// Shohrtcut calls for debug and error messages
	static void Debug(std::string msg);
	static void Warning(std::string msg);
	static void Error(std::string msg);

	// Vector debug-print methods
	// TBD - Refactor all the repeated code
	static void Debug(const std::string msg, const std::vector<unsigned int> list);
	static void Debug(const std::string msg, const std::vector<int> list);
	static void Debug(const std::string msg, const std::vector<double> list);
	static void Debug(const std::string msg, const std::vector<float> list);


	static bool DisplayIntermediateImages();

	static cv::Vec2i GetImageWindowSize(const cv::Mat& img);

	static void ShowImage(std::string name, const cv::Mat& img, const std::vector < cv::Point >& pointFeatures = {});

	// Save the image (possibly with some pointFeatures) to a timestamped file whose name
	// will include the fileNameTag.  Example, with fileNameTag = "last_hit":
	//      "gs_log_img__last_hit__2023-11-13_12-52-47.0.png"
	// If forceFixedFileName is true, the logged image filename will be fixedFileName
	// Otherwise, the file name will have a date & 
	static bool LogImage(const std::string& fileNameTag,
							const cv::Mat& img,
							const std::vector < cv::Point >& pointFeatures,
							bool forceFixedFileName = false,
							const std::string& fixedFileName = std::string("") );

	// Create a unique, seconds-based date-time string
	static std::string GetUniqueLogName();


	// Creates its own copy of the image, so does not affect the original
	// start/endPoints are (x,y) tuples
	static void ShowRectangleOnImage(std::string name, const cv::Mat& baseImage, cv::Point startPoint, cv::Point endPoint);

	static void DebugShowColorSwatch(std::string name, GsColorTriplet bgr);

	// Creates its own copy of the image, so does not affect the original
	static void ShowContours(std::string name, const cv::Mat& baseImage, std::vector<std::vector<cv::Point>> contours);

	// Only shows the image if the logging level is at or below debug
	static void DebugShowContours(std::string name, const cv::Mat& baseImage, std::vector<std::vector<cv::Point>> contours);

	static void GetOneImage(std::vector<cv::Mat> images);

	// The ordinal value provides a mechanism to adjust the outline and 
	// colors a little to make them easier to see for each (ordinal) circle
	// Ordinals must start at 1 to be used
	static void DrawCircleOutlineAndCenter(cv::Mat& img, GsCircle circle, std::string label, int ordinal = 0, bool de_emphasize = false);


	// Only shows the image if the logging level is at or below debug
	static void DebugShowImage(std::string name, const cv::Mat& img, const std::vector < cv::Point > &pointFeatures = {});

	// Prints out theh basic information about an image, but not all the data
	static std::string SummarizeImage(const cv::Mat& img);

	// The following format_xxx functions prepare a human-readable output that represents the given object
	static std::string FormatCircle(const GsCircle& c);
	static std::string FormatCircleList(const std::vector<GsCircle>& cList);

	static std::string FormatVec3f(const cv::Vec3f& v);
	static std::string FormatGsColorTriplet(const GsColorTriplet& v);
};

// Used as a define so that we can get file/line-numbers in our tracing if we want
#define GS_LOG_MSG(LEVEL, MSG) BOOST_LOG_FUNCTION();  BOOST_LOG_TRIVIAL(LEVEL) << MSG

// Trace logging is everywhere, so this macro allows just that macro to be undefined
// in order to increase performance
#define GS_LOG_TRACE_MSG(LEVEL, MSG) BOOST_LOG_FUNCTION();  BOOST_LOG_TRIVIAL(LEVEL) << MSG


}

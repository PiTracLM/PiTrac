/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include <algorithm>
#include <format>
#include "logging_tools.h"
#include "cv_utils.h"

#ifdef __unix__
const std::string kBaseImageLoggingDir = "/mnt/VerdantShare/dev/GolfSim/LM/Images/";
#else
const std::string kBaseImageLoggingDir = "D:\\GolfSim\\LM\\Images\\";
#endif

namespace golf_sim {

    bool LoggingTools::show_intermediate_images_ = false;
    bool LoggingTools::logging_is_initialized_ = false;

    // Waits for the user to press a key before continuing after showing an image
    bool LoggingTools::logging_tool_wait_for_keypress_ = false;

    void LoggingTools::InitLogging()
    {
        boost::log::add_common_attributes();
        boost::log::core::get()->add_global_attribute("Scope", boost::log::attributes::named_scope());

        boost::log::core::get()->set_filter(
            boost::log::trivial::severity >= boost::log::trivial::trace
        );

        LoggingTools::show_intermediate_images_ = true;

        /* log formatter:
        * [TimeStamp] [ThreadId] [Severity Level] [Scope] Log message
        */
        auto fmtTimeStamp = boost::log::expressions::
            format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S.%f");
        auto fmtThreadId = boost::log::expressions::
            attr<boost::log::attributes::current_thread_id::value_type>("ThreadID");
        auto fmtSeverity = boost::log::expressions::
            attr<boost::log::trivial::severity_level>("Severity");
        auto fmtScope = boost::log::expressions::format_named_scope("Scope",
            boost::log::keywords::format = "%n(%f:%l)",
            boost::log::keywords::iteration = boost::log::expressions::reverse,
            boost::log::keywords::depth = 2);

        boost::log::formatter logFmt =
            boost::log::expressions::format("[%1%] (%2%) [%3%] %4%")
            % fmtTimeStamp % fmtThreadId % fmtSeverity
            % boost::log::expressions::smessage;
        /*
        boost::log::formatter logFmt =
            boost::log::expressions::format("[%1%] (%2%) [%3%] [%4%] %5%")
            % fmtTimeStamp % fmtThreadId % fmtSeverity % fmtScope
            % boost::log::expressions::smessage;
    */
    /* console sink */
        auto consoleSink = boost::log::add_console_log(std::clog);
        consoleSink->set_formatter(logFmt);

        /* fs sink */
        auto fsSink = boost::log::add_file_log(
            boost::log::keywords::file_name = "Logs/test_%Y-%m-%d_%H-%M-%S.%N.log",
            boost::log::keywords::rotation_size = 10 * 1024 * 1024,
            boost::log::keywords::min_free_space = 30 * 1024 * 1024,
            boost::log::keywords::open_mode = std::ios_base::app);
        fsSink->set_formatter(logFmt);
        fsSink->locked_backend()->auto_flush(true);
    }


    void LoggingTools::Debug(std::string msg) {
        BOOST_LOG_TRIVIAL(debug) << msg;
    }

    void LoggingTools::Warning(std::string msg) {
        BOOST_LOG_TRIVIAL(warning) << msg;
    }

    void LoggingTools::Error(std::string msg) {
        BOOST_LOG_TRIVIAL(error) << msg;
    }


    // TBD - Refactor all the repeated code
    void LoggingTools::Debug(const std::string msg, const std::vector<unsigned int> list) {
        BOOST_LOG_TRIVIAL(debug) << msg;

        for (auto element : list) {
            BOOST_LOG_TRIVIAL(debug) << element;
        }
    }

    void LoggingTools::Debug(const std::string msg, const std::vector<int> list) {
        BOOST_LOG_TRIVIAL(debug) << msg;

        for (auto element : list) {
            BOOST_LOG_TRIVIAL(debug) << element;
        }
    }

    void LoggingTools::Debug(const std::string msg, const std::vector<double> list) {
        BOOST_LOG_TRIVIAL(debug) << msg;

        for (auto element : list) {
            BOOST_LOG_TRIVIAL(debug) << element;
        }
    }

    void LoggingTools::Debug(const std::string msg, const std::vector<float> list) {
        BOOST_LOG_TRIVIAL(debug) << msg;

        for (auto element : list) {
            BOOST_LOG_TRIVIAL(debug) << element;
        }
    }



    bool LoggingTools::DisplayIntermediateImages()
    {
        return (show_intermediate_images_);
    }

    // Get a window size (x, y) appropriate to the image, limited to something reasonable -- 600 pixels
    cv::Vec2i LoggingTools::GetImageWindowSize(const cv::Mat& img)
    {
        int largestDim = (int)std::max(CvUtils::CvWidth(img), CvUtils::CvHeight(img));
        largestDim = std::min((int)largestDim, 750);

        int xDim = 0;
        int yDim = 0;

        if (CvUtils::CvWidth(img) > CvUtils::CvHeight(img))
        {
            xDim = largestDim;
            yDim = (int)((float)largestDim * ((float)CvUtils::CvHeight(img) / (float)CvUtils::CvWidth(img)));
        }
        else
        {
            yDim = largestDim;
            xDim = (int)((float)largestDim * (int)((float)CvUtils::CvWidth(img) / (float)CvUtils::CvHeight(img)));
        }

        return cv::Vec2i(xDim, yDim);
    }



    void LoggingTools::ShowImage(std::string name, const cv::Mat& img, const std::vector < cv::Point >& pointFeatures)
    {
        BOOST_LOG_FUNCTION();
        BOOST_LOG_TRIVIAL(trace) << "ShowImage(" << name << ", " << SummarizeImage(img);

        name = name + " (" + std::to_string(CvUtils::CvWidth(img)) + ", " + std::to_string(CvUtils::CvHeight(img)) + ")";
        cv::namedWindow((cv::String)name, cv::WINDOW_NORMAL);

        if (img.rows > 0 && img.cols > 0) {
            cv::Vec2i windowSize = GetImageWindowSize(img);
            cv::resizeWindow(name, windowSize[0], windowSize[1]);
        }
        else {
            cv::resizeWindow(name, 400, 400);
        }

        cv::Mat imgToShow = img.clone();

        for (auto& point : pointFeatures)
        {
            cv::circle(imgToShow, point, 2, cv::Scalar{ 0, 0, 0 }, 24);  // Nominal width was 24
        }


        cv::imshow(name, imgToShow);
        cv::setWindowProperty(name, cv::WND_PROP_TOPMOST, 1);

        // Move the windows out of the way of the debugger window
        // TBD - determine how to move to right - most side of screen
        cv::moveWindow(name, 1200, 20);

        // "S" means to save thhe picture that is being shown
        if (logging_tool_wait_for_keypress_)
        {
            int keyPressed = cv::waitKey(0) & 0xFF;
            if (keyPressed == 115)    // "s"
            {
                std::string fname = std::string{ kDefaultSaveFileName };
                cv::imwrite(fname, img);
                BOOST_LOG_TRIVIAL(debug) << "Saved to file: " << kDefaultSaveFileName << ". Press any key to continue";
                cv::waitKey(0);  // Give user a chance to move the saved file if necessary
            }
        }
    }

    std::string LoggingTools::GetUniqueLogName()
    { 
        using namespace boost::posix_time;
        using namespace boost::gregorian;

        //get the current time from the clock -- one second resolution
        ptime now = second_clock::local_time();
        //Get the date part out of the time
        date today = now.date();

        std::string s = to_simple_string(today);

        return s;
    }

    const std::string kLogImagePrefix = "gs_log_img__";

    // Save the image (possibly with some pointFeatures) to a timestamped file whose name
    // will include the fileNameTag.  Example, with fileNameTag = "last_hit":
    //      "gs_log_img__last_hit__2023-11-13_12-52-47.0.png"
    // If forceFixedFileName is true, the logged image filename will be fixedFileName, but.  
    // still preceded by the default logging directory.
    // For example, "last_GetBall_img.png", so that the file keeps getting overwritten.
    // Otherwise, the file name will have a date & 
    bool LoggingTools::LogImage(const std::string& fileNameTag,
        const cv::Mat& img,
        const std::vector < cv::Point >& pointFeatures,
        bool forceFixedFileName,
        const std::string& fixedFileName) {

        if (img.empty()) {
            BOOST_LOG_TRIVIAL(debug) << "LogImage: image was empty - ignoring.";
            return false;
        }
        std::string fname;

        if (forceFixedFileName && !fixedFileName.empty()) {
            fname = kBaseImageLoggingDir + fixedFileName;
        }
        else {
            std::string dateTimeStr = GetUniqueLogName();

            fname = kBaseImageLoggingDir + kLogImagePrefix + fileNameTag + dateTimeStr + ".png";
        }

        cv::Mat imgToLog = img.clone();

        for (auto& point : pointFeatures)
        {
            cv::circle(imgToLog, point, 2, cv::Scalar{ 0, 0, 0 }, 24);  // Nominal width was 24
        }


        BOOST_LOG_TRIVIAL(debug) << "About to log image to file: " << fname << ".";
        cv::imwrite(fname, imgToLog);
        BOOST_LOG_TRIVIAL(debug) << "Logged image to file: " << fname << ".";

        return true;
    }

    // Only shows the image if the logging level is at or below debug
    void LoggingTools::DebugShowImage(std::string name, const cv::Mat& img, const std::vector < cv::Point >& pointFeatures)
    {

        // TBD - Figure out logging level
        if (DisplayIntermediateImages())
        {
            ShowImage(name, img, pointFeatures);
        }
    }

    // Creates its own copy of the image, so does not affect the original
    // start/endPoints are (x,y) tuples
    void LoggingTools::ShowRectangleOnImage(std::string name, const cv::Mat& baseImage, cv::Point startPoint, cv::Point endPoint)
    {
        cv::Scalar c1{ 0, 255, 0 };
        cv::Mat debugImage = baseImage.clone();
        cv::rectangle(debugImage, startPoint, endPoint, c1, 2);

        DebugShowImage(name, debugImage);
    }

    void LoggingTools::DebugShowColorSwatch(std::string name, GsColorTriplet bgr)
    {
        cv::Mat img = cv::Mat::zeros(200, 200, CV_8UC3);
        cv::Scalar color{ bgr };
        cv::rectangle(img, cv::Point(0, 0), cv::Point(199, 199), color, -1);

        DebugShowImage(name, img);
    }


    // Creates its own copy of the image, so does not affect the original
    void LoggingTools::ShowContours(std::string name, const cv::Mat& baseImage, std::vector<std::vector<cv::Point>> contours)
    {
        cv::Mat debugImage = baseImage.clone();

        for (auto i = 0; i < contours.size(); i++)
        {
            cv::Scalar c1{ 0, 255, 0 };
            cv::drawContours(debugImage, contours, i, c1, 2);
        }

        DebugShowImage(name, debugImage);
    }

    // Only shows the image if the logging level is at or below debug
    void LoggingTools::DebugShowContours(std::string name, const cv::Mat& baseImage, std::vector<std::vector<cv::Point>> contours)
    {
        // TBD - Figure out logging level
        ShowContours(name, baseImage, contours);
    }

    void LoggingTools::GetOneImage(std::vector<cv::Mat> images)
    {
        std::vector<cv::Mat> imgList;

        const int padding = 200;

        for (auto& img : images)
        {
            imgList.push_back(img);
        }

        std::vector<int> max_width;
        int max_height = 0;

        for (auto& img : imgList)
        {
            max_width.push_back(img.rows);
            max_height += img.cols;
        }

        int w = *max_element(max_width.begin(), max_width.end());
        int h = max_height + padding;

        // create a new array with a size large enough to contain all the images
        cv::Mat final_image = cv::Mat::zeros(h, w, CV_8U);

        int current_y = 0;  // keep track of where your current image was last placed in the y coordinate

        for (auto& image : imgList)
        {
            // add an image to the final array and increment the y coordinate
            image.copyTo(final_image(cv::Rect(0, current_y, image.cols, current_y + image.rows)));
            current_y += image.rows;
        }
        cv::imwrite(std::string{ kDefaultSaveFileName }, final_image);
    }

    void LoggingTools::DrawCircleOutlineAndCenter(cv::Mat& img, GsCircle circle, std::string label, int ordinal, bool de_emphasize)
    {
        uchar rotatingColor = ordinal * 30;

        cv::Scalar c1 = cv::Scalar(rotatingColor, 255 - rotatingColor, rotatingColor);
        cv::Scalar c2 = cv::Scalar(0, rotatingColor, 255);
        cv::Scalar c3{ 0, 0, 0 };

        if (ordinal > 0) {
            c3 = c1;
            c2 = c1;
        }

        int thickness = 1;

        if (img.cols < 600) {
            thickness = 1;
        }

        int kMaxCirclesToEmphasize = 10;

        if (de_emphasize) {
            c1 = { 240,240,240 };
            c2 = { 240,240,240 };
            c3 = { 40,40,40 };
        }
        cv::circle(img, cv::Point((int)circle[0], (int)circle[1]), (int)circle[2], c1, thickness);
        cv::circle(img, cv::Point((int)circle[0], (int)circle[1]), 2, c2, 4);
        cv::putText(img, label, cv::Point((int)circle[0] + 2 + ((float)(ordinal) * .2), (int)circle[1] + ((float)(ordinal) * .2)), cv::FONT_HERSHEY_SIMPLEX, 1, c3, 2, cv::LINE_AA);

    }

    std::string LoggingTools::SummarizeImage(const cv::Mat& img) {
        std::string s = "(sizeX, sizeY) = (" + std::to_string(img.cols) + ", " + std::to_string(img.rows) + ")";
        return s;
    }

    std::string LoggingTools::FormatVec3f(const cv::Vec3f& v) {

        auto f = std::format("[{: >6.2f}, {: >6.2f}, {: >6.2f}]", v[0], v[1], v[2]);
        return f;
    }

    std::string LoggingTools::FormatGsColorTriplet(const GsColorTriplet& v) {

        auto f = std::format("[{: >6.2f}, {: >6.2f}, {: >6.2f}]", v[0], v[1], v[2]);
        return f;
    }

    std::string LoggingTools::FormatCircle(const GsCircle& c) {
        auto f = std::format( "[(x,y)=({: >4},{: <4}), r={: <6.1f}]", c[0], c[1], c[2] );
        return f;
    }

    std::string LoggingTools::FormatCircleList(const std::vector<GsCircle>& circleList) {
        std::string s;
        for (auto& c : circleList) {
            s += FormatCircle(c);
        }
        return s;
    }

}

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "cv_utils.h"


namespace golf_sim {

    double CvUtils::CircleRadius(const GsCircle& circle)
    {
        return ((double) circle[2]);
    }

    cv::Vec2i CvUtils::CircleXY(const GsCircle& circle)
    {
        return (cv::Vec2i((int)std::round(circle[0]), (int)std::round(circle[1])));
    }

    int CvUtils::CircleX(const GsCircle& circle) {
        return ((int)std::round(circle[0]));
    }

    int CvUtils::CircleY(const GsCircle& circle) {
        return ((int)std::round(circle[1]));
    }


    cv::Vec2i CvUtils::CvSize(const cv::Mat& img)
    {
        return (cv::Vec2i(img.cols, img.rows));
    }

    int CvUtils::CvHeight(const cv::Mat& img)
    {
        return (img.rows);
    }

    int CvUtils::CvWidth(const cv::Mat& img)
    {
        return (img.cols);
    }

    cv::Vec3f CvUtils::Round(const cv::Vec3f& v)
    {
       return cv::Vec3f(round(v[0]), round(v[1]), round(v[2]));
    }

    void CvUtils::MakeEven(int& value) {
        value += ((int)value % 2);
    }

    int CvUtils::RoundAndMakeEven(double value) {
        return (int)(round(value * 0.5) * 2.0);
    }

    int CvUtils::RoundAndMakeEven(int value) {
        return (int)(round((double)value * 0.5) * 2.0);
    }

    // Note that the rgb value will be stored in openCV format - i.e., as bgr
    GsColorTriplet CvUtils::ConvertRgbToHsv(const GsColorTriplet& rgb)
    {
        GsColorTriplet hsvUnscaled = colorsys::rgb_to_hsv(GsColorTriplet(rgb[2] / 255, rgb[1] / 255, rgb[0] / 255));
        GsColorTriplet hsv = GsColorTriplet(hsvUnscaled[0] * (float)kOpenCvHueMax, hsvUnscaled[1] * (float)kOpenCvSatMax, hsvUnscaled[2] * (float)kOpenCvValMax);

        return (hsv);
    }


    // Note that the hsv value will be stored in openCV format 
    GsColorTriplet CvUtils::ConvertHsvToRgb(const GsColorTriplet& hsv)
    {
        GsColorTriplet rgbUnscaled = colorsys::hsv_to_rgb(GsColorTriplet( hsv[0] / (float)kOpenCvHueMax, hsv[1] / (float)kOpenCvSatMax, hsv[2] / (float)kOpenCvValMax ));
        GsColorTriplet rgb = GsColorTriplet(int(rgbUnscaled[2] * 255), int(rgbUnscaled[1] * 255), int(rgbUnscaled[0] * 255));

        return (rgb);
    }


    float CvUtils::ColorDistance(const GsColorTriplet & rgb1, const GsColorTriplet& rgb2)
    {
        float rgbDiff = sqrt((float)(pow((double)(rgb1[0] - rgb2[0]), 2.) + pow((double)(rgb1[1] - rgb2[1]), 2.) + pow((double)(rgb1[2] - rgb2[2]), 2.)));
        return (rgbDiff);
    }

    bool CvUtils::IsDarker(const GsColorTriplet& rgb1, const GsColorTriplet& rgb2)
    {
        float rgbDiff = (rgb1[0] - rgb2[0]) + (rgb1[1] - rgb2[1]) + (rgb1[2] - rgb2[2]);
        return (rgbDiff < 0);
    }




    // The ball color will be an average of the colors near the middle of the determined ball
    // The returned color is in RGB form
    std::vector<GsColorTriplet> CvUtils::GetBallColorRgb(const cv::Mat &img, const GsCircle &circle)
    {
        BOOST_LOG_FUNCTION();

        int r = CircleRadius(circle);
        cv::Vec2i xy = CircleXY(circle);
        int x = xy[0];
        int y = xy[1];

        if (r == 0) {
            GS_LOG_MSG(error, "CvUtils::GetBallColorRgb called with circle of 0 radius.");
            std::vector<GsColorTriplet> empty;
            return empty;
        }

        const double BOUNDED_BOX_RADIUS_RATIO = ((2.0*r) * 0.707) / 2.0;   // 1.6 (which is almost the whole ball may be resulting in too much averaging)
        int xmin = std::max(0, (int)round(x - BOUNDED_BOX_RADIUS_RATIO));
        int xmax = std::min(CvWidth(img), (int)round(x + BOUNDED_BOX_RADIUS_RATIO));    // Somehow, the box otherwise seems too far to the left?
        int ymin = std::max(0, (int)round(y - BOUNDED_BOX_RADIUS_RATIO));    // Round up to focus more on the better-lit top of the ball
        int ymax = std::min(CvHeight(img), (int)round(y + BOUNDED_BOX_RADIUS_RATIO));

        // GS_LOG_TRACE_MSG(trace, "GetBall_color: xmin,max, ymin,max = (" + std::to_string(xmin) + "," + std::to_string(xmax) + ") : (" + std::to_string(ymin) + "," + std::to_string(ymax) + ")");

        // LoggingTools::ShowRectangleOnImage("getBallColor area to average: ", img, cv::Point(xmin, ymin), cv::Point(xmax, ymax));

        cv::Mat subImg = img(cv::Range(ymin, ymax), cv::Range(xmin, xmax));

        cv::Scalar avg_color, std_color;
        cv::meanStdDev(subImg, avg_color, std_color);
        cv::Scalar median_color = avg_color; // TBD - Compute Median

        // TBD - Not sure the first element of the array is the correct one
        // GS_LOG_TRACE_MSG(trace, "Average is (RGB):" + LoggingTools::FormatGsColorTriplet(avg_color) + " Median is: " + LoggingTools::FormatGsColorTriplet(median_color) + " STD is " + LoggingTools::FormatGsColorTriplet(std_color));

        GsColorTriplet average_color_Scalar{ avg_color };
        // LoggingTools::DebugShowColorSwatch("Average Color of  Ball (BGR)", average_color_Scalar );

        // TBD - Not sure whaht we're going to be getting from the statistical calls, above
        GsColorTriplet avg_color_vec3f{ (float)avg_color[0], (float)avg_color[1], (float)avg_color[2] };
        GsColorTriplet median_color_vec3f{ (float)median_color[0], (float)median_color[1], (float)median_color[2] };
        GsColorTriplet std_color_vec3f{ (float)std_color[0], (float)std_color[1], (float)std_color[2] };

        std::vector<GsColorTriplet> results{ avg_color_vec3f, median_color_vec3f, std_color_vec3f };
        return results;  
    }

    cv::Mat CvUtils::GetAreaMaskImage(int resolution_x_, int resolution_y_, int expected_ball_X, int expected_ball_Y, int mask_radius, cv::Rect& mask_dimensions, bool use_square)
    {
        BOOST_LOG_FUNCTION();

        cv::Mat initialAreaMaskImage = cv::Mat::zeros(resolution_y_, resolution_x_, CV_8UC3);
        cv::Mat maskImage;
        cv::cvtColor(initialAreaMaskImage, maskImage, cv::COLOR_BGR2HSV);

        // A white circle on a black background will act as our mask

        // Setup white color scalar for re-use below
        cv::Scalar c1(255, 255, 255);

        // Make sure we're not drawing outside the image
        if (expected_ball_X + mask_radius > resolution_x_) {
            mask_radius = resolution_x_ - expected_ball_X;
            LoggingTools::Warning("Attempted to draw mask area outside image (1).");
        }
        if (expected_ball_X - mask_radius < 0) {
            mask_radius = expected_ball_X;
            LoggingTools::Warning("Attempted to draw mask area outside image (2).");
        }
        if (expected_ball_Y + mask_radius > resolution_y_) {
            mask_radius = resolution_y_ - expected_ball_Y;
            LoggingTools::Warning("Attempted to draw mask area outside image (3).");
        }
        if (expected_ball_Y - mask_radius < 0) {
            mask_radius = expected_ball_Y;
            LoggingTools::Warning("Attempted to draw mask area outside image (4).");
        }

        if (!use_square)
        {
            cv::circle(maskImage, cv::Point(expected_ball_X, expected_ball_Y), mask_radius, c1, -1);
        }
        else
        {
            cv::rectangle(maskImage,
                cv::Point(expected_ball_X - mask_radius, expected_ball_Y - mask_radius),
                cv::Point(expected_ball_X + mask_radius, expected_ball_Y + mask_radius),
                c1, -1);
        }

        // Return the rectangle around the ROI
        mask_dimensions = cv::Rect(expected_ball_X - mask_radius, expected_ball_Y - mask_radius, 2 * mask_radius, 2 * mask_radius);

        cv::Mat area_mask_image_(maskImage.rows, maskImage.cols, CV_8U, cv::Scalar(0));
        cv::inRange(maskImage, c1, c1, area_mask_image_);

        // We will create our own mask based on where the ball should be
        // TBD - The blur matrix may be touchy.  If not blurry enough, things break
        //  For now, assume the mask will be blurred later
        cv::GaussianBlur(area_mask_image_, area_mask_image_, cv::Size(5, 5), 0);  // nominal was 11x11
        cv::erode(area_mask_image_, area_mask_image_, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
        cv::dilate(area_mask_image_, area_mask_image_, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);

        return (area_mask_image_);
    }

    double CvUtils::MetersToFeet(double m)
    {
        return (3.281 * m);
    }

    double CvUtils::MetersToInches(double m)
    {
        return (12 * MetersToFeet(m));
    }

    double CvUtils::InchesToMeters(double i)
    {
        return (0.0254 * i);
    }

    double CvUtils::MetersPerSecondToMPH(double mps) {
        return (mps * 2.23694);
    }

    double CvUtils::MetersToYards(double m) {
        return (MetersToFeet(m) / 3.0);
    }


    // Size the result image to the size of the image_to_size
    void CvUtils::SetMatSize(const cv::Mat& image_to_size, cv::Mat& result_image) {
        result_image = cv::Mat::zeros(image_to_size.rows, image_to_size.cols, CV_8UC3);
    }

    // Note - the bar for "upright" is REALLY low here
    bool CvUtils::IsUprightRect(float theta) {
        const float kUprightRectTolerance = 25.0;
        bool isUpright = false;

        if (std::abs(theta - 0.) < kUprightRectTolerance ||
            std::abs(theta - 90.) < kUprightRectTolerance ||
            std::abs(theta - 180.) < kUprightRectTolerance ||
            std::abs(theta - 270.) < kUprightRectTolerance) {
            isUpright = true;
        }

        if (!isUpright) {
            LoggingTools::Warning("Found non-upright ellipse.  Theta = " + std::to_string(theta));
        }
        return isUpright;
    }

    void CvUtils::DrawGrayImgHistogram(const cv::Mat& img, const bool ignore_zeros) {
        /// Establish the number of bins
        const int histSize = 256;

        float range[] = { 0, 256 };
        const float* histRange = { range };

        bool uniform = true; bool accumulate = false;

        cv::Mat b_hist;
        /// Compute the histograms:
        calcHist(&img, 1, 0, cv::Mat(), b_hist, 1, &histSize, &histRange, uniform, accumulate);

        // Draw the histograms for B, G and R
        int hist_w = 512; int hist_h = 400;
        int bin_w = cvRound((double)hist_w / histSize);

        cv::Mat histImage(hist_h, hist_w, CV_8UC3, cv::Scalar(0, 0, 0));

        // Normalize the result to [ 0, histImage.rows ]
        cv::normalize(b_hist, b_hist, 0, histImage.rows, cv::NORM_MINMAX, -1, cv::Mat());

        // Draw for each channel
        int startingBin = ignore_zeros ? 2 : 1;

        for (int i = startingBin; i < histSize; i++)
        {
            cv::line(histImage, cv::Point(bin_w * (i - 1), hist_h - cvRound(b_hist.at<float>(i - 1))),
                cv::Point(bin_w * (i), hist_h - cvRound(b_hist.at<float>(i))),
                cv::Scalar(255, 0, 0), 2, 8, 0);
        }

        LoggingTools::DebugShowImage("calcHist Dem", histImage);

        cv::waitKey(0);
    }

    cv::Mat CvUtils::GetSubImage(const cv::Mat& full_image, cv::Rect& ball_ROI_rectInfull_image, cv::Point& offset_sub_to_full, cv::Point& offset_full_to_sub) {

        cv::Mat subImg;

        cv::Rect finalRoi = ball_ROI_rectInfull_image;

        if (ball_ROI_rectInfull_image.tl().x < 0 ) {
            finalRoi.x = 0;
            LoggingTools::Warning("CvUtils::GetSubImage received invalid sub-image parameters.  tl.x = " + std::to_string(ball_ROI_rectInfull_image.tl().x) + ".  Corrected");
        }
        if (ball_ROI_rectInfull_image.tl().y < 0) {
            finalRoi.y = 0;
            LoggingTools::Warning("CvUtils::GetSubImage received invalid sub-image parameters.  tl.y = " + std::to_string(ball_ROI_rectInfull_image.tl().y) + ".  Corrected");
        }
        if (ball_ROI_rectInfull_image.br().x >= full_image.cols) {
            finalRoi.width -= ball_ROI_rectInfull_image.br().x - full_image.cols;
            LoggingTools::Warning("CvUtils::GetSubImage received invalid sub-image parameters.  br.x = " + std::to_string(ball_ROI_rectInfull_image.br().x) + ".  Corrected");
        }
        if (ball_ROI_rectInfull_image.br().y >= full_image.rows) {
            finalRoi.height -= ball_ROI_rectInfull_image.br().y - full_image.rows;
            LoggingTools::Warning("CvUtils::GetSubImage received invalid sub-image parameters.  br.x = " + std::to_string(ball_ROI_rectInfull_image.br().x) + ".  Corrected");
        }

        // TBD - Might be better to pass in a reference to avoide the sub-image copy?
        subImg = full_image(cv::Range(finalRoi.tl().y, finalRoi.br().y),
            cv::Range(finalRoi.tl().x, finalRoi.br().x));

        offset_sub_to_full = cv::Point(finalRoi.tl().x, finalRoi.tl().y);
        offset_full_to_sub = cv::Point(-finalRoi.tl().x, -finalRoi.tl().y);

        // At least for now, correct the original Roi to prevent downstream errors
        ball_ROI_rectInfull_image = finalRoi;

        return subImg;
    }

    double CvUtils::GetDistance(const cv::Vec3d& location) {
        return std::sqrt(std::pow(location[0], 2.) + std::pow(location[1], 2.) + std::pow(location[2], 2.));
    }

    double CvUtils::GetDistance(const cv::Point& point1, const cv::Point& point2) {
        return std::sqrt(std::pow(std::abs(point1.x - point2.x), 2.) + std::pow(std::abs(point1.y - point2.y), 2.));
    }


void CvUtils::BrightnessAndContrastAutoAlgo2(const cv::Mat& bgr_image, cv::Mat& dst) {
    cv::Mat lab_image;
    cv::cvtColor(bgr_image, lab_image, cv::COLOR_BGR2Lab);

    // Extract the L channel
    std::vector<cv::Mat> lab_planes(3);
    cv::split(lab_image, lab_planes);  // now we have the L image in lab_planes[0]

    // apply the CLAHE algorithm to the L channel
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
    clahe->setClipLimit(4);
    clahe->apply(lab_planes[0], dst);

    // Merge the the color planes back into an Lab image
    dst.copyTo(lab_planes[0]);
    cv::merge(lab_planes, lab_image);

    // convert back to RGB
    cv::Mat image_clahe;
    cv::cvtColor(lab_image, image_clahe, cv::COLOR_Lab2BGR);

    // display the results  (you might also want to see lab_planes[0] before and after).
    cv::imshow("image original", bgr_image);
    cv::imshow("image CLAHE", image_clahe);

    dst = image_clahe;
}

/**
 *  \brief Automatic brightness and contrast optimization with optional histogram clipping
 *  \param [in]src Input image GRAY or BGR or BGRA
 *  \param [out]dst Destination image
 *  \param clip_hist_percent cut wings of histogram at given percent tipical=>1, 0=>Disabled
 *              (Seems like it must be at least 30 or so to make much of a difference)
 *  \note In case of BGRA image, we won't touch the transparency
*/
void CvUtils::BrightnessAndContrastAutoAlgo1(const cv::Mat& src, cv::Mat& dst, float clip_hist_percent) {

    CV_Assert(clip_hist_percent >= 0);
    CV_Assert((src.type() == CV_8UC1) || (src.type() == CV_8UC3) || (src.type() == CV_8UC4));

    int histSize = 256;
    float alpha, beta;
    double minGray = 0, maxGray = 0;

    //to calculate grayscale histogram
    cv::Mat gray;
    if (src.type() == CV_8UC1) gray = src;
    else if (src.type() == CV_8UC3) cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    else if (src.type() == CV_8UC4) cvtColor(src, gray, cv::COLOR_BGRA2GRAY);
    if (clip_hist_percent == 0)
    {
        // keep full available range
        cv::minMaxLoc(gray, &minGray, &maxGray);
    }
    else
    {
        cv::Mat hist; //the grayscale histogram

        float range[] = { 0, 256 };
        const float* histRange = { range };
        bool uniform = true;
        bool accumulate = false;
        calcHist(&gray, 1, 0, cv::Mat(), hist, 1, &histSize, &histRange, uniform, accumulate);

        // calculate cumulative distribution from the histogram
        std::vector<float> accumulator(histSize);
        accumulator[0] = hist.at<float>(0);
        for (int i = 1; i < histSize; i++)
        {
            accumulator[i] = (float)(accumulator[i - 1] + hist.at<float>(i));
        }

        // locate points that cuts at required value
        float max = accumulator.back();
        clip_hist_percent *= (float)(max / 100.0); //make percent as absolute
        clip_hist_percent /= 2.0; // left and right wings
        // locate left cut
        minGray = 0;
        while (accumulator[(int)minGray] < (float)clip_hist_percent)
            minGray++;

        // locate right cut
        maxGray = (double)(histSize - 1);
        while (accumulator[(int)maxGray] >= (float)(max - clip_hist_percent))
            maxGray--;
    }

    // current range
    float inputRange = (float)(maxGray - minGray);

    alpha = (histSize - 1) / inputRange;   // alpha expands current range to histsize range
    beta = (float)(-minGray * alpha);             // beta shifts current range so that minGray will go to 0

    // Apply brightness and contrast normalization
    // convertTo operates with saurate_cast
    src.convertTo(dst, -1, alpha, beta);

    // restore alpha channel from source 
    if (dst.type() == CV_8UC4)
    {
        int from_to[] = { 3, 3 };
        cv::mixChannels(&src, 4, &dst, 1, from_to, 1);
    }
    return;
}

}


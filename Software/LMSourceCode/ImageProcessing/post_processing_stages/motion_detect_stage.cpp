#ifdef __unix__  // Ignore in Windows environment

// NOTE - This is the file from the libcamera-apps distribution with a few 
// changes to connect the post-processing with strobe pulsing.


// A motion detector. It needs to be given a low resolution image and it
// compares pixels in the current low res image against the value in the corresponding
// location in the previous one. If it exceeds a threshold it gets counted as
// "different". If enough pixels are different, that indicates "motion".
// A low res image of something like 128x96 is probably more than enough, and you
// can always subsample with hskip and vksip.

// Because this gets run in parallel by the post-processing framework, it means
// the "previous frame" is not totally guaranteed to be the actual previous one,
// though in practice it is, and it doesn't actually matter even if it wasn't.

// The stage adds "motion_detect.result" to the metadata. When this claims motion,
// the application can take that as true immediately. To be sure there's no motion,
// an application should probably wait for "a few frames" of "no motion".



#include <opencv2/core.hpp>
#include <opencv2/photo.hpp>
#include <opencv2/core/cvdef.h>
#include <opencv2/highgui.hpp>
#include <boost/circular_buffer.hpp>

#include "ball_watcher_image_buffer.h"
#include "gs_club_data.h"

#include <libcamera/stream.h>
#include "core/rpicam_app.hpp"

#include "gs_globals.h"
#include "gs_options.h"
#include "pulse_strobe.h"
#include "logging_tools.h"
#include "gs_fsm.h"
#include "motion_detect.h"



namespace gs = golf_sim;
using Stream = libcamera::Stream;

#define NAME "motion_detect"


MotionDetectStage::Config MotionDetectStage::incoming_configuration;


char const *MotionDetectStage::Name() const
{
	return NAME;
}

void MotionDetectStage::Read(boost::property_tree::ptree const &params)
{
	if (incoming_configuration.use_incoming_configuration) {
		// If the configuration was set programmatically already,
		// we won't do anything, including pulling that configuration 
		// from the external .json file
		GS_LOG_MSG(trace, "MotionDetectStage::Read - using internal data.");
		config_ = incoming_configuration;
	}
	else {
		GS_LOG_MSG(trace, "MotionDetectStage::Read - using external .json data.");
		config_.roi_x = params.get<int>("roi_x", 0);
		config_.roi_y = params.get<int>("roi_y", 0);
		config_.roi_width = params.get<int>("roi_width", 1);
		config_.roi_height = params.get<int>("roi_height", 1);
		config_.hskip = params.get<int>("hskip", 1);
		config_.vskip = params.get<int>("vskip", 1);
		config_.difference_m = params.get<float>("difference_m", 0.1);
		config_.difference_c = params.get<int>("difference_c", 10);
		config_.region_threshold = params.get<float>("region_threshold", 0.005);
		config_.max_region_threshold = params.get<float>("max_region_threshold", 0.005);
		config_.frame_period = params.get<int>("frame_period", 5);
		config_.verbose = params.get<int>("verbose", 0);
		config_.showroi = params.get<int>("show_roi", 0);
	}

	GS_LOG_MSG(trace, "MotionDetectStage::Configure set the following values:");
	GS_LOG_MSG(trace, "    config_.roi_x: " + std::to_string(config_.roi_x));
	GS_LOG_MSG(trace, "    config_.roi_y: " + std::to_string(config_.roi_y));
	GS_LOG_MSG(trace, "    config_.roi_width: " + std::to_string(config_.roi_width));
	GS_LOG_MSG(trace, "    config_.roi_height: " + std::to_string(config_.roi_height));
	GS_LOG_MSG(trace, "    config_.hskip: " + std::to_string(config_.hskip));
	GS_LOG_MSG(trace, "    config_.vskip: " + std::to_string(config_.vskip));
	GS_LOG_MSG(trace, "    config_.difference_m: " + std::to_string(config_.difference_m));
	GS_LOG_MSG(trace, "    config_.difference_c: " + std::to_string(config_.difference_c));
	GS_LOG_MSG(trace, "    config_.region_threshold: " + std::to_string(config_.region_threshold));
	GS_LOG_MSG(trace, "    config_.ax_region_threshold " + std::to_string(config_.max_region_threshold));
	GS_LOG_MSG(trace, "    config_.frame_period: " + std::to_string(config_.frame_period));
	GS_LOG_MSG(trace, "    config_.verbose: " + std::to_string(config_.verbose));
	GS_LOG_MSG(trace, "    config_.showroi: " + std::to_string(config_.showroi));
}

void MotionDetectStage::Configure()
{
	GS_LOG_MSG(trace, "MotionDetectStage::Configure");

	if (gs::GolfSimClubData::kGatherClubData) {
		int final_frame_buffer_size = 1 + gs::GolfSimClubData::kNumberFramesToSaveBeforeHit + 
			gs::GolfSimClubData::kNumberFramesToSaveAfterHit;
		golf_sim::RecentFrames.resize(final_frame_buffer_size);

		GS_LOG_MSG(trace, "Circular frame buffer size re-set to: " + std::to_string(final_frame_buffer_size));
	}

	// Let's process the main stream!
	// stream_ = app_->LoresStream(&info);
	stream_ = app_->GetMainStream();
	
	if (!stream_)
		return;

	StreamInfo info = app_->GetStreamInfo(stream_);

	config_.hskip = std::max(config_.hskip, 1);
	config_.vskip = std::max(config_.vskip, 1);

	info.width /= config_.hskip;
	info.height /= config_.vskip;

	// Store ROI values as if in an image subsampled by hskip and vskip.
	roi_x_ = config_.roi_x / config_.hskip;
	roi_y_ = config_.roi_y / config_.vskip;

	GS_LOG_MSG(trace, "After decimating, config_.roi_x = " + std::to_string(config_.roi_x) + ", config_.roi_y = " + std::to_string(config_.roi_y));
	GS_LOG_MSG(trace, "roi_x_ = " + std::to_string(roi_x_) + ", roi_y_ = " + std::to_string(roi_y_));

	roi_width_ = config_.roi_width / config_.hskip;
	roi_height_ = config_.roi_height / config_.vskip;

	// config_.region_threshold is a % of pixels that have changed
	// scale it down based on the fraction the ROI is of the whole
	// TBD - does it make sense to just leave it alone instead?
	region_threshold_ = config_.region_threshold * roi_width_ * roi_height_;
	max_region_threshold_ = config_.max_region_threshold * roi_width_ * roi_height_;

	// Ensure all values are valid
	roi_x_ = std::clamp(roi_x_, 0u, info.width);
	roi_y_ = std::clamp(roi_y_, 0u, info.height);
	roi_width_ = std::clamp(roi_width_, 0u, info.width - roi_x_);
	roi_height_ = std::clamp(roi_height_, 0u, info.height - roi_y_);
	region_threshold_ = std::clamp(region_threshold_, 0u, roi_width_ * roi_height_);

	if (config_.verbose)
		LOG(1, "Sampled (vskip/hskip) Image x,y (smaller): " << info.width << "x" << info.height << " roi: (" << roi_x_ << "," << roi_y_ << ") ROI Width/height: "
						 << roi_width_ << "x" << roi_height_ << " threshold: " << region_threshold_);

	previous_frame_.resize(roi_width_ * roi_height_);

	first_time_ = true;
	motion_detected_ = false;
	detectionPaused_ = false;
	postMotionFramesToCapture_ = 0;   // Will be set later
	// TBD - Need to use the kNumberFramesToSaveBeforeHit, for example to size the circular buffer.
}

bool MotionDetectStage::Process(CompletedRequestPtr &completed_request)
{
	if (!stream_)
		return false;

	completed_request->post_process_metadata.Set("motion_detect.result", false);

	if (detectionPaused_ && postMotionFramesToCapture_ <= 0) {
		// std::cout << "Process skipping." << std::endl;
		return false;
	}

	if (config_.frame_period && completed_request->sequence % config_.frame_period)
		return false;


    libcamera::FrameBuffer *buffer = completed_request->buffers[stream_];

    BufferReadSync r(app_, buffer);

	const std::vector<libcamera::Span<uint8_t>> mem = r.Get();

    uint8_t *image = (uint8_t *)mem[0].data();

    StreamInfo info = app_->GetStreamInfo(stream_);

	// We need to protect access to first_time_, previous_frame_ and motion_detected_.
	std::lock_guard<std::mutex> lock(mutex_);

	unsigned int sampledFrameStride = info.stride * config_.vskip;

	if (first_time_)
	{
		first_time_ = false;
		// Coordinates here are relative to a sampled version of the image
		// The previous_frame_ is just a non-aligned width & height buffer 

		for (unsigned int y = 0; y < roi_height_; y++)
		{
			uint8_t *new_value_ptr = image + ((roi_y_ + y) * sampledFrameStride) + (roi_x_ * config_.hskip);
			uint8_t *old_value_ptr = &previous_frame_[0] + y * roi_width_;

			// Now traverse across the incoming frame in the x direction for this row
			for (unsigned int x = 0; x < roi_width_; x++, new_value_ptr += config_.hskip) {
				*(old_value_ptr++) = *new_value_ptr;
			}
		}

		completed_request->post_process_metadata.Set("motion_detect.result", false);

		return false;
	}

	bool local_motion_detected = false;

	// If we're in a post-motion world, assume motion has already been detected
	// TBD - we need to clean this up.  Too confusing.
	if (detectionPaused_ || postMotionFramesToCapture_ > 0) {
		GS_LOG_MSG(trace, "In post-motion mode, setting local_motion_detected to true.");
		local_motion_detected = true;
	}

	unsigned int regions = 0;

	// Count the  pixels where the difference between the new and previous values
	// exceeds the threshold. At the same time, update the previous image buffer.
	for (unsigned int y = 0; !local_motion_detected && y < roi_height_; y++)
	{
		uint8_t* new_value_ptr = image + ((roi_y_ + y) * sampledFrameStride) + (roi_x_ * config_.hskip);
		uint8_t* old_value_ptr = &previous_frame_[0] + y * roi_width_;
		for (unsigned int x = 0; x < roi_width_; x++, new_value_ptr += config_.hskip)
		{
			int new_value = *new_value_ptr;
			int old_value = *old_value_ptr;

			// LOG(1, "sampledFrameStride: " << sampledFrameStride << ". old= " << old_value << " new= " << new_value << " .New_ptr: " << (long)new_value_ptr << ".Old_ptr : " << (long)old_value_ptr << ".");


			*(old_value_ptr++) = new_value;
			regions += std::abs(new_value - old_value) > config_.difference_m * old_value + config_.difference_c;
		}

		local_motion_detected= regions >= region_threshold_;

		// Break out early if we've already figured out there's motion
		if (local_motion_detected) {
			// std::cout << "Motion detected - Regions = " << regions << ", y row value was: " << y << std::endl;
			break;
		}
	}

 	if (config_.verbose && local_motion_detected) {
		// TBD - Avoid output here to reduce latencyLOG(1, "*************  Motion " << (local_motion_detected? "detected" : "stopped"));
	}

	if (local_motion_detected && !detectionPaused_) {

		// We just now detected movement (this time through this code)

		// TBD - ** Immediately ** pulse the output - we want to do this with as little latency
		// as possible, because otherwise the ball will fly past the camera 2 FoV
		if (gs::GolfSimOptions::GetCommandLineOptions().system_mode_ != gs::kCamera1TestStandalone) {
			gs::PulseStrobe::SendExternalTrigger();
		}
		else {
			// simulate the other system sending an image back
			// TBD gs::GolfSimIpcSystem::SimulateCamera2ImageMessage();
		}

		if (config_.verbose)
			LOG(1, "Saving Image x,y: " << info.width << ", " << info.height << " .");

		// For now, as soon as we detect motion (except for a few frames) we stop recording.  This 
		// and reduces unnecessary processing overhead.
		detectionPaused_ = true;
		if (gs::GolfSimClubData::kGatherClubData ) {
			postMotionFramesToCapture_ = gs::GolfSimClubData::kNumberFramesToSaveAfterHit;
		}
		else
		{
			// No reason to collect post-hit frames if we aren't in club-strike image mode
			postMotionFramesToCapture_ = 0;
		}

		GS_LOG_MSG(trace, "Will save an additional " + std::to_string(postMotionFramesToCapture_) + " frames.");
	}

	// Ensure we don't tell the outer loop that there's motion until we've completed viewing
	// the post-motion images
	if (postMotionFramesToCapture_ > 1) {
		GS_LOG_MSG(trace, "Post-motion frames > 0 - setting result local_motion_detected to false.");
		completed_request->post_process_metadata.Set("motion_detect.result", false);
	}
	else {
		GS_LOG_MSG(trace, "No post-motion frames after this one - setting result local_motion_detected of: " + std::to_string(local_motion_detected) + ".");
		completed_request->post_process_metadata.Set("motion_detect.result", local_motion_detected);
		// Signal motion to the outside world.
		motion_detected_ = local_motion_detected;
	}

	// Save the current frame image information if we are still in a mode to be capturing these images
	// either pre- or post-motion detection

	if ( (!detectionPaused_ || postMotionFramesToCapture_ > 0) ) {

		// std::cout << "postFrames: " << std::to_string(postMotionFramesToCapture_) << std::endl;

		golf_sim::RecentFrameInfo frameInfo;

		frameInfo.requestSequence = completed_request->sequence;
		frameInfo.frameRate = completed_request->framerate;

		// If we haven't started taking any post-motion frames yet, then this is the frame
		// during which the movement was first detected.
		frameInfo.isballHitFrame = (postMotionFramesToCapture_ == gs::GolfSimClubData::kNumberFramesToSaveAfterHit);

		cv::Mat mat = cv::Mat(info.height, info.width, CV_8U, image, info.stride);

		// TBD - Move all motion processing parameters and constant to the main .json file instead of
		// using some parameters from the rpicam_apps configuration file
		if (config_.showroi) {
			cv::Scalar c_black{ 0, 0, 0 }; // black
			cv::Scalar c_green{ 170, 255, 0 }; // bright green

			// We could have different frame sizes for the "hit" frame?
			int rectWidth = frameInfo.isballHitFrame ? 2 : 2;

			cv::Scalar rectangle_color = frameInfo.isballHitFrame ? c_green : c_black;

			cv::Point startPoint = cv::Point(roi_x_ * config_.hskip, roi_y_ * config_.vskip);

			cv::Point endPoint = cv::Point((roi_x_ + roi_width_) * config_.hskip, (roi_y_ + roi_height_) * config_.vskip);

			cv::rectangle(mat, startPoint, endPoint, rectangle_color, rectWidth);
		}

		// Number the frame 
		cv::Scalar c_label{ 170, 255, 0 }; // bright green
		std::string frame_label = std::to_string(completed_request->sequence);
		int text_x = info.width - 60;
		int text_y = 25;

		cv::putText(mat, frame_label, cv::Point(text_x, text_y), cv::FONT_HERSHEY_SIMPLEX, 0.8, c_label, 2, cv::LINE_AA);

		GS_LOG_MSG(trace, "Pushing Post-Motion Frame No. " + std::to_string(postMotionFramesToCapture_) + " - Seq. No. " + std::to_string(completed_request->sequence));

		golf_sim::RecentFrames.push_back(frameInfo);

		golf_sim::RecentFrameInfo& enqueuedFrameInfo = golf_sim::RecentFrames.back();

		// Make sure that the enqueued frame info has its own Mat, as the push_back
		// might only do a shallow copy. TBD - figure this out!
		enqueuedFrameInfo.mat = mat.clone();

		if (enqueuedFrameInfo.mat.empty()) {
			GS_LOG_MSG(error, "Enqueued a null club data image");
		}

		// continue the countdown if we're post-motion
		if (postMotionFramesToCapture_ > 0) {
			postMotionFramesToCapture_--;
		}
	}

	return false;
}

static PostProcessingStage *Create(RPiCamApp *app)
{
	return new MotionDetectStage(app);
}

static RegisterStage reg(NAME, &Create);


#endif // #ifdef __unix__  // Ignore in Windows environment

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


#include "post_processing_stages/post_processing_stage.hpp"

#include <opencv2/core.hpp>
#include <opencv2/photo.hpp>
#include <opencv2/core/cvdef.h>
#include <opencv2/highgui.hpp>
#include <boost/circular_buffer.hpp>

#define SEND_XTR_PULSE
#define BUILD_VID_APP


#ifdef BUILD_VID_APP
#include "ball_watcher_image_buffer.h"
#endif


#include <libcamera/stream.h>
#include "core/rpicam_app.hpp"

#include "gs_globals.h"
#include "gs_options.h"
#include "pulse_strobe.h"
#include "logging_tools.h"
#include "gs_fsm.h"



namespace gs = golf_sim;


using Stream = libcamera::Stream;

class MotionDetectStage : public PostProcessingStage
{
public:
	MotionDetectStage(RPiCamApp *app) : PostProcessingStage(app) {}

	char const *Name() const override;

	void Read(boost::property_tree::ptree const &params) override;

	void Configure() override;

	bool Process(CompletedRequestPtr &completed_request) override;


private:
	// In the Config, dimensions are given as fractions of the image size.
	struct Config
	{
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
	} config_;

	Stream *stream_;
	// Here we convert the dimensions to pixel locations in the image, as if subsampled
	// by hskip and vskip.
	unsigned int roi_x_, roi_y_;
	unsigned int roi_width_, roi_height_;
	unsigned int region_threshold_;
	unsigned int max_region_threshold_;
	std::vector<uint8_t> previous_frame_;
	bool first_time_;
	bool motion_detected_;
	int postMotionFramesToCapture_;
	std::mutex mutex_;

	// MJLMODS
	bool detectionPaused_;
};

#define NAME "motion_detect"

char const *MotionDetectStage::Name() const
{
	return NAME;
}

void MotionDetectStage::Read(boost::property_tree::ptree const &params)
{
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

void MotionDetectStage::Configure()
{
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
	postMotionFramesToCapture_ = 0;

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

		completed_request->post_process_metadata.Set("motion_detect.result", motion_detected_);

		return false;
	}

	bool motion_detected = false;
	unsigned int regions = 0;

	// Count the  pixels where the difference between the new and previous values
	// exceeds the threshold. At the same time, update the previous image buffer.
	for (unsigned int y = 0; y < roi_height_; y++)
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

		motion_detected = regions >= region_threshold_;

		// Break out early if we've already figured out there's motion
		if (motion_detected) {
			// std::cout << "Motion detected - Regions = " << regions << ", y row value was: " << y << std::endl;
			break;
		}
	}

 	if (config_.verbose && motion_detected) {
		// LOG(1, "*************  Motion " << (motion_detected ? "detected" : "stopped"));
	}

	motion_detected_ = motion_detected;

	const int numPostMotionFramesToCapture = 1;

	if (motion_detected && !detectionPaused_) {

		// TBD - Immediately pulse the output
		if (gs::GolfSimOptions::GetCommandLineOptions().system_mode_ != gs::kCamera1TestStandalone) {
			gs::PulseStrobe::SendExternalTrigger();
		}
		else {
			// simulate the other system sending an image back
				// TBD gs::GolfSimIpcSystem::SimulateCamera2ImageMessage();
		}

		// TBD - Trigger camera, etc., shut the process down - we don't need any more frames
		std::cout << "****** motion! SeqNo = " << std::to_string(postMotionFramesToCapture_) << std::to_string(completed_request->sequence) << std::endl;
		LOG(1, "regions: " << regions << ". region_threshold_= " << region_threshold_ << ".");
		if (config_.verbose)
			LOG(1, "Saving Image x,y: " << info.width << ", " << info.height << " .");

		// For now, as soon as we detect motion (except for a few frames) we stop recording.  This preserves the prior frames in the buffer
		detectionPaused_ = true;
		postMotionFramesToCapture_ = numPostMotionFramesToCapture;
	}

	// Save the current frame, on the chance that it will be just prior to the actual
	// motion of the ball. 

	if ( (!detectionPaused_ || postMotionFramesToCapture_ > 0) ) {
		// std::cout << "postFrames: " << std::to_string(postMotionFramesToCapture_) << std::endl;

#ifdef BUILD_VID_APP


		golf_sim::RecentFrameInfo frameInfo;

		frameInfo.requestSequence = completed_request->sequence;
		frameInfo.frameRate = completed_request->framerate;

		// If we haven't started taking any post-motion frames yet, then this is the frame
		// during which the movement was first detected.
		frameInfo.isballHitFrame = (postMotionFramesToCapture_ == numPostMotionFramesToCapture);

		if (gs::PulseStrobe::kRecordAllImages) {

			cv::Mat mat = cv::Mat(info.height, info.width, CV_8U, image, info.stride);

			// MJLMOD
			if (config_.showroi) {
				cv::Scalar c1{ 0, 0, 0 }; // green?

				int rectWidth = frameInfo.isballHitFrame ? 10 : 2;

				cv::Point startPoint = cv::Point(roi_x_ * config_.hskip, roi_y_ * config_.vskip);

				cv::Point endPoint = cv::Point((roi_x_ + roi_width_) * config_.hskip, (roi_y_ + roi_height_) * config_.vskip);

				cv::rectangle(mat, startPoint, endPoint, c1, rectWidth);


			}

			// frameInfo.mat.release();
			frameInfo.mat = mat.clone();
		}

		// std::cout << "Pushing Seq No. " << std::to_string(postMotionFramesToCapture_) << std::to_string(completed_request->sequence) << std::endl;
		golf_sim::RecentFrames.push_back(frameInfo);
#endif
		// continue the countdown if we're post-motion
		if (postMotionFramesToCapture_ > 0) {
			postMotionFramesToCapture_--;
		}
	}


	completed_request->post_process_metadata.Set("motion_detect.result", motion_detected);

	return false;
}

static PostProcessingStage *Create(RPiCamApp *app)
{
	return new MotionDetectStage(app);
}

static RegisterStage reg(NAME, &Create);


#endif // #ifdef __unix__  // Ignore in Windows environment

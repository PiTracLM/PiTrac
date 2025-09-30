/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */



#include "logging_tools.h"
#include "gs_options.h"
#include "gs_config.h"
#include "gs_clubs.h"
#include "gs_camera.h"

#ifdef __unix__  // Ignore in Windows environment

#include <lgpio.h>
#include <unistd.h>
#include <thread>
#include <math.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/signalfd.h>

#else
#define NOMINMAX  // Get rid of a std::min/max compile issue
#include <windows.h>

#endif // #ifdef __unix__  // Ignore in Windows environment

#include "pulse_strobe.h"


namespace golf_sim {

	std::vector<float>  PulseStrobe::pulse_intervals_fast_ms_;
	int PulseStrobe::number_bits_for_fast_on_pulse_ = 0;

	std::vector<float>  PulseStrobe::pulse_intervals_slow_ms_;
	int PulseStrobe::number_bits_for_slow_on_pulse_ = 0;

	// Currently only true if using InnoMaker cameras
	bool PulseStrobe::kUsingActiveHighTriggerCamera = false;

	// The on-pulses for the tail repeat vector will be the same
	// as the slow on pulses
	std::vector<float>  PulseStrobe::pulse_intervals_tail_repeat_ms_;

	char* PulseStrobe::camera_slow_pulse_sequence_ = nullptr;
	char* PulseStrobe::camera_fast_pulse_sequence_ = nullptr;
	char* PulseStrobe::no_pulse_camera_sequence_ = nullptr;
	char* PulseStrobe::tail_repeat_pulse_sequence_ = nullptr;

	unsigned long PulseStrobe::camera_fast_pulse_sequence_length_ = 0;
	unsigned long PulseStrobe::camera_slow_pulse_sequence_length_ = 0;
	unsigned long PulseStrobe::tail_repeat_sequence_length_ = 0;

	int PulseStrobe::spiHandle_ = -1;
	int PulseStrobe::lggpio_chip_handle_ = -1;
	bool PulseStrobe::spiOpen_ = false;
	bool PulseStrobe::kRecordAllImages = true;
	bool PulseStrobe::gpio_system_initialized_ = false;
	int PulseStrobe::kPuttingStrobeDelayMs = 0;

	long PulseStrobe::kCam2SetupPeriodMilliseconds = 2000;
	int PulseStrobe::kNumberPrimingPulses = 12;
	int PulseStrobe::kPrimingPulseFPS = 15;
	long PulseStrobe::kPauseBeforeReadyForTriggerMicroSeconds = 100;
	int PulseStrobe::kPauseToSetUpInnoMakerExternalTriggerMilliseconds = 1000;
	int PulseStrobe::kPauseBeforeReadyForFinalPrimingPulseMs = 100;


	int PulseStrobe::kLastPulsePutterRepeats = 5;
	// Will be set when the pulse vector is set
	unsigned long PulseStrobe::last_pulse_off_time;

	// NOTE - lgpio library appears to use BCM pin numbering by default
	const int kPulseTriggerOutputPin = 25;   // This is BCM GPIO25, pin 22
	const int kRPi4GpioChipNumber = 0;
	const int kRPi5GpioChipNumber = 4;
	const int kRPi5SpiDeviceNumber = 0;
	const int kRPi5SpiDevChannel = 1;

	const int kON = 1;
	const int kOFF = 0;


	const int kShutterSpeed = 100; // microseconds
	const int kFrameRate = 5; // FPS
	const int kShutterOffset = 14; // uS

	const int kOnTimeWidth = (int)((1.0 / kFrameRate) * 1000000. - kShutterSpeed);
	const int kNumInitialPulses = 10;


	const unsigned int kBitsPerWord = 16; // 57600; // 115200; // 38400


	const int kCE0 = 5;
	const int kCE1 = 6;
	const int kMISO = 13;
	const int kMOSI = 19; // BCM numbering.  Same as GPIO25
	const int kSCLK = 12;

	// Whatever test is run, it will run for this long in seconds
	const int kTestPeriodSecs = 10; //  120;


	int PulseStrobe::AlignLengthToWordSize(int initialBufferLength, int wordSizeBits) {

		int leftOver = initialBufferLength % (wordSizeBits / 8);
		if (leftOver == 0)
			return initialBufferLength;
		else
			return initialBufferLength + (wordSizeBits / 8) - leftOver;
	}

	char* PulseStrobe::BuildPulseTrain(const unsigned long baud_rate,
									const std::vector<float>& intervals,
									int number_bits_for_on_pulse_input,
									const unsigned int kBitsPerWord, 
									unsigned long& result_length,
									bool turn_off_strobes) {

		// TBD - All this setup needs to be done prior to the triggering so as not to waste time

		// May need to alter the on-bits
		int number_bits_for_on_pulse = number_bits_for_on_pulse_input;

		double kBaudRatePulseMultiplier = 1.0;
		GolfSimConfiguration::SetConstant("gs_config.strobing.kBaudRatePulseMultiplier", kBaudRatePulseMultiplier);

		// NOTE - The actual speed will depend on the clock speed of the
		// Pi, which can vary unless you set force_turbo = 1 in boot/config
		const double bytesFor1000Ms = (baud_rate / 8.) * kBaudRatePulseMultiplier;
		GS_LOG_TRACE_MSG(trace, "bytesFor1000Ms = " + std::to_string(bytesFor1000Ms));

		static const unsigned long kMaxPulseBufferSize = 800000;   // Big enough for any reasonable pulse train

		bool kTakingOnePulsePicture = false;  // TBD - for debugging

		// A '0' at the end of the pulse sequence just creates one last pause in the last 
		// part of the sequence

		LoggingTools::Trace("pulse_interval (may be fast or slow) vector is:", intervals);

		static char buf[kMaxPulseBufferSize];

		// Make sure the array is cleared initially to be safe
		char* begin = &buf[0];
		char* end = begin + sizeof(buf);
		std::fill(begin, end, 0);

		// For now, the pulse must be 8 bits or less, each bit being about 5 uS of 'on' time
		// For example, 0b01111000 will be about a 20 uS pulse.
		// The pulse pattern must start with the 'on' bits, e.g., 11....

		// NOTE -This value must be initialized to reflect the number of 0's on the
		// right-hand side of the kPulseBitPatternInt, above.
		int remainder_bits_from_prior_pulse = 8 - number_bits_for_on_pulse;

		unsigned long current_byte = 0;
		int next_pattern_zero_bits_pad = 0;

		// TBD- REMOVE -FOR TESTING ONLY
		if ((GolfSimOptions::GetCommandLineOptions().camera_still_mode_ ||
			GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2AutoCalibrate ||
			GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2BallLocation)) {

			// Double the basic "on" pulse length, because we are only going to send one pulse
			number_bits_for_on_pulse *= 2;
			// Ensure the pulse is not so long that the ball will be over-saturated
			number_bits_for_on_pulse = std::min(15, number_bits_for_on_pulse);
			GS_LOG_TRACE_MSG(trace, "Due to still/calibration/locate mode, will send a pulse of length: " + std::to_string(number_bits_for_on_pulse));
		}

		// Invariant - current_byte is the index of the next unused byte in the buffer, and is
		// equal to the length of the in-use buffer
		for (float const& strobe_off_time_ms : intervals) {
			// Determine the on and off bits to fill the next two bytes
			unsigned char first_byte_bit_pattern, second_byte_bit_pattern;

			remainder_bits_from_prior_pulse = GetNextTwoPulseBytes(next_pattern_zero_bits_pad, 
																   number_bits_for_on_pulse,
																   first_byte_bit_pattern,
																   second_byte_bit_pattern);

			// Start with a short "on" pulse to turn on the strobe LED light
			if (turn_off_strobes) {
				GS_LOG_TRACE_MSG(trace, "Creating a dummy pulse train with no strobe-on pulses");
				buf[current_byte++] = 0;
			}
			else {
				buf[current_byte++] = first_byte_bit_pattern;
				buf[current_byte++] = second_byte_bit_pattern;
			}

			// TBD- REMOVE - FOR TESTING ONLY
			if ( (!GolfSimOptions::GetCommandLineOptions().camera_still_mode_ &&
				GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera2AutoCalibrate &&
				GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera2BallLocation) ) {

				// Then, turn off the strobe for the specified number of milliseconds.

				// Minus 1 for first pulse,  Plus 1 for the final strobe trigger.
				// Note that we need to account for the actual on-pulse bits as well
				long off_bits = (long)(std::round(((strobe_off_time_ms / 1000.0) * bytesFor1000Ms * 8.0)) - remainder_bits_from_prior_pulse) - number_bits_for_on_pulse;
				if (off_bits < 0) {
					off_bits = 0;
				}				

				int one_pulse_cycle_length_bytes = (int)std::floor(off_bits / 8);

				next_pattern_zero_bits_pad = off_bits - (one_pulse_cycle_length_bytes * 8);

				// Fill in everything after the on-pulse (and its adjacent pad byte) with 0's
				for (int i = 0; i < one_pulse_cycle_length_bytes; i++) {
					buf[current_byte++] = 0;
				}
			}
			else
			{
				// If we want just a single, simple image, then we'll just send the strobe pulse 
				// followed by a short additional amount of shutter-on time to make sure the 
				// shutter pulse is not too short.
				buf[current_byte++] = 0;

				GS_LOG_TRACE_MSG(trace, "Due to still/calibration/locate mode, will send only one pulse.");

				break;
			}

			if (current_byte > (double)kMaxPulseBufferSize * 0.9) {
				GS_LOG_MSG(error, "Pulse trigger buffer overrun.  Shutting down.  Buffer size was: " +
					std::to_string(kMaxPulseBufferSize) + ", and current strobe is: " + 
					std::to_string(strobe_off_time_ms));
				return nullptr;
			}
		}

		// Round the size of the buffer up in order to end on an even word boundary
		GS_LOG_TRACE_MSG(trace, "Initial buffer size at " + std::to_string(baud_rate) + " baud is " + std::to_string(current_byte) + " bytes.");
		unsigned long final_buffer_size = AlignLengthToWordSize(current_byte, kBitsPerWord);
		GS_LOG_TRACE_MSG(trace, "Final Buffer size is " + std::to_string(final_buffer_size) + " bytes.");

		unsigned long bytes_to_fill = final_buffer_size - current_byte;
		for (unsigned long i = 0; i < bytes_to_fill; i++) {
			buf[current_byte++] = 0;
		}

		result_length = current_byte;

		char* return_buffer = new char[result_length];
		memcpy(return_buffer, buf, result_length);

		last_pulse_off_time = (long)std::round(intervals.back());

		return return_buffer;
	}

	int PulseStrobe::GetNextTwoPulseBytes(const int next_pattern_zero_bits_pad, 
										  const int number_bits_for_on_pulse,
										  unsigned char& first_byte_bit_pattern,
										  unsigned char& second_byte_bit_pattern) {
		if (number_bits_for_on_pulse < 1) {
			GS_LOG_MSG(error, "PulseStrobe::GetNextTwoPulseBytes called with number_bits_for_fast_on_pulse < 1.");
			return -1;
		}

		// Create the default, left-justified bit-pulse pattern
		uint16_t next_bit_pattern = { 0b1000000000000000 };

		for (int b = 0; b < number_bits_for_on_pulse - 1; b++) {
			next_bit_pattern >>= 1;
			next_bit_pattern |= uint16_t(0b1000000000000000);
		}

		// Shift the on-bits to the right and fill in with the remaining 0 bits from the
		// prior pulse sequence
		next_bit_pattern >>= next_pattern_zero_bits_pad;

		// TBD - Is this byte-ordering platform/architecture-independent??
		uint16_t mask{ 0b0000000011111111 };
		second_byte_bit_pattern = (unsigned char)(next_bit_pattern & mask);
		uint16_t tmp_word = next_bit_pattern;
		tmp_word >>= 8;
		first_byte_bit_pattern = (unsigned char)tmp_word;

		int right_most_zero_bits = 16 - (next_pattern_zero_bits_pad + number_bits_for_on_pulse);

		return right_most_zero_bits;
	}

	int PulseStrobe::OpenSpi(const unsigned int baud, int wordSizeBits) {

		GS_LOG_TRACE_MSG(trace, "OpenSpi called with baud = " + std::to_string(baud) + ", word-size = " + std::to_string(wordSizeBits));

		int spi_handle = -1;

#ifdef __unix__  // Ignore in Windows environment

		if (spiOpen_) {
			GS_LOG_TRACE_MSG(trace, "Spi already opened - closing before re-opening.  Handle was: " + std::to_string(lggpio_chip_handle_));
			lgSpiClose(spiHandle_);
			spiHandle_ = -1;
			spiOpen_ = false;
		}

		// Approximate pulse lengths:
		// 38,400       12uS
		// lgGpioFlags consists of the least significant 22 bits.
		// 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
		//  b  b  b  b  b  b  R  T  n  n  n  n  W  A u2 u1 u0 p2 p1 p0  m  m
		//  bbbbbb defines the word size in bits (0-32). The default (0) sets 8 bits per word. Auxiliary SPI only.
		unsigned int lgSpiFlags = 0;

		/*** DEPRECATED - WAS FOR PIGPIO
		if (wordSizeBits == 32) {
			lgGpioFlags = 0b00000000001000000000000000000000;
		}
		else if (wordSizeBits == 16) {
			lgGpioFlags = 0b00000000000100000000000000000000;
		}
		****/
		unsigned int lgGpioChan = 1;

		int spiDevice = 0;

		// TBD - Setup flags to allow for multi-byte (32-bit) 
		// transfers
		spi_handle = lgSpiOpen(kRPi5SpiDeviceNumber, kRPi5SpiDevChannel, baud, lgSpiFlags);

		if (spi_handle < 0) {
			GS_LOG_MSG(error, "lgSpiOpen failed.  Returned" + std::to_string(spi_handle));
		}
		else {
			GS_LOG_TRACE_MSG(trace, "lgSpiOpen - handle is " + std::to_string(spi_handle));
		}
#endif // #ifdef __unix__  // Ignore in Windows environment

		spiOpen_ = true;

		return spi_handle;
	}


	

	bool PulseStrobe::SendCameraStrobeTriggerAndShutter(int lgGpioHandle, bool send_no_strobes) {

		// The pulse sequence should have been pre-computed prior to calling this
		unsigned long result_length = 0;
		char* buf = 0;

		if (send_no_strobes) {
			// DEPRECATED - REMOVE
			GS_LOG_MSG(error, "SendCameraStrobeTriggerAndShutter sending dummy strobe sequence (with no ON strobes).");
			buf = camera_fast_pulse_sequence_;
			result_length = camera_fast_pulse_sequence_length_;
		}
		else {
			if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::GsClubType::kPutter) {
				buf = camera_slow_pulse_sequence_;
				result_length = camera_slow_pulse_sequence_length_;
			}
			else {
				buf = camera_fast_pulse_sequence_;
				result_length = camera_fast_pulse_sequence_length_;
			}
		}

		if (camera_fast_pulse_sequence_length_ == 0 || 
			camera_slow_pulse_sequence_length_ == 0 || 
			buf == nullptr ) {
			GS_LOG_MSG(error, "SendCameraStrobeTriggerAndShutter called before camera_pulse_sequence was set up.");
			return false;
		}

		// For putting mode, we need to wait a bit to ensure the ball is in the frame

#ifdef __unix__  // Ignore in Windows environment

		if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::GsClubType::kPutter) {
			// TBD - CHANGES TIMING - GS_LOG_TRACE_MSG(trace, "In putting mode.  Waiting " + std::to_string(kPuttingStrobeDelayMs) + "ms before trigger.");
			usleep(1000 * kPuttingStrobeDelayMs);
		}


		// Open shutter - 
		// Note - the hardware will invert the signal to the XTR camera trigger

		// TBD - Remove for speed
		// GS_LOG_MSG(trace, "Shutter OPEN");

		// InnoMaker cameras are active high, not low
		if (kUsingActiveHighTriggerCamera) {
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kOFF);
		}
		else {
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kON);
		}

		int bytes_sent = lgSpiWrite(spiHandle_, buf, result_length);
		bool shutter_failure = false;

		if (bytes_sent != (int)result_length) {
			GS_LOG_MSG(error, "Main lgSpiWrite failed.  Returned " + std::to_string(bytes_sent) + ". Bytes were supposed to be: " + std::to_string(result_length));
			shutter_failure = true;
		}

		// Close shutter now that all of the strobe pulses should have been sent

		// TBD - Remove for speed
		// GS_LOG_MSG(trace, "Shutter SHUT");

		if (kUsingActiveHighTriggerCamera) {
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kON);
		}
		else {
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kOFF);
		}

		GS_LOG_TRACE_MSG(trace, "SendCameraStrobeTriggerAndShutter sent pulse sequence of length = " + std::to_string(camera_fast_pulse_sequence_length_) + " bytes.");


		return !shutter_failure;
#endif // #ifdef __unix__  // Ignore in Windows environment
		return true;
	}


	bool PulseStrobe::InitGPIOSystem(GsSignalCallback callback_function) {
		GS_LOG_TRACE_MSG(trace, "PulseStrobe::InitGPIOSystem");

		if (gpio_system_initialized_) {
			GS_LOG_MSG(warning, "PulseStrobe::InitGPIOSystem called more than once!  Ignoring");
			return true;
		}

		GolfSimConfiguration::SetConstant("gs_config.strobing.kCam2SetupPeriodMilliseconds", kCam2SetupPeriodMilliseconds);
		GolfSimConfiguration::SetConstant("gs_config.strobing.kNumberPrimingPulses", kNumberPrimingPulses);
		GolfSimConfiguration::SetConstant("gs_config.strobing.kPrimingPulseFPS", kPrimingPulseFPS);
		GolfSimConfiguration::SetConstant("gs_config.strobing.kPauseBeforeReadyForTriggerMicroSeconds", kPauseBeforeReadyForTriggerMicroSeconds);
		GolfSimConfiguration::SetConstant("gs_config.strobing.kPauseToSetUpInnoMakerExternalTriggerMilliseconds", kPauseToSetUpInnoMakerExternalTriggerMilliseconds);
		GolfSimConfiguration::SetConstant("gs_config.strobing.kPauseBeforeReadyForFinalPrimingPulseMs", kPauseBeforeReadyForFinalPrimingPulseMs);

		gpio_system_initialized_ = true;

	        // If this is the non-camera1 process, then no need to further initialize the GPIO system, as the
                // camera1 process would already have done so
                if (GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera1 &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera1TestStandalone &&
                    !GolfSimOptions::GetCommandLineOptions().camera_still_mode_ &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera1AutoCalibrate &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera2AutoCalibrate &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera1BallLocation &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera2BallLocation) {
			GS_LOG_MSG(trace, "PulseStrobe::InitGPIOSystem setting constants and then returning.");
			return true;

		}
	

#ifdef __unix__  // Ignore in Windows environment

		if (GolfSimConfiguration::GetPiModel() == GolfSimConfiguration::PiModel::kRPi5) {
			lggpio_chip_handle_ = lgGpiochipOpen(kRPi5GpioChipNumber);
		}
		else {
			lggpio_chip_handle_ = lgGpiochipOpen(kRPi4GpioChipNumber);
		}

		if (lggpio_chip_handle_ < 0) {
			GS_LOG_MSG(trace, "PulseStrobe::InitGPIOSystem failed to initialize (lgGpioChipOpen). Attempting with different chip number kRPi4GpioChipNumber.");
			lggpio_chip_handle_ = lgGpiochipOpen(kRPi4GpioChipNumber);
		}

		if (lggpio_chip_handle_ < 0) {
			GS_LOG_MSG(error, "PulseStrobe::InitGPIOSystem failed to initialize (lgGpioChipOpen).  Received handle: " + std::to_string(lggpio_chip_handle_) );
			return false;
		}

		if (lgGpioClaimOutput(lggpio_chip_handle_, 0, kPulseTriggerOutputPin, 0) != LG_OKAY) {
			GS_LOG_MSG(error, "PulseStrobe::InitGPIOSystem failed to ClaimOutput pin");
			return false;
		}


		const CameraHardware::CameraModel  camera_model = GolfSimCamera::kSystemSlot2CameraType;

		// Certain InnoMaker cameras may be active high, not low,  If so, activate the next line
		// kUsingActiveHighTriggerCamera = (camera_model == CameraHardware::CameraModel::InnoMakerIMX296GS_Mono);
		kUsingActiveHighTriggerCamera = false;

		// Note that the Connector Board will invert the shutter output
		if (kUsingActiveHighTriggerCamera) {
			GS_LOG_MSG(trace, "PulseStrobe::InitGPIOSystem - Will be using an active-HIGH camera");
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kON);
		}
		else {
			GS_LOG_MSG(trace, "PulseStrobe::InitGPIOSystem - Will be using an active-LOW camera");
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kOFF);
		}

		if (callback_function != nullptr) {
			/* TBD
			gpioSetSignalFunc(SIGUSR1, callback_function);
			gpioSetSignalFunc(SIGUSR2, callback_function);
			gpioSetSignalFunc(SIGINT, callback_function);
			*/
		}

#endif // #ifdef __unix__  // Ignore in Windows environment

		// Pull the pulse intervals and strobe-on times from the JSON file each time to allow changes on the fly
		GolfSimConfiguration::SetConstant("gs_config.strobing.kStrobePulseVectorDriver", pulse_intervals_fast_ms_);
		GolfSimConfiguration::SetConstant("gs_config.strobing.kStrobePulseVectorPutter", pulse_intervals_slow_ms_);
		GolfSimConfiguration::SetConstant("gs_config.strobing.kDynamicFollowOnPulseVectorPutter", pulse_intervals_tail_repeat_ms_);

		// We generally want longer pulses in the optically-noisy comparison environment
		if (GolfSimOptions::GetCommandLineOptions().lm_comparison_mode_) {
			GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvNumber_bits_for_fast_on_pulse_", number_bits_for_fast_on_pulse_);
		}
		else {
			GolfSimConfiguration::SetConstant("gs_config.strobing.number_bits_for_fast_on_pulse_", number_bits_for_fast_on_pulse_);
		}

		GolfSimConfiguration::SetConstant("gs_config.strobing.number_bits_for_slow_on_pulse_", number_bits_for_slow_on_pulse_);

		long kBaudRateForFastPulses;
		long kBaudRateForSlowPulses;
		GolfSimConfiguration::SetConstant("gs_config.strobing.kBaudRateForFastPulses", kBaudRateForFastPulses);
		GolfSimConfiguration::SetConstant("gs_config.strobing.kBaudRateForSlowPulses", kBaudRateForSlowPulses);

		// Pre-compute the pulse sequences to save time later
		GS_LOG_TRACE_MSG(trace, "Building Fast pulse sequence.");
		camera_fast_pulse_sequence_ = PulseStrobe::BuildPulseTrain((unsigned long)kBaudRateForFastPulses, pulse_intervals_fast_ms_, number_bits_for_fast_on_pulse_,
														kBitsPerWord, camera_fast_pulse_sequence_length_, false);
		GS_LOG_TRACE_MSG(trace, "Building Slow pulse sequence.");
		camera_slow_pulse_sequence_ = PulseStrobe::BuildPulseTrain((unsigned long)kBaudRateForSlowPulses, pulse_intervals_slow_ms_, number_bits_for_slow_on_pulse_,
														kBitsPerWord, camera_slow_pulse_sequence_length_, false);
		GS_LOG_TRACE_MSG(trace, "Building follow-on pulse sequence.");
		tail_repeat_pulse_sequence_ = PulseStrobe::BuildPulseTrain((unsigned long)kBaudRateForSlowPulses, pulse_intervals_tail_repeat_ms_, number_bits_for_slow_on_pulse_,
			kBitsPerWord, tail_repeat_sequence_length_, false);

		if (camera_fast_pulse_sequence_ == nullptr || camera_slow_pulse_sequence_ == nullptr) {
			GS_LOG_MSG(error, "Failed to build pulse sequences.");
			return false;
		}

		return true;
	}

	bool PulseStrobe::DeinitGPIOSystem() {
#ifdef __unix__  // Ignore in Windows environment
		GS_LOG_TRACE_MSG(trace, "PulseStrobe::DeinitGPIOSystem.");

		if (spiOpen_) {
			lgSpiClose(spiHandle_);
			spiHandle_ = -1;
			spiOpen_ = false;
		}

		lgGpiochipClose(lggpio_chip_handle_);
		lggpio_chip_handle_ = -1;
		std::this_thread::yield();

#endif // #ifdef __unix__  // Ignore in Windows environment

		gpio_system_initialized_ = false;
		return true;
	}

	void PulseStrobe::SendOnOffPulse(long length_us) {
#ifdef __unix__  // Ignore in Windows environment

		if (!kUsingActiveHighTriggerCamera) {
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kON);
			usleep(length_us);
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kOFF);
		} else {
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kOFF);
			usleep(length_us);
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kON);
		}
#endif // #ifdef __unix__  // Ignore in Windows environment
	}

	bool PulseStrobe::SendCameraPrimingPulses(bool use_high_speed) {

#ifdef __unix__  // Ignore in Windows environment

		// Re-establish putting delay each time to make it easier to adjust on the fly
		GolfSimConfiguration::SetConstant("gs_config.strobing.kPuttingStrobeDelayMs", kPuttingStrobeDelayMs);

		// Make sure we are sending pulses at a known speed.  In this case, in the "fast" setting
		unsigned int baud_rate = 0;

		GolfSimConfiguration::SetConstant("gs_config.strobing.kBaudRateForFastPulses", baud_rate);

		// This will determine how fast the later pulses are sent
		spiHandle_ = OpenSpi(baud_rate, kBitsPerWord);

		if (spiHandle_ < 0) {
			GS_LOG_MSG(error, "lgGpioOpen failed.");
			return false;
		}

		// Make sure the camera 2 system has had a chance to receive the IPC "arm" message
		GolfSimConfiguration::SetConstant("gs_config.strobing.kCam2SetupPeriodMilliseconds", kCam2SetupPeriodMilliseconds);

		long wait_time_in_ms = kCam2SetupPeriodMilliseconds;

		GS_LOG_TRACE_MSG(trace, "Waiting " + std::to_string(wait_time_in_ms) + " milliseconds for the Camera2 system to prepare its camera.");
		usleep(wait_time_in_ms * 1000);
		GS_LOG_TRACE_MSG(trace, "Assuming camera 2 is ready.  Sending PRIMING pulses...");

		// Generate a short low pulse (aka shutter speed) at a
		// relatively low frame rate as priming pulses
		const int kShutterSpeed = 100; // microseconds
		const int kShutterOffset = 14; // uS

		const int kOnTimeWidth = (int)((1.0 / kPrimingPulseFPS) * 1000000. - kShutterSpeed);

		GS_LOG_TRACE_MSG(trace, "Priming Pulse kOffTimeWidth = " + std::to_string(kShutterSpeed));
		GS_LOG_TRACE_MSG(trace, "Priming Pulse kOnTimeWidth =  " + std::to_string(kOnTimeWidth));

		// Send the priming pulses
		for (int i = 0; i < kNumberPrimingPulses; i++) {
			GS_LOG_TRACE_MSG(trace, "Sent priming pulse");
			SendOnOffPulse(kShutterSpeed - kShutterOffset);
			usleep(kOnTimeWidth);

			// If we are running an InnoMaker camera, the camera 2 system needs a moment
			// to (re)setup the external trigger after the first image is received
			const CameraHardware::CameraModel  camera_model = GolfSimCamera::kSystemSlot2CameraType;

			// The camera 2 system only needs to set up an InnoMaker camera external trigger once after the camera has started running
			if (i == 0 && camera_model == CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
				GS_LOG_TRACE_MSG(trace, "Pausing after first trigger for camera 2 system to setup external triggering for InnoMaker Camera.  Will delay:  " + std::to_string(kPauseToSetUpInnoMakerExternalTriggerMilliseconds) + " mS.");				usleep(kPauseToSetUpInnoMakerExternalTriggerMilliseconds);
				usleep(kPauseToSetUpInnoMakerExternalTriggerMilliseconds * 1000);
			}
		}

		GS_LOG_TRACE_MSG(trace, "Sent " + std::to_string(kNumberPrimingPulses) + " initial priming pulses.  About to pause for " + 
				std::to_string(kPauseBeforeReadyForFinalPrimingPulseMs) + " milliSeconds before sending penultimate priming pulse.");

		// Wait a little longer to make sure the camera 2 system has ended its period looking for priming pulses --
		// The next pulses will cause the camera 2 system to ready itself for the REAL shot trigger
		usleep(kPauseBeforeReadyForFinalPrimingPulseMs * 1000);

		// This next priming pulses gets the camera2 state machine ready to take an actual image
		SendOnOffPulse(kShutterSpeed - kShutterOffset);

		GS_LOG_TRACE_MSG(trace, "Sent final priming pulse. Camera 2 should be primed at this point.");

		// Deal with a pre-image exposure if we need to (mostly deprecated - didn't work well)
		GolfSimConfiguration::SetConstant("gs_config.ball_exposure_selection.kUsePreImageSubtraction", 
												GolfSimCamera::kUsePreImageSubtraction);

		if (GolfSimCamera::kUsePreImageSubtraction) {
			GS_LOG_TRACE_MSG(trace, "Sent last priming pulse before pre-image.");

			long kPauseBeforeSendingPreImageTriggerMs = 0;
			GolfSimConfiguration::SetConstant("gs_config.strobing.kPauseBeforeSendingPreImageTriggerMs", kPauseBeforeSendingPreImageTriggerMs);
			usleep(kPauseBeforeSendingPreImageTriggerMs * 1000);

			SendCameraStrobeTriggerAndShutter(lggpio_chip_handle_);
			GS_LOG_TRACE_MSG(trace, "Sent pre-image trigger.");

			long kPauseBeforeSendingImageFlushMs = 0;
			GolfSimConfiguration::SetConstant("gs_config.strobing.kPauseBeforeSendingImageFlushMs", kPauseBeforeSendingImageFlushMs);
			usleep(kPauseBeforeSendingImageFlushMs * 1000);

			// This acts as a flush, and it forces the actual image to be received and processed
			SendOnOffPulse(kShutterSpeed - kShutterOffset);
			GS_LOG_TRACE_MSG(trace, "Sent pre-image flush.");

			// It will take the camera2 system a moment to package up the pre-image and send it to the object broker and to the
			// camera1 system (the one executing this code).  Give it a chance
			long kPauseAfterSendingPreImageTriggerMs = 0;
			GolfSimConfiguration::SetConstant("gs_config.strobing.kPauseAfterSendingPreImageTriggerMs", kPauseAfterSendingPreImageTriggerMs);
			usleep(kPauseAfterSendingPreImageTriggerMs * 1000);
		}

		// Set the final baud rate
		if (use_high_speed) {
			GolfSimConfiguration::SetConstant("gs_config.strobing.kBaudRateForFastPulses", baud_rate);
		}
		else {
			GolfSimConfiguration::SetConstant("gs_config.strobing.kBaudRateForSlowPulses", baud_rate);
		}

		GS_LOG_TRACE_MSG(trace, "Setting baud rate to " + std::to_string(baud_rate));
		spiHandle_ = OpenSpi(baud_rate, kBitsPerWord);

		if (spiHandle_ < 0) {
			GS_LOG_MSG(error, "spiHandle_ failed.");
			return false;
		}

		// The camera should be ready to receive the 'real' external trigger pulse at this point

#endif // #ifdef __unix__  // Ignore in Windows environment

		return true;
	}

	bool PulseStrobe::SendExternalTrigger() {

#ifdef __unix__  // Ignore in Windows environment

		// GS_LOG_TRACE_MSG(trace, "Sent final camera trigger(s) and strobe pulses.");
		SendCameraStrobeTriggerAndShutter(lggpio_chip_handle_);

		if (golf_sim::GolfSimCamera::kCameraRequiresFlushPulse) {

			GS_LOG_TRACE_MSG(trace, "Waiting a moment to send flush trigger.");

			long kPauseBeforeSendingImageFlushMs = 0;
			GolfSimConfiguration::SetConstant("gs_config.strobing.kPauseBeforeSendingImageFlushMs", kPauseBeforeSendingImageFlushMs);
			usleep(kPauseBeforeSendingImageFlushMs * 1000);


			GS_LOG_TRACE_MSG(trace, "Sending additional trigger to flush last frame.");
			SendOnOffPulse(10000);
		}
#endif
		return true;
	}



	const std::vector<float> PulseStrobe::GetPulseIntervals() {

		std::vector<float> intervals;

		if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::GsClubType::kPutter) {
			intervals = pulse_intervals_slow_ms_;
		}
		else {
			intervals = pulse_intervals_fast_ms_;
		}

		if (intervals.empty()) {
			GS_LOG_TRACE_MSG(error, "GetPulseIntervals: pulse intervals vector is empty. Check JSON configuration or InitGPIOSystem() call.");
			return intervals;
		}

		if (intervals[intervals.size()-1] > 0.0001) {
			GS_LOG_TRACE_MSG(warning, "Expected last pulse interval to be 0.  Check .json file.");
		}

		return intervals;
	}
} // namespace golf_sim



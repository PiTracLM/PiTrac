/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Represents a vector of timed pulses used for strobing in the system
// also contains a number related helper methods.

#pragma once


#include <vector>

#include "logging_tools.h"


namespace golf_sim {

	class PulseStrobe {

		typedef void (*GsSignalCallback) (int signal_number);

	public:

		static bool gpio_system_initialized_;

		static bool kUsingActiveHighTriggerCamera;
		static int kPuttingStrobeDelayMs;
		static long kCam2SetupPeriodMilliseconds;
		static int kNumberPrimingPulses;
		static int kPrimingPulseFPS;
		static long kPauseBeforeReadyForTriggerMicroSeconds;
		static int kPauseToSetUpInnoMakerExternalTriggerMilliseconds;
		static int kPauseBeforeReadyForFinalPrimingPulseMs;

		static bool InitGPIOSystem(GsSignalCallback callback_function = nullptr);
		static bool DeinitGPIOSystem();

		// Example output:
		//	    pulse sequence:  { 3,      5,       11,      15,       20,   0 }
		//      ratio sequence:  {    1.67,    2.2       2.5      1.33         }
		static std::vector<double> GetPulseRatios();

		// Caller owns the byte buffer that is returned
		static char* BuildPulseTrain(const unsigned long baud_rate, 
									 const std::vector<float>& intervals,
									 const int number_bits_for_on_pulse,
									 const unsigned int kBitsPerWord, 
									 unsigned long& result_length,
									 bool turn_off_strobes = false);

		static int GetNextTwoPulseBytes(const int next_pattern_zero_bits_pad,
										const int number_bits_for_on_pulse,
										unsigned char& first_byte_bit_pattern,
										unsigned char& second_byte_bit_pattern);

		static bool SendCameraPrimingPulses(bool use_high_speed);
		static bool SendExternalTrigger();

		// Sends the already-created pulse buffer to the strobes via SPI, and also
		// opens the shutter while the pulses are sent.
		// requires the camera_fast_pulse_sequence_ to have already been created by
		// the BuildPulseTrain function.
		// send_no_strobes can be set to true in order to get a "before" or "pre" image
		// that shows just the ambient light.
		static bool SendCameraStrobeTriggerAndShutter(int spiHandle, bool send_no_strobes = false);

		// Returns a handle to the now - open SPI channel,
		// or a negative value on failure
		static int OpenSpi(const unsigned int baud, int wordSizeBits = 8);

		// Deprecated, but keep around for now...

		static bool SendCameraSpiPrimingPulses();

		static bool SendSpiMsg(const unsigned int baud,
			const unsigned long repeats,
			char* buf,
			int bufferLength);

		static bool SendCameraTrigger(int handle);

		static const std::vector<float> GetPulseIntervals();

		static void SendOnOffPulse(long length_us);

		static bool kRecordAllImages;

	protected:

		// This vector describes the amount of time to send 0's after sending a strobe
		// pulse.  The last pulse should be of size 0 to ensure the pulse sequence ends
		// with the pulse turned OFF.
		// Should be setup in the BuildPulseTrain function
		// Pulse intervals must be > 0.0 for all but the last pulse
		static std::vector<float> pulse_intervals_fast_ms_;
		static std::vector<float> pulse_intervals_slow_ms_;
		static std::vector<float> pulse_intervals_tail_repeat_ms_;

		static int number_bits_for_fast_on_pulse_;
		static int number_bits_for_slow_on_pulse_;

		// This is the buffer that will be written out (bit-banged) to the SPI channel
		static char* camera_slow_pulse_sequence_;
		static char* camera_fast_pulse_sequence_;
		static char* no_pulse_camera_sequence_;  // Will be all 0's, but the same length as the 'real' pulse sequence
		static unsigned long camera_fast_pulse_sequence_length_;
		static unsigned long camera_slow_pulse_sequence_length_;

		static char* tail_repeat_pulse_sequence_;
		static unsigned long tail_repeat_sequence_length_;

		static int spiHandle_;
		static bool spiOpen_;
		static int lggpio_chip_handle_;


		// The number of times the last (usually quite long pulse-off interval)
		// will be repeated after the earlier part of the pulse pattern
		static int kLastPulsePutterRepeats;
		static unsigned long last_pulse_off_time;

		static int AlignLengthToWordSize(int initialBufferLength, int wordSizeBits);
	};

}


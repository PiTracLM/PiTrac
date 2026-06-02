/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Timed-pulse strobe vector plus helpers.

#pragma once


#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "logging_tools.h"


namespace golf_sim {

	class PicoStrobeClient;

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

		// e.g. pulse { 3, 5, 11, 15, 20, 0 } -> ratio { 1.67, 2.2, 2.5, 1.33 }
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

		// Pico autonomous-trigger bridge. When open, the Pico owns the strobe +
		// cam2 external trigger; legacy SPI/BCM fire paths stand down and the FSM
		// arms-and-waits. Forward to the protected pico_client_.
		static bool IsPicoActive();
		static bool ArmPicoForShot();
		static bool DisarmPico();
		static uint64_t PicoEventCount();

		// Pushes out the Pico's auto-disarm deadline so the arm lives as long as
		// the LM does, not on a fixed per-shot timer.
		static bool PicoHeartbeat();

		// Firmware auto-disarms kPicoArmTimeoutMs after the last Arm()/Heartbeat();
		// FSM pings every kPicoHeartbeatIntervalMs. Interval << timeout so one
		// dropped ping won't trip the safety disarm, but a dead LM still disarms.
		static constexpr long kPicoHeartbeatIntervalMs = 1000;
		static constexpr long kPicoArmTimeoutMs = 3000;

		// Sends the pre-built pulse buffer via SPI and holds the shutter open for
		// its duration. Requires camera_fast_pulse_sequence_ built by BuildPulseTrain.
		// send_no_strobes=true yields an ambient-only "pre" image.
		static bool SendCameraStrobeTriggerAndShutter(int spiHandle, bool send_no_strobes = false);

		// Returns the open SPI handle, negative on failure.
		static int OpenSpi(const unsigned int baud, int wordSizeBits = 8);

		// Deprecated, kept for now.
		static bool SendCameraSpiPrimingPulses();

		static bool SendSpiMsg(const unsigned int baud,
			const unsigned long repeats,
			char* buf,
			int bufferLength);

		static bool SendCameraTrigger(int handle);

		static const std::vector<float> GetPulseIntervals();

		static void SendOnOffPulse(long length_us);

		static bool kRecordAllImages;

		// Cleared by cam2's event loop on entry, set once it exits the
		// priming/quiesce window. gs_fsm waits on this before arming cam1 so a fast
		// hit can't fire the trigger while cam2 is still ignoring it.
		static std::atomic<bool> cam2_ready_for_final_trigger_;

	protected:

		// Off-time (ms) after each strobe pulse; built in BuildPulseTrain. Last entry
		// must be 0 so the sequence ends strobe-OFF; all others must be > 0.0.
		static std::vector<float> pulse_intervals_fast_ms_;
		static std::vector<float> pulse_intervals_slow_ms_;

		static int number_bits_for_fast_on_pulse_;
		static int number_bits_for_slow_on_pulse_;

		// Buffers bit-banged out to the SPI channel
		static char* camera_slow_pulse_sequence_;
		static char* camera_fast_pulse_sequence_;
		static char* no_pulse_camera_sequence_;  // all 0's, same length as the real sequence
		static unsigned long camera_fast_pulse_sequence_length_;
		static unsigned long camera_slow_pulse_sequence_length_;

		static int spiHandle_;
		static bool spiOpen_;
		static int lggpio_chip_handle_;

		// When non-null and open, strobe trigger + on/off pulse paths route through
		// USB-CDC + BCM 26 instead of the legacy SPI + GPIO 10 path.
		static std::unique_ptr<PicoStrobeClient> pico_client_;

		static int AlignLengthToWordSize(int initialBufferLength, int wordSizeBits);
	};

}


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
#include <chrono>
#include <math.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/signalfd.h>

#else
#define NOMINMAX  // Get rid of a std::min/max compile issue
#include <windows.h>

#endif // #ifdef __unix__  // Ignore in Windows environment

#include "pulse_strobe.h"
#include "pico_strobe_client.h"

#include <cstdlib>


namespace golf_sim {

	std::atomic<bool> PulseStrobe::cam2_ready_for_final_trigger_{false};

	std::vector<float>  PulseStrobe::pulse_intervals_fast_ms_;
	int PulseStrobe::number_bits_for_fast_on_pulse_ = 0;

	std::vector<float>  PulseStrobe::pulse_intervals_slow_ms_;
	int PulseStrobe::number_bits_for_slow_on_pulse_ = 0;

	// True for Pi and InnoMaker cameras. Set false only on the V1 Connector board,
	// which inverts the external shutter (XTR) signal; V2 does not.
	bool PulseStrobe::kUsingActiveHighTriggerCamera = true;

	char* PulseStrobe::camera_slow_pulse_sequence_ = nullptr;
	char* PulseStrobe::camera_fast_pulse_sequence_ = nullptr;
	char* PulseStrobe::no_pulse_camera_sequence_ = nullptr;

	unsigned long PulseStrobe::camera_fast_pulse_sequence_length_ = 0;
	unsigned long PulseStrobe::camera_slow_pulse_sequence_length_ = 0;

	int PulseStrobe::spiHandle_ = -1;
	int PulseStrobe::lggpio_chip_handle_ = -1;
	bool PulseStrobe::spiOpen_ = false;
	bool PulseStrobe::kRecordAllImages = true;
	bool PulseStrobe::gpio_system_initialized_ = false;
	std::unique_ptr<PicoStrobeClient> PulseStrobe::pico_client_;

	// Hooks declared in pico_strobe_client.cpp; route wire-level events to
	// pitrac.log. Test target stubs these out in test_pico_strobe_client.cpp.
	void PicoLogTrace(const std::string& msg) {
		GS_LOG_TRACE_MSG(trace, msg);
	}
	void PicoLogWarn(const std::string& msg) {
		GS_LOG_MSG(warning, msg);
	}

	bool PulseStrobe::IsPicoActive() {
		return pico_client_ && pico_client_->IsOpen();
	}

	bool PulseStrobe::ArmPicoForShot() {
		if (!pico_client_) return false;
		// Select the club's strobe pattern before arming so autonomous fire uses the
		// putter vector on putts. The legacy SendCameraStrobeTriggerAndShutter does
		// this per-shot, but it's gated off in Pico mode, so do it here.
		const PicoStrobeClient::ClubProfile profile =
			(GolfSimClubs::GetCurrentClubType() == GolfSimClubs::GsClubType::kPutter)
				? PicoStrobeClient::ClubProfile::kPutter
				: PicoStrobeClient::ClubProfile::kDriver;
		if (!pico_client_->SelectClubProfile(profile)) {
			GS_LOG_MSG(warning, "Pico club-profile select failed; arming on the previously-active strobe pattern");
		}
		// Firmware refuses to arm while the room is louder than threshold/4 (arm-quiet
		// gate). Right after ball placement the player is still settling, so retry for
		// ~2s to catch a quiet window -- lets the threshold sit low enough to detect
		// the quiet strike yet still arm.
		constexpr int kArmAttempts = 12;
		for (int attempt = 0; attempt < kArmAttempts; ++attempt) {
			if (pico_client_->Arm()) {
				if (attempt > 0) {
					GS_LOG_MSG(info, "Pico armed after " + std::to_string(attempt + 1) + " attempt(s) (waited for a quiet window).");
				}
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
		return false;
	}

	bool PulseStrobe::DisarmPico() {
		return pico_client_ && pico_client_->Disarm();
	}

	bool PulseStrobe::PicoHeartbeat() {
		return pico_client_ && pico_client_->Heartbeat();
	}

	uint64_t PulseStrobe::PicoEventCount() {
		return pico_client_ ? pico_client_->LastEventCount() : 0;
	}

	bool PulseStrobe::FirePicoForStill() {
		// Reuses the autonomous fire path: SendCameraStrobeTriggerAndShutter's Pico
		// branch selects the club train and pulses GP9 FIRE_IN (strobe + cam2 XTR
		// coincident), which bumps event_count. spiHandle is ignored in Pico mode.
		if (!IsPicoActive()) return false;
		return SendCameraStrobeTriggerAndShutter(lggpio_chip_handle_);
	}

	int PulseStrobe::kPuttingStrobeDelayMs = 0;

	long PulseStrobe::kCam2SetupPeriodMilliseconds = 2000;
	int PulseStrobe::kNumberPrimingPulses = 12;
	int PulseStrobe::kPrimingPulseFPS = 15;
	long PulseStrobe::kPauseBeforeReadyForTriggerMicroSeconds = 100;
	int PulseStrobe::kPauseToSetUpInnoMakerExternalTriggerMilliseconds = 1000;
	int PulseStrobe::kPauseBeforeReadyForFinalPrimingPulseMs = 100;



	// lgpio uses BCM pin numbering by default
	const int kPulseTriggerOutputPin = 25;   // BCM GPIO25, header pin 22
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

		// TBD - do this setup before triggering to avoid wasting time

		int number_bits_for_on_pulse = number_bits_for_on_pulse_input;

		double kBaudRatePulseMultiplier = 1.0;
		GolfSimConfiguration::SetConstant("gs_config.strobing.kBaudRatePulseMultiplier", kBaudRatePulseMultiplier);

		// Actual speed tracks the Pi clock, which varies unless force_turbo=1 in boot/config
		const double bytesFor1000Ms = (baud_rate / 8.) * kBaudRatePulseMultiplier;
		GS_LOG_TRACE_MSG(trace, "bytesFor1000Ms = " + std::to_string(bytesFor1000Ms));

		static const unsigned long kMaxPulseBufferSize = 800000;   // Big enough for any reasonable pulse train

		bool kTakingOnePulsePicture = false;  // TBD - for debugging

		// A trailing 0 in the sequence is just one final pause

		LoggingTools::Trace("pulse_interval (may be fast or slow) vector is:", intervals);

		static char buf[kMaxPulseBufferSize];

		char* begin = &buf[0];
		char* end = begin + sizeof(buf);
		std::fill(begin, end, 0);

		// Pulse must be <= 8 bits, each bit ~5 uS of 'on' time (0b01111000 ~ 20 uS).
		// Pattern must start with the 'on' bits, e.g. 11....

		// Must reflect the count of trailing 0's on the right of the pulse pattern.
		int remainder_bits_from_prior_pulse = 8 - number_bits_for_on_pulse;

		unsigned long current_byte = 0;
		int next_pattern_zero_bits_pad = 0;

		// TBD- REMOVE -FOR TESTING ONLY
		if ((GolfSimOptions::GetCommandLineOptions().camera_still_mode_ ||
			GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2AutoCalibrate ||
			GolfSimOptions::GetCommandLineOptions().system_mode_ == SystemMode::kCamera2BallLocation)) {

			// Single pulse, so double its length, but cap at 15 to avoid over-saturating the ball
			number_bits_for_on_pulse *= 2;
			number_bits_for_on_pulse = std::min(15, number_bits_for_on_pulse);
			GS_LOG_TRACE_MSG(trace, "Due to still/calibration/locate mode, will send a pulse of length: " + std::to_string(number_bits_for_on_pulse));
		}

		// Invariant: current_byte indexes the next unused byte == in-use buffer length
		for (float const& strobe_off_time_ms : intervals) {
			unsigned char first_byte_bit_pattern, second_byte_bit_pattern;

			remainder_bits_from_prior_pulse = GetNextTwoPulseBytes(next_pattern_zero_bits_pad, 
																   number_bits_for_on_pulse,
																   first_byte_bit_pattern,
																   second_byte_bit_pattern);

			// Lead with a short "on" pulse to fire the strobe LED
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

				// Strobe off for strobe_off_time_ms, less the prior pad and the on-pulse bits
				long off_bits = (long)(std::round(((strobe_off_time_ms / 1000.0) * bytesFor1000Ms * 8.0)) - remainder_bits_from_prior_pulse) - number_bits_for_on_pulse;
				if (off_bits < 0) {
					off_bits = 0;
				}				

				int one_pulse_cycle_length_bytes = (int)std::floor(off_bits / 8);

				next_pattern_zero_bits_pad = off_bits - (one_pulse_cycle_length_bytes * 8);

				// Zero-fill everything after the on-pulse (and its pad byte)
				for (int i = 0; i < one_pulse_cycle_length_bytes; i++) {
					buf[current_byte++] = 0;
				}
			}
			else
			{
				// Single image: one strobe pulse plus a little extra shutter-on time so
				// the shutter pulse isn't too short.
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

		// Round buffer size up to an even word boundary
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

		// Left-justified on-bit pattern
		uint16_t next_bit_pattern = { 0b1000000000000000 };

		for (int b = 0; b < number_bits_for_on_pulse - 1; b++) {
			next_bit_pattern >>= 1;
			next_bit_pattern |= uint16_t(0b1000000000000000);
		}

		// Shift on-bits right by the 0-bit pad left over from the prior pulse
		next_bit_pattern >>= next_pattern_zero_bits_pad;

		// TBD - is this byte-ordering platform-independent?
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

		// ~pulse length: 38,400 baud -> 12uS. lgGpioFlags = low 22 bits:
		// 21..0 = bbbbbb R T nnnn W A u2 u1 u0 p2 p1 p0 m m
		//  bbbbbb = word size in bits (0-32); 0 -> 8 bits/word. Auxiliary SPI only.
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

		// TBD - flags for multi-byte (32-bit) transfers
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

		if (pico_client_ && pico_client_->IsOpen()) {
			if (send_no_strobes) return true;
			// Fire the club's pattern. Pico keeps the last selected train, so this
			// only re-sends config on a club change.
			const PicoStrobeClient::ClubProfile profile =
				(GolfSimClubs::GetCurrentClubType() == GolfSimClubs::GsClubType::kPutter)
					? PicoStrobeClient::ClubProfile::kPutter
					: PicoStrobeClient::ClubProfile::kDriver;
			// On re-push failure the Pico still holds the prior train, so the shot
			// fires on the wrong pattern. Fire anyway (losing it is worse), loudly.
			if (!pico_client_->SelectClubProfile(profile)) {
				GS_LOG_MSG(warning, "Pico club-profile select failed; firing on the previously-active strobe pattern");
			}
			return pico_client_->FireWithShutter();
		}

		// Pulse sequence must have been pre-computed before this call
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

		// Putting mode: wait so the ball is in frame before triggering

#ifdef __unix__  // Ignore in Windows environment

		if (GolfSimClubs::GetCurrentClubType() == GolfSimClubs::GsClubType::kPutter) {
			// TBD - CHANGES TIMING - GS_LOG_TRACE_MSG(trace, "In putting mode.  Waiting " + std::to_string(kPuttingStrobeDelayMs) + "ms before trigger.");
			usleep(1000 * kPuttingStrobeDelayMs);
		}


		// Open shutter. V1 Connector Board inverts the XTR trigger signal.
		// Current cameras are active-low, so kOFF opens the shutter.
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

		// Close shutter now the strobe pulses are sent
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

	        // Non-camera1 processes skip GPIO init; the camera1 process owns it.
                if (GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera1 &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera1TestStandalone &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kTest &&
                    !GolfSimOptions::GetCommandLineOptions().camera_still_mode_ &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera1AutoCalibrate &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera2AutoCalibrate &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera1BallLocation &&
                    GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kCamera2BallLocation) {
			GS_LOG_MSG(trace, "PulseStrobe::InitGPIOSystem setting constants and then returning.");
			return true;

		}


#ifdef __unix__  // Ignore in Windows environment

		// Test mode needs only pulse timing, not GPIO hardware
		if (GolfSimOptions::GetCommandLineOptions().system_mode_ != SystemMode::kTest) {
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

			// PITRAC_PICO_ENABLED: legacy=skip Pico, required=fail loudly on open
			// failure, auto=probe and fall through on miss.
			{
				const char* gate_env = std::getenv("PITRAC_PICO_ENABLED");
				std::string gate = gate_env ? gate_env : "auto";
				if (gate != "legacy") {
					const char* dev_env = std::getenv("PITRAC_PICO_DEVICE");
					std::string device = dev_env ? dev_env : "/dev/ttyACM0";

					bool should_open = (gate == "required") || PicoStrobeClient::Probe(device);
					if (should_open) {
						pico_client_ = std::make_unique<PicoStrobeClient>(lggpio_chip_handle_);
						if (!pico_client_->Open(device)) {
							pico_client_.reset();
							if (gate == "required") {
								GS_LOG_MSG(error, "PITRAC_PICO_ENABLED=required but Pico open failed: " + device);
								return false;
							}
							GS_LOG_MSG(warning, "Pico probe succeeded but Open failed: " + device + " -- falling back to legacy strobe. cam2 will NOT trigger if its XTR is wired to the Pico.");
						} else {
							GS_LOG_MSG(trace, "PicoStrobeClient open on " + device);
							// Pulse trains staged below, after the per-club interval
							// vectors and on-pulse widths load from config.
						}
					} else if (gate == "auto") {
						// A silent legacy fallback on probe miss once hid a mis-wired
						// cam2 trigger for hours. Say it out loud.
						GS_LOG_MSG(warning, "PITRAC_PICO_ENABLED=auto but no Pico answered on " + device + " -- falling back to legacy strobe. cam2 will NOT trigger if its XTR is wired to the Pico.");
					}
				}
			}

			// Active-high setting depends on the configured board version
			int kConnectionBoardVersionIntValue = 0;
			GolfSimConfiguration::SetConstant("gs_config.strobing.kConnectionBoardVersion", kConnectionBoardVersionIntValue);
			GolfSimConfiguration::ConnectionBoardType kConnectionBoardVersion = (GolfSimConfiguration::ConnectionBoardType)kConnectionBoardVersionIntValue;

			// Drives whether this class inverts the shutter signal; V1 board inverts.
			kUsingActiveHighTriggerCamera = (kConnectionBoardVersion == GolfSimConfiguration::ConnectionBoardType::kVersion1_0) ? false : true;

			if (kUsingActiveHighTriggerCamera) {
				GS_LOG_MSG(trace, "PulseStrobe::InitGPIOSystem - Will be using an active-HIGH camera");
				lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kON);
			}
			else {
				GS_LOG_MSG(trace, "PulseStrobe::InitGPIOSystem - Will be using an active-LOW camera");
				lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kOFF);
			}

			// Enclosure version sets the auto-calibration target ball position relative to the cameras
			int kEnclosureVersionIntValue = 0;
			GolfSimConfiguration::SetConstant("gs_config.strobing.kEnclosureVersion", kEnclosureVersionIntValue);
			GolfSimConfiguration::EnclosureType knclosureVersion = (GolfSimConfiguration::EnclosureType)kEnclosureVersionIntValue;

			if (callback_function != nullptr) {
				/* TBD
				gpioSetSignalFunc(SIGUSR1, callback_function);
				gpioSetSignalFunc(SIGUSR2, callback_function);
				gpioSetSignalFunc(SIGINT, callback_function);
				*/
			}
		} else {
			GS_LOG_MSG(trace, "PulseStrobe::InitGPIOSystem - Test mode: skipping GPIO hardware init, loading pulse timing only");
		}

#endif // #ifdef __unix__  // Ignore in Windows environment
		// Re-read pulse intervals and strobe-on times from JSON each call for on-the-fly changes
		GolfSimConfiguration::SetConstant("gs_config.strobing.kStrobePulseVectorDriver", pulse_intervals_fast_ms_);
		GolfSimConfiguration::SetConstant("gs_config.strobing.kStrobePulseVectorPutter", pulse_intervals_slow_ms_);

		// Longer pulses for the optically-noisy comparison environment
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

		// Stage both club pulse trains on the Pico from the configured vectors;
		// SendCameraStrobeTriggerAndShutter picks driver vs putter per shot,
		// mirroring the legacy SPI fast/slow pick.
		if (pico_client_ && pico_client_->IsOpen()) {
			const float driver_pulse_us =
				(static_cast<float>(number_bits_for_fast_on_pulse_) / static_cast<float>(kBaudRateForFastPulses)) * 1e6f;
			const float putter_pulse_us =
				(static_cast<float>(number_bits_for_slow_on_pulse_) / static_cast<float>(kBaudRateForSlowPulses)) * 1e6f;

			// Per-club acoustic floors (putt mode drops the floor for soft taps). From
			// gs_config.pico.mic_threshold[_putt] via the LM env; an unset putt floor reuses
			// the shot floor; 0 leaves the Pico's threshold alone.
			int32_t shot_threshold = 0;
			int32_t putt_threshold = 0;
			if (const char* thr = std::getenv("PITRAC_PICO_MIC_THRESHOLD"); thr && *thr) {
				try {
					shot_threshold = std::stoi(thr);
				} catch (const std::exception&) {
					GS_LOG_MSG(warning, "ignoring malformed PITRAC_PICO_MIC_THRESHOLD=" + std::string(thr));
				}
			}
			if (const char* thr = std::getenv("PITRAC_PICO_MIC_THRESHOLD_PUTT"); thr && *thr) {
				try {
					putt_threshold = std::stoi(thr);
				} catch (const std::exception&) {
					GS_LOG_MSG(warning, "ignoring malformed PITRAC_PICO_MIC_THRESHOLD_PUTT=" + std::string(thr));
				}
			}
			if (putt_threshold <= 0) putt_threshold = shot_threshold;

			pico_client_->StageClubProfile(PicoStrobeClient::ClubProfile::kDriver,
				driver_pulse_us, pulse_intervals_fast_ms_, shot_threshold);
			pico_client_->StageClubProfile(PicoStrobeClient::ClubProfile::kPutter,
				putter_pulse_us, pulse_intervals_slow_ms_, putt_threshold);
			pico_client_->SelectClubProfile(PicoStrobeClient::ClubProfile::kDriver);

			// Match the auto-disarm window to our heartbeat cadence: armed across the
			// swing (FSM pings every kPicoHeartbeatIntervalMs) yet safety-disarms
			// within kPicoArmTimeoutMs if the LM crashes mid-session.
			pico_client_->SetArmTimeout(static_cast<uint32_t>(kPicoArmTimeoutMs));

			// Cam2 XTR settle delay, tuned via /pico and persisted to
			// gs_config.pico.cam_xtr_setup_us. Re-push it so a power-cycled Pico
			// recovers the operator's value instead of the compiled default.
			if (const char* xtr = std::getenv("PITRAC_PICO_CAM_XTR_SETUP_US"); xtr && *xtr) {
				try {
					pico_client_->SetCamXtrSetup(static_cast<uint32_t>(std::stoul(xtr)));
				} catch (const std::exception&) {
					GS_LOG_MSG(warning, "ignoring malformed PITRAC_PICO_CAM_XTR_SETUP_US=" + std::string(xtr));
				}
			}
		}

		// Pre-compute the pulse sequences to save time at trigger
		GS_LOG_TRACE_MSG(trace, "Building Fast pulse sequence.");
		camera_fast_pulse_sequence_ = PulseStrobe::BuildPulseTrain((unsigned long)kBaudRateForFastPulses, pulse_intervals_fast_ms_, number_bits_for_fast_on_pulse_,
														kBitsPerWord, camera_fast_pulse_sequence_length_, false);
		GS_LOG_TRACE_MSG(trace, "Building Slow pulse sequence.");
		camera_slow_pulse_sequence_ = PulseStrobe::BuildPulseTrain((unsigned long)kBaudRateForSlowPulses, pulse_intervals_slow_ms_, number_bits_for_slow_on_pulse_,
														kBitsPerWord, camera_slow_pulse_sequence_length_, false);

		if (camera_fast_pulse_sequence_ == nullptr || camera_slow_pulse_sequence_ == nullptr) {
			GS_LOG_MSG(error, "Failed to build pulse sequences.");
			return false;
		}

		return true;
	}

	bool PulseStrobe::DeinitGPIOSystem() {
#ifdef __unix__  // Ignore in Windows environment
		GS_LOG_TRACE_MSG(trace, "PulseStrobe::DeinitGPIOSystem.");

		// Drop the Pico client first: its destructor closes the USB-CDC fd before
		// we release the gpio chip it shared for the fast path.
		pico_client_.reset();

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

		// When the bridge is up, send a single CAM_PULSE; firmware handles edge timing.
		if (pico_client_ && pico_client_->IsOpen()) {
			pico_client_->CamPulse(static_cast<uint32_t>(length_us));
			return;
		}

		if (kUsingActiveHighTriggerCamera) {
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kOFF);
			usleep(length_us);
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kON);
		} else {
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kON);
			usleep(length_us);
			lgGpioWrite(lggpio_chip_handle_, kPulseTriggerOutputPin, kOFF);
		}
#endif // #ifdef __unix__  // Ignore in Windows environment
	}

	bool PulseStrobe::SendCameraPrimingPulses(bool use_high_speed) {

#ifdef __unix__  // Ignore in Windows environment

		// In Pico mode the Pico self-fires strobe + cam2 trigger on the mic strike;
		// no legacy priming train, so skip the SPI open and the priming machine.
		if (IsPicoActive()) {
			GS_LOG_TRACE_MSG(trace, "SendCameraPrimingPulses: Pico active -- skipping legacy priming pulses.");
			return true;
		}

		// Re-read putting delay each call for on-the-fly tuning
		GolfSimConfiguration::SetConstant("gs_config.strobing.kPuttingStrobeDelayMs", kPuttingStrobeDelayMs);

		// Prime at the "fast" baud
		unsigned int baud_rate = 0;

		GolfSimConfiguration::SetConstant("gs_config.strobing.kBaudRateForFastPulses", baud_rate);

		spiHandle_ = OpenSpi(baud_rate, kBitsPerWord);

		if (spiHandle_ < 0) {
			GS_LOG_MSG(error, "lgGpioOpen failed.");
			return false;
		}

		// Camera2 thread is already armed and waiting for trigger — minimal settle time
		GolfSimConfiguration::SetConstant("gs_config.strobing.kCam2SetupPeriodMilliseconds", kCam2SetupPeriodMilliseconds);
		usleep(200 * 1000);  // 200ms — pipeline pre-opened, just StartCamera + settle
		GS_LOG_TRACE_MSG(trace, "Sending PRIMING pulses...");

		// Priming pulses: short low pulse (shutter speed) at a low frame rate
		const int kShutterSpeed = 100; // microseconds
		const int kShutterOffset = 14; // uS

		const int kOnTimeWidth = (int)((1.0 / kPrimingPulseFPS) * 1000000. - kShutterSpeed);

		GS_LOG_TRACE_MSG(trace, "Priming Pulse kOffTimeWidth = " + std::to_string(kShutterSpeed));
		GS_LOG_TRACE_MSG(trace, "Priming Pulse kOnTimeWidth =  " + std::to_string(kOnTimeWidth));

		for (int i = 0; i < kNumberPrimingPulses; i++) {
			GS_LOG_TRACE_MSG(trace, "Sent priming pulse");
			SendOnOffPulse(kShutterSpeed - kShutterOffset);
			usleep(kOnTimeWidth);

			// InnoMaker cam2 needs a moment to set up its external trigger once,
			// after the first image post-start.
			const CameraHardware::CameraModel  camera_model = GolfSimCamera::kSystemSlot2CameraType;

			if (i == 0 && camera_model == CameraHardware::CameraModel::InnoMakerIMX296GS_Mono) {
				GS_LOG_TRACE_MSG(trace, "Pausing for InnoMaker external trigger setup: " + std::to_string(kPauseToSetUpInnoMakerExternalTriggerMilliseconds) + " ms");
				usleep(kPauseToSetUpInnoMakerExternalTriggerMilliseconds * 1000);
			}
		}

		GS_LOG_TRACE_MSG(trace, "Sent " + std::to_string(kNumberPrimingPulses) + " initial priming pulses.  About to pause for " + 
				std::to_string(kPauseBeforeReadyForFinalPrimingPulseMs) + " milliSeconds before sending penultimate priming pulse.");

		usleep(kPauseBeforeReadyForFinalPrimingPulseMs * 1000);

		// Gets the cam2 state machine ready to take a real image
		SendOnOffPulse(kShutterSpeed - kShutterOffset);

		GS_LOG_TRACE_MSG(trace, "Sent final priming pulse. Camera 2 should be primed at this point.");

		// Optional pre-image exposure (mostly deprecated - didn't work well)
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

		// In Pico mode the Pico drives the cam2 trigger + strobe autonomously on
		// the acoustic strike; camera1 motion is stability-only. Never fire the
		// legacy SPI strobe / BCM trigger from here.
		if (IsPicoActive()) {
			GS_LOG_TRACE_MSG(trace, "SendExternalTrigger: Pico active -- skipping legacy trigger.");
			return true;
		}

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



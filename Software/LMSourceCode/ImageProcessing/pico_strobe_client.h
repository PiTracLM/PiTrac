/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace golf_sim {

struct PicoStatus {
    bool        armed = false;
    int32_t     mic_threshold = 0;
    float       pulse_width_us = 0.0f;
    uint32_t    min_inter_shot_ms = 0;
    uint32_t    decay_confirm_ms = 0;
    bool        strobe_hold = false;
    uint64_t    event_count = 0;
    std::vector<float> intervals;   // strobe pulse-gap vector echoed by firmware
    std::string fw_version;
};

class PicoStrobeClient {
public:
    explicit PicoStrobeClient(int lggpio_chip_handle = -1);
    ~PicoStrobeClient();

    PicoStrobeClient(const PicoStrobeClient&) = delete;
    PicoStrobeClient& operator=(const PicoStrobeClient&) = delete;

    // True iff device_path answers STATUS with a PiTrac Pico reply. Opens, probes, closes.
    static bool Probe(const std::string& device_path);

    bool Open(const std::string& device_path);
    void Close();
    bool IsOpen() const;

    bool SendPulseConfig(float pulse_width_us,
                         const std::vector<float>& intervals_ms);

    // Legacy SPI path uses a different pulse train for putting vs full swings.
    // Stage both profiles once, then select the active one per shot. Each profile
    // also carries its own acoustic mic floor (putts sit far lower than full shots);
    // mic_threshold of 0 leaves the Pico's current threshold untouched on select.
    enum class ClubProfile { kDriver, kPutter };

    void StageClubProfile(ClubProfile profile,
                          float pulse_width_us,
                          const std::vector<float>& intervals_ms,
                          int32_t mic_threshold);

    // Pushes the staged config only if it differs from the last push, so
    // back-to-back shots of the same club don't re-send over CDC.
    bool SelectClubProfile(ClubProfile profile);

    bool CamPulse(uint32_t microseconds);
    bool FireWithShutter();

    // Arm the on-Pico acoustic trigger: a mic strike fires strobe + cam2 XTR on the
    // Pico itself. STATUS-verified -- firmware refuses to arm when the room is louder
    // than threshold/quiet-factor.
    bool Arm();
    bool Disarm();

    // Keep-alive: while armed, each ping pushes the Pico's auto-disarm deadline out so
    // an attended session survives a swing instead of self-disarming on a per-shot timer.
    // Fire-and-forget (no STATUS echo); a dropped ping is harmless, the next refreshes it.
    bool Heartbeat();

    // Auto-disarm window in ms -- the keep-alive timeout. Must exceed the heartbeat
    // period. Blind write (STATUS does not report it); firmware clamps to [100, 300000].
    bool SetArmTimeout(uint32_t ms);

    // Persist DSP detector config so a power-cycled Pico recovers operator tuning
    // instead of compiled defaults. Each STATUS-verifies the echo.
    bool SetMicThreshold(int32_t threshold);
    bool SetDecayConfirm(uint32_t ms);

    // Calibration hold.
    bool HoldOn();
    bool HoldOff();

    bool ReadStatus(PicoStatus& out);

    // Cumulative strike counter (event_count). cam2 timeout path uses it to tell
    // "Pico never fired" from "Pico fired but cam2 dropped the frame". 0 if the port
    // is closed or STATUS does not answer.
    uint64_t LastEventCount();

    // Test-only seam: bypass termios and adopt an already-configured fd.
    // Production must use Open(device_path).
    bool AttachFd(int fd);

    // GPIO write seam matching lgGpioWrite(handle, gpio, level); returns its status.
    // Production defaults to lgGpioWrite; tests inject a recorder for the BCM 26 path.
    using GpioWriteFn = std::function<int(int handle, int gpio, int level)>;
    void SetGpioWriterForTest(GpioWriteFn writer);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace golf_sim

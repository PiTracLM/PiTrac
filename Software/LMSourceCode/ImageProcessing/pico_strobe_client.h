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
    bool        strobe_hold = false;
    uint64_t    event_count = 0;
    std::string fw_version;
};

class PicoStrobeClient {
public:
    explicit PicoStrobeClient(int lggpio_chip_handle = -1);
    ~PicoStrobeClient();

    PicoStrobeClient(const PicoStrobeClient&) = delete;
    PicoStrobeClient& operator=(const PicoStrobeClient&) = delete;

    // True iff the device behind device_path answers STATUS with a recognisable
    // PiTrac Pico reply. Opens, probes, closes.
    static bool Probe(const std::string& device_path);

    bool Open(const std::string& device_path);
    void Close();
    bool IsOpen() const;

    // Send pulse + train config; called once at startup and on any config change.
    bool SendPulseConfig(float pulse_width_us,
                         const std::vector<float>& intervals_ms);

    // The legacy SPI path picks a different pulse train for putting versus full
    // swings. These let the bridge mirror that: stage both profiles once, then
    // select the active one so a putt fires the putter pattern.
    enum class ClubProfile { kDriver, kPutter };

    void StageClubProfile(ClubProfile profile,
                          float pulse_width_us,
                          const std::vector<float>& intervals_ms);

    // Pushes the staged config for profile iff it differs from the last one
    // pushed, so back-to-back shots of the same club don't re-send over CDC.
    bool SelectClubProfile(ClubProfile profile);

    // The two bridge intercepts.
    bool CamPulse(uint32_t microseconds);
    bool FireWithShutter();

    // Calibration hold.
    bool HoldOn();
    bool HoldOff();

    // Diagnostic readback.
    bool ReadStatus(PicoStatus& out);

    // Test-only seam: bypass termios and adopt an already-configured fd.
    // Production code must use Open(device_path).
    bool AttachFd(int fd);

    // GPIO write seam. Signature matches lgGpioWrite(handle, gpio, level) and
    // returns its status code. Production defaults to lgGpioWrite on the Pi; the
    // test target injects a recorder to exercise the BCM 26 fast path.
    using GpioWriteFn = std::function<int(int handle, int gpio, int level)>;
    void SetGpioWriterForTest(GpioWriteFn writer);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace golf_sim

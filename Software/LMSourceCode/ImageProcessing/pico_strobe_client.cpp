/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#include "pico_strobe_client.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

// Logging hooks. Production-linked from pulse_strobe.cpp (LoggingTools); the
// test target supplies stubs so it doesn't pull Boost.Log.
namespace golf_sim {
void PicoLogTrace(const std::string& msg);
void PicoLogWarn(const std::string& msg);
}

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#if __has_include(<lgpio.h>)
#include <lgpio.h>
#define PICO_HAS_LGPIO 1
#else
#define PICO_HAS_LGPIO 0
#endif

namespace golf_sim {

namespace {
// Pi BCM 26 is the dedicated fire-trigger line into Pico GP9. Pulled HIGH by the
// Pi and edge-detected on the Pico side. BCM 10 is owned by the legacy DIAG path
// and BCM 25 is the Innomaker external trigger; do not reuse either here.
constexpr int kPicoFirePin = 26;

bool ParseBoolDigit(const std::string& v) {
    return !v.empty() && v[0] != '0';
}
}  // namespace

struct PicoStrobeClient::Impl {
    int fd = -1;
    int gpio_handle = -1;

    GpioWriteFn gpio_writer;

    // -1 means nothing has been pushed yet, so the first SelectClubProfile of
    // either profile always writes.
    int active_profile = -1;

    struct StagedConfig {
        bool valid = false;
        float pulse_width_us = 0.0f;
        std::vector<float> intervals_ms;
    };
    StagedConfig driver;
    StagedConfig putter;

    bool WriteLine(const std::string& line);
    bool ReadLineWithTimeout(std::string& out, int timeout_ms);
};

namespace {
int DefaultGpioWrite(int handle, int gpio, int level) {
#if PICO_HAS_LGPIO
    return lgGpioWrite(handle, gpio, level);
#else
    (void)handle;
    (void)gpio;
    (void)level;
    return 0;
#endif
}
}  // namespace

PicoStrobeClient::PicoStrobeClient(int lggpio_chip_handle)
    : impl_(std::make_unique<Impl>()) {
    impl_->gpio_handle = lggpio_chip_handle;
    impl_->gpio_writer = &DefaultGpioWrite;
}

PicoStrobeClient::~PicoStrobeClient() {
    Close();
}

bool PicoStrobeClient::Probe(const std::string& device_path) {
    PicoStrobeClient probe;
    if (!probe.Open(device_path)) return false;
    PicoStatus s;
    bool ok = probe.ReadStatus(s);
    probe.Close();
    // Reply must start with "STATUS " which is sufficient to distinguish the
    // PiTrac Pico from any other CDC device that happens to be listening.
    return ok;
}

bool PicoStrobeClient::Open(const std::string& device_path) {
    if (!impl_) return false;
    if (impl_->fd >= 0) Close();

    int fd = ::open(device_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        PicoLogWarn("PicoStrobeClient::Open: open(" + device_path +
                              ") failed: " + std::string(::strerror(errno)));
        return false;
    }

    struct termios tio {};
    if (::tcgetattr(fd, &tio) != 0) {
        ::close(fd);
        return false;
    }
    ::cfmakeraw(&tio);
    ::cfsetispeed(&tio, B115200);
    ::cfsetospeed(&tio, B115200);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(CSTOPB | PARENB);
    tio.c_cflag = (tio.c_cflag & ~CSIZE) | CS8;
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;
    if (::tcsetattr(fd, TCSANOW, &tio) != 0) {
        ::close(fd);
        return false;
    }

    impl_->fd = fd;
    return true;
}

void PicoStrobeClient::Close() {
    if (impl_ && impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
}

bool PicoStrobeClient::IsOpen() const {
    return impl_ && impl_->fd >= 0;
}

bool PicoStrobeClient::AttachFd(int fd) {
    if (!impl_) return false;
    if (impl_->fd >= 0) ::close(impl_->fd);
    impl_->fd = fd;
    return true;
}

bool PicoStrobeClient::SendPulseConfig(float pulse_width_us,
                                      const std::vector<float>& intervals_ms) {
    if (!IsOpen()) return false;

    std::ostringstream w;
    w << std::fixed << std::setprecision(3) << "CFG PULSE_WIDTH_US=" << pulse_width_us;
    if (!impl_->WriteLine(w.str())) return false;

    std::ostringstream iv;
    iv << std::fixed << std::setprecision(3) << "CFG PULSE_INTERVALS=";
    for (size_t i = 0; i < intervals_ms.size(); ++i) {
        if (i > 0) iv << ',';
        iv << intervals_ms[i];
    }
    return impl_->WriteLine(iv.str());
}

bool PicoStrobeClient::CamPulse(uint32_t microseconds) {
    if (!IsOpen()) return false;
    std::ostringstream w;
    w << "CAM_PULSE " << microseconds;
    return impl_->WriteLine(w.str());
}

bool PicoStrobeClient::FireWithShutter() {
    if (!IsOpen()) return false;

    if (impl_->gpio_handle < 0 || !impl_->gpio_writer) {
        // No gpio chip handle (tests, dev builds): fall back to USB-CDC FIRE.
        return impl_->WriteLine("FIRE");
    }

    // Fast path: pulse BCM 26 HIGH then LOW. Pico GP9 IRQ catches the rising edge.
    impl_->gpio_writer(impl_->gpio_handle, kPicoFirePin, 1);
    // Few-microsecond busy-loop: lgGpioWrite already takes microseconds, but a
    // short hold guarantees the rising edge is visible even with USB-CDC slack.
    for (int spin = 0; spin < 50; ++spin) {
        asm volatile("" : : : "memory");
    }
    impl_->gpio_writer(impl_->gpio_handle, kPicoFirePin, 0);
    return true;
}

void PicoStrobeClient::SetGpioWriterForTest(GpioWriteFn writer) {
    if (impl_) impl_->gpio_writer = std::move(writer);
}

void PicoStrobeClient::StageClubProfile(ClubProfile profile,
                                        float pulse_width_us,
                                        const std::vector<float>& intervals_ms) {
    if (!impl_) return;
    Impl::StagedConfig& slot =
        (profile == ClubProfile::kPutter) ? impl_->putter : impl_->driver;
    slot.valid = true;
    slot.pulse_width_us = pulse_width_us;
    slot.intervals_ms = intervals_ms;
}

bool PicoStrobeClient::SelectClubProfile(ClubProfile profile) {
    if (!IsOpen()) return false;
    if (impl_->active_profile == static_cast<int>(profile)) return true;

    const Impl::StagedConfig& slot =
        (profile == ClubProfile::kPutter) ? impl_->putter : impl_->driver;
    if (!slot.valid) {
        PicoLogWarn("PicoStrobeClient::SelectClubProfile: requested profile not staged");
        return false;
    }

    if (!SendPulseConfig(slot.pulse_width_us, slot.intervals_ms)) return false;
    impl_->active_profile = static_cast<int>(profile);
    return true;
}

bool PicoStrobeClient::HoldOn() {
    if (!IsOpen()) return false;
    return impl_->WriteLine("CFG STROBE_HOLD=1");
}

bool PicoStrobeClient::HoldOff() {
    if (!IsOpen()) return false;
    return impl_->WriteLine("CFG STROBE_HOLD=0");
}

bool PicoStrobeClient::Arm() {
    if (!IsOpen()) return false;
    if (!impl_->WriteLine("CFG ARMED=1")) return false;
    PicoStatus st;
    if (!ReadStatus(st)) return false;
    if (!st.armed) {
        PicoLogWarn("PicoStrobeClient::Arm: firmware refused arm "
                    "(room louder than threshold/quiet-factor?)");
        return false;
    }
    return true;
}

bool PicoStrobeClient::Disarm() {
    if (!IsOpen()) return false;
    if (!impl_->WriteLine("CFG ARMED=0")) return false;
    PicoStatus st;
    if (!ReadStatus(st)) return false;
    return !st.armed;
}

uint64_t PicoStrobeClient::LastEventCount() {
    PicoStatus st;
    if (!ReadStatus(st)) return 0;
    return st.event_count;
}

bool PicoStrobeClient::ReadStatus(PicoStatus& out) {
    if (!IsOpen()) return false;
    if (!impl_->WriteLine("STATUS")) return false;

    std::string line;
    if (!impl_->ReadLineWithTimeout(line, 1000)) return false;

    // Reply must start with "STATUS " to count.
    constexpr const char* kPrefix = "STATUS ";
    if (line.rfind(kPrefix, 0) != 0) return false;

    out = PicoStatus{};
    std::istringstream iss(line.substr(std::strlen(kPrefix)));
    std::string token;
    while (iss >> token) {
        auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = token.substr(0, eq);
        const std::string val = token.substr(eq + 1);
        try {
            if (key == "armed") {
                out.armed = ParseBoolDigit(val);
            } else if (key == "threshold") {
                out.mic_threshold = std::stoi(val);
            } else if (key == "pulse_us") {
                out.pulse_width_us = std::stof(val);
            } else if (key == "min_inter_shot_ms") {
                out.min_inter_shot_ms = static_cast<uint32_t>(std::stoul(val));
            } else if (key == "event_count") {
                out.event_count = static_cast<uint64_t>(std::stoull(val));
            } else if (key == "strobe_hold") {
                out.strobe_hold = ParseBoolDigit(val);
            } else if (key == "fw") {
                out.fw_version = val;
            }
            // Unknown keys are tolerated.
        } catch (const std::exception&) {
            // Skip malformed values; do not fail the whole parse.
        }
    }
    return true;
}

bool PicoStrobeClient::Impl::WriteLine(const std::string& line) {
    if (fd < 0) {
        PicoLogWarn("PicoStrobeClient::WriteLine: fd closed, dropping '" + line + "'");
        return false;
    }
    std::string buf = line;
    if (buf.empty() || buf.back() != '\n') buf.push_back('\n');
    const char* p = buf.data();
    size_t remaining = buf.size();
    while (remaining > 0) {
        ssize_t n = ::write(fd, p, remaining);
        if (n > 0) {
            p += n;
            remaining -= static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        PicoLogWarn("PicoStrobeClient::WriteLine: write error '" +
                              std::string(::strerror(errno)) + "' on line: " + line);
        return false;
    }
    PicoLogTrace("PicoStrobeClient -> " + line);
    return true;
}

bool PicoStrobeClient::Impl::ReadLineWithTimeout(std::string& out, int timeout_ms) {
    out.clear();
    if (fd < 0) return false;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    char buf[256];
    std::string acc;
    while (std::chrono::steady_clock::now() < deadline) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            acc.append(buf, static_cast<size_t>(n));
            auto nl = acc.find('\n');
            if (nl != std::string::npos) {
                out = acc.substr(0, nl);
                if (!out.empty() && out.back() == '\r') out.pop_back();
                return true;
            }
            continue;
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

}  // namespace golf_sim

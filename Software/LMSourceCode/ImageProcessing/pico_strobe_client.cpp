/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#include "pico_strobe_client.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <mutex>
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

    // Serializes every CDC transaction (a WriteLine plus its STATUS echo read). The FSM
    // thread (Arm/Disarm/PicoEventCount) and the cam2 thread (CamPulse warm-up) share this
    // one fd; without serialization their request/response lines interleave and the
    // echo-verify reads the wrong reply. Recursive because the verified setters (Arm,
    // Disarm, SetMicThreshold, SelectClubProfile, ...) lock and then call ReadStatus /
    // SendPulseConfig, which lock again on the same thread.
    std::recursive_mutex io_mutex;

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

    // Bytes pulled off the fd that have not yet been handed back as a complete line.
    // The Pico multiplexes ONE USB-CDC text stream: solicited replies (STATUS ...,
    // SELFTEST ...) interleaved with unsolicited async lines (LOG <ack/error>, EVENT
    // <telemetry>). Linux cdc-acm delivers an undelimited byte stream -- USB packet
    // boundaries are NOT line boundaries -- so a single read() routinely returns several
    // lines at once (a CFG's "LOG ..." ack immediately ahead of the "STATUS ..." reply)
    // and lines can span reads. We MUST buffer here and reassemble; assuming one read()
    // == one line silently dropped every line past the first newline and stalled the
    // STATUS demux. Cleared on flush/open/close so it tracks a tcflush of the OS buffer.
    std::string rx_buf;

    bool WriteLine(const std::string& line);
    bool ReadLineWithTimeout(std::string& out, int timeout_ms);
    // Demux the single channel: return the next SOLICITED reply beginning with `prefix`
    // within one TOTAL deadline, surfacing (not dropping) any async LOG/EVENT lines that
    // arrive ahead of it. The firmware emits STATUS only on demand, so the next matching
    // line is the reply.
    bool ReadSolicitedReply(const char* prefix, int timeout_ms, std::string& out);
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
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
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
    impl_->rx_buf.clear();
    return true;
}

void PicoStrobeClient::Close() {
    if (!impl_) return;
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
    impl_->rx_buf.clear();
}

bool PicoStrobeClient::IsOpen() const {
    return impl_ && impl_->fd >= 0;
}

bool PicoStrobeClient::AttachFd(int fd) {
    if (!impl_) return false;
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    if (impl_->fd >= 0) ::close(impl_->fd);
    impl_->fd = fd;
    // Match what Open() produces: a non-blocking fd. ReadLineWithTimeout relies on read()
    // returning EAGAIN to honour its deadline -- a blocking fd would wedge the demux
    // waiting for a line that never comes (this seam adopts a raw fd, e.g. a test socket).
    int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl >= 0) { ::fcntl(fd, F_SETFL, fl | O_NONBLOCK); }
    impl_->rx_buf.clear();
    return true;
}

bool PicoStrobeClient::SendPulseConfig(float pulse_width_us,
                                      const std::vector<float>& intervals_ms) {
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
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
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    if (!IsOpen()) return false;
    std::ostringstream w;
    w << "CAM_PULSE " << microseconds;
    return impl_->WriteLine(w.str());
}

bool PicoStrobeClient::FireWithShutter() {
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
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
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    Impl::StagedConfig& slot =
        (profile == ClubProfile::kPutter) ? impl_->putter : impl_->driver;
    slot.valid = true;
    slot.pulse_width_us = pulse_width_us;
    slot.intervals_ms = intervals_ms;
}

bool PicoStrobeClient::SelectClubProfile(ClubProfile profile) {
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
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
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    if (!IsOpen()) return false;
    return impl_->WriteLine("CFG STROBE_HOLD=1");
}

bool PicoStrobeClient::HoldOff() {
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    if (!IsOpen()) return false;
    return impl_->WriteLine("CFG STROBE_HOLD=0");
}

bool PicoStrobeClient::Arm() {
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
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
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    if (!IsOpen()) return false;
    if (!impl_->WriteLine("CFG ARMED=0")) return false;
    PicoStatus st;
    if (!ReadStatus(st)) return false;
    return !st.armed;
}

bool PicoStrobeClient::SetMicThreshold(int32_t threshold) {
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    if (!IsOpen()) return false;
    if (!impl_->WriteLine("CFG MIC_THRESHOLD=" + std::to_string(threshold))) return false;
    PicoStatus st;
    if (!ReadStatus(st)) return false;
    return st.mic_threshold == threshold;
}

bool PicoStrobeClient::SetDecayConfirm(uint32_t ms) {
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    if (!IsOpen()) return false;
    if (!impl_->WriteLine("CFG DECAY_CONFIRM_MS=" + std::to_string(ms))) return false;
    PicoStatus st;
    if (!ReadStatus(st)) return false;
    return st.decay_confirm_ms == ms;
}

uint64_t PicoStrobeClient::LastEventCount() {
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    PicoStatus st;
    if (!ReadStatus(st)) return 0;
    return st.event_count;
}

bool PicoStrobeClient::ReadStatus(PicoStatus& out) {
    std::lock_guard<std::recursive_mutex> lock(impl_->io_mutex);
    if (!IsOpen()) return false;
    // Drop stale/unsolicited bytes (a boot banner, a prior CFG's LOG ack, a partial
    // line) before asking, so the first read after a fresh CDC connect returns THIS
    // STATUS reply rather than a leftover line -- otherwise the echo-verify in
    // Arm/SetMicThreshold/etc. false-negatives on the first contact. Mirrors the
    // Python side's reset_input_buffer(); a no-op (harmless) on the test socket fd.
    if (impl_->fd >= 0) { ::tcflush(impl_->fd, TCIFLUSH); }
    impl_->rx_buf.clear();  // tcflush drops the OS input buffer; drop our software one too.
    if (!impl_->WriteLine("STATUS")) return false;

    // A CFG command sent just before STATUS (Arm, SetMicThreshold, SetDecayConfirm)
    // lands its "LOG <ack>" / "LOG error: ..." ahead of the reply, and an EVENT strike
    // can arrive at any time -- ReadSolicitedReply skips those async lines (logging them
    // rather than dropping them) and returns the STATUS reply, all under one bounded
    // deadline. (The old loop polled 32 x 1000ms PER line, so a single dropped/absent
    // line could stall it ~32s; now it is a single total timeout.)
    constexpr const char* kPrefix = "STATUS ";
    constexpr int kReplyTimeoutMs = 1500;
    std::string line;
    if (!impl_->ReadSolicitedReply(kPrefix, kReplyTimeoutMs, line)) return false;

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
            } else if (key == "decay_confirm_ms") {
                out.decay_confirm_ms = static_cast<uint32_t>(std::stoul(val));
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

    // Hand back a line already sitting in rx_buf from a previous read first -- a single
    // read() can deliver several lines, and the trailing ones must survive to the next
    // call (the bug this fixes: they were discarded, stalling the STATUS demux).
    auto take_buffered_line = [&]() -> bool {
        auto nl = rx_buf.find('\n');
        if (nl == std::string::npos) return false;
        out.assign(rx_buf, 0, nl);
        if (!out.empty() && out.back() == '\r') out.pop_back();
        rx_buf.erase(0, nl + 1);
        return true;
    };
    if (take_buffered_line()) return true;
    if (fd < 0) return false;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    char buf[256];
    while (std::chrono::steady_clock::now() < deadline) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            rx_buf.append(buf, static_cast<size_t>(n));
            if (take_buffered_line()) return true;
            continue;
        }
        if (n == 0) return false;  // EOF: peer closed the port, no more lines coming.
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

bool PicoStrobeClient::Impl::ReadSolicitedReply(const char* prefix, int timeout_ms,
                                                std::string& out) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    std::string line;
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;
        const int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        if (!ReadLineWithTimeout(line, remaining)) return false;
        if (line.rfind(prefix, 0) == 0) {
            out = std::move(line);
            return true;
        }
        // Unsolicited async line (LOG ack/error, EVENT strike/telemetry) landed ahead of
        // the reply. Surface it so it is observable rather than black-holed, then keep
        // waiting for the solicited reply within the same deadline.
        if (!line.empty()) {
            PicoLogTrace("PicoStrobeClient <- (async) " + line);
        }
    }
}

}  // namespace golf_sim

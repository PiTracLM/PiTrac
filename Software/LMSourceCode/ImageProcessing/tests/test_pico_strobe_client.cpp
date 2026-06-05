/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2026, Verdant Consultants, LLC.
 */

#define BOOST_TEST_MODULE PicoStrobeClient
#include <boost/test/unit_test.hpp>

#include "pico_strobe_client.h"

// Stub the log hooks pico_strobe_client.cpp forward-declares; keeps the test
// target Boost.Log-free (production links the real ones from pulse_strobe.cpp).
#include <string>
namespace golf_sim {
    void PicoLogTrace(const std::string&) {}
    void PicoLogWarn(const std::string&)  {}
}

#include <chrono>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

using golf_sim::PicoStatus;
using golf_sim::PicoStrobeClient;

// Drains everything currently readable on the host side into a string.
static std::string DrainHost(int fd, int settle_ms = 50) {
    std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
    std::string out;
    char buf[256];
    while (true) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
    }
    return out;
}

// host fd stays here; client fd is handed to PicoStrobeClient via AttachFd().
struct SocketPair {
    int host = -1;
    int client = -1;

    SocketPair() {
        int fds[2] = {-1, -1};
        BOOST_REQUIRE_EQUAL(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
        host = fds[0];
        client = fds[1];
    }

    ~SocketPair() {
        if (host >= 0) ::close(host);  // client fd owned by PicoStrobeClient once attached
    }
};

BOOST_AUTO_TEST_CASE(attach_fd_marks_open) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    BOOST_CHECK(client.IsOpen());
}

BOOST_AUTO_TEST_CASE(open_dev_null_fails_cleanly) {
    PicoStrobeClient client;
    // /dev/null opens but tcsetattr() rejects it; Open() must clean up and fail.
    BOOST_CHECK(!client.Open("/dev/null"));
    BOOST_CHECK(!client.IsOpen());
}

BOOST_AUTO_TEST_CASE(open_missing_path_returns_false) {
    PicoStrobeClient client;
    BOOST_CHECK(!client.Open("/nonexistent/path/should-fail"));
    BOOST_CHECK(!client.IsOpen());
}

BOOST_AUTO_TEST_CASE(close_after_attach_clears_state) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    client.Close();
    BOOST_CHECK(!client.IsOpen());
}

BOOST_AUTO_TEST_CASE(read_status_parses_firmware_reply) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    // Non-blocking host side so DrainHost terminates.
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    // Reply after a short delay so the client's STATUS write lands first.
    std::thread responder([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const char reply[] =
            "STATUS armed=0 threshold=4096 pulse_us=8.68 "
            "min_inter_shot_ms=200 pre_trigger_delay_ms=0 "
            "strobe_hold=1 vsys_mv=0 vbus=1 "
            "intervals=0.70,1.80,3.00,2.20,3.00,7.10,4.00,0.00\n";
        ::write(pair.host, reply, sizeof(reply) - 1);
    });

    PicoStatus status;
    bool ok = client.ReadStatus(status);
    responder.join();

    BOOST_REQUIRE(ok);
    BOOST_CHECK_EQUAL(status.armed, false);
    BOOST_CHECK_EQUAL(status.mic_threshold, 4096);
    BOOST_CHECK_CLOSE(status.pulse_width_us, 8.68f, 0.01f);
    BOOST_CHECK_EQUAL(status.min_inter_shot_ms, 200u);
    BOOST_CHECK_EQUAL(status.strobe_hold, true);
    BOOST_CHECK(status.fw_version.empty());  // fw= not in fw 0.3.0 STATUS

    std::string host_in = DrainHost(pair.host, 10);
    BOOST_CHECK_EQUAL(host_in, "STATUS\n");
}

// Writes the STATUS reply after a delay so the client's own writes land first.
static std::thread RespondStatus(int host_fd, const std::string& line) {
    return std::thread([host_fd, line]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ::write(host_fd, line.c_str(), line.size());
    });
}

BOOST_AUTO_TEST_CASE(arm_succeeds_when_firmware_echoes_armed) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    std::thread responder = RespondStatus(pair.host, "STATUS armed=1 threshold=4096 fw=0.6.1\n");
    bool ok = client.Arm();
    responder.join();

    BOOST_CHECK(ok);
    std::string host_in = DrainHost(pair.host, 10);
    BOOST_CHECK(host_in.find("CFG ARMED=1\n") != std::string::npos);
    BOOST_CHECK(host_in.find("STATUS\n") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(arm_succeeds_when_log_ack_precedes_status) {
    // Firmware emits its "LOG armed" ack ahead of STATUS; ReadStatus must skip
    // the LOG line or every arm verify false-negatives and cam2 restart-loops. Regression.
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    std::thread responder = RespondStatus(pair.host,
        "LOG armed\nSTATUS armed=1 threshold=4096 fw=0.6.1\n");
    bool ok = client.Arm();
    responder.join();

    BOOST_CHECK(ok);
}

BOOST_AUTO_TEST_CASE(set_mic_threshold_succeeds_when_log_ack_precedes_status) {
    // "LOG threshold updated" lands before the STATUS echo; verify must skip it
    // and still read threshold=8000.
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    std::thread responder = RespondStatus(pair.host,
        "LOG threshold updated\nSTATUS armed=0 threshold=8000 fw=0.6.1\n");
    bool ok = client.SetMicThreshold(8000);
    responder.join();

    BOOST_CHECK(ok);
}

BOOST_AUTO_TEST_CASE(arm_fails_when_firmware_refuses) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    // Arm ignored (room too loud) — STATUS still reports armed=0.
    std::thread responder = RespondStatus(pair.host, "STATUS armed=0 threshold=4096 fw=0.6.1\n");
    bool ok = client.Arm();
    responder.join();

    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(disarm_succeeds_when_firmware_echoes_disarmed) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    std::thread responder = RespondStatus(pair.host, "STATUS armed=0 threshold=4096 fw=0.6.1\n");
    bool ok = client.Disarm();
    responder.join();

    BOOST_CHECK(ok);
    std::string host_in = DrainHost(pair.host, 10);
    BOOST_CHECK(host_in.find("CFG ARMED=0\n") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(read_status_parses_event_count) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    std::thread responder = RespondStatus(
        pair.host,
        "STATUS armed=1 threshold=4096 pulse_us=8.68 min_inter_shot_ms=200 "
        "event_count=123456789 fw=0.6.1\n");
    PicoStatus status;
    bool ok = client.ReadStatus(status);
    responder.join();

    BOOST_REQUIRE(ok);
    BOOST_CHECK_EQUAL(status.event_count, 123456789ull);
}

BOOST_AUTO_TEST_CASE(last_event_count_returns_parsed_value) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    std::thread responder = RespondStatus(pair.host, "STATUS armed=1 threshold=4096 event_count=42 fw=0.6.1\n");
    uint64_t count = client.LastEventCount();
    responder.join();

    BOOST_CHECK_EQUAL(count, 42ull);
    // LastEventCount queries live, so a STATUS line must have gone out.
    std::string host_in = DrainHost(pair.host, 10);
    BOOST_CHECK(host_in.find("STATUS\n") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(last_event_count_zero_when_status_fails) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    // Garbage reply -> ReadStatus rejects it -> LastEventCount falls back to 0.
    std::thread responder = RespondStatus(pair.host, "Hello from some other CDC device\n");
    uint64_t count = client.LastEventCount();
    responder.join();

    BOOST_CHECK_EQUAL(count, 0ull);
}

BOOST_AUTO_TEST_CASE(read_status_rejects_unrecognised_reply) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    std::thread responder([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const char reply[] = "Hello from some other CDC device\n";
        ::write(pair.host, reply, sizeof(reply) - 1);
    });

    PicoStatus status;
    bool ok = client.ReadStatus(status);
    responder.join();
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(set_mic_threshold_verifies_echo) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    std::thread responder = RespondStatus(pair.host, "STATUS armed=0 threshold=8000 fw=0.6.1\n");
    bool ok = client.SetMicThreshold(8000);
    responder.join();

    BOOST_CHECK(ok);
    std::string host_in = DrainHost(pair.host, 10);
    BOOST_CHECK(host_in.find("CFG MIC_THRESHOLD=8000\n") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(set_mic_threshold_fails_on_echo_mismatch) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));
    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    // Echoed threshold differs from requested -> rejected.
    std::thread responder = RespondStatus(pair.host, "STATUS armed=0 threshold=4096 fw=0.6.1\n");
    bool ok = client.SetMicThreshold(8000);
    responder.join();

    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(send_pulse_config_writes_both_lines_and_echo_verifies) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    // Firmware echoes the train back via STATUS at %.2f, so verify passes.
    std::thread responder([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const char reply[] =
            "STATUS armed=0 pulse_us=8.68 intervals=0.70,1.80,3.00,0.00\n";
        ::write(pair.host, reply, sizeof(reply) - 1);
    });
    bool ok = client.SendPulseConfig(8.68f, {0.7f, 1.8f, 3.0f, 0.0f});
    responder.join();
    BOOST_CHECK(ok);

    std::string wire = DrainHost(pair.host, 10);
    BOOST_CHECK(wire.find("CFG PULSE_WIDTH_US=8.680\n") != std::string::npos);
    BOOST_CHECK(wire.find("CFG PULSE_INTERVALS=0.700,1.800,3.000,0.000\n") != std::string::npos);
    BOOST_CHECK(wire.find("STATUS\n") != std::string::npos);  // round-trips to verify
}

BOOST_AUTO_TEST_CASE(send_pulse_config_fails_when_firmware_echo_mismatches) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    // Firmware kept a different vector — must fail so SelectClubProfile won't
    // mark the club active on a discarded config.
    std::thread responder([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const char reply[] = "STATUS armed=0 pulse_us=8.68 intervals=9.90,9.90\n";
        ::write(pair.host, reply, sizeof(reply) - 1);
    });
    bool ok = client.SendPulseConfig(8.68f, {0.7f, 1.8f, 3.0f, 0.0f});
    responder.join();
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(hold_on_writes_strobe_hold_1) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    BOOST_REQUIRE(client.HoldOn());
    BOOST_CHECK_EQUAL(DrainHost(pair.host, 20), "CFG STROBE_HOLD=1\n");
}

BOOST_AUTO_TEST_CASE(hold_off_writes_strobe_hold_0) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    BOOST_REQUIRE(client.HoldOff());
    BOOST_CHECK_EQUAL(DrainHost(pair.host, 20), "CFG STROBE_HOLD=0\n");
}

BOOST_AUTO_TEST_CASE(heartbeat_writes_bare_keepalive_line) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    BOOST_REQUIRE(client.Heartbeat());
    BOOST_CHECK_EQUAL(DrainHost(pair.host, 20), "HEARTBEAT\n");
}

BOOST_AUTO_TEST_CASE(heartbeat_fails_when_closed) {
    PicoStrobeClient client;
    BOOST_CHECK(!client.Heartbeat());
}

BOOST_AUTO_TEST_CASE(set_arm_timeout_writes_cfg_line) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    BOOST_REQUIRE(client.SetArmTimeout(3000));
    BOOST_CHECK_EQUAL(DrainHost(pair.host, 20), "CFG ARM_TIMEOUT_MS=3000\n");
}

BOOST_AUTO_TEST_CASE(cam_pulse_writes_decimal_microseconds) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    BOOST_REQUIRE(client.CamPulse(123));
    BOOST_CHECK_EQUAL(DrainHost(pair.host, 20), "CAM_PULSE 123\n");
}

BOOST_AUTO_TEST_CASE(fire_with_shutter_falls_back_to_usb_cdc_without_gpio) {
    SocketPair pair;
    // Default ctor: gpio_handle = -1 => no BCM 26, must use USB-CDC FIRE.
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    BOOST_REQUIRE(client.FireWithShutter());
    BOOST_CHECK_EQUAL(DrainHost(pair.host, 20), "FIRE\n");
}

// With a chip handle present, FireWithShutter drives BCM 26 high/low via the
// injected writer instead of writing "FIRE" to CDC — exercises the low-latency
// path without real lgpio hardware.
BOOST_AUTO_TEST_CASE(fire_with_shutter_pulses_gpio_when_handle_present) {
    SocketPair pair;
    PicoStrobeClient client(7);  // any non-negative handle selects the fast path
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    std::vector<std::pair<int, int>> writes;  // (pin, level) in order
    client.SetGpioWriterForTest([&](int handle, int pin, int level) {
        BOOST_CHECK_EQUAL(handle, 7);
        writes.emplace_back(pin, level);
        return 0;
    });

    BOOST_REQUIRE(client.FireWithShutter());

    BOOST_REQUIRE_EQUAL(writes.size(), 2u);
    BOOST_CHECK_EQUAL(writes[0].first, 26);   // BCM 26 high
    BOOST_CHECK_EQUAL(writes[0].second, 1);
    BOOST_CHECK_EQUAL(writes[1].first, 26);   // BCM 26 low
    BOOST_CHECK_EQUAL(writes[1].second, 0);

    // Fast path must not leak onto the CDC wire.
    BOOST_CHECK_EQUAL(DrainHost(pair.host, 20), "");
}

// SelectClubProfile re-pushes the staged pulse width + interval vector only on
// an actual club change — the branch SendCameraStrobeTriggerAndShutter relies on
// so a putt fires the putter pattern, not the driver's.
BOOST_AUTO_TEST_CASE(select_club_profile_pushes_matching_vector_on_change) {
    SocketPair pair;
    PicoStrobeClient client;
    BOOST_REQUIRE(client.AttachFd(pair.client));

    int flags = ::fcntl(pair.host, F_GETFL, 0);
    ::fcntl(pair.host, F_SETFL, flags | O_NONBLOCK);

    client.StageClubProfile(PicoStrobeClient::ClubProfile::kDriver,
                            8.68f, {0.7f, 1.8f, 3.0f, 0.0f}, 12000000);
    client.StageClubProfile(PicoStrobeClient::ClubProfile::kPutter,
                            12.5f, {1.1f, 2.4f, 5.0f, 0.0f}, 2500000);

    // First driver selection pushes + echo-verifies; feed a matching STATUS.
    {
        std::thread r([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const char reply[] =
                "STATUS armed=0 pulse_us=8.68 intervals=0.70,1.80,3.00,0.00\n";
            ::write(pair.host, reply, sizeof(reply) - 1);
        });
        BOOST_REQUIRE(client.SelectClubProfile(PicoStrobeClient::ClubProfile::kDriver));
        r.join();
        std::string wire = DrainHost(pair.host, 10);
        BOOST_CHECK(wire.find("CFG PULSE_WIDTH_US=8.680\n") != std::string::npos);
        BOOST_CHECK(wire.find("CFG PULSE_INTERVALS=0.700,1.800,3.000,0.000\n") != std::string::npos);
    }

    // Re-selecting the same club is a no-op on the wire (no push, no echo).
    BOOST_REQUIRE(client.SelectClubProfile(PicoStrobeClient::ClubProfile::kDriver));
    BOOST_CHECK_EQUAL(DrainHost(pair.host, 20), "");

    // Switching to the putter pushes + echo-verifies the putter config.
    {
        std::thread r([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const char reply[] =
                "STATUS armed=0 pulse_us=12.50 intervals=1.10,2.40,5.00,0.00\n";
            ::write(pair.host, reply, sizeof(reply) - 1);
        });
        BOOST_REQUIRE(client.SelectClubProfile(PicoStrobeClient::ClubProfile::kPutter));
        r.join();
        std::string wire = DrainHost(pair.host, 10);
        BOOST_CHECK(wire.find("CFG PULSE_WIDTH_US=12.500\n") != std::string::npos);
        BOOST_CHECK(wire.find("CFG PULSE_INTERVALS=1.100,2.400,5.000,0.000\n") != std::string::npos);
    }
}

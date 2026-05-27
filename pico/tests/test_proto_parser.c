#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cmd_dispatcher.h"
#include "proto.h"
#include "proto_parser.h"

static int failures = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        failures++; \
    } \
} while (0)

static void test_fire_parses_correctly(void) {
    pitrac_cmd_t cmd;
    char line[] = "FIRE";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "FIRE should parse");
    EXPECT(cmd.kind == CMD_FIRE, "FIRE kind");
}

static void test_status_parses_correctly(void) {
    pitrac_cmd_t cmd;
    char line[] = "STATUS";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "STATUS should parse");
    EXPECT(cmd.kind == CMD_STATUS, "STATUS kind");
}

static void test_cfg_threshold_parses_value(void) {
    pitrac_cmd_t cmd;
    char line[] = "CFG MIC_THRESHOLD=8192";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "CFG MIC_THRESHOLD should parse");
    EXPECT(cmd.kind == CMD_CFG_THRESHOLD, "kind");
    EXPECT(cmd.u.threshold == 8192, "value");
}

static void test_cfg_pulse_width_in_range(void) {
    pitrac_cmd_t cmd;
    char line[] = "CFG PULSE_WIDTH_US=10.0";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "10us should parse");
    EXPECT(cmd.kind == CMD_CFG_PULSE_WIDTH, "kind");
    EXPECT(cmd.u.pulse_width_us > 9.9f && cmd.u.pulse_width_us < 10.1f, "value");
}

static void test_cfg_pulse_width_out_of_range_rejected(void) {
    pitrac_cmd_t cmd;
    char line[] = "CFG PULSE_WIDTH_US=999";  /* > STROBE_MAX_PULSE_WIDTH_US */
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "999us should be rejected");
    EXPECT(cmd.kind == CMD_INVALID, "kind should be INVALID");
}

static void test_cfg_intervals_parses_list(void) {
    pitrac_cmd_t cmd;
    char line[] = "CFG PULSE_INTERVALS=0.7,1.8,3.0,0.0";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "intervals list should parse");
    EXPECT(cmd.kind == CMD_CFG_INTERVALS, "kind");
    EXPECT(cmd.u.intervals.count == 4, "four entries");
    EXPECT(cmd.u.intervals.intervals_ms[0] > 0.69f && cmd.u.intervals.intervals_ms[0] < 0.71f, "first value");
    EXPECT(cmd.u.intervals.intervals_ms[3] == 0.0f, "terminator");
}

static void test_unknown_command_invalid(void) {
    pitrac_cmd_t cmd;
    char line[] = "GARBAGE";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "GARBAGE should not parse");
}

static void test_fire_prefix_does_not_match_fireworks(void) {
    /* Regression test: older strncmp-based match accepted any trailing chars. */
    pitrac_cmd_t cmd;
    char line[] = "FIREWORKS";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "FIREWORKS should not match FIRE");
}

static void test_cfg_threshold_bad_value_rejected(void) {
    pitrac_cmd_t cmd;
    char line[] = "CFG MIC_THRESHOLD=abc";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "non-numeric threshold should be rejected");
}

static void test_cfg_armed_bad_value_rejected(void) {
    pitrac_cmd_t cmd;
    char line[] = "CFG ARMED=hello";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "non-numeric armed should be rejected");
}

static void test_cfg_intervals_with_bad_entry_rejected(void) {
    pitrac_cmd_t cmd;
    char line[] = "CFG PULSE_INTERVALS=1.0,abc,3.0";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "bad entry in intervals list should reject the whole list");
}

static void test_cam_pulse_parses_value(void) {
    pitrac_cmd_t cmd;
    char line[] = "CAM_PULSE 100";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "CAM_PULSE 100 should parse");
    EXPECT(cmd.kind == CMD_CAM_PULSE, "kind");
    EXPECT(cmd.u.u32 == 100u, "microseconds");
}

static void test_cam_pulse_missing_arg_invalid(void) {
    pitrac_cmd_t cmd;
    char line[] = "CAM_PULSE";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "CAM_PULSE without arg should fail");
    EXPECT(cmd.kind == CMD_INVALID, "kind INVALID");
}

static void test_cam_pulse_garbage_arg_invalid(void) {
    pitrac_cmd_t cmd;
    char line[] = "CAM_PULSE abc";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "CAM_PULSE abc should fail");
    EXPECT(cmd.kind == CMD_INVALID, "kind INVALID");
}

static void test_cam_pulse_zero_parses_for_clamp_at_strobe_layer(void) {
    /* Parser is permissive; bounds clamping happens in strobe_cam_pulse. */
    pitrac_cmd_t cmd;
    char line[] = "CAM_PULSE 0";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "CAM_PULSE 0 should parse (clamped later)");
    EXPECT(cmd.kind == CMD_CAM_PULSE, "kind");
    EXPECT(cmd.u.u32 == 0u, "value zero passes through");
}

static void test_cam_pulse_huge_parses_for_clamp_at_strobe_layer(void) {
    pitrac_cmd_t cmd;
    char line[] = "CAM_PULSE 1000000";  /* 1 second; will be clamped to 100 ms */
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "CAM_PULSE huge should parse (clamped later)");
    EXPECT(cmd.kind == CMD_CAM_PULSE, "kind");
    EXPECT(cmd.u.u32 == 1000000u, "value passes through");
}

static void test_cam_pulse_prefix_does_not_match_cam(void) {
    pitrac_cmd_t cmd;
    char line[] = "CAM_PULSEX 100";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "CAM_PULSEX should not match");
}

static void test_cfg_stream_rms_zero_stops(void) {
    pitrac_cmd_t cmd;
    char line[] = "CFG STREAM_RMS=0";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "STREAM_RMS=0 should parse");
    EXPECT(cmd.kind == CMD_CFG_STREAM_RMS, "kind");
    EXPECT(cmd.u.u32 == 0u, "zero passes through (stop)");
}

static void test_cfg_stream_rms_in_range(void) {
    pitrac_cmd_t cmd;
    char line[] = "CFG STREAM_RMS=20";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "STREAM_RMS=20 should parse");
    EXPECT(cmd.kind == CMD_CFG_STREAM_RMS, "kind");
    EXPECT(cmd.u.u32 == 20u, "20 hz passes through");
}

static void test_cfg_stream_rms_clamped_to_max(void) {
    pitrac_cmd_t cmd;
    char line[] = "CFG STREAM_RMS=999";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "STREAM_RMS=999 should parse (clamped)");
    EXPECT(cmd.kind == CMD_CFG_STREAM_RMS, "kind");
    EXPECT(cmd.u.u32 == STREAM_RMS_MAX_HZ, "out-of-range gets clamped");
}

/* --- dispatcher test (mock vtable captures the side effect) --- */

static uint32_t mock_cam_pulse_last_us = 0;
static int      mock_cam_pulse_calls = 0;
static void mock_cam_pulse(uint32_t us) {
    mock_cam_pulse_last_us = us;
    mock_cam_pulse_calls++;
}

static uint32_t mock_stream_rms_last_hz = 999;
static int      mock_stream_rms_calls   = 0;
static void mock_set_stream_rms_hz(uint32_t hz) {
    mock_stream_rms_last_hz = hz;
    mock_stream_rms_calls++;
}

static int mock_emit_log_calls = 0;
static void mock_emit_log(const char *msg) {
    (void)msg;
    mock_emit_log_calls++;
}

static void test_dispatcher_routes_cam_pulse_to_vtable(void) {
    hw_driver_t hw = (hw_driver_t){ .cam_pulse = mock_cam_pulse };
    pitrac_cmd_t cmd = { .kind = CMD_CAM_PULSE, .u.u32 = 250 };
    mock_cam_pulse_calls = 0;
    mock_cam_pulse_last_us = 0;
    cmd_dispatcher_apply(&cmd, &hw);
    EXPECT(mock_cam_pulse_calls == 1, "cam_pulse called once");
    EXPECT(mock_cam_pulse_last_us == 250u, "argument passed through");
}

static void test_dispatcher_routes_stream_rms_to_vtable(void) {
    hw_driver_t hw = (hw_driver_t){
        .set_stream_rms_hz = mock_set_stream_rms_hz,
        .emit_log = mock_emit_log,
    };
    mock_stream_rms_calls = 0;
    mock_stream_rms_last_hz = 999;
    mock_emit_log_calls = 0;
    pitrac_cmd_t cmd = { .kind = CMD_CFG_STREAM_RMS, .u.u32 = 25 };
    cmd_dispatcher_apply(&cmd, &hw);
    EXPECT(mock_stream_rms_calls == 1, "set_stream_rms_hz called once");
    EXPECT(mock_stream_rms_last_hz == 25u, "argument passed through");
    EXPECT(mock_emit_log_calls == 1, "emit_log called for the LOG line");

    cmd.u.u32 = 0;
    cmd_dispatcher_apply(&cmd, &hw);
    EXPECT(mock_stream_rms_last_hz == 0u, "zero argument forwards as stop");
}

int main(void) {
    test_fire_parses_correctly();
    test_status_parses_correctly();
    test_cfg_threshold_parses_value();
    test_cfg_pulse_width_in_range();
    test_cfg_pulse_width_out_of_range_rejected();
    test_cfg_intervals_parses_list();
    test_unknown_command_invalid();
    test_fire_prefix_does_not_match_fireworks();
    test_cfg_threshold_bad_value_rejected();
    test_cfg_armed_bad_value_rejected();
    test_cfg_intervals_with_bad_entry_rejected();
    test_cam_pulse_parses_value();
    test_cam_pulse_missing_arg_invalid();
    test_cam_pulse_garbage_arg_invalid();
    test_cam_pulse_zero_parses_for_clamp_at_strobe_layer();
    test_cam_pulse_huge_parses_for_clamp_at_strobe_layer();
    test_cam_pulse_prefix_does_not_match_cam();
    test_cfg_stream_rms_zero_stops();
    test_cfg_stream_rms_in_range();
    test_cfg_stream_rms_clamped_to_max();
    test_dispatcher_routes_cam_pulse_to_vtable();
    test_dispatcher_routes_stream_rms_to_vtable();

    if (failures > 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("%d tests passed\n", 22);
    return 0;
}

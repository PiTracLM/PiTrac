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

static void test_heartbeat_parses_correctly(void) {
    pitrac_cmd_t cmd;
    char line[] = "HEARTBEAT";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "HEARTBEAT should parse");
    EXPECT(cmd.kind == CMD_HEARTBEAT, "HEARTBEAT kind");
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
    /* Parser is permissive; clamping happens in strobe_cam_pulse. */
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

/* --- M4: interval-list validation --- */

static void test_cfg_intervals_empty_list_rejected(void) {
    /* Empty list is a false ACK: downstream keeps current intervals and the host
     * can't tell its CFG was a no-op. Reject like PULSE_WIDTH_US=0. */
    pitrac_cmd_t cmd;
    char line[] = "CFG PULSE_INTERVALS=";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "empty intervals list should be rejected");
    EXPECT(cmd.kind == CMD_INVALID, "kind INVALID for empty list");
}

static void test_cfg_intervals_overlong_list_rejected(void) {
    /* Overlong list must be rejected, not truncated — truncation hides the bad
     * payload from the host. Literal has 33 values (STROBE_MAX_PULSES is 32). */
    pitrac_cmd_t cmd;
    char line[] =
        "CFG PULSE_INTERVALS="
        "0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,"  /* 8  */
        "0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,"  /* 16 */
        "0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,"  /* 24 */
        "0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,"  /* 32 */
        "0.1";                                 /* 33 — one over */
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "33-entry intervals list should be rejected (not truncated)");
    EXPECT(cmd.kind == CMD_INVALID, "kind INVALID for overlong list");
}

static void test_cfg_intervals_out_of_range_rejected(void) {
    /* Each interval must be in [0, STROBE_MAX_INTERVAL_MS]; mirrors the
     * PULSE_WIDTH_US bound check at proto_parser.c:139. */
    pitrac_cmd_t cmd;
    char line[] = "CFG PULSE_INTERVALS=0.7,1.8,9999.0";  /* 9999 >> 1000 ms max */
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "interval > STROBE_MAX_INTERVAL_MS should be rejected");
    EXPECT(cmd.kind == CMD_INVALID, "kind INVALID for out-of-range interval");
}

static void test_cfg_intervals_boundary_values_accepted(void) {
    /* 0.0 and STROBE_MAX_INTERVAL_MS are both inclusive-valid. */
    pitrac_cmd_t cmd;
    char line[] = "CFG PULSE_INTERVALS=0.0,1000.0";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "boundary values 0.0 and 1000.0 should be accepted");
    EXPECT(cmd.kind == CMD_CFG_INTERVALS, "kind");
    EXPECT(cmd.u.intervals.count == 2, "two entries");
    EXPECT(cmd.u.intervals.intervals_ms[0] == 0.0f, "0.0 boundary");
    EXPECT(cmd.u.intervals.intervals_ms[1] >= 999.9f && cmd.u.intervals.intervals_ms[1] <= 1000.1f, "1000.0 boundary");
}

static void test_cfg_intervals_exactly_max_pulses_accepted(void) {
    /* count == STROBE_MAX_PULSES must pass; rejection is only for count >. */
    pitrac_cmd_t cmd;
    char line[] =
        "CFG PULSE_INTERVALS="
        "0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,"  /* 8  */
        "0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,"  /* 16 */
        "0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,"  /* 24 */
        "0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1";  /* 32 — exact max */
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(ok, "exactly STROBE_MAX_PULSES entries should be accepted");
    EXPECT(cmd.kind == CMD_CFG_INTERVALS, "kind");
    EXPECT(cmd.u.intervals.count == 32, "32 entries");
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

static int mock_heartbeat_calls = 0;
static void mock_heartbeat(void) { mock_heartbeat_calls++; }

static void test_dispatcher_routes_heartbeat_to_vtable(void) {
    /* HEARTBEAT reaches the vtable but stays silent — no LOG per ping. */
    hw_driver_t hw = (hw_driver_t){ .heartbeat = mock_heartbeat, .emit_log = mock_emit_log };
    mock_heartbeat_calls = 0;
    mock_emit_log_calls = 0;
    pitrac_cmd_t cmd = { .kind = CMD_HEARTBEAT };
    cmd_dispatcher_apply(&cmd, &hw);
    EXPECT(mock_heartbeat_calls == 1, "heartbeat called once");
    EXPECT(mock_emit_log_calls == 0, "heartbeat emits no LOG");
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

/* --- arm-quiet gate: the int64 widening must survive a loud RMS --- */

static int64_t mock_current_rms_value = 0;
static int64_t mock_current_rms(void) { return mock_current_rms_value; }

static int32_t mock_threshold_value = 0;
static int32_t mock_get_threshold(void) { return mock_threshold_value; }

static int      mock_set_armed_calls = 0;
static bool     mock_set_armed_last = false;
static void mock_set_armed(bool a) {
    mock_set_armed_calls++;
    mock_set_armed_last = a;
}

static void test_arm_refused_when_rms_exceeds_int32(void) {
    /* RMS past INT32_MAX: a narrowing int32 cast wraps negative and sails under
     * the quiet ceiling (arms a noisy room). int64 chain holds, arm refused. */
    hw_driver_t hw = (hw_driver_t){
        .current_rms = mock_current_rms,
        .get_threshold = mock_get_threshold,
        .set_armed = mock_set_armed,
        .emit_log = mock_emit_log,
    };
    mock_threshold_value = 4000;                 /* ceiling = 1000 after /4 */
    mock_current_rms_value = (int64_t)INT32_MAX + 1000;  /* would wrap negative */
    mock_set_armed_calls = 0;
    mock_emit_log_calls = 0;

    pitrac_cmd_t cmd = { .kind = CMD_CFG_ARMED, .u.armed = true };
    cmd_dispatcher_apply(&cmd, &hw);

    EXPECT(mock_set_armed_calls == 0, "loud room must NOT arm");
    EXPECT(mock_emit_log_calls == 1, "refusal logs once");
}

static void test_arm_allowed_when_quiet(void) {
    hw_driver_t hw = (hw_driver_t){
        .current_rms = mock_current_rms,
        .get_threshold = mock_get_threshold,
        .set_armed = mock_set_armed,
        .emit_log = mock_emit_log,
    };
    mock_threshold_value = 4000;                 /* ceiling = 1000 */
    mock_current_rms_value = 10;                 /* well under ceiling */
    mock_set_armed_calls = 0;
    mock_set_armed_last = false;

    pitrac_cmd_t cmd = { .kind = CMD_CFG_ARMED, .u.armed = true };
    cmd_dispatcher_apply(&cmd, &hw);

    EXPECT(mock_set_armed_calls == 1, "quiet room arms");
    EXPECT(mock_set_armed_last == true, "armed=true forwarded");
}

static void test_dispatcher_cmd_none_is_silent_noop(void) {
    /* Blank line parses to CMD_NONE; must not log "invalid command"
     * (regression: it once shared the CMD_INVALID arm). */
    hw_driver_t hw = (hw_driver_t){ .emit_log = mock_emit_log };
    mock_emit_log_calls = 0;
    pitrac_cmd_t cmd = { .kind = CMD_NONE };
    cmd_dispatcher_apply(&cmd, &hw);
    EXPECT(mock_emit_log_calls == 0, "CMD_NONE emits nothing");
}

static void test_dispatcher_cmd_invalid_still_logs(void) {
    /* Flip side: a genuinely bad command must still surface a LOG line. */
    hw_driver_t hw = (hw_driver_t){ .emit_log = mock_emit_log };
    mock_emit_log_calls = 0;
    pitrac_cmd_t cmd = { .kind = CMD_INVALID };
    cmd_dispatcher_apply(&cmd, &hw);
    EXPECT(mock_emit_log_calls == 1, "CMD_INVALID logs once");
}

static void test_whitespace_line_parses_to_cmd_none(void) {
    /* Parser turns a whitespace line into CMD_NONE (feeds the silent-noop path). */
    pitrac_cmd_t cmd;
    char line[] = "   ";
    bool ok = proto_parse_line(line, &cmd);
    EXPECT(!ok, "whitespace line is not a recognised command");
    EXPECT(cmd.kind == CMD_NONE, "whitespace line parses to CMD_NONE");
}

int main(void) {
    test_fire_parses_correctly();
    test_status_parses_correctly();
    test_heartbeat_parses_correctly();
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
    test_dispatcher_routes_heartbeat_to_vtable();
    test_dispatcher_routes_cam_pulse_to_vtable();
    test_dispatcher_routes_stream_rms_to_vtable();
    test_dispatcher_cmd_none_is_silent_noop();
    test_dispatcher_cmd_invalid_still_logs();
    test_whitespace_line_parses_to_cmd_none();
    test_arm_refused_when_rms_exceeds_int32();
    test_arm_allowed_when_quiet();
    test_cfg_intervals_empty_list_rejected();
    test_cfg_intervals_overlong_list_rejected();
    test_cfg_intervals_out_of_range_rejected();
    test_cfg_intervals_boundary_values_accepted();
    test_cfg_intervals_exactly_max_pulses_accepted();

    if (failures > 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("%d tests passed\n", 34);
    return 0;
}

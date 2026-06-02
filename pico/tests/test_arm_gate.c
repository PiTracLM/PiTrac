#include <stdint.h>
#include <stdio.h>

#include "arm_gate.h"

static int failures = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        failures++; \
    } \
} while (0)

#define MS(x) ((uint64_t)(x) * 1000ull)
static const uint32_t kTimeoutMs = 3000;  /* 3 s keep-alive window */

static void test_arm_sets_armed_and_deadline(void) {
    arm_gate_t g = {0};
    arm_gate_arm(&g, MS(1000), kTimeoutMs);
    EXPECT(g.armed, "arm raises the gate");
    EXPECT(g.deadline_us == MS(1000) + MS(3000), "deadline is now + timeout");
}

static void test_disarm_clears(void) {
    arm_gate_t g = {0};
    arm_gate_arm(&g, 0, kTimeoutMs);
    arm_gate_disarm(&g);
    EXPECT(!g.armed, "disarm clears the gate");
}

static void test_poll_before_deadline_keeps_armed(void) {
    arm_gate_t g = {0};
    arm_gate_arm(&g, 0, kTimeoutMs);
    EXPECT(!arm_gate_poll(&g, MS(2999)), "poll before deadline does not disarm");
    EXPECT(g.armed, "still armed just before the deadline");
}

static void test_poll_after_deadline_disarms_once(void) {
    arm_gate_t g = {0};
    arm_gate_arm(&g, 0, kTimeoutMs);
    EXPECT(arm_gate_poll(&g, MS(3001)), "poll past deadline disarms and reports it");
    EXPECT(!g.armed, "disarmed after the deadline");
    EXPECT(!arm_gate_poll(&g, MS(9999)), "second poll reports nothing (already disarmed)");
}

static void test_heartbeat_keeps_alive_then_drops_when_pings_stop(void) {
    /* Keep-alive: pings hold the gate armed; once they stop it drops within one
     * timeout window. */
    arm_gate_t g = {0};
    arm_gate_arm(&g, 0, kTimeoutMs);                 /* deadline = 3s */
    EXPECT(!arm_gate_poll(&g, MS(2900)), "alive at 2.9s");
    arm_gate_heartbeat(&g, MS(2900), kTimeoutMs);    /* deadline -> 5.9s */
    EXPECT(!arm_gate_poll(&g, MS(4000)), "kept alive past the original deadline by the ping");
    arm_gate_heartbeat(&g, MS(5800), kTimeoutMs);    /* deadline -> 8.8s */
    EXPECT(!arm_gate_poll(&g, MS(8700)), "still alive while pinged");
    /* Pings stop here. */
    EXPECT(arm_gate_poll(&g, MS(8801)), "drops one window after the last ping");
    EXPECT(!g.armed, "disarmed after the keep-alive lapses");
}

static void test_heartbeat_while_disarmed_is_noop(void) {
    arm_gate_t g = {0};
    arm_gate_heartbeat(&g, MS(1000), kTimeoutMs);
    EXPECT(!g.armed, "a ping never arms a disarmed gate");
    EXPECT(!arm_gate_poll(&g, MS(9999)), "and nothing to auto-disarm");
}

static void test_disarmed_never_times_out(void) {
    arm_gate_t g = {0};
    EXPECT(!arm_gate_poll(&g, UINT64_MAX), "a disarmed gate never reports a timeout");
}

int main(void) {
    test_arm_sets_armed_and_deadline();
    test_disarm_clears();
    test_poll_before_deadline_keeps_armed();
    test_poll_after_deadline_disarms_once();
    test_heartbeat_keeps_alive_then_drops_when_pings_stop();
    test_heartbeat_while_disarmed_is_noop();
    test_disarmed_never_times_out();

    if (failures > 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("7 tests passed\n");
    return 0;
}

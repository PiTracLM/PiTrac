#include "arm_gate.h"

static uint64_t deadline_at(uint64_t now_us, uint32_t timeout_ms) {
    return now_us + (uint64_t)timeout_ms * 1000u;
}

void arm_gate_arm(arm_gate_t *g, uint64_t now_us, uint32_t timeout_ms) {
    g->deadline_us = deadline_at(now_us, timeout_ms);
    g->armed = true;
}

void arm_gate_disarm(arm_gate_t *g) {
    g->armed = false;
}

void arm_gate_heartbeat(arm_gate_t *g, uint64_t now_us, uint32_t timeout_ms) {
    if (g->armed) {
        g->deadline_us = deadline_at(now_us, timeout_ms);
    }
}

bool arm_gate_poll(arm_gate_t *g, uint64_t now_us) {
    if (g->armed && now_us > g->deadline_us) {
        g->armed = false;
        return true;
    }
    return false;
}

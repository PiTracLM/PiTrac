/*
 * arm_gate.h — trigger arm gate + keep-alive deadline.
 *
 * Split out of main.c's DSP loop so the keep-alive/auto-disarm timing is
 * host-testable; safety-critical, since a stuck-armed gate hot-fires on room
 * noise. main.c owns one gate, mutated only on core 0, and mirrors .armed into
 * shared g_state for the DSP/strobe hot path.
 */

#ifndef PITRAC_PICO_ARM_GATE_H
#define PITRAC_PICO_ARM_GATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     armed;
    uint64_t deadline_us;   /* absolute us; valid only while armed */
} arm_gate_t;

void arm_gate_arm(arm_gate_t *g, uint64_t now_us, uint32_t timeout_ms);

/* Idempotent. */
void arm_gate_disarm(arm_gate_t *g);

/* Push the deadline out, but ONLY while armed: a ping never arms a disarmed gate. */
void arm_gate_heartbeat(arm_gate_t *g, uint64_t now_us, uint32_t timeout_ms);

/* Returns true only on the transition that just auto-disarmed, for a one-shot LOG. */
bool arm_gate_poll(arm_gate_t *g, uint64_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_PICO_ARM_GATE_H */

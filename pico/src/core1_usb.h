/*
 * core1_usb.h — interface to the USB CDC worker that runs on core 1.
 *
 * Public surface is intentionally tiny: launch the worker, and read the
 * shared runtime state (declared here so the strobe + DSP modules can
 * cheaply read g_state.armed in their hot path without going through a
 * multicore round-trip).
 */

#ifndef PITRAC_CORE1_USB_H
#define PITRAC_CORE1_USB_H

#include <stdbool.h>
#include "proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Arm-state request mailbox (core 1 → core 0). core 0 is the SOLE writer of
 * g_state.armed and g_state.arm_deadline_us; core 1 never writes them directly.
 * Instead it posts one of these and lets the DSP loop apply the change, which
 * removes the multi-writer race on `armed` and the torn 64-bit deadline store.
 * The rest of the MAILBOX_* sentinels live in config.h; these two slot into the
 * unused gaps in that namespace (0xA110C003 / 0xA110C007). */
#define MAILBOX_REQ_ARM       0xA110C003u   /* core1 → core0: arm + reset deadline */
#define MAILBOX_REQ_DISARM    0xA110C007u   /* core1 → core0: disarm now           */
#define MAILBOX_RING_OVERFLOW 0xA110C00Eu   /* core0 → core1: I2S ring lapped, audio dropped */

/* Bound for best-effort (droppable) FIFO pushes — telemetry and manual-fire
 * requests. Short on purpose: if the far core hasn't drained within this window
 * it's mid-train, and a dropped notify/request is preferable to stalling the
 * sender. Control messages (arm/disarm) use a blocking push and ignore this. */
#define FIFO_PUSH_TIMEOUT_US  1000u

/* Entry point — pass this to multicore_launch_core1(). */
void core1_usb_entry(void);

/* Initialize g_state at runtime. Must be called from main() before any
 * other module reads g_state. Replaces the static initializer to avoid a
 * .data-layout quirk that corrupts TinyUSB internal state at boot. */
void g_state_runtime_init(void);

/* Shared with core 0. See core1_usb.c for the concurrency contract. */
extern volatile pitrac_state_t g_state;

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_CORE1_USB_H */

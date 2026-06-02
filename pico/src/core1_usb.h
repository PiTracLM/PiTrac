/*
 * core1_usb.h — interface to the USB CDC worker on core 1.
 *
 * Surface is tiny: launch the worker and read shared runtime state. g_state is
 * declared here so strobe + DSP can read g_state.armed in their hot path
 * without a multicore round-trip.
 */

#ifndef PITRAC_CORE1_USB_H
#define PITRAC_CORE1_USB_H

#include <stdbool.h>
#include "proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Arm-state request mailbox (core 1 → core 0). core 0 is the SOLE writer of
 * g_state.armed and the arm_gate deadline; core 1 posts intent instead of
 * writing them, removing the multi-writer race on `armed` and the 64-bit
 * deadline. Other MAILBOX_* sentinels live in config.h; these fill the unused
 * gaps (0xA110C003 / 0xA110C007 / 0xA110C00F). */
#define MAILBOX_REQ_ARM       0xA110C003u   /* core1 → core0: arm + reset deadline */
#define MAILBOX_REQ_DISARM    0xA110C007u   /* core1 → core0: disarm now           */
#define MAILBOX_REQ_HEARTBEAT 0xA110C00Fu   /* core1 → core0: refresh arm deadline (keep-alive) */
#define MAILBOX_RING_OVERFLOW 0xA110C00Eu   /* core0 → core1: I2S ring lapped, audio dropped */

/* Bound for best-effort (droppable) FIFO pushes — telemetry and manual-fire.
 * Short on purpose: if the far core hasn't drained in this window it's mid-train,
 * and dropping beats stalling the sender. arm/disarm push blocking, ignore this. */
#define FIFO_PUSH_TIMEOUT_US  1000u

/* Pass to multicore_launch_core1(). */
void core1_usb_entry(void);

/* Init g_state; call from main() before any module reads it. Replaces the static
 * initializer to dodge a .data-layout quirk that corrupts TinyUSB state at boot. */
void g_state_runtime_init(void);

/* Shared with core 0. See core1_usb.c for the concurrency contract. */
extern volatile pitrac_state_t g_state;

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_CORE1_USB_H */

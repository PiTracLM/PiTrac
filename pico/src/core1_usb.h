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

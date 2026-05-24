/*
 * impact_detect.h — acoustic golf-impact detection.
 *
 * Algorithm summary (see impact_detect.c for full implementation notes):
 *
 *   1. I2S samples arrive at 48 kHz via PIO + DMA into the ring buffer.
 *   2. Decimate by 3 → 16 kHz with a 3-tap box-average anti-alias.
 *   3. Pre-compute fast (2-6 kHz band) and slow (<1 kHz band) energies
 *      using two single-pole IIR band-pass biquads in fixed-point Q15.
 *   4. Compute 1 ms RMS envelopes of both bands.
 *   5. Onset detection: high-band RMS jumps >18 dB over 4 ms baseline.
 *   6. Two-band ratio gate: high/low energy ratio > 2.0.
 *   7. Decay confirmation: high-band energy persists ≥40 ms post-onset.
 *   8. Debounce: 300 ms lockout after each successful trigger.
 *
 * The full energy + onset + ratio path runs on every 16 kHz sample (≈16 kHz
 * update rate); decay confirmation is a state machine that latches after
 * onset and watches for sustained energy. The complete inner loop costs
 * roughly 300 cycles/sample on Cortex-M0+ — well under our 7800 cycle
 * budget per 16 kHz sample at 125 MHz.
 *
 * All public state is module-internal; this header just declares the
 * lifecycle + per-tick entry points the rest of the firmware needs.
 */

#ifndef PITRAC_IMPACT_DETECT_H
#define PITRAC_IMPACT_DETECT_H

#include <stdbool.h>
#include <stdint.h>

#include "ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise detector state. Pass the ring buffer that I2S DMA fills. */
void impact_detect_init(ring_buffer_t *source);

/* Pull whatever samples are available from the ring buffer, run them
 * through the detector. Returns true exactly once per impact event, and
 * sets *rms_out to the peak high-band RMS value observed at the trigger
 * point. Should be called in a tight loop from core0's main thread.
 *
 * `armed` is the current host-arm state — if false, the detector still
 * processes audio (so the baseline tracking stays warm) but never returns
 * true. This way arming/disarming is glitch-free.
 */
bool impact_detect_step(bool armed, int32_t *rms_out);

/* Mutators for host CFG commands. Threshold is the linear RMS units a
 * sample's high-band RMS must exceed to *start* the onset evaluation —
 * acts as a noise floor gate before the 18 dB jump check runs. */
void impact_detect_set_threshold(int32_t threshold);
int32_t impact_detect_get_threshold(void);

/* Decay-confirm window: how long high-band energy must persist after onset
 * before we accept the impact. Clamped to 1..200 ms. Default comes from
 * DSP_DECAY_CONFIRM_MS in config.h. Tuned via CFG DECAY_CONFIRM_MS=<n>. */
void     impact_detect_set_decay_confirm_ms(uint32_t ms);
uint32_t impact_detect_get_decay_confirm_ms(void);

/* Snapshot of the latest high-band RMS the detector computed. Read from
 * core 1 to decide whether the room is quiet enough to honour a CFG ARMED=1
 * request — refusing the arm prevents auto-firing on a noise event's decay. */
int32_t impact_detect_current_rms(void);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_IMPACT_DETECT_H */

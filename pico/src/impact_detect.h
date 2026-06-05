/*
 * impact_detect.h — acoustic golf-impact detection.
 *
 * Algorithm summary (see impact_detect.c for full implementation notes):
 *
 *   1. I2S samples arrive at 48 kHz via PIO + DMA into the ring buffer.
 *   2. Decimate by 3 → 16 kHz with a 3-tap box-average anti-alias.
 *   3. Band-pass the high band (2-6 kHz) via two single-pole IIR low-passes
 *      in fixed-point Q15: high LPF − low LPF.
 *   4. Compute a 1 ms RMS envelope of the high band.
 *   5. Fire when armed and the high-band energy exceeds the threshold.
 *   6. Debounce: 300 ms lockout after each successful trigger.
 *
 * The inner loop is well under the 7800 cycle budget per 16 kHz sample at
 * 125 MHz.
 *
 * All state is module-internal; this header declares only the lifecycle and
 * per-tick entry points.
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

/* Drain available ring samples through the detector. Returns true once per
 * impact and sets *rms_out to the peak high-band RMS at the trigger point.
 * Call in a tight loop from core0's main thread.
 *
 * `armed` false: still process audio (baseline stays warm) but never fire, so
 * arm/disarm is glitch-free. */
bool impact_detect_step(bool armed, int32_t *rms_out);

/* Threshold = high-band RMS-squared energy the envelope must exceed to fire. */
void impact_detect_set_threshold(int32_t threshold);

/* Latest high-band RMS, read from core 1 to decide whether the room is quiet
 * enough to honour CFG ARMED=1 (refusing prevents auto-fire on a noise decay).
 *
 * int64 mean-square energy. A loud strike pushes the per-sample square past
 * INT32_MAX (crossover at band amplitude ~46k), so the old int32 return wrapped
 * negative; STATUS now emits this un-narrowed. EVENT strike rms and the
 * arm-quiet gate still go through int32 (last_trigger_rms) and keep wrapping
 * until that chain is widened separately. */
int64_t impact_detect_current_rms(void);

/* Peak windowed RMS since the last call (resets on read). The EVENT RMS mic-stream
 * telemetry uses this so a sub-sample ball-strike transient isn't missed between
 * emits (the instantaneous level falls between 100-500Hz samples). */
int64_t impact_detect_take_peak_rms(void);

/* Count of 16 kHz samples pushed through the DSP since init. Diagnostic — lets
 * a host confirm de-interleave + decimation realise fs/2/DSP_DECIMATION rather
 * than drifting. */
uint32_t impact_detect_processed_sample_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_IMPACT_DETECT_H */

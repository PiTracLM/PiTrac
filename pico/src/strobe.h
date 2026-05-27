/*
 * strobe.h — IR LED strobe + cam2 external trigger driver.
 *
 * Owns:
 *   - The ir_strobe PIO state machine (pio1, sm 0).
 *   - DMA channel 1 (one-shot, fed by strobe_compile_pulse_train).
 *   - The cam2 XTR GPIO (active-low pulse, driven from C — no PIO needed).
 *
 * Lifecycle:
 *   strobe_init()                          — once at boot
 *   strobe_set_pulse_train(intervals, ...) — whenever the host sends new CFG
 *   strobe_fire()                          — on impact or manual FIRE command
 */

#ifndef PITRAC_STROBE_H
#define PITRAC_STROBE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise PIO, DMA, GPIO, and load default pulse vector from config.h.
 * Call once after stdio_init_all(). Returns true on success. */
bool strobe_init(void);

/* Re-compile the strobe waveform with new tunables. Stashes the result in
 * an internal RAM buffer; the next strobe_fire() will use this pattern.
 *
 *   intervals_ms : array of inter-pulse intervals (rising-edge to rising-edge).
 *                  Length = `count`. A trailing 0.0 means "no pulse after this
 *                  one" — terminator, not a tight back-to-back pulse.
 *   count        : number of intervals (max STROBE_MAX_PULSES).
 *   pulse_width_us: HIGH duration of each pulse.
 *
 * Returns true on success; false if the compiled pattern would exceed the
 * internal buffer or any individual interval is negative.
 */
bool strobe_set_pulse_train(const float *intervals_ms,
                            uint8_t count,
                            float pulse_width_us);

/* Trigger one strobe sequence:
 *   1. Pulse cam2 XTR low → opens the camera shutter (~ms-scale latency).
 *   2. After a small setup delay, DMA the compiled pulse train into the
 *      strobe PIO FIFO. Pulses run cycle-accurately from PIO.
 *   3. Release cam2 XTR back high once the train completes.
 *
 * Non-blocking on the DMA side — we kick the DMA and return. The PIO will
 * keep clocking out pulses while core0 can go back to the DSP loop.
 *
 * Note: this *blocks* briefly on the XTR setup delay (currently 1 ms) so
 * the camera has time to arm its shutter. If that becomes a problem we can
 * move the XTR sequencing into a second PIO state machine.
 *
 * Returns false if the train was refused (no pattern compiled, hold gate
 * active, or fire-time sanity check tripped).
 */
bool strobe_fire(void);

/* Has the most-recent fire completed (DMA drained)? */
bool strobe_is_idle(void);

/* Like strobe_fire(), but additionally oversamples ADC channel 0 (which the
 * caller must have initialised on GP26 wired to V3 CUR-SENSE) for the entire
 * duration of the strobe train. Writes the peak ADC reading (0..4095) into
 * *peak_adc_out and the total sample count into *samples_out, then returns
 * the same true/false success flag as strobe_fire().
 *
 * Used by the Pi-side LED current calibration sweep: the Pico's PIO drives
 * the strobe with single-cycle determinism, the ADC samples in a tight loop
 * with no USB or Python jitter, and the host gets back one peak value per
 * DAC step. Total LED on-time per call matches a normal shot (~60 us across
 * the 7-pulse train) — inherently safe.
 */
bool strobe_fire_peak(uint16_t *peak_adc_out, uint32_t *samples_out);

/* Drive PIN_CAM2_XTR LOW for `microseconds`, then back HIGH. No PIO, no DMA,
 * no IR strobe pulse. Active-low to match strobe_fire. Bounds: 1..100000 us.
 * Refuses (no-op) while strobe_hold is asserted. */
void strobe_cam_pulse(uint32_t microseconds);

/* For STATUS reporting: read back what we have configured. */
float    strobe_get_pulse_width_us(void);
uint8_t  strobe_get_interval_count(void);
const float *strobe_get_intervals(void);

/* --- Sustained-on hold for LED-current calibration -----------------------
 *
 * `strobe_hold_assert()` takes PIN_STROBE_OUT away from the PIO state
 * machine and drives it HIGH as a regular SIO GPIO. While held, the gate
 * driver sinks DC current through the LED bank — only safe for short
 * windows because the IR LEDs are pulse-rated, not DC.
 *
 * SAFETY: a hardware timeout (200 ms) automatically releases the assertion
 * even if nothing else calls release(). Caller must invoke
 * `strobe_check_hold_timeout()` from the DSP loop so the timeout fires.
 *
 * While held, `strobe_fire()` is a no-op (silently refuses).
 */
bool strobe_hold_assert(void);
void strobe_hold_release(void);
bool strobe_is_held(void);

/* Call once per DSP-loop iteration. If the hold has been active longer
 * than STROBE_MAX_HOLD_MS, releases it automatically. */
void strobe_check_hold_timeout(void);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_STROBE_H */

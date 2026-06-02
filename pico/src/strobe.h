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

/* Init PIO, DMA, GPIO, load default pulse vector from config.h. Call once after
 * stdio_init_all(). */
bool strobe_init(void);

/* Re-compile the waveform with new tunables into an internal RAM buffer; next
 * strobe_fire() uses it.
 *
 *   intervals_ms : inter-pulse gaps — entry N is the LOW time after pulse N, not
 *                  the rising-edge period. Trailing 0.0 = terminator (no pulse
 *                  after), not a back-to-back pulse. Length = `count`.
 *   count        : number of intervals (max STROBE_MAX_PULSES).
 *   pulse_width_us: HIGH duration of each pulse.
 *
 * False if the pattern would overflow the buffer or any interval is negative.
 */
bool strobe_set_pulse_train(const float *intervals_ms,
                            uint8_t count,
                            float pulse_width_us);

/* Trigger one strobe sequence:
 *   1. Pulse cam2 XTR low → opens the shutter (~ms latency).
 *   2. After the XTR setup delay, DMA the compiled train into the PIO FIFO;
 *      pulses run cycle-accurately from PIO.
 *   3. Release cam2 XTR high once the train completes.
 *
 * Blocks briefly on the 1 ms XTR setup so the camera can arm its shutter, then
 * waits for the train. False if refused (no pattern, hold gate, sanity check).
 */
bool strobe_fire(void);

/* Deferred-completion fire for the GP9 FIRE_IN ISR, which must not block (a
 * wait-for-finish in an ISR stalls every IRQ for the whole train; the
 * pre-trigger sleep_ms could deadlock).
 *
 *   strobe_fire_begin() — hold/sanity check, open shutter, kick DMA, return.
 *                         Skips pre-trigger delay. False if refused or a fire is
 *                         already in flight (re-kick would corrupt it).
 *   strobe_fire_end()   — release cam XTR pins. Call from loop context once
 *                         strobe_is_idle() reports the train drained.
 */
bool strobe_fire_begin(void);
void strobe_fire_end(void);

/* Has the most-recent fire completed (DMA drained)? */
bool strobe_is_idle(void);

/* ADC mux mutual exclusion for off-strobe callers (core 1's VSYS read).
 * acquire() returns false if a FIRE_PEAK sweep owns the mux (skip the read); on
 * true it holds the ADC lock — select channel, read, release(). Keep the held
 * region short. */
bool strobe_adc_acquire(void);
void strobe_adc_release(void);

/* Like strobe_fire(), but oversamples ADC channel 0 (caller must have init'd it
 * on GP26 wired to V3 CUR-SENSE) for the whole train. Writes peak (0..4095) to
 * *peak_adc_out, sample count to *samples_out; same success flag as strobe_fire.
 *
 * For the Pi-side LED current calibration sweep: PIO drives the strobe with
 * single-cycle determinism, the ADC samples in a tight loop with no USB/Python
 * jitter, host gets one peak per DAC step. On-time per call matches a normal
 * shot (~60 us / 7-pulse train) — inherently safe.
 */
bool strobe_fire_peak(uint16_t *peak_adc_out, uint32_t *samples_out);

/* Drive PIN_CAM2_XTR LOW for `microseconds` then HIGH — no PIO/DMA/IR pulse.
 * Active-low like strobe_fire. Bounds 1..100000 us. No-op while hold asserted. */
void strobe_cam_pulse(uint32_t microseconds);

/* STATUS readback of configured values. */
float    strobe_get_pulse_width_us(void);
uint8_t  strobe_get_interval_count(void);
const float *strobe_get_intervals(void);

/* --- Sustained-on hold for LED-current calibration -----------------------
 *
 * strobe_hold_assert() takes PIN_STROBE_OUT from PIO and drives it HIGH as SIO.
 * While held the gate driver sinks DC through the LED bank — only safe briefly,
 * the IR LEDs are pulse-rated, not DC. strobe_fire() is a silent no-op while held.
 *
 * SAFETY: 200 ms timeout auto-releases even with no release() call; caller must
 * pump strobe_check_hold_timeout() from the DSP loop for it to fire.
 */
bool strobe_hold_assert(void);
void strobe_hold_release(void);
bool strobe_is_held(void);

/* Pump once per DSP-loop iteration; auto-releases a hold older than
 * STROBE_MAX_HOLD_MS. */
void strobe_check_hold_timeout(void);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_STROBE_H */

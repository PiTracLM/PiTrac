/*
 * impact_detect.c — acoustic impact detection DSP.
 *
 * Design choices:
 *
 * 1. Fixed-point throughout: M0+ has no FPU (each float op is a 50-200 cycle
 *    helper). int32_t state, Q15-ish IIR coefficients — fraction isn't
 *    formalised, multiplies just stay in int32 and shifts apply consistently.
 *
 * 2. Two single-pole IIR low-passes instead of biquads: ~8x fewer cycles/sample
 *    and selective enough to separate 2-6 kHz golf-impact from <1 kHz HVAC /
 *    footsteps. Band-pass = (6 kHz LPF) − (2 kHz LPF).
 *
 * 3. Envelope via running sum-of-squares, no sqrt: compare squared RMS to
 *    squared threshold/baseline. Same decisions, energy units.
 *
 * 4. Decimate 48 → 16 kHz: 3-tap box average then keep 1 of 3. The SPH0645's
 *    analog filter rolls off above 20 kHz, so we only fight digital aliasing
 *    of the band of interest (well below the 8 kHz Nyquist).
 *
 * State machine:
 *
 *   IDLE
 *     ├─ high-band energy > threshold AND ratio passes → ONSET
 *     │
 *   ONSET
 *     ├─ start decay-confirm timer (40 ms)
 *     ├─ wait for sustained energy through the timer
 *     │   ├─ energy drops below threshold/2 mid-timer → IDLE (false alarm)
 *     │   └─ timer expires with energy still elevated → TRIGGER
 *     │
 *   TRIGGER
 *     └─ return true, enter DEBOUNCE
 *
 *   DEBOUNCE
 *     └─ DSP_DEBOUNCE_MS pass → IDLE
 */

#include "impact_detect.h"

#include <stdlib.h>   /* abs */

#include "config.h"

typedef enum {
    ST_IDLE = 0,
    ST_ONSET_WATCH,
    ST_DEBOUNCE,
} state_t;

static struct {
    ring_buffer_t *src;
    /* Read on core 0 (DSP loop), written on core 1 (USB worker). volatile to
     * stop the compiler hoisting it across the inner loop — 32-bit reads on
     * M0+ are atomic, so no other sync needed. */
    volatile int32_t threshold;     /* high-band RMS-squared threshold, raw units */

    state_t        state;
    uint32_t       state_samples;   /* samples elapsed in current state */
    uint32_t       sample_count;    /* diagnostics */

    /* How long high-band energy must persist post-onset to accept the impact.
     * Host-tunable via CFG DECAY_CONFIRM_MS. */
    uint32_t       decay_confirm_samples;

    /* Single-pole IIR low-pass state (Q15 coefficients), one per band. */
    int32_t        lpf_lo_y;        /* output of <1 kHz low-pass */
    int32_t        lpf_hi_y;        /* output of <6 kHz low-pass */

    /* I2S slot phase. PIO emits left-then-right, so the first word out of a
     * freshly-reset ring is left. Toggle per raw word to track absolute L/R —
     * `sample_count` can't (it only advances per decimated output, which once
     * let right-slot zeros bleed into the decimator and dragged the realised
     * rate off 16 kHz). 0 = left. */
    uint8_t        lr_phase;

    /* Anti-alias decimator: sum of last 3 raw samples. */
    int32_t        dec_sum;
    uint8_t        dec_count;

    /* Sum-of-squares envelopes — running sums over RMS_WINDOW_SAMPLES, kept O(1)
     * via a circular history (subtract outgoing square, add incoming).
     *
     * Slots are int64: the per-sample square reaches ~1.7e10 (18b × 18b) >
     * INT32_MAX. Narrowing to int32 adds the full square but subtracts a
     * truncated one 16 samples later, so the running sum drifts
     * (observed: mic_rms = -1.6e9 with a working mic). */
    int64_t        sq_hi_sum;       /* high-band squared envelope */
    int64_t        sq_lo_sum;       /* low-band  squared envelope */
    int64_t        sq_hist_hi[DSP_RMS_WINDOW_SAMPLES];
    int64_t        sq_hist_lo[DSP_RMS_WINDOW_SAMPLES];
    uint16_t       sq_hist_idx;

    /* Baseline: slow exponential average of high-band envelope, compared
     * against the instantaneous envelope for the >18 dB jump. Updated every
     * ~4 ms (64 samples at 16 kHz). */
    int64_t        baseline_hi_sq;
    uint16_t       baseline_update_counter;

    /* Latched at onset for return to caller. */
    int32_t        last_trigger_rms;
} s;

/* IIR coefficients and baseline tuning live in config.h (DSP_LPF_ALPHA_*,
 * DSP_BASELINE_*). 1 kHz LPF output is the "low band"; (6 kHz − 1 kHz) is the
 * "high band". */

/* Unpack one I2S word to a signed 18-bit sample (SPH0645's usable range).
 *
 * Framing, traced against i2s_rx.pio: PIO samples on the first BCLK after LRCLK
 * flips, but SPH0645 Philips framing puts a dummy delay bit there and presents
 * the MSB on the *next* BCLK. With shift_left + 32-bit autopush the word lands:
 *
 *   [31]    = dummy delay bit (NOT the sign)
 *   [30:13] = 18-bit signed mantissa, MSB first (bit 30 = true sign)
 *   [12:0]  = trailing / undefined, last bit garbage per datasheet
 *
 * `(int32_t)raw >> 14` would sign-extend the dummy bit and halve/invert every
 * sample. Burning a non-sampling BCLK in the PIO would drop the dummy on the
 * wire, but that scope-verified timing loop (vijaymarupudi's workaround) is what
 * keeps the bits stable at 3 MHz BCLK — don't disturb it. Instead shift the
 * dummy off the top (lifts true MSB to bit 31), then >>14 sign-extends the top
 * 18 bits. Range ~±131071. Bit-30-is-sign per the PIO trace; confirm on a logic
 * analyser against the live mic before trusting it. */
static inline int32_t unpack_sample(uint32_t raw) {
    return ((int32_t)(raw << 1)) >> 14;
}

/* Update both single-pole LPFs and return the band-pass result.
 *
 * DSP_LPF_ALPHA_*_Q15 × err overflows int32 when err is full-scale (~±131k)
 * and alpha ~23000 (product ~3e9). Cast to int64 for the multiply, then >>15. */
static inline int32_t band_filter(int32_t x) {
    int32_t err_lo = x - s.lpf_lo_y;
    s.lpf_lo_y += (int32_t)(((int64_t)DSP_LPF_ALPHA_LO_Q15 * (int64_t)err_lo) >> 15);

    int32_t err_hi = x - s.lpf_hi_y;
    s.lpf_hi_y += (int32_t)(((int64_t)DSP_LPF_ALPHA_HI_Q15 * (int64_t)err_hi) >> 15);

    return s.lpf_hi_y - s.lpf_lo_y;   /* band-pass = 6 kHz LPF − 1 kHz LPF */
}

/* Circular-buffer envelope push. Slots are int64: square reaches ~1.7e10, and
 * narrowing breaks "subtract-outgoing == what-we-added" so the sum drifts. */
static inline void env_push_hi(int32_t sample) {
    int64_t sq = (int64_t)sample * sample;
    s.sq_hi_sum += sq;
    s.sq_hi_sum -= s.sq_hist_hi[s.sq_hist_idx];
    s.sq_hist_hi[s.sq_hist_idx] = sq;
}

static inline void env_push_lo(int32_t sample) {
    int64_t sq = (int64_t)sample * sample;
    s.sq_lo_sum += sq;
    s.sq_lo_sum -= s.sq_hist_lo[s.sq_hist_idx];
    s.sq_hist_lo[s.sq_hist_idx] = sq;
}

/* Run one 16-kHz sample through the pipeline. Returns true on a trigger event
 * and sets *rms_out to the high-band sum-of-squares at the trigger point. */
static bool process_sample(int32_t x, bool armed, int32_t *rms_out) {
    int32_t band_hi = band_filter(x);
    int32_t band_lo = s.lpf_lo_y;

    env_push_hi(band_hi);
    env_push_lo(band_lo);

    /* Advance index *after* both pushes so hi/lo reference the same slot. */
    s.sq_hist_idx++;
    if (s.sq_hist_idx >= DSP_RMS_WINDOW_SAMPLES) s.sq_hist_idx = 0;

    /* No divide-by-window: both sides of every comparison carry the same scale. */
    int64_t env_hi = s.sq_hi_sum;
    int64_t env_lo = s.sq_lo_sum;

    if (++s.baseline_update_counter >= DSP_BASELINE_UPDATE_INTERVAL) {
        s.baseline_update_counter = 0;
        /* Update only below threshold so impacts don't pull the baseline up. */
        if (env_hi < s.threshold * (int64_t)DSP_RMS_WINDOW_SAMPLES) {
            int64_t err = env_hi - s.baseline_hi_sq;
            s.baseline_hi_sq += err >> DSP_BASELINE_STEP_SHIFT;
            if (s.baseline_hi_sq < 1) s.baseline_hi_sq = 1;  /* never divide-by-zero */
        }
    }

    s.sample_count++;

    switch (s.state) {

    case ST_DEBOUNCE:
        s.state_samples++;
        if (s.state_samples >= DSP_DEBOUNCE_MS * DSP_SAMPLE_RATE_HZ / 1000u) {
            s.state = ST_IDLE;
            s.state_samples = 0;
        }
        return false;

    case ST_IDLE: {
        /* Gate 1: noise floor — high-band envelope above raw threshold. */
        if (env_hi < (int64_t)s.threshold * DSP_RMS_WINDOW_SAMPLES) return false;

        /* Gate 2: onset jump over baseline. Squared comparison, so square the
         * ratio: (2033/256)^2 ≈ 63.05 → env_hi > baseline * ~63. */
        int64_t jump_threshold = s.baseline_hi_sq * DSP_ONSET_JUMP_RATIO_SQUARED;
        if (env_hi < jump_threshold) return false;

        /* Gate 3: two-band ratio > 2.0, squared → env_hi > 4 * env_lo. */
        if (env_lo > 0 && env_hi < (env_lo * 4)) return false;
        if (!armed) {
            /* DSP still runs to keep the baseline warm; just don't fire. */
            return false;
        }

        s.state = ST_ONSET_WATCH;
        s.state_samples = 0;
        s.last_trigger_rms = (int32_t)(env_hi / DSP_RMS_WINDOW_SAMPLES);
        return false;
    }

    case ST_ONSET_WATCH: {
        s.state_samples++;
        /* Energy collapses to <1/4 threshold before the timer → transient
         * click, abandon. */
        if (env_hi < ((int64_t)s.threshold * DSP_RMS_WINDOW_SAMPLES) / 4) {
            s.state = ST_IDLE;
            s.state_samples = 0;
            return false;
        }
        /* Timer expired with energy still elevated → genuine impact. */
        if (s.state_samples >= s.decay_confirm_samples) {
            s.state = ST_DEBOUNCE;
            s.state_samples = 0;
            if (rms_out) *rms_out = s.last_trigger_rms;
            return true;
        }
        return false;
    }

    default:
        s.state = ST_IDLE;
        return false;
    }
}

void impact_detect_init(ring_buffer_t *source) {
    /* Explicit zero so a re-init from a debug entry point doesn't leave stale
     * envelopes behind (BSS only covers the first init). */
    for (uint32_t i = 0; i < DSP_RMS_WINDOW_SAMPLES; ++i) {
        s.sq_hist_hi[i] = 0;
        s.sq_hist_lo[i] = 0;
    }
    s.src = source;
    s.threshold = DSP_DEFAULT_THRESHOLD;
    s.state = ST_IDLE;
    s.state_samples = 0;
    s.sample_count = 0;
    s.lpf_lo_y = 0;
    s.lpf_hi_y = 0;
    s.lr_phase = 0;
    s.dec_sum = 0;
    s.dec_count = 0;
    s.sq_hi_sum = 0;
    s.sq_lo_sum = 0;
    s.sq_hist_idx = 0;
    s.baseline_hi_sq = 1;  /* never zero — used as a divisor proxy */
    s.baseline_update_counter = 0;
    s.last_trigger_rms = 0;
    s.decay_confirm_samples = DSP_DECAY_CONFIRM_MS * DSP_SAMPLE_RATE_HZ / 1000u;
}

void impact_detect_set_decay_confirm_ms(uint32_t ms) {
    /* Clamp 1..200 ms: <1 ms rounds to 0 samples (always-fire on any onset);
     * 200 ms is well past any real golf-impact decay tail. */
    if (ms < 1)   ms = 1;
    if (ms > 200) ms = 200;
    s.decay_confirm_samples = ms * DSP_SAMPLE_RATE_HZ / 1000u;
}

uint32_t impact_detect_get_decay_confirm_ms(void) {
    return s.decay_confirm_samples * 1000u / DSP_SAMPLE_RATE_HZ;
}

bool impact_detect_step(bool armed, int32_t *rms_out) {
    /* Drain the ring in chunks of 32 to amortise bookkeeping, then process each
     * sample in order so the state machine sees the data sequentially. */
    uint32_t buf[32];
    uint32_t got = ring_buffer_pop(s.src, buf, 32);
    if (got == 0u) return false;

    bool triggered = false;
    for (uint32_t i = 0; i < got; ++i) {
        /* SPH0645 (SEL=GND) outputs left-slot only; right slot reads zeros.
         * Toggle the phase anchored to the first word, and advance it before
         * any `continue` so parity never desyncs from the raw word stream. */
        bool is_right = (s.lr_phase != 0u);
        s.lr_phase ^= 1u;
        if (is_right) continue;

        int32_t raw_sample = unpack_sample(buf[i]);

        /* Decimate 48 → 16 kHz with a 3-tap moving average. */
        s.dec_sum += raw_sample;
        s.dec_count++;
        if (s.dec_count < DSP_DECIMATION) continue;

        int32_t decimated = s.dec_sum / (int32_t)DSP_DECIMATION;
        s.dec_sum = 0;
        s.dec_count = 0;

        if (process_sample(decimated, armed, rms_out)) {
            triggered = true;
            /* Don't break — finish the chunk so envelopes stay current through
             * the debounce window. */
        }
    }
    return triggered;
}

void impact_detect_set_threshold(int32_t threshold) {
    if (threshold < 1) threshold = 1;
    s.threshold = threshold;
}

/* Mean-square = sum-of-squares / window, same energy units as `s.threshold`
 * (NOT amplitude — name is loose, there's no sqrt). The detector trips on the
 * undivided env_hi vs threshold × WINDOW; this getter divides so callers
 * compare against threshold directly.
 *
 * int64: a real strike drives mean-square past INT32_MAX (crossover at band
 * amplitude ~46k), and the old int32 cast wrapped negative — fooling the
 * arm-quiet gate into thinking the room was silent. Reads `sq_hi_sum` updated
 * every sample on core 0; a torn 64-bit read is harmless (one-sample-stale). */
int64_t impact_detect_current_rms(void) {
    int64_t env = s.sq_hi_sum;
    if (env < 0) env = 0;
    return env / DSP_RMS_WINDOW_SAMPLES;
}

uint32_t impact_detect_processed_sample_count(void) {
    return s.sample_count;
}

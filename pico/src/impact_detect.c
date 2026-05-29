/*
 * impact_detect.c — acoustic impact detection DSP.
 *
 * Design choices and the reasoning behind them:
 *
 * 1. Fixed-point math throughout. RP2040 is Cortex-M0+ with no FPU; every
 *    floating-point operation is a software helper that costs 50-200 cycles.
 *    We use int32_t for state and Q15-ish scaling for IIR coefficients.
 *    The "ish" is because we don't formalise the fraction — what matters is
 *    that all multiplies stay within int32 range and the right shifts get
 *    applied consistently.
 *
 * 2. Two single-pole IIR band-pass filters instead of full biquads. A real
 *    biquad is more selective but a single-pole is enough to distinguish
 *    2-6 kHz golf-impact from <1 kHz HVAC / footsteps. The trade is ~8x
 *    fewer cycles per sample, which actually matters at 16 kHz × multi-
 *    band processing on M0+.
 *
 *    "Band-pass" via two single-pole filters: subtract the output of a
 *    low-pass with cutoff 6 kHz from the output of a low-pass with cutoff
 *    2 kHz to get the band. Equivalently we compute one LPF for each cutoff
 *    and use the difference. Cheap enough.
 *
 * 3. Envelope via running sum-of-squares (NOT sqrt). We compare squared
 *    RMS to squared threshold and squared baseline — drops one expensive
 *    sqrt per sample. Doesn't change the algorithm's decisions, just the
 *    units they're expressed in.
 *
 * 4. Decimation from 48 → 16 kHz: a 3-tap box filter (sum of three samples
 *    divided by 3) then drop two of every three samples. Not perfect anti-
 *    aliasing but the SPH0645's own analog filter rolls off well above
 *    20 kHz, so we're only fighting digital aliasing of the band
 *    of interest, which is well below 8 kHz Nyquist for 16 kHz.
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

/* --- module state --------------------------------------------------------- */

typedef enum {
    ST_IDLE = 0,
    ST_ONSET_WATCH,
    ST_DEBOUNCE,
} state_t;

static struct {
    ring_buffer_t *src;
    /* `threshold` is read on core 0 (DSP loop) and written on core 1 (USB
     * worker). Mark volatile so the compiler doesn't hoist it into a
     * register across iterations of the inner loop — single 32-bit reads
     * on M0+ are atomic, so no other sync is needed. */
    volatile int32_t threshold;     /* high-band RMS-squared threshold, raw units */

    state_t        state;
    uint32_t       state_samples;   /* samples elapsed in current state */
    uint32_t       sample_count;    /* monotonically increasing for diagnostics */

    /* How long high-band energy must persist after onset before we trust
     * it as a real impact. Host-tunable via CFG DECAY_CONFIRM_MS. */
    uint32_t       decay_confirm_samples;

    /* Single-pole IIR low-pass state (Q15 coefficients). One per band. */
    int32_t        lpf_lo_y;        /* output of <1 kHz low-pass */
    int32_t        lpf_hi_y;        /* output of <6 kHz low-pass */

    /* I2S slot phase. The PIO emits left-then-right every frame, so the very
     * first word out of a freshly-reset ring is the left slot. Toggle this per
     * raw word consumed to track absolute L/R position — `sample_count` can't,
     * it only advances per decimated *output*, so using it (as we once did)
     * let right-slot zeros bleed into the decimator and dragged the realised
     * rate off the 16 kHz every downstream constant assumes. 0 = left. */
    uint8_t        lr_phase;

    /* Anti-alias decimator state — sum of last 3 raw samples */
    int32_t        dec_sum;
    uint8_t        dec_count;

    /* Sum-of-squares envelopes — running sums over RMS_WINDOW_SAMPLES samples.
     * We maintain them with a circular history buffer so each new sample is
     * O(1): subtract the outgoing square, add the incoming square.
     *
     * History slots are int64 because the per-sample square can reach
     * ~1.7e10 (18-bit signed × 18-bit signed) which exceeds INT32_MAX. If
     * we narrowed them to int32 we'd add the full square but subtract a
     * truncated value 16 samples later — the running sum would drift to
     * nonsense (observed: mic_rms = -1.6e9 with a working mic). */
    int64_t        sq_hi_sum;       /* high-band squared envelope */
    int64_t        sq_lo_sum;       /* low-band  squared envelope */
    int64_t        sq_hist_hi[DSP_RMS_WINDOW_SAMPLES];
    int64_t        sq_hist_lo[DSP_RMS_WINDOW_SAMPLES];
    uint16_t       sq_hist_idx;

    /* Baseline tracker: slow exponential average of the high-band envelope.
     * Compared against the instantaneous envelope to detect the >18 dB jump.
     * Updated every ~4 ms (64 samples at 16 kHz). */
    int64_t        baseline_hi_sq;
    uint16_t       baseline_update_counter;

    /* Latched at onset for return to caller */
    int32_t        last_trigger_rms;
} s;

/* IIR coefficients and baseline-tracker tuning are in config.h
 * (DSP_LPF_ALPHA_*, DSP_BASELINE_*). We use the 1 kHz LPF output as the
 * "low band" energy directly, and (6kHz - 1kHz) as the "high band" — a
 * crude band-pass that's good enough for impact discrimination. */

/* --- helpers -------------------------------------------------------------- */

/* Pull one I2S word, unpack to a signed 18-bit sample (the SPH0645's actual
 * usable range), and return it as a 32-bit signed int.
 *
 * The framing reality, traced against i2s_rx.pio: the PIO starts sampling on
 * the very first BCLK after LRCLK flips. SPH0645 Philips framing puts a dummy
 * delay bit there and only presents the MSB on the *next* BCLK. So with our
 * shift_left + 32-bit autopush, the word actually lands as:
 *
 *   [31]    = dummy delay bit (NOT the sign)
 *   [30:13] = 18-bit signed mantissa, MSB first (bit 30 is the true sign)
 *   [12:0]  = trailing / undefined bits, last one garbage per the datasheet
 *
 * A bare `(int32_t)raw >> 14` would sign-extend from the dummy bit and halve
 * (or invert) every sample. We could instead burn a non-sampling BCLK in the
 * PIO to drop the dummy on the wire, but that scope-verified timing loop
 * (vijaymarupudi's workaround) is exactly what keeps the bits from shifting at
 * 3 MHz BCLK — not worth disturbing. So shift the dummy off the top first,
 * which lifts the true MSB into bit 31, then arithmetic-right-shift 14 to keep
 * the top 18 bits sign-extended. Result is a signed int in roughly ±131071.
 *
 * This assumes bit 30 is the sign, which is what the i2s_rx.pio trace says;
 * the mapping is only fully nailed down with a logic analyser on the live
 * mic, so confirm on a scope before trusting it in anger. */
static inline int32_t unpack_sample(uint32_t raw) {
    return ((int32_t)(raw << 1)) >> 14;
}

/* Update both single-pole LPFs and return the band-pass result.
 *
 * DSP_LPF_ALPHA_*_Q15 × err can blow past int32 in the corner case where
 * err is at full 18-bit range (~±131k) and alpha is ~23000 — the product
 * is ~3e9, over INT32_MAX. Cast to int64 so the multiply happens in 64-bit,
 * then >>15 back into int32 range. */
static inline int32_t band_filter(int32_t x) {
    /* Low-pass at 1 kHz */
    int32_t err_lo = x - s.lpf_lo_y;
    s.lpf_lo_y += (int32_t)(((int64_t)DSP_LPF_ALPHA_LO_Q15 * (int64_t)err_lo) >> 15);

    /* Low-pass at 6 kHz */
    int32_t err_hi = x - s.lpf_hi_y;
    s.lpf_hi_y += (int32_t)(((int64_t)DSP_LPF_ALPHA_HI_Q15 * (int64_t)err_hi) >> 15);

    /* Band-pass (1..6 kHz) = 6kHz-LPF minus 1kHz-LPF. */
    return s.lpf_hi_y - s.lpf_lo_y;
}

/* Push a new squared sample into the high-band envelope sum (circular).
 * `sample` is up to ~18 bits signed (~±131k); the square reaches ~1.7e10.
 * History slots must be int64 to match — narrowing would break the
 * "subtract the outgoing exactly equals what we added" invariant and the
 * running sum drifts. */
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

/* Process one 16-kHz sample through the full pipeline.
 * Returns: trigger event seen this sample (true/false) and via out param
 * the high-band sum-of-squares value at the moment of triggering. */
static bool process_sample(int32_t x, bool armed, int32_t *rms_out) {
    /* Filter into the two bands. */
    int32_t band_hi = band_filter(x);
    int32_t band_lo = s.lpf_lo_y;

    /* Update envelopes (rotates hist_idx). */
    env_push_hi(band_hi);
    env_push_lo(band_lo);

    /* Advance circular index *after* both pushes so they reference the same
     * slot. (Otherwise hi and lo would be off by one in the history ring.) */
    s.sq_hist_idx++;
    if (s.sq_hist_idx >= DSP_RMS_WINDOW_SAMPLES) s.sq_hist_idx = 0;

    /* Approximate RMS = sum-of-squares / window. We don't divide — both
     * sides of every comparison carry the same scale. */
    int64_t env_hi = s.sq_hi_sum;
    int64_t env_lo = s.sq_lo_sum;

    /* --- baseline tracker --- */
    if (++s.baseline_update_counter >= DSP_BASELINE_UPDATE_INTERVAL) {
        s.baseline_update_counter = 0;
        /* Only update when below threshold — don't let actual impacts pull
         * the baseline up. */
        if (env_hi < s.threshold * (int64_t)DSP_RMS_WINDOW_SAMPLES) {
            int64_t err = env_hi - s.baseline_hi_sq;
            s.baseline_hi_sq += err >> DSP_BASELINE_STEP_SHIFT;
            if (s.baseline_hi_sq < 1) s.baseline_hi_sq = 1;  /* never divide-by-zero */
        }
    }

    s.sample_count++;

    /* --- state machine --- */
    switch (s.state) {

    case ST_DEBOUNCE:
        s.state_samples++;
        if (s.state_samples >= DSP_DEBOUNCE_MS * DSP_SAMPLE_RATE_HZ / 1000u) {
            s.state = ST_IDLE;
            s.state_samples = 0;
        }
        return false;

    case ST_IDLE: {
        /* Gate 1: noise floor — high-band envelope above raw threshold? */
        if (env_hi < (int64_t)s.threshold * DSP_RMS_WINDOW_SAMPLES) return false;

        /* Gate 2: onset — jump over baseline. Compare env_hi vs
         * baseline_hi_sq * (DSP_ONSET_RATIO_X256/256)^2. We compare squared
         * values so we square the ratio: (2033/256)^2 ≈ 63.05. To avoid
         * overflow we factor: env_hi > baseline * 63 means
         * env_hi * 256 * 256 > baseline * 2033 * 2033. We just multiply
         * baseline by ~63 (shift-and-add: 64×base - base) and compare. */
        int64_t jump_threshold = s.baseline_hi_sq * DSP_ONSET_JUMP_RATIO_SQUARED;
        if (env_hi < jump_threshold) return false;

        /* Gate 3: two-band ratio. env_hi / env_lo > (DSP_BAND_RATIO_X256/256)^2.
         * (2.0)^2 = 4.0. Compare env_hi > 4 * env_lo. */
        if (env_lo > 0 && env_hi < (env_lo * 4)) return false;
        if (!armed) {
            /* DSP runs anyway so the baseline stays warm; just don't fire. */
            return false;
        }

        /* All three gates passed → start watching the decay window. */
        s.state = ST_ONSET_WATCH;
        s.state_samples = 0;
        s.last_trigger_rms = (int32_t)(env_hi / DSP_RMS_WINDOW_SAMPLES);
        return false;
    }

    case ST_ONSET_WATCH: {
        s.state_samples++;
        /* If energy collapses to near-baseline before the timer expires,
         * this was probably a transient click → reset. Use 1/4 of threshold
         * as the abandonment level. */
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

/* --- public API ---------------------------------------------------------- */

void impact_detect_init(ring_buffer_t *source) {
    /* Zero everything explicitly — we're a static struct so BSS would have
     * done it for us, but a re-init from a debug entry point shouldn't leave
     * stale envelopes behind. */
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
    /* Clamp to 1..200 ms — below 1 ms isn't meaningful at our 16 kHz rate
     * (would resolve to 0 samples = always-fire on any onset). 200 ms is a
     * sane ceiling that's well past any realistic golf-impact decay tail. */
    if (ms < 1)   ms = 1;
    if (ms > 200) ms = 200;
    s.decay_confirm_samples = ms * DSP_SAMPLE_RATE_HZ / 1000u;
}

uint32_t impact_detect_get_decay_confirm_ms(void) {
    return s.decay_confirm_samples * 1000u / DSP_SAMPLE_RATE_HZ;
}

bool impact_detect_step(bool armed, int32_t *rms_out) {
    /* Drain whatever the DMA has put in the ring. We pull in chunks of 32
     * to amortise the bookkeeping cost, then process each chunk one sample
     * at a time so the state machine sees the data in order. */
    uint32_t buf[32];
    uint32_t got = ring_buffer_pop(s.src, buf, 32);
    if (got == 0u) return false;

    bool triggered = false;
    for (uint32_t i = 0; i < got; ++i) {
        /* I2S stereo: SPH0645 only outputs on the left slot (SEL=GND); the
         * right slot reads as zeros. The PIO emits left-then-right, so we ride
         * a per-word toggle anchored to the first word out of the ring. Drop
         * the right slot and advance the phase before anything can `continue`
         * past it, so the parity never desyncs from the raw word stream. */
        bool is_right = (s.lr_phase != 0u);
        s.lr_phase ^= 1u;
        if (is_right) continue;

        int32_t raw_sample = unpack_sample(buf[i]);

        /* Decimate 48 kHz → 16 kHz with a 3-tap moving average. */
        s.dec_sum += raw_sample;
        s.dec_count++;
        if (s.dec_count < DSP_DECIMATION) continue;

        int32_t decimated = s.dec_sum / (int32_t)DSP_DECIMATION;
        s.dec_sum = 0;
        s.dec_count = 0;

        if (process_sample(decimated, armed, rms_out)) {
            triggered = true;
            /* Don't break — keep processing the rest of the chunk so the
             * envelopes stay current for the debounce window. We just
             * remember that we fired. */
        }
    }
    return triggered;
}

void impact_detect_set_threshold(int32_t threshold) {
    if (threshold < 1) threshold = 1;
    s.threshold = threshold;
}

int32_t impact_detect_get_threshold(void) {
    return s.threshold;
}

/* Returns mean-square = sum-of-squares / window — same energy units as
 * `s.threshold` (NOT amplitude). The name `current_rms` is loose shorthand;
 * the value is energy, no sqrt. Detector trips when env_hi (not divided by
 * window) exceeds threshold × WINDOW; this getter divides the sum by WINDOW so
 * callers compare on the same scale as threshold directly.
 *
 * Returned as int64: a real strike drives the mean-square well past INT32_MAX
 * (band amplitude ~46k is the crossover), and the old int32 cast wrapped that
 * to a negative number — which then fooled the arm-quiet gate into thinking
 * the room was silent. The whole comparison chain is already int64, so this
 * just stops truncating at the boundary. Reads `sq_hi_sum` updated every
 * sample on core 0; a torn 64-bit read is harmless — worst case the arm-quiet
 * check sees a one-sample-stale value. */
int64_t impact_detect_current_rms(void) {
    int64_t env = s.sq_hi_sum;
    if (env < 0) env = 0;
    return env / DSP_RMS_WINDOW_SAMPLES;
}

uint32_t impact_detect_processed_sample_count(void) {
    return s.sample_count;
}

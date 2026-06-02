/*
 * test_impact_detect.c — host-side checks for the acoustic DSP front end.
 * Driven through a real ring buffer so L/R parity is exercised, not mocked.
 *
 *   - de-interleave: only the left slot (SPH0645 data with SEL=GND) reaches the
 *     filters; the right slot's zeros never fold into the decimator;
 *   - realised rate is exactly fs/2/DSP_DECIMATION (16 kHz) — downstream timer
 *     and filter constants depend on it;
 *   - reported RMS stays non-negative even past the int32 mean-square overflow.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "impact_detect.h"
#include "ring_buffer.h"

static int failures = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        failures++; \
    } \
} while (0)

static ring_buffer_t g_ring;

/* Pack a signed sample into the raw I2S word the PIO produces so unpack_sample()
 * recovers it. Philips framing: bit 31 dummy delay, 18-bit mantissa in bits
 * 30..13; placing the MSB at bit 30 round-trips the firmware's `(raw<<1)>>14`. */
static uint32_t pack_left_word(int32_t sample18) {
    /* 18-bit signed (±131071) landed in bits 30..13. */
    uint32_t mantissa = (uint32_t)(sample18 & 0x3FFFF);   /* 18 bits */
    return mantissa << 13;
}

/* Feed n stereo frames with explicit L/R values. The mic only drives left
 * (SEL=GND tristates right to 0); the de-interleave test plants energy in the
 * right slot to prove the firmware discards it. */
static void push_stereo_frames_lr(int32_t left_value, int32_t right_value, uint32_t n) {
    uint32_t left_word = pack_left_word(left_value);
    uint32_t right_word = pack_left_word(right_value);  /* same word layout, right slot */
    for (uint32_t f = 0; f < n; ++f) {
        uint32_t base = g_ring.head & (I2S_RING_SAMPLES - 1u);
        g_ring.storage[base] = left_word;                                       /* left slot */
        g_ring.storage[(base + 1u) & (I2S_RING_SAMPLES - 1u)] = right_word;     /* right slot */
        g_ring.head += 2u;
    }
}

/* The common case: left carries the signal, right is the SEL=GND zero. */
static void push_stereo_frames(int32_t left_value, uint32_t n) {
    push_stereo_frames_lr(left_value, 0, n);
}

static void drain_all(bool armed) {
    int32_t rms = 0;
    /* Each step pulls up to 32 words. */
    while (ring_buffer_available(&g_ring) > 0u) {
        impact_detect_step(armed, &rms);
    }
}

/* De-interleave by contradiction: silence left, slam right with a loud square
 * wave. The band-pass passes that alternation, so any right-slot bleed lights
 * up the high band. Correct parity leaves only the flat left DC, which the
 * band-pass rejects → envelope near zero. */
static void test_only_left_slot_contributes(void) {
    /* Baseline: the energy the loud wave would produce if it were (wrongly) on
     * the left slot — what we're proving stays out of the result. */
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    const int32_t loud = 120000;  /* near full scale, alternated for strong AC */
    const uint32_t frames = (DSP_RMS_WINDOW_SAMPLES + 8u) * DSP_DECIMATION;
    for (uint32_t f = 0; f < frames; ++f) {
        int32_t lvl = ((f / DSP_DECIMATION) & 1u) ? -loud : loud;
        push_stereo_frames(lvl, 1u);  /* on the LEFT slot */
    }
    drain_all(false);
    int64_t leaked_if_on_left = impact_detect_current_rms();
    EXPECT(leaked_if_on_left > (int64_t)(1 << 28),
           "control: a loud left-slot wave must produce large high-band energy");

    /* Same wave on the RIGHT slot, left silent — correct de-interleave discards it. */
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    for (uint32_t f = 0; f < frames; ++f) {
        int32_t rt = ((f / DSP_DECIMATION) & 1u) ? -loud : loud;
        push_stereo_frames_lr(0, rt, 1u);  /* left silent, loud wave on RIGHT */
    }
    drain_all(false);

    /* Left flat zero → band-pass zero → envelope ~0. Floor sits orders of
     * magnitude below the control. */
    int64_t env = impact_detect_current_rms();
    EXPECT(env >= 0, "rms must be non-negative");
    EXPECT(env < (leaked_if_on_left >> 8),
           "right-slot energy must not leak into the left-only envelope");
}

/* The realised rate is fs/2/DSP_DECIMATION: half the words are the right slot
 * we discard, then we decimate the left stream by DSP_DECIMATION. */
static void test_processed_sample_count_matches_realised_rate(void) {
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);

    const uint32_t frames = 600u;          /* 600 left words after de-interleave */
    push_stereo_frames(3000, frames);
    drain_all(false);

    uint32_t expected = frames / DSP_DECIMATION;  /* one output per DSP_DECIMATION left words */
    EXPECT(impact_detect_processed_sample_count() == expected,
           "N stereo frames must yield N/DSP_DECIMATION processed samples");
}

/* Loud strike drives the high-band mean-square past INT32_MAX; the old int32
 * cast wrapped negative, the int64 getter must report the true positive.
 * Alternating level per decimation group swings the band-pass near full scale. */
static void test_loud_impulse_rms_does_not_wrap(void) {
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);

    const int32_t amp = 131071;  /* +full scale of the 18-bit mantissa */
    const uint32_t frames = (DSP_RMS_WINDOW_SAMPLES + 8u) * DSP_DECIMATION;
    for (uint32_t f = 0; f < frames; ++f) {
        int32_t level = ((f / DSP_DECIMATION) & 1u) ? -amp : amp;
        push_stereo_frames(level, 1u);
    }
    drain_all(false);

    int64_t env = impact_detect_current_rms();
    EXPECT(env > 0, "loud signal RMS must be positive (no int32 wrap)");
    EXPECT(env > (int64_t)INT32_MAX,
           "loud signal RMS must exceed INT32_MAX — proving the int64 path carries it");
}

/* Feed `decimated` output samples, draining as we go; return trigger count.
 * alternate=true flips level each sample → high-band AC (a strike);
 * alternate=false is constant DC, which the band-pass rejects (rumble). */
static uint32_t feed_count(int32_t amp, uint32_t decimated, bool armed, bool alternate) {
    uint32_t fires = 0;
    int32_t rms = 0;
    for (uint32_t k = 0; k < decimated; ++k) {
        int32_t level = (alternate && (k & 1u)) ? -amp : amp;
        push_stereo_frames(level, DSP_DECIMATION);   /* DSP_DECIMATION frames -> one output */
        while (ring_buffer_available(&g_ring) > 0u) {
            if (impact_detect_step(armed, &rms)) fires++;
        }
    }
    return fires;
}

/* Armed detector must fire on a loud in-band impact. */
static void test_fires_on_loud_impact_when_armed(void) {
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    uint32_t fires = feed_count(120000, 200, /*armed=*/true, /*alternate=*/true);
    EXPECT(fires >= 1, "armed detector must fire on a loud in-band impact");
}

/* Disarmed gate must produce nothing — DSP still runs (baseline warm) but can't fire. */
static void test_does_not_fire_when_disarmed(void) {
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    uint32_t fires = feed_count(120000, 200, /*armed=*/false, /*alternate=*/true);
    EXPECT(fires == 0, "disarmed detector must never fire, even on a loud impact");
}

/* Gate 1 (noise floor): a strike fires at the default threshold but is gated by
 * a floor above its energy. Moderate amplitude keeps energy inside int32 so the
 * floor can sit above it. */
static void test_noise_floor_threshold_gates_a_strike(void) {
    /* Warm the envelope disarmed to read this strike's energy. */
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    feed_count(10000, 60, /*armed=*/false, /*alternate=*/true);
    int64_t strike_energy = impact_detect_current_rms();   /* env_hi / window */
    EXPECT(strike_energy > 0, "control: the moderate strike has measurable energy");

    /* At the default threshold it fires... */
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    EXPECT(feed_count(10000, 200, true, true) >= 1, "strike fires at the default threshold");

    /* ...but a noise floor set above its energy gates it out. */
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    impact_detect_set_threshold((int32_t)(strike_energy * 2));
    EXPECT(feed_count(10000, 200, true, true) == 0, "strike below a higher noise floor must not fire");
}

/* Gate 3 (band ratio): constant loud DC has no high-band AC, so the band-pass
 * rejects it. Low-frequency rumble / a held DC offset must not trip the trigger. */
static void test_dc_rumble_does_not_fire(void) {
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    uint32_t fires = feed_count(120000, 200, true, /*alternate=*/false);
    EXPECT(fires == 0, "constant low-band/DC energy must not trip the impact detector");
}

/* Debounce: a sustained strike would re-onset every sample; the 300 ms lockout
 * collapses it to a single fire. */
static void test_debounce_suppresses_immediate_refire(void) {
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    uint32_t fires = feed_count(120000, 600, true, true);  /* 600 < the debounce window */
    EXPECT(fires == 1, "debounce must collapse a sustained strike to a single fire");
}

/* Decay confirmation: a click shorter than the decay-confirm window must be
 * rejected. Onset may start, but the energy has to persist before we trust it. */
static void test_short_transient_does_not_fire(void) {
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    uint32_t fires = feed_count(120000, 30, true, true);   /* ~2 ms strike, < 5 ms confirm */
    fires += feed_count(0, 120, true, true);               /* then quiet: onset abandons */
    EXPECT(fires == 0, "a transient shorter than the decay-confirm window must not fire");
}

int main(void) {
    test_only_left_slot_contributes();
    test_processed_sample_count_matches_realised_rate();
    test_loud_impulse_rms_does_not_wrap();
    test_fires_on_loud_impact_when_armed();
    test_does_not_fire_when_disarmed();
    test_noise_floor_threshold_gates_a_strike();
    test_dc_rumble_does_not_fire();
    test_debounce_suppresses_immediate_refire();
    test_short_transient_does_not_fire();

    if (failures > 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("9 tests passed\n");
    return 0;
}

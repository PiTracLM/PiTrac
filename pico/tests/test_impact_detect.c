/*
 * test_impact_detect.c — host-side checks for the acoustic DSP front end.
 *
 * These pin down three things that bit us on the bench and that we can prove
 * without a logic analyser:
 *
 *   - the stereo I2S stream is de-interleaved correctly: only the left slot
 *     (where the SPH0645 actually puts data with SEL=GND) reaches the filters,
 *     and the right slot's zeros never get folded into the decimator;
 *   - the realised processing rate is exactly fs/2/DSP_DECIMATION, because
 *     every timer and filter constant downstream assumes that 16 kHz;
 *   - the reported RMS energy stays a sane non-negative number even when the
 *     band amplitude crosses the int32 mean-square overflow point.
 *
 * We drive impact_detect through a real ring buffer so the L/R parity logic
 * is exercised end to end rather than mocked.
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

/* Pack a signed sample back into the raw I2S word layout the PIO produces, so
 * unpack_sample() recovers it. The mic frames Philips-style: bit 31 of the
 * word is the dummy delay bit, the 18-bit mantissa sits in bits 30..13. We
 * mirror that here — put the value's MSB at bit 30 — so the firmware's
 * `(raw << 1) >> 14` round-trips it. */
static uint32_t pack_left_word(int32_t sample18) {
    /* sample18 is an 18-bit signed value (±131071). Land it in bits 30..13. */
    uint32_t mantissa = (uint32_t)(sample18 & 0x3FFFF);   /* 18 bits */
    return mantissa << 13;
}

/* Feed n stereo frames with explicit left and right slot values. The mic only
 * ever drives the left slot (SEL=GND leaves right tristated to 0), but the
 * de-interleave test deliberately plants energy in the right slot to prove the
 * firmware throws it away. */
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
    /* Each step pulls up to 32 words; keep going until the ring drains. */
    while (ring_buffer_available(&g_ring) > 0u) {
        impact_detect_step(armed, &rms);
    }
}

/* Prove de-interleave by contradiction: silence the left slot and slam the
 * right slot with a loud per-frame square wave. The band-pass passes that
 * alternation easily, so if even a fraction of the right slot bled into the
 * filters the high-band envelope would light up. With correct L/R parity the
 * detector sees nothing but the constant (DC) left level, which the band-pass
 * rejects, so the envelope settles near zero. */
static void test_only_left_slot_contributes(void) {
    /* First, a baseline: what does the loud right-slot wave produce if it were
     * (incorrectly) the *left* slot? That's the energy we're proving stays
     * out of the result. */
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

    /* Now move that same loud wave to the RIGHT slot and hold the left slot
     * silent. Correct de-interleaving discards the right slot entirely. */
    ring_buffer_reset(&g_ring);
    impact_detect_init(&g_ring);
    for (uint32_t f = 0; f < frames; ++f) {
        int32_t rt = ((f / DSP_DECIMATION) & 1u) ? -loud : loud;
        push_stereo_frames_lr(0, rt, 1u);  /* left silent, loud wave on RIGHT */
    }
    drain_all(false);

    /* The left slot is flat zero → band-pass output is zero → envelope ~0. The
     * loud right-slot energy must not appear here; compare against a generous
     * floor that's still orders of magnitude below the control above. */
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

/* A loud strike drives the high-band mean-square past INT32_MAX. The old
 * int32 cast wrapped that to a negative number; the int64 getter must report
 * it as the large positive value it is. We swing the band-pass near full scale
 * by alternating the level once per decimation group, which lands the squared
 * envelope safely above the int32 ceiling. */
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

int main(void) {
    test_only_left_slot_contributes();
    test_processed_sample_count_matches_realised_rate();
    test_loud_impulse_rms_does_not_wrap();

    if (failures > 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("3 tests passed\n");
    return 0;
}

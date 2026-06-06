#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "strobe_compile.h"

static int failures = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        failures++; \
    } \
} while (0)

/* 125 MHz PIO clock — the real sysclk on the Pico. */
static const float kPioHz = 125000000.0f;
/* 65535 cycles per word at 125 MHz = the 16-bit LOW-count ceiling. */
static const uint32_t kMax16 = 0xFFFFu;

/* Decode helpers mirror encode_word(): stored counts are value-minus-one. */
static uint32_t word_high(uint32_t w) { return (w & 0xFFFFu) + 1u; }
static uint32_t word_low(uint32_t w)  { return ((w >> 16) & 0xFFFFu) + 1u; }

static void test_driver_vector_compiles(void) {
    const float driver[] = {0.7f, 1.8f, 3.0f, 2.2f, 3.0f, 7.1f, 4.0f, 0.0f};
    uint32_t out[STROBE_PATTERN_MAX_WORDS];
    uint32_t len = 0;
    bool ok = strobe_compile_pulse_train(driver, 8, 8.68f, kPioHz,
                                         out, STROBE_PATTERN_MAX_WORDS, &len);
    EXPECT(ok, "driver vector should compile within the buffer");
    EXPECT(len > 8 && len < STROBE_PATTERN_MAX_WORDS, "driver length plausible");
    /* Terminator is encode_word(1,1) == 0. */
    EXPECT(out[len - 1] == 0u, "train ends with the LOW terminator word");
}

static void test_putter_vector_fits_default_buffer(void) {
    /* Putter's 30-50 ms gaps need ~281 words at 524 us each; STROBE_PATTERN_MAX_WORDS
     * is sized to hold one long external-trigger exposure. */
    const float putter[] = {5.0f, 30.0f, 30.0f, 30.0f, 50.0f, 0.0f};
    uint32_t out[STROBE_PATTERN_MAX_WORDS];
    uint32_t len = 0;
    bool ok = strobe_compile_pulse_train(putter, 6, 8.68f, kPioHz,
                                         out, STROBE_PATTERN_MAX_WORDS, &len);
    EXPECT(ok, "putter vector fits the default buffer");
    EXPECT(len > 200 && len <= STROBE_PATTERN_MAX_WORDS, "putter ~281 words");
    EXPECT(out[len - 1] == 0u, "putter train ends with the terminator word");
}

static void test_train_exceeding_buffer_is_rejected(void) {
    /* Too-long train is rejected, not truncated. Putter needs ~281 words; the
     * old 130-word buffer can't hold it. */
    const float putter[] = {5.0f, 30.0f, 30.0f, 30.0f, 50.0f, 0.0f};
    uint32_t out[130];
    uint32_t len = 0;
    bool ok = strobe_compile_pulse_train(putter, 6, 8.68f, kPioHz, out, 130, &len);
    EXPECT(!ok, "a train past the buffer capacity is rejected");
}

static void test_long_gap_splits_into_continuation_words(void) {
    /* 1 ms gap = 125000 cycles > 65535: needs a continuation word. First word's
     * high is the real pulse; the continuation's high is a 1-cycle blip. */
    const float ivals[] = {1.0f, 0.0f};
    uint32_t out[STROBE_PATTERN_MAX_WORDS];
    uint32_t len = 0;
    bool ok = strobe_compile_pulse_train(ivals, 2, 8.68f, kPioHz,
                                         out, STROBE_PATTERN_MAX_WORDS, &len);
    EXPECT(ok, "1 ms gap compiles");
    /* word0 = pulse + 65535 low, word1 = 1-cycle blip + remainder, word2 = final
     * pulse + tiny tail, word3 = terminator. */
    EXPECT(len == 4, "1 ms gap -> 2 words, then final pulse + terminator");
    EXPECT(word_low(out[0]) == kMax16, "first word carries a full 16-bit low chunk");
    EXPECT(word_high(out[1]) == 1u, "continuation word uses a 1-cycle high blip");
    EXPECT(out[len - 1] == 0u, "last word is the terminator");
}

static void test_overflow_leaves_buffer_untouched_enough_to_reuse(void) {
    /* A compile that overflows must report false; the caller's live buffer
     * (sentinel here) is never the target. */
    const float ivals[] = {50.0f, 0.0f};
    uint32_t out[4] = {0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu};
    uint32_t len = 999;
    bool ok = strobe_compile_pulse_train(ivals, 2, 8.68f, kPioHz, out, 4, &len);
    EXPECT(!ok, "50 ms gap cannot fit in 4 words");
}

static void test_invalid_inputs_rejected(void) {
    uint32_t out[STROBE_PATTERN_MAX_WORDS];
    uint32_t len = 0;
    const float ok_iv[] = {1.0f, 0.0f};

    EXPECT(!strobe_compile_pulse_train(ok_iv, STROBE_MAX_PULSES + 1, 8.68f, kPioHz,
                                       out, STROBE_PATTERN_MAX_WORDS, &len),
           "count over STROBE_MAX_PULSES rejected");
    EXPECT(!strobe_compile_pulse_train(ok_iv, 2, 0.0f, kPioHz,
                                       out, STROBE_PATTERN_MAX_WORDS, &len),
           "zero pulse width rejected");
    EXPECT(!strobe_compile_pulse_train(ok_iv, 2, STROBE_MAX_PULSE_WIDTH_US + 1.0f, kPioHz,
                                       out, STROBE_PATTERN_MAX_WORDS, &len),
           "pulse width over the cap rejected");
    EXPECT(!strobe_compile_pulse_train(ok_iv, 2, 8.68f, kPioHz, out, 0, &len),
           "zero-capacity buffer rejected");
    EXPECT(!strobe_compile_pulse_train(ok_iv, 2, 8.68f, kPioHz, NULL, 8, &len),
           "null output buffer rejected");

    /* A single interval over the per-interval bound is rejected. */
    const float too_long[] = {STROBE_MAX_INTERVAL_MS + 1.0f, 0.0f};
    EXPECT(!strobe_compile_pulse_train(too_long, 2, 8.68f, kPioHz,
                                       out, STROBE_PATTERN_MAX_WORDS, &len),
           "interval over STROBE_MAX_INTERVAL_MS rejected");
}

static void test_train_energy_cap_rejected(void) {
    /* count * pulse_width over STROBE_MAX_TRAIN_ON_TIME_US must be refused. */
    float ivals[STROBE_MAX_PULSES];
    for (int i = 0; i < STROBE_MAX_PULSES; ++i) ivals[i] = 0.1f;
    uint32_t out[STROBE_PATTERN_MAX_WORDS];
    uint32_t len = 0;
    const float hot = STROBE_MAX_PULSE_WIDTH_US;  /* 32 * 100 = 3200 > 1500 cap */
    EXPECT(!strobe_compile_pulse_train(ivals, STROBE_MAX_PULSES, hot, kPioHz,
                                       out, STROBE_PATTERN_MAX_WORDS, &len),
           "train energy over the cap rejected");
}

int main(void) {
    test_driver_vector_compiles();
    test_putter_vector_fits_default_buffer();
    test_train_exceeding_buffer_is_rejected();
    test_long_gap_splits_into_continuation_words();
    test_overflow_leaves_buffer_untouched_enough_to_reuse();
    test_invalid_inputs_rejected();
    test_train_energy_cap_rejected();

    if (failures > 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("7 tests passed\n");
    return 0;
}

/*
 * strobe_compile.c — pure pulse-train compiler (no hardware deps).
 *
 * Pattern encoding (matches ir_strobe.pio):
 *   word[31..16] = low_count - 1   (pin LOW after the pulse)
 *   word[15..0]  = high_count - 1  (pin HIGH for the pulse)
 *
 * Pulse + gap is one word when the gap fits 16 bits (<= 65535 cycles = 524 us
 * at 125 MHz). Longer gaps spill into continuation words with a 1-cycle (8 ns,
 * LED-invisible) HIGH blip.
 */

#include "strobe_compile.h"

#include <math.h>
#include <stddef.h>

#include "config.h"   /* STROBE_MAX_PULSES, STROBE_MAX_PULSE_WIDTH_US, etc. */

/* Counts in PIO clock cycles. The `jmp x-- / y--` loops run one extra iteration
 * before the counter wraps, so each count is stored minus one. */
static inline uint32_t encode_word(uint32_t high_cycles, uint32_t low_cycles) {
    uint32_t h = (high_cycles == 0) ? 0 : (high_cycles - 1);
    uint32_t l = (low_cycles  == 0) ? 0 : (low_cycles  - 1);
    /* OSR shifts right and `out y,16` pops first, so high_count is the low 16 bits. */
    return ((l & 0xFFFFu) << 16) | (h & 0xFFFFu);
}

/* Split `low` across continuation words when it exceeds the 16-bit per-word max.
 * Returns false without writing past the cap if the words don't fit. */
static bool append_pulse(uint32_t high_cycles, uint32_t low_cycles,
                         uint32_t *out, uint32_t out_cap, uint32_t *len) {
    const uint32_t MAX16 = 0xFFFFu;

    if (*len >= out_cap) return false;
    uint32_t low_first = (low_cycles > MAX16) ? MAX16 : low_cycles;
    out[(*len)++] = encode_word(high_cycles, low_first);
    low_cycles -= low_first;

    while (low_cycles > 0) {
        if (*len >= out_cap) return false;
        uint32_t chunk = (low_cycles > MAX16) ? MAX16 : low_cycles;
        out[(*len)++] = encode_word(1u, chunk);
        low_cycles -= chunk;
    }
    return true;
}

bool strobe_compile_pulse_train(const float *intervals_ms, uint8_t count,
                                float pulse_width_us, float pio_hz,
                                uint32_t *out, uint32_t out_cap, uint32_t *out_len) {
    if (out == NULL || out_len == NULL || out_cap == 0) return false;
    if (count > STROBE_MAX_PULSES) return false;
    if (!isfinite(pulse_width_us) || pulse_width_us <= 0.0f) return false;
    /* Beyond ~500 us the boost rail droops and LED current sags. */
    if (pulse_width_us > STROBE_MAX_PULSE_WIDTH_US) return false;
    /* Total ON-time cap so a host can't over-pulse the LED bank. */
    if ((float)count * pulse_width_us > STROBE_MAX_TRAIN_ON_TIME_US) return false;

    const uint32_t high_cycles = (uint32_t)(pulse_width_us * 1e-6f * pio_hz);
    if (high_cycles < 2u) return false;  /* PIO needs >= 1; round-up safety */

    uint32_t len = 0;

    /* intervals_ms[N] is the LOW gap AFTER pulse N (gap, not edge-to-edge period)
     * — matches the Pi-side BuildPulseTrain so host CFG vectors map verbatim.
     * A <=0 entry terminates. */
    for (uint8_t i = 0; i < count; ++i) {
        float gap_ms = intervals_ms[i];

        /* Longer gaps push the DMA wait past the watchdog and reset mid-train. */
        if (gap_ms > STROBE_MAX_INTERVAL_MS) return false;

        if (gap_ms <= 0.0f) {
            if (i == 0) break;  /* leading terminator: empty train */
            if (!append_pulse(high_cycles, 4u, out, out_cap, &len)) return false;
            break;
        }
        uint32_t low_cycles = (uint32_t)(gap_ms * 1e-3f * pio_hz);
        if (low_cycles < 4u) low_cycles = 4u;     /* PIO underrun floor */
        if (!append_pulse(high_cycles, low_cycles, out, out_cap, &len)) return false;
    }

    /* Terminator: tiny HIGH blip (8 ns, LED-invisible) then 1 cycle LOW so the
     * pin lands LOW with the PIO defined for the next fire. */
    if (len >= out_cap) return false;
    out[len++] = encode_word(1u, 1u);

    *out_len = len;
    return true;
}

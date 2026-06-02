/*
 * strobe_compile.h — pure IR-strobe pulse-train compiler.
 *
 * Split from strobe.c so the cycle math can run under host unit tests without
 * the Pico SDK / PIO hardware. strobe.c calls it with the live PIO clock.
 */

#ifndef PITRAC_PICO_STROBE_COMPILE_H
#define PITRAC_PICO_STROBE_COMPILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compile `intervals_ms` (count entries, the LOW gap after each pulse; a <=0
 * entry terminates) into `out` (out_cap 32-bit PIO words), with a
 * `pulse_width_us` HIGH pulse at PIO clock `pio_hz`.
 *
 * On success: *out_len gets the word count (<= out_cap, includes the trailing
 * terminator) and returns true. Returns false on any rejection (bad count,
 * pulse width out of range, ON-time cap, interval over bound, or not fitting
 * out_cap), leaving *out_len unspecified. Never writes past out[out_cap-1], so
 * a caller can compile into scratch and commit only on success. */
bool strobe_compile_pulse_train(const float *intervals_ms, uint8_t count,
                                float pulse_width_us, float pio_hz,
                                uint32_t *out, uint32_t out_cap, uint32_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_PICO_STROBE_COMPILE_H */

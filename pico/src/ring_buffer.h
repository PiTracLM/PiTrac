/*
 * ring_buffer.h — single-producer / single-consumer ring of 32-bit samples.
 *
 * Used to decouple the I2S DMA (producer — fills via hardware) from the DSP
 * (consumer — reads via core0). Lock-free by construction: the producer only
 * writes `head`, the consumer only writes `tail`, and a memory barrier on
 * RP2040 is a no-op for plain word-aligned stores (Cortex-M0+ has a strongly
 * ordered memory model on its single internal bus).
 *
 * Size must be a power of two (compile-time enforced in config.h via
 * I2S_RING_LOG2). We bitmask the indices into the storage array — cheap on
 * M0+ which has no division/modulo support in hardware.
 *
 * The "samples" we store are raw 32-bit I2S words straight out of the PIO
 * RX FIFO. The MSB-aligned 18-bit SPH0645 mantissa lives in bits 31..14
 * after the chip's documented one-bit delay and undefined LSB. We don't
 * normalise here — that's the DSP's job.
 */

#ifndef PITRAC_RING_BUFFER_H
#define PITRAC_RING_BUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* `volatile` because both cores (and DMA, indirectly via memory) touch
     * head/tail without a lock. The compiler must not hoist these into
     * registers across the read loop. */
    volatile uint32_t head;   /* producer writes here */
    volatile uint32_t tail;   /* consumer reads here  */

    /* The actual storage. Must be aligned to its total size in bytes because
     * the I2S RX DMA uses ring-buffer mode (channel_config_set_ring with
     * size_bits = log2(I2S_RING_SAMPLES * 4)), which wraps the write address
     * by masking the low bits — only works if the base address is a multiple
     * of the wrap size. Without this alignment, the DMA writes past the end
     * of storage into adjacent BSS, corrupting whatever module's globals live
     * next (observed to clobber cyw43-driver SPI state, hanging the LED and
     * USB CDC). I2S_RING_SAMPLES * 4 = 8192 bytes here. */
    uint32_t storage[I2S_RING_SAMPLES]
        __attribute__((aligned(I2S_RING_SAMPLES * sizeof(uint32_t))));
} ring_buffer_t;

/* Zero head/tail. Safe to call before DMA starts, NOT safe to call while
 * DMA is actively writing (you'd race with the producer). */
void ring_buffer_reset(ring_buffer_t *rb);

/* Number of unread samples currently in the ring. Snapshot — value may go
 * up by the time you act on it (producer can race ahead). */
uint32_t ring_buffer_available(const ring_buffer_t *rb);

/* Pop up to `max` samples into `dst`. Returns the count actually copied.
 * Non-blocking: returns 0 if empty. */
uint32_t ring_buffer_pop(ring_buffer_t *rb, uint32_t *dst, uint32_t max);

/* Peek at the next sample without removing it. Returns false if empty.
 * Used by the DSP onset check — we only consume if a full RMS window's
 * worth is available. */
bool ring_buffer_peek(const ring_buffer_t *rb, uint32_t *out);

/* Drop `n` samples (advance tail). Used after peek-driven consumption. */
void ring_buffer_skip(ring_buffer_t *rb, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_RING_BUFFER_H */

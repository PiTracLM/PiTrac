/*
 * ring_buffer.h — SPSC ring of 32-bit samples.
 *
 * Decouples I2S DMA producer from the core0 DSP consumer. Lock-free: producer
 * writes only head, consumer writes only tail; no barrier needed since M0+ is
 * strongly ordered for word-aligned stores on its single internal bus.
 *
 * Size must be a power of two (enforced in config.h via I2S_RING_LOG2); indices
 * are bitmasked (M0+ has no HW divide).
 *
 * Stores raw 32-bit I2S words from the PIO RX FIFO. SPH0645's Philips delay
 * puts a dummy in bit 31, so the 18-bit signed mantissa sits in bits 30..13
 * (see unpack_sample in impact_detect.c). Not normalised here — DSP's job.
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
    /* volatile: touched lock-free by both cores; must not be hoisted into
     * registers across the read loop. */
    volatile uint32_t head;   /* producer writes here */
    volatile uint32_t tail;   /* consumer reads here  */

    /* Must be aligned to its total byte size: I2S RX DMA ring mode
     * (channel_config_set_ring, size_bits = log2(I2S_RING_SAMPLES*4)) wraps the
     * write address by masking low bits, which requires the base to be a
     * multiple of the wrap size. Misaligned, the DMA spills past the end into
     * adjacent BSS — observed clobbering cyw43-driver SPI state, hanging LED and
     * USB CDC. I2S_RING_SAMPLES*4 = 8192 bytes here. */
    uint32_t storage[I2S_RING_SAMPLES]
        __attribute__((aligned(I2S_RING_SAMPLES * sizeof(uint32_t))));
} ring_buffer_t;

/* Zero head/tail. Call before DMA starts; racy if DMA is actively writing. */
void ring_buffer_reset(ring_buffer_t *rb);

/* Unread sample count — a snapshot; producer may race ahead. */
uint32_t ring_buffer_available(const ring_buffer_t *rb);

/* Pop up to max samples into dst, returning the count copied (0 if empty). */
uint32_t ring_buffer_pop(ring_buffer_t *rb, uint32_t *dst, uint32_t max);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_RING_BUFFER_H */

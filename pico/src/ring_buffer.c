/*
 * ring_buffer.c — SPSC ring buffer for I2S samples.
 *
 * Power-of-two size: mask with (size-1), no modulo (RP2040 has no HW divide).
 * head/tail are monotonic 32-bit counters masked only when indexing storage[],
 * so head-tail gives depth directly and stays correct across the 2^32 wrap
 * (~24h at 48kHz).
 */

#include "ring_buffer.h"

#include <string.h>

#define RB_MASK (I2S_RING_SAMPLES - 1u)

void ring_buffer_reset(ring_buffer_t *rb) {
    rb->head = 0;
    rb->tail = 0;
    /* No memset of storage — head/tail reset alone makes it empty. */
}

uint32_t ring_buffer_available(const ring_buffer_t *rb) {
    /* Unsigned: correct across 32-bit wrap. */
    return rb->head - rb->tail;
}

uint32_t ring_buffer_pop(ring_buffer_t *rb, uint32_t *dst, uint32_t max) {
    uint32_t avail = ring_buffer_available(rb);
    if (avail == 0u) return 0u;

    uint32_t to_copy = (avail < max) ? avail : max;
    for (uint32_t i = 0; i < to_copy; ++i) {
        dst[i] = rb->storage[(rb->tail + i) & RB_MASK];
    }

    /* Single store to tail commits; producer may now reuse those slots. */
    rb->tail += to_copy;
    return to_copy;
}

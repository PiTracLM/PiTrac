/*
 * ring_buffer.c — SPSC ring buffer for I2S samples.
 *
 * Implementation notes:
 *   - I2S_RING_SAMPLES is a power of two so we bitmask with (size-1) instead
 *     of taking a modulo. RP2040 has no hardware division/modulo unit.
 *   - We store `head` and `tail` as monotonically increasing 32-bit counters
 *     and only mask when indexing storage[]. This way the difference
 *     `head - tail` gives the queue depth directly, with correct behaviour
 *     across the natural wrap at 2^32 (≈ 24 hours at 48 kHz — irrelevant in
 *     practice but it's the "right" implementation).
 */

#include "ring_buffer.h"

#include <string.h>

#define RB_MASK (I2S_RING_SAMPLES - 1u)

void ring_buffer_reset(ring_buffer_t *rb) {
    rb->head = 0;
    rb->tail = 0;
    /* Don't bother memsetting storage — the head/tail reset alone is enough
     * to make the buffer appear empty. Saves a couple kB of pointless write
     * bandwidth at boot. */
}

uint32_t ring_buffer_available(const ring_buffer_t *rb) {
    /* `head - tail` works correctly across 32-bit wraparound thanks to
     * unsigned arithmetic. */
    return rb->head - rb->tail;
}

uint32_t ring_buffer_pop(ring_buffer_t *rb, uint32_t *dst, uint32_t max) {
    uint32_t avail = ring_buffer_available(rb);
    if (avail == 0u) return 0u;

    uint32_t to_copy = (avail < max) ? avail : max;
    for (uint32_t i = 0; i < to_copy; ++i) {
        dst[i] = rb->storage[(rb->tail + i) & RB_MASK];
    }

    /* Single store to `tail` — that's our commit. After this point the
     * producer is free to overwrite the slots we just read. */
    rb->tail += to_copy;
    return to_copy;
}

bool ring_buffer_peek(const ring_buffer_t *rb, uint32_t *out) {
    if (ring_buffer_available(rb) == 0u) return false;
    *out = rb->storage[rb->tail & RB_MASK];
    return true;
}

void ring_buffer_skip(ring_buffer_t *rb, uint32_t n) {
    /* Caller asserts there are at least n samples — we don't double-check
     * because in our usage pattern the caller has already ring_buffer_pop()'d
     * exactly that many. */
    rb->tail += n;
}

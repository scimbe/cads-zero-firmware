/*
 * CaDS Zero toolbox - byte ring buffer.
 *
 * See ring.h for the ownership rule that makes this lock free. The single
 * thing to preserve when editing: the byte must be stored before `head`
 * advances, and consumed before `tail` advances. Both indices are volatile, so
 * the compiler may not reorder those stores past each other, and on a
 * single-core Cortex-M that is the whole of the required synchronisation - an
 * ISR either ran entirely before an instruction or entirely after it.
 */

#include "cads/toolbox/ring.h"

static bool cads_ring_is_power_of_two(uint32_t value) {
    return value >= 2u && (value & (value - 1u)) == 0u;
}

bool cads_ring_init(cads_ring_t* ring, uint8_t* storage, uint32_t size) {
    if(!ring) return false;

    ring->storage = NULL;
    ring->size = 0u;
    ring->mask = 0u;
    ring->head = 0u;
    ring->tail = 0u;
    ring->dropped = 0u;

    if(!storage || !cads_ring_is_power_of_two(size)) return false;

    ring->storage = storage;
    ring->size = size;
    ring->mask = size - 1u;
    return true;
}

void cads_ring_reset(cads_ring_t* ring) {
    if(!ring) return;
    ring->head = 0u;
    ring->tail = 0u;
    ring->dropped = 0u;
}

bool cads_ring_push(cads_ring_t* ring, uint8_t byte) {
    if(!ring || !ring->storage) return false;

    uint32_t head = ring->head;
    uint32_t next = (head + 1u) & ring->mask;
    if(next == ring->tail) {
        /* Drop the newest byte rather than the whole buffer: a console line
         * that loses its tail is still readable, one that loses its head is
         * a different command. Count it so the loss is never silent. */
        ring->dropped++;
        return false;
    }

    ring->storage[head] = byte;
    ring->head = next;
    return true;
}

uint32_t cads_ring_write(cads_ring_t* ring, const void* data, uint32_t length) {
    if(!ring || !ring->storage || !data) return 0u;

    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t written = 0u;
    while(written < length && cads_ring_push(ring, bytes[written])) {
        written++;
    }
    /* cads_ring_push() counted the byte that failed; the rest never reached
     * the ring at all and must be counted here or the drop total lies. */
    if(written < length) {
        ring->dropped += (length - written) - 1u;
    }
    return written;
}

bool cads_ring_pop(cads_ring_t* ring, uint8_t* byte) {
    if(!ring || !ring->storage || !byte) return false;

    uint32_t tail = ring->tail;
    if(tail == ring->head) return false;

    *byte = ring->storage[tail];
    ring->tail = (tail + 1u) & ring->mask;
    return true;
}

bool cads_ring_peek(const cads_ring_t* ring, uint8_t* byte) {
    if(!ring || !ring->storage || !byte) return false;
    if(ring->tail == ring->head) return false;

    *byte = ring->storage[ring->tail];
    return true;
}

uint32_t cads_ring_read(cads_ring_t* ring, void* data, uint32_t length) {
    if(!ring || !ring->storage || !data) return 0u;

    uint8_t* bytes = (uint8_t*)data;
    uint32_t read = 0u;
    while(read < length && cads_ring_pop(ring, &bytes[read])) {
        read++;
    }
    return read;
}

uint32_t cads_ring_dropped(const cads_ring_t* ring) {
    return ring ? ring->dropped : 0u;
}

uint32_t cads_ring_count(const cads_ring_t* ring) {
    if(!ring || !ring->storage) return 0u;
    return (ring->head - ring->tail) & ring->mask;
}

uint32_t cads_ring_space(const cads_ring_t* ring) {
    if(!ring || !ring->storage) return 0u;
    return cads_ring_capacity(ring) - cads_ring_count(ring);
}

uint32_t cads_ring_capacity(const cads_ring_t* ring) {
    if(!ring || !ring->storage) return 0u;
    return ring->size - 1u;
}

bool cads_ring_is_empty(const cads_ring_t* ring) {
    if(!ring || !ring->storage) return true;
    return ring->head == ring->tail;
}

bool cads_ring_is_full(const cads_ring_t* ring) {
    if(!ring || !ring->storage) return false;
    return ((ring->head + 1u) & ring->mask) == ring->tail;
}

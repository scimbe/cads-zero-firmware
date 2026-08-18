/*
 * CaDS Zero toolbox - byte ring buffer.
 *
 * Single producer, single consumer, and lock free between exactly those two:
 * the producer is the only writer of `head`, the consumer the only writer of
 * `tail`, so neither ever needs to disable interrupts. That is what lets a
 * USART receive ISR fill a buffer a main loop drains without the ISR ever
 * being blocked - see targets/itsboard/hal/hal_console.c, whose hand-rolled
 * version this generalises.
 *
 * The capacity is a power of two so the wrap is a mask rather than a modulo or
 * a compare-and-reset; on a Cortex-M4 that is one AND instead of a division.
 */

#ifndef CADS_TOOLBOX_RING_H
#define CADS_TOOLBOX_RING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ring state. The storage is supplied by the caller, so the buffer can live
 * wherever it has to - DMA-capable SRAM, a task's static block, the stack.
 * Treat every field as private.
 */
typedef struct {
    uint8_t* storage;
    uint32_t size; /**< storage bytes, a power of two                       */
    uint32_t mask; /**< size - 1                                            */
    volatile uint32_t head;    /**< producer only                           */
    volatile uint32_t tail;    /**< consumer only                           */
    volatile uint32_t dropped; /**< producer only                           */
} cads_ring_t;

/**
 * Bind `ring` to `storage`.
 *
 * `size` must be a power of two and at least 2. Returns false and leaves the
 * ring unusable otherwise - a wrong size is a programming error, and silently
 * rounding it would hide the bug rather than the consequence.
 *
 * One slot is spent distinguishing "full" from "empty", so the usable capacity
 * is `size - 1`; cads_ring_capacity() reports it.
 */
bool cads_ring_init(cads_ring_t* ring, uint8_t* storage, uint32_t size);

/** Discard everything buffered, including the drop count. Not safe to call
 *  while the other side is running. */
void cads_ring_reset(cads_ring_t* ring);

/* --- producer side -------------------------------------------------------- */

/** Append one byte. Returns false and increments the drop count when full. */
bool cads_ring_push(cads_ring_t* ring, uint8_t byte);

/** Append up to `length` bytes; returns how many were accepted. Bytes that did
 *  not fit are counted as drops, exactly as cads_ring_push() counts them. */
uint32_t cads_ring_write(cads_ring_t* ring, const void* data, uint32_t length);

/** Bytes refused because the consumer fell behind. Monotonic until reset. */
uint32_t cads_ring_dropped(const cads_ring_t* ring);

/* --- consumer side -------------------------------------------------------- */

/** Take the oldest byte. Returns false when empty; `byte` is then untouched. */
bool cads_ring_pop(cads_ring_t* ring, uint8_t* byte);

/** Read the oldest byte without removing it. */
bool cads_ring_peek(const cads_ring_t* ring, uint8_t* byte);

/** Take up to `length` bytes; returns how many were delivered. */
uint32_t cads_ring_read(cads_ring_t* ring, void* data, uint32_t length);

/* --- observation ----------------------------------------------------------
 *
 * Safe from either side, but only ever a lower bound (count) or an upper bound
 * (space) once the other side is live: the answer can improve before it is
 * used, never get worse.
 */

uint32_t cads_ring_count(const cads_ring_t* ring);
uint32_t cads_ring_space(const cads_ring_t* ring);

/** Usable capacity, i.e. `size - 1`. */
uint32_t cads_ring_capacity(const cads_ring_t* ring);

bool cads_ring_is_empty(const cads_ring_t* ring);
bool cads_ring_is_full(const cads_ring_t* ring);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_RING_H */

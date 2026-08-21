/*
 * CaDS Zero toolbox - switch-style MAC address learning table with aging.
 *
 * A real switch's CAM table learns a source address off whichever port a
 * frame arrived on, and quietly forgets it once nothing has been heard
 * from it for the port's aging interval. This is that policy as a plain
 * data structure - "port" does not apply (this board has one physical
 * Ethernet interface, promiscuous capture stands in for "every port at
 * once"), but the two behaviours that make it a *learning* table rather
 * than a plain list are both here: an existing entry is refreshed, not
 * duplicated, on every further sighting, and an entry idle past its
 * aging window is evicted to make room rather than blocking a new
 * address forever.
 *
 * NO HEAP, CALLER-OWNED STORAGE, NO CLOCK OF ITS OWN
 * ----------------------------------------------------
 * cads_mactable_init() takes a caller-supplied entry array, the same
 * convention as cads_record.h. `now_ms` is passed in by the caller on
 * every call rather than read from a HAL clock in here, for the same
 * reason cads_ring.h's storage is caller-supplied: it keeps this file
 * free of any board dependency, so the learning/aging policy itself -
 * the part actually worth getting right - is unit-testable on the host
 * with a fake clock, independent of whether real Ethernet traffic is
 * available on whatever bench happens to be running it.
 *
 * NOT THREAD SAFE, for the same reason cads_record.h gives: below
 * modules/kernel in the dependency graph, cannot reach cads_mutex. A
 * single-threaded capture loop calling cads_mactable_learn() needs none.
 */

#ifndef CADS_TOOLBOX_MACTABLE_H
#define CADS_TOOLBOX_MACTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One learned address. Read freely via cads_mactable_at() - only the
 * table's own bookkeeping of which slots are free is private to this
 * module, not this struct's fields.
 */
typedef struct {
    uint8_t mac[6];
    uint32_t last_seen_ms;
    uint32_t frame_count; /**< sightings since this slot was (re)claimed */
    /* --- private --- */
    bool in_use;
} cads_mactable_entry_t;

typedef struct {
    /* --- private --- */
    cads_mactable_entry_t* entries;
    size_t capacity;
    uint32_t aging_ms;
    uint32_t aged_out_total; /**< lifetime count, across both eviction paths below */
    uint32_t dropped_total;  /**< lifetime count of a new address refused - table full, nothing aged */
} cads_mactable_t;

/** `storage` holds up to `capacity` entries and must outlive the table.
 *  `aging_ms` is how long an entry may go unseen before it is eligible
 *  for eviction, by either cads_mactable_learn() or cads_mactable_age(). */
void cads_mactable_init(cads_mactable_t* table, cads_mactable_entry_t* storage, size_t capacity, uint32_t aging_ms);

/**
 * Record one sighting of `mac` at `now_ms`.
 *
 * An existing entry for `mac` is refreshed (last_seen_ms updated,
 * frame_count incremented) rather than duplicated. A new address claims
 * a free slot if one exists. If the table is full, the entry idle
 * longest is evicted to make room for the new address - but only if
 * that entry has actually gone past `aging_ms`; a full table of entries
 * that are all still active refuses the new address instead
 * (cads_mactable_dropped_total() counts this) rather than evicting
 * something still live just because a new address showed up.
 */
void cads_mactable_learn(cads_mactable_t* table, const uint8_t mac[6], uint32_t now_ms);

/**
 * Evict every entry idle longer than `aging_ms` as of `now_ms`, whether
 * or not cads_mactable_learn() has run recently - so a caller listing
 * the table between sightings sees who is actually still active, not
 * just who was active as of the last learned frame. Returns how many
 * entries this call evicted.
 */
uint32_t cads_mactable_age(cads_mactable_t* table, uint32_t now_ms);

/** Live entries right now (post-aging if cads_mactable_age() was called). */
size_t cads_mactable_count(const cads_mactable_t* table);

/** The `index`-th live entry, 0 <= index < cads_mactable_count(). NULL out
 *  of range. Slot layout is not exposed - indices are dense over live
 *  entries only, the caller never sees a freed or unused slot. */
const cads_mactable_entry_t* cads_mactable_at(const cads_mactable_t* table, size_t index);

/** Lifetime total of entries evicted for being idle, by either eviction path. */
uint32_t cads_mactable_aged_out_total(const cads_mactable_t* table);

/** Lifetime total of a new address refused because the table was full and
 *  nothing in it had aged out yet. */
uint32_t cads_mactable_dropped_total(const cads_mactable_t* table);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_MACTABLE_H */

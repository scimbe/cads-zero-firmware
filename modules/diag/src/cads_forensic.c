/*
 * CaDS Zero - crash forensics ring buffer implementation.
 *
 * WHERE THIS LIVES, AND WHY THAT IS SAFE
 * ---------------------------------------
 * The ring is placed in CCM via CADS_CCM_SECTION (core/cads_hal.h) - the
 * same attribute apps/bringup/tasks.c already uses for task stacks. Two
 * properties of CCM make it the right (and cheapest) place for this,
 * rather than plain static storage:
 *
 *   1. targets/itsboard/startup/startup_stm32f429.c's Reset_Handler only
 *      zeroes .bss (__cads_bss_start..__cads_bss_end); .ccm is a separate
 *      NOLOAD section the zero loop never touches, so its content survives
 *      exactly the kind of reset this module exists to survive - a
 *      watchdog timeout, a software reset, or NRST. It does NOT survive a
 *      genuine power-on reset, which is why every record carries a magic
 *      number rather than assuming the ring starts empty.
 *
 *   2. It costs nothing from the RAM heap margin
 *      scripts/check_ram_budget.py enforces (targets/itsboard/linker/
 *      cads_itsboard.ld's ASSERT(__cads_heap_size >= 48K)) - CCM is a
 *      wholly separate 64 KB region, currently a few KB used for task
 *      stacks, the rest free. A RAM (.bss-adjacent) placement would have
 *      eaten directly into that budget's already-thin margin for no
 *      reason: this data has no use for the DMA capability RAM provides
 *      and CCM already exists with the exact persistence property needed.
 *
 * NO PERSISTENT CURSOR
 * ---------------------
 * The obvious ring-buffer design keeps a separate "next write index"
 * alongside the slots. That variable would need the same survives-a-reset
 * property as the slots themselves, which means it would need its own
 * validity marker, which means it is really just another slot. Simpler:
 * every slot already carries a monotonic sequence number once it is ever
 * written, so cads_forensic_record() derives where to write by scanning -
 * prefer an empty (invalid-magic) slot if one exists, otherwise evict the
 * lowest sequence number among the valid ones. No cursor to get out of
 * sync with the data it is supposed to describe.
 */

#include "cads/diag/forensic.h"

#include "cads_hal.h"

#define CADS_FORENSIC_MAGIC 0x43614673u /* "CaFs" in little-endian bytes */

typedef struct {
    uint32_t magic;
    cads_forensic_record_t record;
} cads_forensic_slot_t;

CADS_CCM_SECTION static cads_forensic_slot_t cads_forensic_ring[CADS_FORENSIC_RING_DEPTH];

static bool cads_forensic_slot_valid(uint32_t index) {
    return cads_forensic_ring[index].magic == CADS_FORENSIC_MAGIC;
}

void cads_forensic_record(
    const char* reason,
    const cads_forensic_frame_t* frame,
    uint32_t cfsr,
    uint32_t hfsr,
    bool mmfar_valid,
    uint32_t mmfar,
    bool bfar_valid,
    uint32_t bfar) {
    uint32_t target = 0u;
    bool have_target = false;
    uint32_t max_sequence = 0u;
    uint32_t min_sequence = 0u;

    for(uint32_t i = 0; i < CADS_FORENSIC_RING_DEPTH; i++) {
        if(!cads_forensic_slot_valid(i)) {
            target = i;
            have_target = true;
            break;
        }
        uint32_t sequence = cads_forensic_ring[i].record.sequence;
        if(sequence > max_sequence) max_sequence = sequence;
        if(!have_target || sequence < min_sequence) {
            min_sequence = sequence;
            target = i;
        }
        have_target = true;
    }

    cads_forensic_record_t* out = &cads_forensic_ring[target].record;
    out->sequence = max_sequence + 1u;
    out->reason = reason;
    out->uptime_ms = cads_hal_ticks_ms();
    out->has_frame = frame != NULL;
    if(frame != NULL) {
        out->frame = *frame;
    }
    out->cfsr = cfsr;
    out->hfsr = hfsr;
    out->mmfar_valid = mmfar_valid;
    out->mmfar = mmfar;
    out->bfar_valid = bfar_valid;
    out->bfar = bfar;

    cads_forensic_ring[target].magic = CADS_FORENSIC_MAGIC;
}

uint32_t cads_forensic_count(void) {
    uint32_t count = 0u;
    for(uint32_t i = 0; i < CADS_FORENSIC_RING_DEPTH; i++) {
        if(cads_forensic_slot_valid(i)) count++;
    }
    return count;
}

bool cads_forensic_get(uint32_t index, cads_forensic_record_t* out) {
    if(out == NULL || index >= CADS_FORENSIC_RING_DEPTH) return false;

    /* index 0 = highest sequence (most recent). Selection over a 6-slot
     * ring, called from a human at an explorer prompt, not a hot path -
     * clarity wins over a cleverer sort. */
    bool excluded[CADS_FORENSIC_RING_DEPTH] = {0};
    for(uint32_t pick = 0; pick <= index; pick++) {
        uint32_t best = CADS_FORENSIC_RING_DEPTH;
        uint32_t best_sequence = 0u;
        for(uint32_t i = 0; i < CADS_FORENSIC_RING_DEPTH; i++) {
            if(excluded[i] || !cads_forensic_slot_valid(i)) continue;
            uint32_t sequence = cads_forensic_ring[i].record.sequence;
            if(best == CADS_FORENSIC_RING_DEPTH || sequence > best_sequence) {
                best = i;
                best_sequence = sequence;
            }
        }
        if(best == CADS_FORENSIC_RING_DEPTH) return false; /* fewer than index+1 valid records */
        excluded[best] = true;
        if(pick == index) {
            *out = cads_forensic_ring[best].record;
            return true;
        }
    }
    return false;
}

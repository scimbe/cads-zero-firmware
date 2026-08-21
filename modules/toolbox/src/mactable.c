#include "cads/toolbox/mactable.h"

#include <string.h>

void cads_mactable_init(cads_mactable_t* table, cads_mactable_entry_t* storage, size_t capacity, uint32_t aging_ms) {
    if(!table) return;
    table->entries = storage;
    table->capacity = storage ? capacity : 0u;
    table->aging_ms = aging_ms;
    table->aged_out_total = 0u;
    table->dropped_total = 0u;
    for(size_t i = 0u; i < table->capacity; i++) {
        table->entries[i].in_use = false;
    }
}

static cads_mactable_entry_t* cads_mactable_find(const cads_mactable_t* table, const uint8_t mac[6]) {
    for(size_t i = 0u; i < table->capacity; i++) {
        cads_mactable_entry_t* entry = &table->entries[i];
        if(entry->in_use && memcmp(entry->mac, mac, 6u) == 0) return entry;
    }
    return NULL;
}

static void cads_mactable_claim(cads_mactable_entry_t* entry, const uint8_t mac[6], uint32_t now_ms) {
    memcpy(entry->mac, mac, 6u);
    entry->last_seen_ms = now_ms;
    entry->frame_count = 1u;
    entry->in_use = true;
}

void cads_mactable_learn(cads_mactable_t* table, const uint8_t mac[6], uint32_t now_ms) {
    if(!table || !mac) return;

    cads_mactable_entry_t* existing = cads_mactable_find(table, mac);
    if(existing) {
        existing->last_seen_ms = now_ms;
        existing->frame_count++;
        return;
    }

    for(size_t i = 0u; i < table->capacity; i++) {
        if(!table->entries[i].in_use) {
            cads_mactable_claim(&table->entries[i], mac, now_ms);
            return;
        }
    }

    /* Table full - evict whichever entry has been idle longest, but only
     * if it has actually crossed aging_ms. An entry still within its
     * aging window is never sacrificed just because a new address
     * arrived; the new address is refused instead. */
    size_t oldest_index = 0u;
    uint32_t oldest_idle = 0u;
    bool found_aged = false;
    for(size_t i = 0u; i < table->capacity; i++) {
        uint32_t idle = now_ms - table->entries[i].last_seen_ms;
        if(idle >= table->aging_ms && (!found_aged || idle > oldest_idle)) {
            oldest_index = i;
            oldest_idle = idle;
            found_aged = true;
        }
    }
    if(found_aged) {
        table->aged_out_total++;
        cads_mactable_claim(&table->entries[oldest_index], mac, now_ms);
        return;
    }

    table->dropped_total++;
}

uint32_t cads_mactable_age(cads_mactable_t* table, uint32_t now_ms) {
    if(!table) return 0u;
    uint32_t evicted = 0u;
    for(size_t i = 0u; i < table->capacity; i++) {
        cads_mactable_entry_t* entry = &table->entries[i];
        if(entry->in_use && (now_ms - entry->last_seen_ms) >= table->aging_ms) {
            entry->in_use = false;
            table->aged_out_total++;
            evicted++;
        }
    }
    return evicted;
}

size_t cads_mactable_count(const cads_mactable_t* table) {
    if(!table) return 0u;
    size_t count = 0u;
    for(size_t i = 0u; i < table->capacity; i++) {
        if(table->entries[i].in_use) count++;
    }
    return count;
}

const cads_mactable_entry_t* cads_mactable_at(const cads_mactable_t* table, size_t index) {
    if(!table) return NULL;
    size_t seen = 0u;
    for(size_t i = 0u; i < table->capacity; i++) {
        if(!table->entries[i].in_use) continue;
        if(seen == index) return &table->entries[i];
        seen++;
    }
    return NULL;
}

uint32_t cads_mactable_aged_out_total(const cads_mactable_t* table) {
    return table ? table->aged_out_total : 0u;
}

uint32_t cads_mactable_dropped_total(const cads_mactable_t* table) {
    return table ? table->dropped_total : 0u;
}

#include "cads/toolbox/record.h"

#include "cads/toolbox/str.h"

void cads_record_init(cads_record_t* registry, cads_record_entry_t* storage, size_t capacity) {
    if(!registry) return;
    registry->entries = storage;
    registry->capacity = storage ? capacity : 0u;
    for(size_t i = 0u; i < registry->capacity; i++) {
        registry->entries[i].used = false;
    }
}

static cads_record_entry_t* cads_record_find(const cads_record_t* registry, const char* name) {
    if(!registry || !name) return NULL;
    for(size_t i = 0u; i < registry->capacity; i++) {
        cads_record_entry_t* entry = &registry->entries[i];
        if(entry->used && cads_str_equal(entry->name, name)) return entry;
    }
    return NULL;
}

bool cads_record_register(cads_record_t* registry, const char* name, void* instance) {
    if(!registry || !name) return false;
    if(cads_str_len(name, CADS_RECORD_NAME_MAX + 1u) > CADS_RECORD_NAME_MAX) return false;
    if(cads_record_find(registry, name) != NULL) return false;

    for(size_t i = 0u; i < registry->capacity; i++) {
        cads_record_entry_t* entry = &registry->entries[i];
        if(entry->used) continue;
        cads_str_copy(entry->name, sizeof(entry->name), name);
        entry->instance = instance;
        entry->used = true;
        return true;
    }
    return false; /* full */
}

bool cads_record_unregister(cads_record_t* registry, const char* name) {
    cads_record_entry_t* entry = cads_record_find(registry, name);
    if(!entry) return false;
    entry->used = false;
    entry->instance = NULL;
    return true;
}

void* cads_record_lookup(const cads_record_t* registry, const char* name) {
    cads_record_entry_t* entry = cads_record_find(registry, name);
    return entry ? entry->instance : NULL;
}

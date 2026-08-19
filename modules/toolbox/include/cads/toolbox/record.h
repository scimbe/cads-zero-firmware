/*
 * CaDS Zero toolbox - a named registry, so a module can find another module's
 * instance without the two of them being linked together.
 *
 * The motivating case is the same layering problem cads_pubsub.h exists for:
 * apps/netinfo wants the Ethernet PHY's link state, but the driver that knows
 * it is board-only (targets/itsboard/hal/) and apps/netinfo is portable
 * (docs/reference/module-layout.md forbids a portable app from including a
 * targets/ header - the exact mistake already made once in
 * apps/bringup/explorer.c). A named registry lets the board-specific side
 * register an instance under a name at startup, and the portable side look it
 * up by that name, with neither one's source needing to mention the other's
 * header.
 *
 * NO HEAP, CALLER-OWNED STORAGE
 * ------------------------------
 * cads_record_init() takes a caller-supplied array of entries, sized by
 * whoever owns the registry, the same convention as every other module here.
 * A registry has no destructor because it owns nothing to release: the
 * pointers it stores are borrowed, never freed.
 *
 * NOT THREAD SAFE, for the same reason cads_pubsub.h gives: this sits below
 * modules/kernel in the dependency graph and cannot reach up to cads_mutex.
 * A registry populated once during single-threaded startup and only read
 * afterwards - the expected use - needs no locking at all.
 */

#ifndef CADS_TOOLBOX_RECORD_H
#define CADS_TOOLBOX_RECORD_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest name, excluding the NUL. Short and fixed rather than dynamic,
 *  because every name in this codebase so far is a short module identifier
 *  ("eth", "storage") rather than a sentence. */
#define CADS_RECORD_NAME_MAX 15u

typedef struct {
    /* --- private --- */
    char name[CADS_RECORD_NAME_MAX + 1u];
    void* instance;
    bool used;
} cads_record_entry_t;

typedef struct {
    /* --- private --- */
    cads_record_entry_t* entries;
    size_t capacity;
} cads_record_t;

/** `storage` holds up to `capacity` entries and must outlive the registry. */
void cads_record_init(cads_record_t* registry, cads_record_entry_t* storage, size_t capacity);

/**
 * Register `instance` under `name`.
 *
 * Fails (returns false, registers nothing) when `name` is already
 * registered, when it is longer than CADS_RECORD_NAME_MAX, or when the
 * registry is full - a silent overwrite or a silently truncated name would
 * both let two unrelated modules collide under one name without either of
 * them finding out.
 */
bool cads_record_register(cads_record_t* registry, const char* name, void* instance);

/** Remove the entry named `name`. Returns false if no such entry exists. */
bool cads_record_unregister(cads_record_t* registry, const char* name);

/** The instance registered under `name`, or NULL if there is none. */
void* cads_record_lookup(const cads_record_t* registry, const char* name);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_RECORD_H */

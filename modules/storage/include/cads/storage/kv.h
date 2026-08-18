/*
 * CaDS Zero storage - a small typed key/value store on top of the volume.
 *
 * What the settings screen actually needs: a handful of named values -
 * backlight percent, the SPI clock choice, a calibration offset - persisted
 * across power cycles. That does not justify a database; it justifies one
 * file holding a flat table, rewritten in one shot whenever something
 * changes. Reads are free (the table lives in RAM once open), and a screen
 * that changes eight values in a row costs one erase-cycle-friendly write
 * instead of eight.
 *
 * Layered on cads/storage/storage.h and nothing else - open, read, write,
 * close - so it shares that module's status codes and its rule: one task,
 * no internal locking.
 *
 * NO ALLOCATION. Entries live in a fixed table sized by CADS_KV_MAX_ENTRIES;
 * cads_kv_set_*() returns CADS_STORAGE_ERR_NOSLOT once it is full rather than
 * growing it.
 */

#ifndef CADS_STORAGE_KV_H
#define CADS_STORAGE_KV_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest key, excluding the terminator. */
#define CADS_KV_KEY_MAX 23u

/** Longest string value, excluding the terminator. */
#define CADS_KV_STR_MAX 31u

#ifndef CADS_KV_MAX_ENTRIES
#define CADS_KV_MAX_ENTRIES 32u
#endif

typedef enum {
    CADS_KV_TYPE_I32 = 1,
    CADS_KV_TYPE_BOOL = 2,
    CADS_KV_TYPE_STR = 3,
} cads_kv_type_t;

/**
 * Load `path` into the in-memory table.
 *
 * A missing file is not a fault: it means nothing has been saved yet, so the
 * table starts empty and the first cads_kv_save() creates the file. A file
 * that exists but fails to parse (bad magic, truncated, a count past
 * CADS_KV_MAX_ENTRIES) reports CADS_STORAGE_ERR_CORRUPT and leaves the table
 * empty rather than partially populated.
 *
 * Requires a mounted volume. There is one table, not one per path - a second
 * call to cads_kv_open() discards whatever the first one loaded.
 */
int cads_kv_open(const char* path);

/** Write the in-memory table back to the path passed to cads_kv_open(). */
int cads_kv_save(void);

int cads_kv_get_i32(const char* key, int32_t* out);
int cads_kv_set_i32(const char* key, int32_t value);

int cads_kv_get_bool(const char* key, bool* out);
int cads_kv_set_bool(const char* key, bool value);

/** Copies at most out_size - 1 bytes plus a terminator into `out`. */
int cads_kv_get_str(const char* key, char* out, uint32_t out_size);
int cads_kv_set_str(const char* key, const char* value);

/**
 * Type of `key`, or a negative status when it is absent. Lets a caller ask
 * "is this a string or a number" before choosing which getter to call.
 */
int cads_kv_type(const char* key);

bool cads_kv_has(const char* key);
int cads_kv_remove(const char* key);

/** Entries currently held, for a settings screen that wants to know how much
 *  headroom is left before CADS_KV_MAX_ENTRIES. */
uint32_t cads_kv_count(void);

#ifdef __cplusplus
}
#endif

#endif /* CADS_STORAGE_KV_H */

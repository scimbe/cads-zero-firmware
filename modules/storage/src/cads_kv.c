/*
 * CaDS Zero storage - the key/value store in cads/storage/kv.h.
 *
 * A client of cads/storage/storage.h like any other - it never touches
 * lfs.h or cads/storage/flash.h. The whole table is read into a static array
 * by cads_kv_open() and written back in one cads_storage_write() by
 * cads_kv_save(); there is no per-key file and no partial write.
 */

#include "cads/storage/kv.h"

#include <string.h>

#include "cads/storage/storage.h"

#define CADS_KV_MAGIC 0x314B4143u /* "CAK1", little endian */
#define CADS_KV_VERSION 1u

typedef struct {
    char key[CADS_KV_KEY_MAX + 1u];
    uint8_t type;
    uint8_t reserved[3];
    union {
        int32_t i32;
        uint8_t boolean;
        char str[CADS_KV_STR_MAX + 1u];
    } value;
} cads_kv_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
} cads_kv_header_t;

static cads_kv_entry_t cads_kv_table[CADS_KV_MAX_ENTRIES];
static uint32_t cads_kv_entry_count = 0;
static char cads_kv_path[CADS_STORAGE_PATH_MAX + 1u];
static bool cads_kv_is_open = false;

static cads_kv_entry_t* cads_kv_find(const char* key) {
    for(uint32_t i = 0; i < cads_kv_entry_count; i++) {
        if(strcmp(cads_kv_table[i].key, key) == 0) return &cads_kv_table[i];
    }
    return NULL;
}

static int cads_kv_reset(void) {
    cads_kv_entry_count = 0;
    memset(cads_kv_table, 0, sizeof(cads_kv_table));
    return CADS_STORAGE_OK;
}

int cads_kv_open(const char* path) {
    if(!path) return CADS_STORAGE_ERR_INVAL;
    if(strlen(path) > CADS_STORAGE_PATH_MAX) return CADS_STORAGE_ERR_NAMETOOLONG;

    cads_kv_reset();
    cads_kv_is_open = false;

    cads_storage_file_t* file = NULL;
    int status = cads_storage_open(&file, path, CADS_STORAGE_RDONLY);
    if(status == CADS_STORAGE_ERR_NOENT) {
        /* Nothing saved yet - an empty table is the correct starting point. */
        strncpy(cads_kv_path, path, sizeof(cads_kv_path) - 1u);
        cads_kv_path[sizeof(cads_kv_path) - 1u] = '\0';
        cads_kv_is_open = true;
        return CADS_STORAGE_OK;
    }
    if(status != CADS_STORAGE_OK) return status;

    cads_kv_header_t header;
    int32_t rc = cads_storage_read(file, &header, sizeof(header));
    if(rc != (int32_t)sizeof(header) || header.magic != CADS_KV_MAGIC || header.version != CADS_KV_VERSION ||
       header.count > CADS_KV_MAX_ENTRIES) {
        cads_storage_close(file);
        return CADS_STORAGE_ERR_CORRUPT;
    }

    uint32_t bytes = header.count * (uint32_t)sizeof(cads_kv_entry_t);
    rc = cads_storage_read(file, cads_kv_table, bytes);
    cads_storage_close(file);
    if(rc != (int32_t)bytes) {
        cads_kv_reset();
        return CADS_STORAGE_ERR_CORRUPT;
    }

    cads_kv_entry_count = header.count;
    strncpy(cads_kv_path, path, sizeof(cads_kv_path) - 1u);
    cads_kv_path[sizeof(cads_kv_path) - 1u] = '\0';
    cads_kv_is_open = true;
    return CADS_STORAGE_OK;
}

int cads_kv_save(void) {
    if(!cads_kv_is_open) return CADS_STORAGE_ERR_NOTMOUNTED;

    cads_storage_file_t* file = NULL;
    int status =
        cads_storage_open(&file, cads_kv_path, CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT | CADS_STORAGE_TRUNC);
    if(status != CADS_STORAGE_OK) return status;

    cads_kv_header_t header = {
        .magic = CADS_KV_MAGIC,
        .version = CADS_KV_VERSION,
        .count = cads_kv_entry_count,
    };

    int32_t rc = cads_storage_write(file, &header, sizeof(header));
    if(rc == (int32_t)sizeof(header) && cads_kv_entry_count > 0u) {
        uint32_t bytes = cads_kv_entry_count * (uint32_t)sizeof(cads_kv_entry_t);
        rc = cads_storage_write(file, cads_kv_table, bytes);
        status = (rc == (int32_t)bytes) ? CADS_STORAGE_OK : rc;
    } else if(rc != (int32_t)sizeof(header)) {
        status = rc;
    }

    int close_status = cads_storage_close(file);
    if(status < 0) return status;
    return close_status;
}

static int cads_kv_upsert(const char* key, uint8_t type, const void* value, size_t value_size) {
    if(!key) return CADS_STORAGE_ERR_INVAL;
    if(strlen(key) > CADS_KV_KEY_MAX) return CADS_STORAGE_ERR_NAMETOOLONG;
    if(!cads_kv_is_open) return CADS_STORAGE_ERR_NOTMOUNTED;

    cads_kv_entry_t* entry = cads_kv_find(key);
    if(!entry) {
        if(cads_kv_entry_count >= CADS_KV_MAX_ENTRIES) return CADS_STORAGE_ERR_NOSLOT;
        entry = &cads_kv_table[cads_kv_entry_count++];
        memset(entry, 0, sizeof(*entry));
        strncpy(entry->key, key, CADS_KV_KEY_MAX);
    }

    entry->type = type;
    memset(&entry->value, 0, sizeof(entry->value));
    memcpy(&entry->value, value, value_size);
    return CADS_STORAGE_OK;
}

int cads_kv_get_i32(const char* key, int32_t* out) {
    if(!key || !out) return CADS_STORAGE_ERR_INVAL;
    if(!cads_kv_is_open) return CADS_STORAGE_ERR_NOTMOUNTED;

    cads_kv_entry_t* entry = cads_kv_find(key);
    if(!entry) return CADS_STORAGE_ERR_NOENT;
    if(entry->type != CADS_KV_TYPE_I32) return CADS_STORAGE_ERR_INVAL;

    *out = entry->value.i32;
    return CADS_STORAGE_OK;
}

int cads_kv_set_i32(const char* key, int32_t value) {
    return cads_kv_upsert(key, CADS_KV_TYPE_I32, &value, sizeof(value));
}

int cads_kv_get_bool(const char* key, bool* out) {
    if(!key || !out) return CADS_STORAGE_ERR_INVAL;
    if(!cads_kv_is_open) return CADS_STORAGE_ERR_NOTMOUNTED;

    cads_kv_entry_t* entry = cads_kv_find(key);
    if(!entry) return CADS_STORAGE_ERR_NOENT;
    if(entry->type != CADS_KV_TYPE_BOOL) return CADS_STORAGE_ERR_INVAL;

    *out = entry->value.boolean != 0u;
    return CADS_STORAGE_OK;
}

int cads_kv_set_bool(const char* key, bool value) {
    uint8_t raw = value ? 1u : 0u;
    return cads_kv_upsert(key, CADS_KV_TYPE_BOOL, &raw, sizeof(raw));
}

int cads_kv_get_str(const char* key, char* out, uint32_t out_size) {
    if(!key || !out || out_size == 0u) return CADS_STORAGE_ERR_INVAL;
    if(!cads_kv_is_open) return CADS_STORAGE_ERR_NOTMOUNTED;

    cads_kv_entry_t* entry = cads_kv_find(key);
    if(!entry) return CADS_STORAGE_ERR_NOENT;
    if(entry->type != CADS_KV_TYPE_STR) return CADS_STORAGE_ERR_INVAL;

    size_t len = strlen(entry->value.str);
    if(len > out_size - 1u) len = out_size - 1u;
    memcpy(out, entry->value.str, len);
    out[len] = '\0';
    return CADS_STORAGE_OK;
}

int cads_kv_set_str(const char* key, const char* value) {
    if(!value) return CADS_STORAGE_ERR_INVAL;
    if(strlen(value) > CADS_KV_STR_MAX) return CADS_STORAGE_ERR_INVAL;

    char buffer[CADS_KV_STR_MAX + 1u] = {0};
    strncpy(buffer, value, CADS_KV_STR_MAX);
    return cads_kv_upsert(key, CADS_KV_TYPE_STR, buffer, sizeof(buffer));
}

int cads_kv_type(const char* key) {
    if(!key) return CADS_STORAGE_ERR_INVAL;
    if(!cads_kv_is_open) return CADS_STORAGE_ERR_NOTMOUNTED;

    cads_kv_entry_t* entry = cads_kv_find(key);
    if(!entry) return CADS_STORAGE_ERR_NOENT;
    return entry->type;
}

bool cads_kv_has(const char* key) {
    if(!key || !cads_kv_is_open) return false;
    return cads_kv_find(key) != NULL;
}

int cads_kv_remove(const char* key) {
    if(!key) return CADS_STORAGE_ERR_INVAL;
    if(!cads_kv_is_open) return CADS_STORAGE_ERR_NOTMOUNTED;

    for(uint32_t i = 0; i < cads_kv_entry_count; i++) {
        if(strcmp(cads_kv_table[i].key, key) == 0) {
            cads_kv_table[i] = cads_kv_table[cads_kv_entry_count - 1u];
            cads_kv_entry_count--;
            return CADS_STORAGE_OK;
        }
    }
    return CADS_STORAGE_ERR_NOENT;
}

uint32_t cads_kv_count(void) {
    return cads_kv_entry_count;
}

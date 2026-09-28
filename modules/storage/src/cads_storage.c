/*
 * CaDS Zero storage - the littlefs glue and the service in cads/storage/storage.h.
 *
 * This file is the only place in the module that includes lfs.h. It turns
 * cads/storage/flash.h into the four callbacks lfs_config wants, and turns
 * lfs_t/lfs_file_t/lfs_dir_t into the pool-backed handles storage.h promises.
 *
 * Target neutral: everything here is C11 against flash.h and lfs.h, so it
 * compiles unchanged for the board and for the host.
 */

#include "cads/storage/storage.h"

#include <string.h>

#include "cads/storage/flash.h"
#include "lfs.h"

/* --- geometry-derived constants --------------------------------------------
 *
 * cache_size must divide block_size and be a multiple of read_size/prog_size;
 * lookahead_size just needs to be non-zero. 64 bytes of cache and 8 bytes of
 * lookahead (which alone can track 64 blocks - we have 7) are generous for
 * this volume and cheap against 192 KB of RAM.
 */
#define CADS_STORAGE_CACHE_SIZE 64u
#define CADS_STORAGE_LOOKAHEAD_SIZE 8u

/* Metadata pairs move to a fresh block after this many erases of the same
 * one. Mid-range of littlefs' suggested 100-1000; see README.md for why a
 * seven-block volume makes this number matter more than usual. */
#define CADS_STORAGE_BLOCK_CYCLES 500

/* LFS_NAME_MAX is a build define (see CMakeLists.txt) rather than a value
 * chosen here: it sizes struct lfs_info's name field at compile time, so it
 * has to be fixed before lfs.h is parsed. This assertion is what catches the
 * build define drifting away from storage.h's own CADS_STORAGE_NAME_MAX - the
 * failure mode otherwise is a directory entry silently truncated one way but
 * not the other. */
_Static_assert(LFS_NAME_MAX == CADS_STORAGE_NAME_MAX,
    "LFS_NAME_MAX (build define) and CADS_STORAGE_NAME_MAX must agree");

/* --- block device: cads/storage/flash.h wired into struct lfs_config ------ */

static int cads_bd_read(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer,
                         lfs_size_t size) {
    uint32_t offset = block * c->block_size + off;
    return (cads_flash_read(offset, buffer, size) == CADS_FLASH_OK) ? 0 : LFS_ERR_IO;
}

static int cads_bd_prog(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer,
                         lfs_size_t size) {
    uint32_t offset = block * c->block_size + off;
    return (cads_flash_program(offset, buffer, size) == CADS_FLASH_OK) ? 0 : LFS_ERR_IO;
}

static int cads_bd_erase(const struct lfs_config* c, lfs_block_t block) {
    (void)c;
    return (cads_flash_erase_block(block) == CADS_FLASH_OK) ? 0 : LFS_ERR_IO;
}

static int cads_bd_sync(const struct lfs_config* c) {
    (void)c;
    return (cads_flash_sync() == CADS_FLASH_OK) ? 0 : LFS_ERR_IO;
}

/* --- static state: one volume, fixed handle pools -------------------------- */

static uint8_t cads_read_buffer[CADS_STORAGE_CACHE_SIZE];
static uint8_t cads_prog_buffer[CADS_STORAGE_CACHE_SIZE];
static uint8_t cads_lookahead_buffer[CADS_STORAGE_LOOKAHEAD_SIZE];

static struct lfs_config cads_lfs_cfg;
static lfs_t cads_lfs;
static bool cads_configured = false;
static bool cads_mounted = false;

struct cads_storage_file {
    bool in_use;
    lfs_file_t lfs_file;
    struct lfs_file_config lfs_file_cfg;
    uint8_t buffer[CADS_STORAGE_CACHE_SIZE];
};

struct cads_storage_dir {
    bool in_use;
    lfs_dir_t lfs_dir;
};

static struct cads_storage_file cads_files[CADS_STORAGE_MAX_FILES];
static struct cads_storage_dir cads_dirs[CADS_STORAGE_MAX_DIRS];

static bool cads_any_handle_open(void) {
    for(uint32_t i = 0; i < CADS_STORAGE_MAX_FILES; i++) {
        if(cads_files[i].in_use) return true;
    }
    for(uint32_t i = 0; i < CADS_STORAGE_MAX_DIRS; i++) {
        if(cads_dirs[i].in_use) return true;
    }
    return false;
}

static struct cads_storage_file* cads_file_alloc(void) {
    for(uint32_t i = 0; i < CADS_STORAGE_MAX_FILES; i++) {
        if(!cads_files[i].in_use) {
            cads_files[i].in_use = true;
            return &cads_files[i];
        }
    }
    return NULL;
}

static struct cads_storage_dir* cads_dir_alloc(void) {
    for(uint32_t i = 0; i < CADS_STORAGE_MAX_DIRS; i++) {
        if(!cads_dirs[i].in_use) {
            cads_dirs[i].in_use = true;
            return &cads_dirs[i];
        }
    }
    return NULL;
}

/* --- error mapping: lfs.h stays inside this file, cads_storage_status_t is
 *  everything a caller ever sees --------------------------------------------- */

static int cads_map_lfs_error(int err) {
    switch(err) {
        case LFS_ERR_OK: return CADS_STORAGE_OK;
        case LFS_ERR_IO: return CADS_STORAGE_ERR_IO;
        case LFS_ERR_CORRUPT: return CADS_STORAGE_ERR_CORRUPT;
        case LFS_ERR_NOENT: return CADS_STORAGE_ERR_NOENT;
        case LFS_ERR_EXIST: return CADS_STORAGE_ERR_EXIST;
        case LFS_ERR_NOTDIR: return CADS_STORAGE_ERR_NOTDIR;
        case LFS_ERR_ISDIR: return CADS_STORAGE_ERR_ISDIR;
        case LFS_ERR_NOTEMPTY: return CADS_STORAGE_ERR_NOTEMPTY;
        case LFS_ERR_BADF: return CADS_STORAGE_ERR_INVAL;
        case LFS_ERR_FBIG: return CADS_STORAGE_ERR_NOSPC;
        case LFS_ERR_INVAL: return CADS_STORAGE_ERR_INVAL;
        case LFS_ERR_NOSPC: return CADS_STORAGE_ERR_NOSPC;
        case LFS_ERR_NOMEM: return CADS_STORAGE_ERR_NOSLOT;
        case LFS_ERR_NAMETOOLONG: return CADS_STORAGE_ERR_NAMETOOLONG;
        default: return CADS_STORAGE_ERR_IO;
    }
}

/* --- volume ----------------------------------------------------------------- */

static int cads_configure(void) {
    if(cads_flash_init() != CADS_FLASH_OK) return CADS_STORAGE_ERR_IO;

    const cads_flash_geometry_t* geom = cads_flash_geometry();
    if(!geom) return CADS_STORAGE_ERR_IO;

    memset(&cads_lfs_cfg, 0, sizeof(cads_lfs_cfg));
    cads_lfs_cfg.read = cads_bd_read;
    cads_lfs_cfg.prog = cads_bd_prog;
    cads_lfs_cfg.erase = cads_bd_erase;
    cads_lfs_cfg.sync = cads_bd_sync;
    cads_lfs_cfg.read_size = geom->read_size;
    cads_lfs_cfg.prog_size = geom->prog_size;
    cads_lfs_cfg.block_size = geom->block_size;
    cads_lfs_cfg.block_count = geom->block_count;
    cads_lfs_cfg.block_cycles = CADS_STORAGE_BLOCK_CYCLES;
    cads_lfs_cfg.cache_size = CADS_STORAGE_CACHE_SIZE;
    cads_lfs_cfg.lookahead_size = CADS_STORAGE_LOOKAHEAD_SIZE;
    cads_lfs_cfg.read_buffer = cads_read_buffer;
    cads_lfs_cfg.prog_buffer = cads_prog_buffer;
    cads_lfs_cfg.lookahead_buffer = cads_lookahead_buffer;
    cads_lfs_cfg.name_max = LFS_NAME_MAX;

    cads_configured = true;
    return CADS_STORAGE_OK;
}

int cads_storage_mount(void) {
    if(cads_mounted) return CADS_STORAGE_OK;
    if(!cads_configured) {
        int status = cads_configure();
        if(status != CADS_STORAGE_OK) return status;
    }

    int rc = lfs_mount(&cads_lfs, &cads_lfs_cfg);
    if(rc != 0) return cads_map_lfs_error(rc);

    cads_mounted = true;
    return CADS_STORAGE_OK;
}

int cads_storage_format(void) {
    if(cads_any_handle_open()) return CADS_STORAGE_ERR_BUSY;

    if(!cads_configured) {
        int status = cads_configure();
        if(status != CADS_STORAGE_OK) return status;
    }

    if(cads_mounted) {
        lfs_unmount(&cads_lfs);
        cads_mounted = false;
    }

    int rc = lfs_format(&cads_lfs, &cads_lfs_cfg);
    if(rc != 0) return cads_map_lfs_error(rc);

    rc = lfs_mount(&cads_lfs, &cads_lfs_cfg);
    if(rc != 0) return cads_map_lfs_error(rc);

    cads_mounted = true;
    return CADS_STORAGE_OK;
}

int cads_storage_unmount(void) {
    if(!cads_mounted) return CADS_STORAGE_OK;
    if(cads_any_handle_open()) return CADS_STORAGE_ERR_BUSY;

    int rc = lfs_unmount(&cads_lfs);
    cads_mounted = false;
    return (rc == 0) ? CADS_STORAGE_OK : cads_map_lfs_error(rc);
}

bool cads_storage_is_mounted(void) {
    return cads_mounted;
}

int cads_storage_stats(cads_storage_stats_t* stats) {
    if(!stats) return CADS_STORAGE_ERR_INVAL;
    if(!cads_mounted) return CADS_STORAGE_ERR_NOTMOUNTED;

    lfs_ssize_t used = lfs_fs_size(&cads_lfs);
    if(used < 0) return cads_map_lfs_error((int)used);

    stats->block_size = cads_lfs_cfg.block_size;
    stats->block_count = cads_lfs_cfg.block_count;
    stats->used_blocks = (uint32_t)used;
    stats->total_bytes = cads_lfs_cfg.block_size * cads_lfs_cfg.block_count;
    stats->used_bytes = cads_lfs_cfg.block_size * (uint32_t)used;
    return CADS_STORAGE_OK;
}

/* --- files ------------------------------------------------------------------ */

static int cads_translate_open_flags(uint32_t flags) {
    int lfs_flags;
    switch(flags & CADS_STORAGE_RDWR) {
        case CADS_STORAGE_RDONLY: lfs_flags = LFS_O_RDONLY; break;
        case CADS_STORAGE_WRONLY: lfs_flags = LFS_O_WRONLY; break;
        case CADS_STORAGE_RDWR: lfs_flags = LFS_O_RDWR; break;
        default: return -1;
    }
    if(flags & CADS_STORAGE_CREAT) lfs_flags |= LFS_O_CREAT;
    if(flags & CADS_STORAGE_EXCL) lfs_flags |= LFS_O_EXCL;
    if(flags & CADS_STORAGE_TRUNC) lfs_flags |= LFS_O_TRUNC;
    if(flags & CADS_STORAGE_APPEND) lfs_flags |= LFS_O_APPEND;
    return lfs_flags;
}

int cads_storage_open(cads_storage_file_t** file, const char* path, uint32_t flags) {
    if(!file) return CADS_STORAGE_ERR_INVAL;
    *file = NULL;
    if(!cads_mounted) return CADS_STORAGE_ERR_NOTMOUNTED;
    if(!path) return CADS_STORAGE_ERR_INVAL;

    int lfs_flags = cads_translate_open_flags(flags);
    if(lfs_flags < 0) return CADS_STORAGE_ERR_INVAL;

    struct cads_storage_file* handle = cads_file_alloc();
    if(!handle) return CADS_STORAGE_ERR_NOSLOT;

    handle->lfs_file_cfg = (struct lfs_file_config){0};
    handle->lfs_file_cfg.buffer = handle->buffer;

    int rc = lfs_file_opencfg(&cads_lfs, &handle->lfs_file, path, lfs_flags, &handle->lfs_file_cfg);
    if(rc != 0) {
        handle->in_use = false;
        return cads_map_lfs_error(rc);
    }

    *file = (cads_storage_file_t*)handle;
    return CADS_STORAGE_OK;
}

int cads_storage_close(cads_storage_file_t* file) {
    if(!file) return CADS_STORAGE_OK;
    struct cads_storage_file* handle = (struct cads_storage_file*)file;
    /* A double close would hand littlefs an already-closed lfs_file_t; with
     * LFS_NO_ASSERT that corrupts its open-file list instead of stopping. */
    if(!handle->in_use) return CADS_STORAGE_ERR_INVAL;

    int rc = lfs_file_close(&cads_lfs, &handle->lfs_file);
    handle->in_use = false;
    return (rc == 0) ? CADS_STORAGE_OK : cads_map_lfs_error(rc);
}

int32_t cads_storage_read(cads_storage_file_t* file, void* buffer, uint32_t size) {
    if(!file || (!buffer && size != 0u)) return CADS_STORAGE_ERR_INVAL;
    struct cads_storage_file* handle = (struct cads_storage_file*)file;
    if(!handle->in_use) return CADS_STORAGE_ERR_INVAL;

    lfs_ssize_t rc = lfs_file_read(&cads_lfs, &handle->lfs_file, buffer, size);
    return (rc < 0) ? (int32_t)cads_map_lfs_error((int)rc) : (int32_t)rc;
}

int32_t cads_storage_write(cads_storage_file_t* file, const void* data, uint32_t size) {
    if(!file || (!data && size != 0u)) return CADS_STORAGE_ERR_INVAL;
    struct cads_storage_file* handle = (struct cads_storage_file*)file;
    if(!handle->in_use) return CADS_STORAGE_ERR_INVAL;

    lfs_ssize_t rc = lfs_file_write(&cads_lfs, &handle->lfs_file, data, size);
    return (rc < 0) ? (int32_t)cads_map_lfs_error((int)rc) : (int32_t)rc;
}

int32_t cads_storage_seek(cads_storage_file_t* file, int32_t offset, cads_storage_whence_t whence) {
    if(!file) return CADS_STORAGE_ERR_INVAL;
    struct cads_storage_file* handle = (struct cads_storage_file*)file;
    if(!handle->in_use) return CADS_STORAGE_ERR_INVAL;

    int lfs_whence;
    switch(whence) {
        case CADS_STORAGE_SEEK_SET: lfs_whence = LFS_SEEK_SET; break;
        case CADS_STORAGE_SEEK_CUR: lfs_whence = LFS_SEEK_CUR; break;
        case CADS_STORAGE_SEEK_END: lfs_whence = LFS_SEEK_END; break;
        default: return CADS_STORAGE_ERR_INVAL;
    }

    lfs_soff_t rc = lfs_file_seek(&cads_lfs, &handle->lfs_file, offset, lfs_whence);
    return (rc < 0) ? (int32_t)cads_map_lfs_error((int)rc) : (int32_t)rc;
}

int32_t cads_storage_tell(cads_storage_file_t* file) {
    if(!file) return CADS_STORAGE_ERR_INVAL;
    struct cads_storage_file* handle = (struct cads_storage_file*)file;
    if(!handle->in_use) return CADS_STORAGE_ERR_INVAL;

    lfs_soff_t rc = lfs_file_tell(&cads_lfs, &handle->lfs_file);
    return (rc < 0) ? (int32_t)cads_map_lfs_error((int)rc) : (int32_t)rc;
}

int32_t cads_storage_size(cads_storage_file_t* file) {
    if(!file) return CADS_STORAGE_ERR_INVAL;
    struct cads_storage_file* handle = (struct cads_storage_file*)file;
    if(!handle->in_use) return CADS_STORAGE_ERR_INVAL;

    lfs_soff_t rc = lfs_file_size(&cads_lfs, &handle->lfs_file);
    return (rc < 0) ? (int32_t)cads_map_lfs_error((int)rc) : (int32_t)rc;
}

int cads_storage_sync(cads_storage_file_t* file) {
    if(!file) return CADS_STORAGE_ERR_INVAL;
    struct cads_storage_file* handle = (struct cads_storage_file*)file;
    if(!handle->in_use) return CADS_STORAGE_ERR_INVAL;

    int rc = lfs_file_sync(&cads_lfs, &handle->lfs_file);
    return (rc == 0) ? CADS_STORAGE_OK : cads_map_lfs_error(rc);
}

/* --- paths ------------------------------------------------------------------ */

static void cads_info_from_lfs(cads_storage_info_t* info, const struct lfs_info* lfs_info) {
    memset(info, 0, sizeof(*info));
    size_t len = strlen(lfs_info->name);
    if(len > CADS_STORAGE_NAME_MAX) len = CADS_STORAGE_NAME_MAX;
    memcpy(info->name, lfs_info->name, len);
    info->name[len] = '\0';
    info->size = lfs_info->size;
    info->type = (lfs_info->type == LFS_TYPE_DIR) ? CADS_STORAGE_TYPE_DIR : CADS_STORAGE_TYPE_FILE;
}

int cads_storage_stat(const char* path, cads_storage_info_t* info) {
    if(!path || !info) return CADS_STORAGE_ERR_INVAL;
    if(!cads_mounted) return CADS_STORAGE_ERR_NOTMOUNTED;

    struct lfs_info lfs_info;
    int rc = lfs_stat(&cads_lfs, path, &lfs_info);
    if(rc != 0) return cads_map_lfs_error(rc);

    cads_info_from_lfs(info, &lfs_info);
    return CADS_STORAGE_OK;
}

bool cads_storage_exists(const char* path) {
    cads_storage_info_t info;
    return cads_storage_stat(path, &info) == CADS_STORAGE_OK;
}

int cads_storage_remove(const char* path) {
    if(!path) return CADS_STORAGE_ERR_INVAL;
    if(!cads_mounted) return CADS_STORAGE_ERR_NOTMOUNTED;

    int rc = lfs_remove(&cads_lfs, path);
    return (rc == 0) ? CADS_STORAGE_OK : cads_map_lfs_error(rc);
}

int cads_storage_rename(const char* from, const char* to) {
    if(!from || !to) return CADS_STORAGE_ERR_INVAL;
    if(!cads_mounted) return CADS_STORAGE_ERR_NOTMOUNTED;

    int rc = lfs_rename(&cads_lfs, from, to);
    return (rc == 0) ? CADS_STORAGE_OK : cads_map_lfs_error(rc);
}

int cads_storage_mkdir(const char* path) {
    if(!path) return CADS_STORAGE_ERR_INVAL;
    if(!cads_mounted) return CADS_STORAGE_ERR_NOTMOUNTED;

    int rc = lfs_mkdir(&cads_lfs, path);
    return (rc == 0) ? CADS_STORAGE_OK : cads_map_lfs_error(rc);
}

/* --- directories -------------------------------------------------------------
 *
 * "." and ".." are filtered here rather than passed through: every caller in
 * this firmware would otherwise have to remember to skip them, and a browser
 * that lists them by accident is a browser that navigates into itself.
 */

int cads_storage_dir_open(cads_storage_dir_t** dir, const char* path) {
    if(!dir) return CADS_STORAGE_ERR_INVAL;
    *dir = NULL;
    if(!cads_mounted) return CADS_STORAGE_ERR_NOTMOUNTED;
    if(!path) return CADS_STORAGE_ERR_INVAL;

    struct cads_storage_dir* handle = cads_dir_alloc();
    if(!handle) return CADS_STORAGE_ERR_NOSLOT;

    int rc = lfs_dir_open(&cads_lfs, &handle->lfs_dir, path);
    if(rc != 0) {
        handle->in_use = false;
        return cads_map_lfs_error(rc);
    }

    *dir = (cads_storage_dir_t*)handle;
    return CADS_STORAGE_OK;
}

int cads_storage_dir_read(cads_storage_dir_t* dir, cads_storage_info_t* info) {
    if(!dir || !info) return CADS_STORAGE_ERR_INVAL;
    struct cads_storage_dir* handle = (struct cads_storage_dir*)dir;

    for(;;) {
        struct lfs_info lfs_info;
        int rc = lfs_dir_read(&cads_lfs, &handle->lfs_dir, &lfs_info);
        if(rc < 0) return cads_map_lfs_error(rc);
        if(rc == 0) return 0;

        if(strcmp(lfs_info.name, ".") == 0 || strcmp(lfs_info.name, "..") == 0) continue;

        cads_info_from_lfs(info, &lfs_info);
        return 1;
    }
}

int cads_storage_dir_close(cads_storage_dir_t* dir) {
    if(!dir) return CADS_STORAGE_OK;
    struct cads_storage_dir* handle = (struct cads_storage_dir*)dir;

    int rc = lfs_dir_close(&cads_lfs, &handle->lfs_dir);
    handle->in_use = false;
    return (rc == 0) ? CADS_STORAGE_OK : cads_map_lfs_error(rc);
}

/* --- diagnostics -------------------------------------------------------------- */

const char* cads_storage_status_text(int status) {
    switch(status) {
        case CADS_STORAGE_OK: return "ok";
        case CADS_STORAGE_ERR_IO: return "medium refused or misbehaved";
        case CADS_STORAGE_ERR_CORRUPT: return "no valid filesystem, or damage";
        case CADS_STORAGE_ERR_NOENT: return "no such file or directory";
        case CADS_STORAGE_ERR_EXIST: return "already exists";
        case CADS_STORAGE_ERR_NOTDIR: return "not a directory";
        case CADS_STORAGE_ERR_ISDIR: return "is a directory";
        case CADS_STORAGE_ERR_NOTEMPTY: return "directory not empty";
        case CADS_STORAGE_ERR_INVAL: return "invalid argument";
        case CADS_STORAGE_ERR_NOSPC: return "volume is full";
        case CADS_STORAGE_ERR_NOSLOT: return "no free handle";
        case CADS_STORAGE_ERR_NOTMOUNTED: return "volume is not mounted";
        case CADS_STORAGE_ERR_BUSY: return "handles are still open";
        case CADS_STORAGE_ERR_NAMETOOLONG: return "name too long";
        default: return "unknown storage error";
    }
}

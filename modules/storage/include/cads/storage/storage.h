/*
 * CaDS Zero storage - path based filesystem service.
 *
 * A littlefs volume on the medium described by cads/storage/flash.h, wrapped
 * in the smallest API a file browser and a settings screen actually need.
 * This header deliberately does not expose littlefs: the type of a file handle
 * is opaque, the error codes are ours, and nothing above this module includes
 * lfs.h. Swapping the filesystem would then be a change to one module rather
 * than a change to every caller.
 *
 * NO ALLOCATION. There is no heap on this device. Handles come from fixed
 * pools sized by CADS_STORAGE_MAX_FILES and CADS_STORAGE_MAX_DIRS; open()
 * returns CADS_STORAGE_ERR_NOSLOT when the pool is exhausted rather than
 * growing it. Close what you open.
 *
 * NOT THREAD SAFE. One task owns the filesystem. There is no internal mutex
 * because the only lock worth having here is one that also covers the multi
 * second erase, and that policy belongs to the application, not to this
 * module. Calling from two tasks at once corrupts the volume.
 */

#ifndef CADS_STORAGE_STORAGE_H
#define CADS_STORAGE_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest file name, excluding the terminator. littlefs stores this limit in
 * the superblock, so it is fixed at format time; 63 rather than littlefs'
 * default of 255 because every directory listing entry costs this many bytes
 * of stack or struct, and 192 KB of RAM does not stretch to 256 byte names. */
#define CADS_STORAGE_NAME_MAX 63u

/* Longest path accepted by the path based calls, terminator excluded. */
#define CADS_STORAGE_PATH_MAX 127u

#ifndef CADS_STORAGE_MAX_FILES
#define CADS_STORAGE_MAX_FILES 4u
#endif

#ifndef CADS_STORAGE_MAX_DIRS
#define CADS_STORAGE_MAX_DIRS 2u
#endif

/**
 * Status codes. Zero is success, negative is failure, and the byte counts
 * returned by read and write are non-negative on success - so `if(result < 0)`
 * is the single test that works everywhere in this API.
 */
typedef enum {
    CADS_STORAGE_OK = 0,
    CADS_STORAGE_ERR_IO = -1,         /**< the medium refused or misbehaved   */
    CADS_STORAGE_ERR_CORRUPT = -2,    /**< no valid filesystem, or damage     */
    CADS_STORAGE_ERR_NOENT = -3,      /**< no such file or directory          */
    CADS_STORAGE_ERR_EXIST = -4,      /**< already exists                     */
    CADS_STORAGE_ERR_NOTDIR = -5,
    CADS_STORAGE_ERR_ISDIR = -6,
    CADS_STORAGE_ERR_NOTEMPTY = -7,   /**< remove() on a populated directory  */
    CADS_STORAGE_ERR_INVAL = -8,      /**< bad argument, or a too long path   */
    CADS_STORAGE_ERR_NOSPC = -9,      /**< the volume is full                 */
    CADS_STORAGE_ERR_NOSLOT = -10,    /**< the static handle pool is empty    */
    CADS_STORAGE_ERR_NOTMOUNTED = -11,
    CADS_STORAGE_ERR_BUSY = -12,      /**< handles are still open             */
    CADS_STORAGE_ERR_NAMETOOLONG = -13,
} cads_storage_status_t;

/** Open flags. Combine one access mode with any of the creation flags. */
#define CADS_STORAGE_RDONLY 0x0001u
#define CADS_STORAGE_WRONLY 0x0002u
#define CADS_STORAGE_RDWR   0x0003u
#define CADS_STORAGE_CREAT  0x0100u /**< create if absent                     */
#define CADS_STORAGE_EXCL   0x0200u /**< with CREAT, fail if it exists        */
#define CADS_STORAGE_TRUNC  0x0400u /**< discard existing contents            */
#define CADS_STORAGE_APPEND 0x0800u /**< every write goes to the end          */

typedef enum {
    CADS_STORAGE_TYPE_FILE = 1,
    CADS_STORAGE_TYPE_DIR = 2,
} cads_storage_type_t;

typedef enum {
    CADS_STORAGE_SEEK_SET = 0,
    CADS_STORAGE_SEEK_CUR = 1,
    CADS_STORAGE_SEEK_END = 2,
} cads_storage_whence_t;

/** One directory entry, or the result of a stat. */
typedef struct {
    char name[CADS_STORAGE_NAME_MAX + 1u];
    uint32_t size; /**< bytes; meaningless for a directory */
    cads_storage_type_t type;
} cads_storage_info_t;

/** Volume level numbers, for a status screen and for deciding whether a write
 *  is worth attempting. `used_bytes` counts whole blocks in use, because that
 *  is what littlefs can answer cheaply and what actually limits the volume. */
typedef struct {
    uint32_t block_size;
    uint32_t block_count;
    uint32_t used_blocks;
    uint32_t total_bytes;
    uint32_t used_bytes;
} cads_storage_stats_t;

/** Opaque handles drawn from the module's static pools. */
typedef struct cads_storage_file cads_storage_file_t;
typedef struct cads_storage_dir cads_storage_dir_t;

/* --- volume ---------------------------------------------------------------- */

/**
 * Initialise the flash driver and mount the volume.
 *
 * Returns CADS_STORAGE_ERR_CORRUPT when the medium holds no valid filesystem,
 * which is the expected answer on a board that has never been formatted. That
 * is not a fault: call cads_storage_format() and mount again. Mounting twice
 * is a no-op that returns CADS_STORAGE_OK.
 */
int cads_storage_mount(void);

/**
 * Erase every block of the window and write a fresh empty filesystem, then
 * leave the volume mounted.
 *
 * This destroys the contents of the storage window and nothing else - the
 * firmware lives in bank 1 and is not reachable from here. It erases seven
 * 128 KB sectors one at a time and therefore takes several seconds.
 *
 * Refused with CADS_STORAGE_ERR_BUSY while any file or directory handle is
 * open, because the alternative is a handle pointing into a filesystem that
 * no longer exists.
 */
int cads_storage_format(void);

/** Flush and unmount. Refused with CADS_STORAGE_ERR_BUSY while handles are
 *  open. Unmounting when not mounted is a no-op. */
int cads_storage_unmount(void);

bool cads_storage_is_mounted(void);

/** Volume geometry and usage. Requires a mounted volume. */
int cads_storage_stats(cads_storage_stats_t* stats);

/* --- files ----------------------------------------------------------------- */

/**
 * Open `path`, taking a handle from the static pool.
 *
 * On success `*file` receives the handle; on failure it is set to NULL. The
 * handle stays valid until cads_storage_close(), which must be called even
 * when a later read or write failed - otherwise the slot leaks for the
 * lifetime of the firmware.
 */
int cads_storage_open(cads_storage_file_t** file, const char* path, uint32_t flags);

/** Flush, release the slot. Passing NULL is a no-op returning OK, so cleanup
 *  paths do not need a null test. */
int cads_storage_close(cads_storage_file_t* file);

/** Bytes read, 0 at end of file, or a negative status. Short reads happen
 *  only at end of file. */
int32_t cads_storage_read(cads_storage_file_t* file, void* buffer, uint32_t size);

/**
 * Bytes written, or a negative status. A write that does not fit returns
 * CADS_STORAGE_ERR_NOSPC; littlefs keeps the file consistent, so the failed
 * write leaves the previous contents intact rather than a truncated mixture.
 *
 * Data reaches the medium at cads_storage_sync() or cads_storage_close(), not
 * necessarily here.
 */
int32_t cads_storage_write(cads_storage_file_t* file, const void* data, uint32_t size);

/** New absolute position, or a negative status. */
int32_t cads_storage_seek(cads_storage_file_t* file, int32_t offset, cads_storage_whence_t whence);

/** Current position, or a negative status. */
int32_t cads_storage_tell(cads_storage_file_t* file);

/** Size in bytes, or a negative status. */
int32_t cads_storage_size(cads_storage_file_t* file);

/** Push buffered writes down to the medium without closing. */
int cads_storage_sync(cads_storage_file_t* file);

/* --- paths ----------------------------------------------------------------- */

int cads_storage_stat(const char* path, cads_storage_info_t* info);
bool cads_storage_exists(const char* path);
int cads_storage_remove(const char* path);
int cads_storage_rename(const char* from, const char* to);
int cads_storage_mkdir(const char* path);

/* --- directories ----------------------------------------------------------- */

int cads_storage_dir_open(cads_storage_dir_t** dir, const char* path);

/**
 * Read the next entry.
 *
 * Returns 1 and fills `info` while entries remain, 0 at the end of the
 * directory, negative on failure. The "." and ".." entries littlefs reports
 * are skipped here: every caller in this firmware filters them out anyway,
 * and a browser that lists them by accident is a browser that navigates into
 * itself.
 */
int cads_storage_dir_read(cads_storage_dir_t* dir, cads_storage_info_t* info);

int cads_storage_dir_close(cads_storage_dir_t* dir);

/** A short, stable, English description of a status code, for logs and for
 *  the screen. Never NULL. */
const char* cads_storage_status_text(int status);

#ifdef __cplusplus
}
#endif

#endif /* CADS_STORAGE_STORAGE_H */

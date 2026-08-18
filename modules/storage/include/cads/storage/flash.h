/*
 * CaDS Zero storage - the narrow interface to erasable storage.
 *
 * This is the only part of the storage module that knows what the medium is,
 * and it exists so that the rest of it does not. Two implementations satisfy
 * it:
 *
 *   src/cads_flash_stm32f4.c   the STM32F429's internal flash, bank 2
 *   src/cads_flash_host.c      a RAM array on the host, optionally file backed
 *
 * Everything above - the littlefs glue, the storage service, the key/value
 * store - is target neutral C11 and links against either one.
 *
 * ADDRESSING. Every offset here is relative to the start of the storage
 * window, never an absolute flash address. That is deliberate: a caller who
 * gets an offset wrong lands somewhere else inside the window, whereas a
 * caller who gets an absolute address wrong lands in the firmware. The board
 * implementation still re-checks the absolute address it computed immediately
 * before it touches the flash controller, because one bounds check that can
 * be reasoned about is worth less than two that disagree loudly.
 *
 * Read docs/SAFETY.md section 4 before changing anything in here.
 */

#ifndef CADS_STORAGE_FLASH_H
#define CADS_STORAGE_FLASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Status codes. Negative on failure so a caller can test `< 0`, and distinct
 * per cause so a refused write can be reported as refused rather than as a
 * generic failure - "the driver said out of range" is a bug report, "the
 * driver said error" is a mystery.
 */
typedef enum {
    CADS_FLASH_OK = 0,
    CADS_FLASH_ERR_RANGE = -1,  /**< outside the window, or would run past it */
    CADS_FLASH_ERR_ALIGN = -2,  /**< offset or size violates the geometry     */
    CADS_FLASH_ERR_ARG = -3,    /**< null buffer, or a size of zero           */
    CADS_FLASH_ERR_STATE = -4,  /**< cads_flash_init() has not succeeded      */
    CADS_FLASH_ERR_IO = -5,     /**< the controller reported an error flag    */
    CADS_FLASH_ERR_VERIFY = -6, /**< read back did not match what was written */
} cads_flash_status_t;

/**
 * Fixed properties of the medium. Returned by pointer to storage the driver
 * owns and never changes after cads_flash_init(); the caller must not free or
 * modify it.
 *
 * `base` is informational - it is the absolute address the window starts at,
 * useful for a diagnostic screen. No API here takes an absolute address.
 */
typedef struct {
    uint32_t base;        /**< absolute address of offset 0                   */
    uint32_t size;        /**< bytes in the window, block_size * block_count  */
    uint32_t block_size;  /**< erase granularity, and littlefs' block size    */
    uint32_t block_count; /**< erasable blocks                                */
    uint32_t read_size;   /**< reads must be a multiple of this               */
    uint32_t prog_size;   /**< programs must be a multiple of this            */
} cads_flash_geometry_t;

/**
 * Bring the driver up and validate that the window it was compiled for
 * actually exists on this part. Idempotent; every other call returns
 * CADS_FLASH_ERR_STATE until this one has returned CADS_FLASH_OK.
 */
int cads_flash_init(void);

/** Geometry, or NULL before a successful cads_flash_init(). */
const cads_flash_geometry_t* cads_flash_geometry(void);

/**
 * Copy `size` bytes from `offset` into `buffer`.
 *
 * `offset` and `size` must both be multiples of `read_size`, and the range
 * must lie entirely inside the window.
 */
int cads_flash_read(uint32_t offset, void* buffer, uint32_t size);

/**
 * Program `size` bytes at `offset`.
 *
 * The range must have been erased since it was last programmed: flash bits go
 * one to zero on a program and only back to one on an erase, so programming
 * over live data silently ANDs the two. The driver reads back what it wrote
 * and returns CADS_FLASH_ERR_VERIFY when they differ, which is what catches
 * exactly that mistake.
 *
 * `offset` and `size` must both be multiples of `prog_size`.
 */
int cads_flash_program(uint32_t offset, const void* data, uint32_t size);

/**
 * Erase one block, leaving every byte of it 0xFF.
 *
 * On the board this erases one 128 KB flash sector and takes on the order of
 * one second, during which the calling thread is blocked. The routine runs
 * from RAM and the sector is in bank 2, so code and interrupt handlers in
 * bank 1 keep running throughout.
 */
int cads_flash_erase_block(uint32_t block);

/**
 * Make everything written so far durable. A no-op on the board - a program
 * that has returned has already reached the array - and a flush of the
 * backing file on the host.
 */
int cads_flash_sync(void);

#ifdef __cplusplus
}
#endif

#endif /* CADS_STORAGE_FLASH_H */

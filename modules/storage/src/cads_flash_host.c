/*
 * CaDS Zero storage - cads/storage/flash.h over a RAM array, for the host.
 *
 * Emulates the two properties of the real medium that the storage service and
 * its tests actually depend on:
 *
 *   - erase sets a block to all-0xFF, program can only clear bits (a program
 *     ANDs the new bytes into what is already there), so "programmed over
 *     live data" is reproducible on the host exactly as it would misbehave on
 *     the board - which is what lets cads_flash_program()'s readback verify
 *     catch it in both places with the same test.
 *   - every offset is bounds and alignment checked against the same geometry
 *     the board reports, so a test that exercises CADS_FLASH_ERR_RANGE here
 *     is exercising the same guard the board's driver runs.
 *
 * Not emulated: erase/program timing, wear, and power loss mid-operation.
 */

#include "cads/storage/flash.h"

#include <stdbool.h>
#include <string.h>

/* Same shape as CADS_FS_* in targets/itsboard/board.h. Not included from
 * board.h: this file must build on the host, which has no board.h, and the
 * geometry is a property of the emulation, not of a real chip. */
#define CADS_FLASH_HOST_BASE 0x08120000u
#define CADS_FLASH_HOST_BLOCK_SIZE (128u * 1024u)
#define CADS_FLASH_HOST_BLOCK_COUNT 7u
#define CADS_FLASH_HOST_SIZE (CADS_FLASH_HOST_BLOCK_SIZE * CADS_FLASH_HOST_BLOCK_COUNT)
#define CADS_FLASH_HOST_UNIT 4u /* mirrors the board's x32 program width */

static uint8_t cads_flash_ram[CADS_FLASH_HOST_SIZE];
static bool cads_flash_ready = false;
static cads_flash_geometry_t cads_flash_geom;

int cads_flash_init(void) {
    if(cads_flash_ready) return CADS_FLASH_OK;

    memset(cads_flash_ram, 0xFF, sizeof(cads_flash_ram));
    cads_flash_geom = (cads_flash_geometry_t){
        .base = CADS_FLASH_HOST_BASE,
        .size = CADS_FLASH_HOST_SIZE,
        .block_size = CADS_FLASH_HOST_BLOCK_SIZE,
        .block_count = CADS_FLASH_HOST_BLOCK_COUNT,
        .read_size = CADS_FLASH_HOST_UNIT,
        .prog_size = CADS_FLASH_HOST_UNIT,
    };
    cads_flash_ready = true;
    return CADS_FLASH_OK;
}

const cads_flash_geometry_t* cads_flash_geometry(void) {
    return cads_flash_ready ? &cads_flash_geom : NULL;
}

static bool cads_flash_in_window(uint32_t offset, uint32_t size) {
    if(size == 0u) return false;
    if(offset > CADS_FLASH_HOST_SIZE) return false;
    if(size > CADS_FLASH_HOST_SIZE - offset) return false; /* overflow-safe */
    return true;
}

int cads_flash_read(uint32_t offset, void* buffer, uint32_t size) {
    if(!cads_flash_ready) return CADS_FLASH_ERR_STATE;
    if(!buffer || size == 0u) return CADS_FLASH_ERR_ARG;
    if((offset % CADS_FLASH_HOST_UNIT) != 0u || (size % CADS_FLASH_HOST_UNIT) != 0u) {
        return CADS_FLASH_ERR_ALIGN;
    }
    if(!cads_flash_in_window(offset, size)) return CADS_FLASH_ERR_RANGE;

    memcpy(buffer, &cads_flash_ram[offset], size);
    return CADS_FLASH_OK;
}

int cads_flash_program(uint32_t offset, const void* data, uint32_t size) {
    if(!cads_flash_ready) return CADS_FLASH_ERR_STATE;
    if(!data) return CADS_FLASH_ERR_ARG;
    if((offset % CADS_FLASH_HOST_UNIT) != 0u || (size % CADS_FLASH_HOST_UNIT) != 0u) {
        return CADS_FLASH_ERR_ALIGN;
    }
    if(!cads_flash_in_window(offset, size)) return CADS_FLASH_ERR_RANGE;

    const uint8_t* src = (const uint8_t*)data;
    for(uint32_t i = 0; i < size; i++) {
        cads_flash_ram[offset + i] &= src[i]; /* NOR flash: a program can only clear bits */
    }

    if(memcmp(&cads_flash_ram[offset], data, size) != 0) return CADS_FLASH_ERR_VERIFY;
    return CADS_FLASH_OK;
}

int cads_flash_erase_block(uint32_t block) {
    if(!cads_flash_ready) return CADS_FLASH_ERR_STATE;
    if(block >= CADS_FLASH_HOST_BLOCK_COUNT) return CADS_FLASH_ERR_RANGE;

    memset(&cads_flash_ram[block * CADS_FLASH_HOST_BLOCK_SIZE], 0xFF, CADS_FLASH_HOST_BLOCK_SIZE);
    return CADS_FLASH_OK;
}

int cads_flash_sync(void) {
    if(!cads_flash_ready) return CADS_FLASH_ERR_STATE;
    return CADS_FLASH_OK;
}

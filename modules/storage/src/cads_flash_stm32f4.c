/*
 * CaDS Zero storage - cads/storage/flash.h over the STM32F429's internal
 * flash, bank 2.
 *
 * Read docs/SAFETY.md section 4 before changing anything in this file.
 *
 * Register level rather than the ST HAL, same choice the rest of this
 * firmware's HAL makes (see targets/itsboard/hal/hal_gpio.h): every write to
 * FLASH->CR is visible here rather than hidden in a vendor init struct.
 *
 * TWO INDEPENDENT BOUNDS CHECKS. cads_flash_read/program() check the
 * caller's offset+size against the window before computing an address, and
 * then check the resulting absolute address again immediately before the
 * register write. Both checks are cheap; what they buy is that a bug in one
 * cannot silently become a write below 0x08120000 while the other agrees.
 * cads_flash_erase_block() does the same against the block index and the
 * sector number.
 *
 * .ramfunc. The chip is dual bank, so erasing bank 2 while fetching
 * instructions from bank 1 is legal - but the two functions that actually
 * touch FLASH_CR/FLASH_SR while an operation is in flight
 * (cads_flash_ramfunc_erase, cads_flash_ramfunc_program_word) are placed in
 * .ramfunc anyway, so the question of whether a stall or a cache effect could
 * ever make that unsafe never comes up. Everything else in this file -
 * bounds checking, the source-buffer copy, the post-write verify - runs from
 * flash as normal, because flash is not busy while it runs.
 *
 * PSIZE. This board runs at 3.3 V (docs/SAFETY.md), which is
 * FLASH_VOLTAGE_RANGE_3 in ST's terminology and allows x32 (word) program
 * parallelism. That is also why the program unit here is 4 bytes: writing a
 * narrower unit would leave PSIZE and the actual write width disagreeing,
 * which the reference manual lists as undefined.
 *
 * SECTOR NUMBERING. FLASH_CR's SNB field is 5 bits: SNB[3:0] selects a
 * sector within a bank, SNB[4] selects bank 2. Sectors 0..11 (bank 1) map
 * straight to SNB 0..11; sectors 12..23 (bank 2) need +4 added to land in the
 * SNB[4] position - sector 17, the first sector of this volume, is SNB 21,
 * not SNB 17. This is not a guess: it is ST's own HAL
 * (FLASH_Erase_Sector(), stm32f4xx_hal_flash_ex.c, the STM32F427/29/37/39
 * dual-bank variant) doing exactly this before writing FLASH_CR, and it is
 * the single easiest constant in this file to get wrong in a way that erases
 * the wrong sector instead of failing loudly.
 */

#include "cads/storage/flash.h"

#include <string.h>

#include "board.h"
#include "cads_hal.h"

/* The unlock sequence's two magic words (RM0090 3.7.1). Not provided by the
 * CMSIS device header - only ST's HAL headers define these, and this file
 * deliberately does not link the HAL. */
#define CADS_FLASH_KEY1 0x45670123u
#define CADS_FLASH_KEY2 0xCDEF89ABu

#define CADS_FLASH_UNIT 4u /* x32 program width at 3.3 V */

#define CADS_FLASH_SR_ERRORS \
    (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_RDERR)

/* The window this build was compiled for must actually fit on a 2 MB F429,
 * start no earlier than the documented filesystem sectors, and cover exactly
 * whole sectors. Checked once, at compile time, rather than by probing
 * hardware that has no register to ask "how much flash do I have" - board.h
 * is the single source of truth and this is what keeps it honest. */
_Static_assert(CADS_FS_BASE >= 0x08120000u, "storage window must start at bank 2 sector 17 or later");
_Static_assert(CADS_FS_BASE + CADS_FS_SIZE <= 0x08200000u, "storage window runs past 2 MB of flash");
_Static_assert(CADS_FS_SIZE == CADS_FS_BLOCK_SIZE * CADS_FS_SECTOR_COUNT,
    "window size must equal block_size * block_count");
_Static_assert(CADS_FS_BLOCK_SIZE % CADS_FLASH_UNIT == 0, "block size must be a multiple of the program unit");

static bool cads_flash_ready = false;
static cads_flash_geometry_t cads_flash_geom;

static void cads_flash_unlock(void) {
    if(FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = CADS_FLASH_KEY1;
        FLASH->KEYR = CADS_FLASH_KEY2;
    }
}

static void cads_flash_lock(void) {
    FLASH->CR |= FLASH_CR_LOCK;
}

/* Bank-relative sector 0..11 stays put; bank 2 (12..23) needs +4 so the value
 * lands with SNB[4] set. See the file header - this is ST's own HAL logic,
 * not a derivation. */
static uint32_t cads_flash_sector_to_snb(uint32_t sector) {
    return (sector > 11u) ? (sector + 4u) : sector;
}

/*
 * Runs from RAM. Owns the sector erase from FLASH_CR write through BSY
 * clearing; nothing outside this function touches FLASH_CR/FLASH_SR while an
 * erase is in flight. Caller has already unlocked the controller and
 * validated the sector twice.
 */
__attribute__((section(".ramfunc"))) static uint32_t cads_flash_ramfunc_erase(uint32_t snb) {
    FLASH->SR = CADS_FLASH_SR_ERRORS;
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SNB)) | FLASH_CR_PSIZE_1 | FLASH_CR_SER |
                (snb << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    while(FLASH->SR & FLASH_SR_BSY) {
    }
    uint32_t sr = FLASH->SR;
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    return sr;
}

/*
 * Runs from RAM. Programs exactly one 4-byte word and waits for it to land;
 * same isolation rule as cads_flash_ramfunc_erase. The word is already
 * assembled by the caller so this function never dereferences the caller's
 * (possibly misaligned) source buffer.
 */
__attribute__((section(".ramfunc"))) static uint32_t cads_flash_ramfunc_program_word(uint32_t addr,
                                                                                       uint32_t word) {
    FLASH->SR = CADS_FLASH_SR_ERRORS;
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_1;
    FLASH->CR |= FLASH_CR_PG;
    *(volatile uint32_t*)addr = word;
    while(FLASH->SR & FLASH_SR_BSY) {
    }
    FLASH->CR &= ~FLASH_CR_PG;
    return FLASH->SR;
}

int cads_flash_init(void) {
    if(cads_flash_ready) return CADS_FLASH_OK;

    cads_flash_geom = (cads_flash_geometry_t){
        .base = CADS_FS_BASE,
        .size = CADS_FS_SIZE,
        .block_size = CADS_FS_BLOCK_SIZE,
        .block_count = CADS_FS_SECTOR_COUNT,
        .read_size = CADS_FLASH_UNIT,
        .prog_size = CADS_FLASH_UNIT,
    };
    cads_flash_ready = true;
    return CADS_FLASH_OK;
}

const cads_flash_geometry_t* cads_flash_geometry(void) {
    return cads_flash_ready ? &cads_flash_geom : NULL;
}

static bool cads_flash_in_window(uint32_t offset, uint32_t size) {
    if(size == 0u) return false;
    if(offset > CADS_FS_SIZE) return false;
    if(size > CADS_FS_SIZE - offset) return false; /* overflow-safe */
    return true;
}

/* Second, independent check: against the absolute address rather than the
 * offset, run again right before the operation that actually touches
 * hardware. See the file header. */
static bool cads_flash_addr_is_safe(uint32_t addr, uint32_t size) {
    if(addr < CADS_FS_BASE) return false;
    uint32_t end = addr + size;
    if(end < addr) return false; /* wrapped */
    if(end > CADS_FS_BASE + CADS_FS_SIZE) return false;
    return true;
}

int cads_flash_read(uint32_t offset, void* buffer, uint32_t size) {
    if(!cads_flash_ready) return CADS_FLASH_ERR_STATE;
    if(!buffer) return CADS_FLASH_ERR_ARG;
    if((offset % CADS_FLASH_UNIT) != 0u || (size % CADS_FLASH_UNIT) != 0u) return CADS_FLASH_ERR_ALIGN;
    if(!cads_flash_in_window(offset, size)) return CADS_FLASH_ERR_RANGE;

    uint32_t addr = CADS_FS_BASE + offset;
    if(!cads_flash_addr_is_safe(addr, size)) return CADS_FLASH_ERR_RANGE;

    /* Bank 2 is memory mapped; a plain read is all this needs. Bank 1 can be
     * fetching instructions at the same time (dual-bank read-while-write). */
    memcpy(buffer, (const void*)addr, size);
    return CADS_FLASH_OK;
}

int cads_flash_program(uint32_t offset, const void* data, uint32_t size) {
    if(!cads_flash_ready) return CADS_FLASH_ERR_STATE;
    if(!data) return CADS_FLASH_ERR_ARG;
    if((offset % CADS_FLASH_UNIT) != 0u || (size % CADS_FLASH_UNIT) != 0u) return CADS_FLASH_ERR_ALIGN;
    if(!cads_flash_in_window(offset, size)) return CADS_FLASH_ERR_RANGE;

    uint32_t addr = CADS_FS_BASE + offset;
    if(!cads_flash_addr_is_safe(addr, size)) return CADS_FLASH_ERR_RANGE;

    const uint8_t* src = (const uint8_t*)data;
    int status = CADS_FLASH_OK;

    cads_flash_unlock();
    for(uint32_t written = 0; written < size; written += CADS_FLASH_UNIT) {
        /* Assembled here, in flash-resident code, before the ramfunc touches
         * the controller - the ramfunc itself never calls out to anything
         * that might be fetched from the flash it is busy writing. */
        uint32_t word;
        memcpy(&word, src + written, sizeof(word));

        uint32_t target = addr + written;
        if(!cads_flash_addr_is_safe(target, CADS_FLASH_UNIT)) {
            status = CADS_FLASH_ERR_RANGE; /* defensive: unreachable given the checks above */
            break;
        }

        uint32_t sr = cads_flash_ramfunc_program_word(target, word);
        if(sr & CADS_FLASH_SR_ERRORS) {
            status = CADS_FLASH_ERR_IO;
            break;
        }
    }
    cads_flash_lock();

    if(status != CADS_FLASH_OK) return status;

    if(memcmp((const void*)addr, data, size) != 0) return CADS_FLASH_ERR_VERIFY;
    return CADS_FLASH_OK;
}

int cads_flash_erase_block(uint32_t block) {
    if(!cads_flash_ready) return CADS_FLASH_ERR_STATE;
    if(block >= CADS_FS_SECTOR_COUNT) return CADS_FLASH_ERR_RANGE;

    uint32_t sector = CADS_FS_FIRST_SECTOR + block;
    uint32_t addr = CADS_FS_BASE + block * CADS_FS_BLOCK_SIZE;

    /* Second, independent check: the sector number against the documented
     * range, alongside the address check every other entry point runs. */
    if(sector < CADS_FS_FIRST_SECTOR || sector >= CADS_FS_FIRST_SECTOR + CADS_FS_SECTOR_COUNT) {
        return CADS_FLASH_ERR_RANGE;
    }
    if(!cads_flash_addr_is_safe(addr, CADS_FS_BLOCK_SIZE)) return CADS_FLASH_ERR_RANGE;

    cads_flash_unlock();
    uint32_t sr = cads_flash_ramfunc_erase(cads_flash_sector_to_snb(sector));
    cads_flash_lock();

    return (sr & CADS_FLASH_SR_ERRORS) ? CADS_FLASH_ERR_IO : CADS_FLASH_OK;
}

int cads_flash_sync(void) {
    if(!cads_flash_ready) return CADS_FLASH_ERR_STATE;
    /* A completed program has already reached the array; nothing to flush. */
    return CADS_FLASH_OK;
}

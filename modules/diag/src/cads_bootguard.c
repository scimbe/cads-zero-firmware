/*
 * CaDS Zero - crash-loop guard implementation. See the header for why.
 *
 * Lives in CCM for the same reason as the forensic ring (cads_forensic.c):
 * Reset_Handler never zeroes .ccm, so the count survives the watchdog reset
 * it is counting. A power-on leaves CCM random, hence the magic word.
 */

#include "cads/diag/bootguard.h"

#include "cads_hal.h"

#define CADS_BOOTGUARD_MAGIC 0x42674421u /* "!DgB" */

typedef struct {
    uint32_t magic;
    uint32_t count;
} cads_bootguard_state_t;

CADS_CCM_SECTION static cads_bootguard_state_t cads_bootguard;

void cads_bootguard_boot(bool watchdog_reset) {
    if(cads_bootguard.magic != CADS_BOOTGUARD_MAGIC) {
        cads_bootguard.magic = CADS_BOOTGUARD_MAGIC;
        cads_bootguard.count = 0u;
    }
    if(!watchdog_reset) {
        cads_bootguard.count = 0u;
    } else if(cads_bootguard.count < UINT32_MAX) {
        cads_bootguard.count++;
    }
}

uint32_t cads_bootguard_count(void) {
    return cads_bootguard.magic == CADS_BOOTGUARD_MAGIC ? cads_bootguard.count : 0u;
}

bool cads_bootguard_tripped(void) {
    return cads_bootguard_count() >= CADS_BOOTGUARD_LIMIT;
}

void cads_bootguard_stable(void) {
    cads_bootguard.magic = CADS_BOOTGUARD_MAGIC;
    cads_bootguard.count = 0u;
}

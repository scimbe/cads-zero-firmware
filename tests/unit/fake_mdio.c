/* See fake_mdio.h. */

#include "fake_mdio.h"

#include <string.h>

#include "hal_eth_mdio.h"

/* MDIO clause 22 addresses both PHY and register in 5 bits, so 32 covers the
 * entire address space with room to spare - this is a host test double, not
 * a memory-constrained target, so there is no reason to size it any tighter. */
#define CADS_FAKE_MDIO_PHYS 32u
#define CADS_FAKE_MDIO_REGS 32u

static uint16_t cads_fake_mdio_values[CADS_FAKE_MDIO_PHYS][CADS_FAKE_MDIO_REGS];
static bool cads_fake_mdio_present[CADS_FAKE_MDIO_PHYS][CADS_FAKE_MDIO_REGS];
static uint32_t cads_fake_mdio_reads[CADS_FAKE_MDIO_PHYS][CADS_FAKE_MDIO_REGS];

static bool cads_fake_mdio_once_valid[CADS_FAKE_MDIO_PHYS][CADS_FAKE_MDIO_REGS];
static uint16_t cads_fake_mdio_once_value[CADS_FAKE_MDIO_PHYS][CADS_FAKE_MDIO_REGS];

void cads_fake_mdio_reset(void) {
    memset(cads_fake_mdio_values, 0, sizeof(cads_fake_mdio_values));
    memset(cads_fake_mdio_present, 0, sizeof(cads_fake_mdio_present));
    memset(cads_fake_mdio_reads, 0, sizeof(cads_fake_mdio_reads));
    memset(cads_fake_mdio_once_valid, 0, sizeof(cads_fake_mdio_once_valid));
    memset(cads_fake_mdio_once_value, 0, sizeof(cads_fake_mdio_once_value));
}

void cads_fake_mdio_set(uint8_t phy, uint8_t reg, uint16_t value) {
    cads_fake_mdio_values[phy][reg] = value;
    cads_fake_mdio_present[phy][reg] = true;
}

void cads_fake_mdio_queue_once(uint8_t phy, uint8_t reg, uint16_t value) {
    cads_fake_mdio_once_value[phy][reg] = value;
    cads_fake_mdio_once_valid[phy][reg] = true;
}

uint32_t cads_fake_mdio_read_count(uint8_t phy, uint8_t reg) {
    return cads_fake_mdio_reads[phy][reg];
}

bool cads_hal_eth_mdio_read(uint8_t phy, uint8_t reg, uint16_t* value) {
    if(phy >= CADS_FAKE_MDIO_PHYS || reg >= CADS_FAKE_MDIO_REGS) return false;
    cads_fake_mdio_reads[phy][reg]++;

    if(cads_fake_mdio_once_valid[phy][reg]) {
        *value = cads_fake_mdio_once_value[phy][reg];
        cads_fake_mdio_once_valid[phy][reg] = false;
        return true;
    }
    if(!cads_fake_mdio_present[phy][reg]) return false;
    *value = cads_fake_mdio_values[phy][reg];
    return true;
}

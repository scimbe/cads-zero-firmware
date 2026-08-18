/*
 * CaDS Zero - Ethernet PHY diagnostic for the hardware explorer (board side).
 *
 * Kept out of explorer.c itself so that file can stay portable: this is the
 * one command whose implementation needs a target-specific header
 * (hal_eth_mdio.h, board only), and the split is the same pattern tasks.c /
 * tasks_sim.c already uses for the scheduler.
 */

#include "explorer_eth.h"

#include <stdint.h>

#include "cads_hal.h"
#include "hal_eth_mdio.h"
#include "input_probe.h"

static void cads_put_hex16(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char out[4];
    for(int i = 3; i >= 0; i--) {
        out[i] = digits[value & 0xFu];
        value >>= 4;
    }
    cads_hal_console_write(out, 4u);
}

void cads_explorer_eth_status(void) {
    /* PHY management only - this never touches PA7, so the display keeps
     * working while the link is inspected. */
    static bool initialised = false;
    if(!initialised) {
        cads_hal_eth_mdio_init();
        initialised = true;
    }

    cads_eth_phy_status_t phy;
    if(!cads_hal_eth_phy_status(0u, &phy)) {
        cads_probe_puts("# PHY: no answer at address 0\r\n");
        return;
    }

    cads_probe_puts("# PHY id=");
    cads_put_hex16(phy.id1);
    cads_probe_puts(":");
    cads_put_hex16(phy.id2);
    cads_probe_puts(" oui=0x");
    cads_put_hex16((uint16_t)(phy.oui >> 8));
    cads_put_hex16((uint16_t)(phy.oui & 0xFFu));
    cads_probe_puts(" model=");
    cads_probe_put_uint(phy.model);
    cads_probe_puts(" rev=");
    cads_probe_put_uint(phy.revision);
    cads_probe_puts("\r\n# PHY bsr=");
    cads_put_hex16(phy.bsr);
    cads_probe_puts(phy.link_up ? " link=UP" : " link=DOWN");
    cads_probe_puts(phy.autoneg_done ? " autoneg=done" : " autoneg=pending");
    cads_probe_puts(" speed=");
    cads_probe_put_uint(phy.speed_mbit);
    cads_probe_puts(phy.full_duplex ? "M full" : "M half");
    cads_probe_puts("\r\n");
}

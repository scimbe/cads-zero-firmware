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
#include "hal_eth_tdr.h"
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

static void cads_report_tdr(const cads_eth_tdr_result_t* r) {
    cads_probe_puts(r->channel == CadsEthChannelMdi ? "# TDR MDI  " : "# TDR MDIX ");
    if(!r->completed) {
        cads_probe_puts("timed out\r\n");
        return;
    }
    switch(r->condition) {
    case CadsEthCableOpen:
        cads_probe_puts("OPEN at ~");
        cads_probe_put_uint(r->distance_m);
        cads_probe_puts("m (raw=");
        cads_probe_put_uint(r->raw_length);
        cads_probe_puts(")\r\n");
        break;
    case CadsEthCableShorted:
        cads_probe_puts("SHORT at ~");
        cads_probe_put_uint(r->distance_m);
        cads_probe_puts("m (raw=");
        cads_probe_put_uint(r->raw_length);
        cads_probe_puts(")\r\n");
        break;
    case CadsEthCableMatched:
        cads_probe_puts("MATCHED (terminated / active far end, or no fault"
                        " on this pair)\r\n");
        break;
    default:
        cads_probe_puts("no condition resolved (raw=");
        cads_probe_put_uint(r->raw_length);
        cads_probe_puts(")\r\n");
        break;
    }
}

void cads_explorer_eth_cable_test(void) {
    static bool initialised = false;
    if(!initialised) {
        cads_hal_eth_mdio_init();
        initialised = true;
    }

    uint16_t matched_before;
    bool had_matched = cads_hal_eth_cable_length_matched(0u, &matched_before);
    if(had_matched) {
        cads_probe_puts("# link was up before the test: matched length ~");
        cads_probe_put_uint(matched_before);
        cads_probe_puts("m\r\n");
    } else {
        cads_probe_puts("# link was down before the test\r\n");
    }

    cads_probe_puts("# running TDR - this will drop the link briefly\r\n");

    cads_eth_tdr_result_t result;
    if(cads_hal_eth_tdr_run(0u, CadsEthChannelMdi, CadsEthCableUnknown, &result)) {
        cads_report_tdr(&result);
    } else {
        cads_probe_puts("# TDR MDI  failed to run (MDIO error)\r\n");
    }
    if(cads_hal_eth_tdr_run(0u, CadsEthChannelMdix, CadsEthCableUnknown, &result)) {
        cads_report_tdr(&result);
    } else {
        cads_probe_puts("# TDR MDIX failed to run (MDIO error)\r\n");
    }

    cads_probe_puts("# cable test done, link will renegotiate\r\n");
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

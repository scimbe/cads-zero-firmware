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
#include "hal_eth_aneg.h"
#include "hal_eth_linklog.h"
#include "hal_eth_mmc.h"
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

static const char* cads_aneg_mode_name(cads_eth_aneg_mode_t mode) {
    switch(mode) {
    case CadsEthAnegMode100Full: return "100 Mbit full duplex";
    case CadsEthAnegMode100Half: return "100 Mbit half duplex";
    case CadsEthAnegMode10Full: return "10 Mbit full duplex";
    case CadsEthAnegMode10Half: return "10 Mbit half duplex";
    default: return "none in common";
    }
}

static void cads_report_ability(const char* label, const cads_eth_aneg_ability_t* a) {
    cads_probe_puts("# ");
    cads_probe_puts(label);
    cads_probe_puts(": ");
    if(a->half_10) cads_probe_puts("10H ");
    if(a->full_10) cads_probe_puts("10F ");
    if(a->half_100) cads_probe_puts("100H ");
    if(a->full_100) cads_probe_puts("100F ");
    if(a->pause) cads_probe_puts("PAUSE ");
    if(a->pause_asym) cads_probe_puts("PAUSE-ASYM ");
    if(a->remote_fault) cads_probe_puts("REMOTE-FAULT ");
    cads_probe_puts("\r\n");
}

void cads_explorer_eth_aneg(void) {
    static bool initialised = false;
    if(!initialised) {
        cads_hal_eth_mdio_init();
        initialised = true;
    }

    cads_eth_aneg_report_t report;
    if(!cads_hal_eth_aneg_report(0u, &report)) {
        cads_probe_puts("# aneg: MDIO read failed\r\n");
        return;
    }

    cads_report_ability("local  ", &report.local);
    cads_report_ability("partner", &report.partner);
    cads_probe_puts("# partner acknowledged our advertisement: ");
    cads_probe_puts(report.partner_acknowledged ? "yes" : "no");
    cads_probe_puts("\r\n# resolved: ");
    cads_probe_puts(cads_aneg_mode_name(report.resolved));
    cads_probe_puts("\r\n");
}

static cads_eth_linklog_t s_linklog;
static bool s_linklog_initialised = false;

static const char* cads_link_event_name(cads_eth_link_event_type_t type) {
    switch(type) {
    case CadsEthLinkEventDown: return "LINK DOWN";
    case CadsEthLinkEventAnegComplete: return "AUTONEG COMPLETE";
    case CadsEthLinkEventRemoteFault: return "REMOTE FAULT";
    default: return "?";
    }
}

void cads_explorer_eth_linklog_poll_and_dump(void) {
    if(!s_linklog_initialised) {
        cads_hal_eth_mdio_init();
        cads_eth_linklog_init(&s_linklog);
        s_linklog_initialised = true;
        cads_probe_puts("# linklog: initialised, watching for events\r\n");
    }

    if(!cads_hal_eth_linklog_poll(0u, &s_linklog)) {
        cads_probe_puts("# linklog: MDIO read failed\r\n");
        return;
    }

    uint32_t count = cads_eth_linklog_count(&s_linklog);
    cads_probe_puts("# linklog: ");
    cads_probe_put_uint(count);
    cads_probe_puts(" event(s), ");
    cads_probe_put_uint(s_linklog.dropped);
    cads_probe_puts(" dropped\r\n");
    for(uint32_t i = 0; i < count; i++) {
        const cads_eth_link_event_t* e = cads_eth_linklog_at(&s_linklog, i);
        cads_probe_puts("#   t=");
        cads_probe_put_uint(e->timestamp_ms);
        cads_probe_puts("ms  ");
        cads_probe_puts(cads_link_event_name(e->type));
        cads_probe_puts("\r\n");
    }
}

void cads_explorer_eth_mmc(void) {
    static bool initialised = false;
    if(!initialised) {
        cads_hal_eth_mdio_init(); /* enables the MAC clock as a side effect */
        initialised = true;
    }

    cads_eth_mmc_counters_t counters;
    cads_hal_eth_mmc_read(&counters);

    cads_probe_puts("# mmc tx_good=");
    cads_probe_put_uint(counters.tx_good_frames);
    cads_probe_puts(" tx_after_1_collision=");
    cads_probe_put_uint(counters.tx_good_after_single_collision);
    cads_probe_puts(" tx_after_n_collisions=");
    cads_probe_put_uint(counters.tx_good_after_multi_collision);
    cads_probe_puts("\r\n#     rx_unicast=");
    cads_probe_put_uint(counters.rx_good_unicast_frames);
    cads_probe_puts(" rx_crc_err=");
    cads_probe_put_uint(counters.rx_crc_errors);
    cads_probe_puts(" rx_align_err=");
    cads_probe_put_uint(counters.rx_alignment_errors);
    cads_probe_puts("\r\n");
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

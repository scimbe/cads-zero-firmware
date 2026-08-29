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

#include <string.h>

#include "board.h"
#include "cads/config/config.h"
#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads_hal.h"
#include "hal_eth_mac.h"
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

/*
 * Locally-administered (bit 1 of the first byte set, per IEEE 802-2014
 * clause 8.2.2), never-forwarded-by-standard-switches unicast (bit 0
 * clear) address. Fixed by default - fine for the single board this
 * firmware currently runs on; the day a second board is on the same
 * segment, this needs to come from something per-device (the STM32's
 * 96-bit UID would be the obvious source) instead of a shared constant.
 *
 * `net.mac_random` (2026-08-29, field-use OPSEC): with that config key set,
 * this becomes a fresh random address every boot instead - a locally
 * administered field device that always shows the same MAC leaves a
 * consistent, trackable identity across every network it ever touches,
 * which is exactly the kind of correlation a field engagement wants to
 * avoid. Generated once, lazily, from the hardware RNG
 * (cads_hal_rng_bytes(), the same driver `J` proves live - RM0090's
 * documented procedure, FIPS 140-2 continuous-test included), then held
 * for the rest of this boot: lwIP and every caller here need one stable
 * address per session, not a new one per packet.
 */
static uint8_t cads_net_mac_value[6] = {0x02, 0xCA, 0xD5, 0x5E, 0x00, 0x01};
static bool cads_net_mac_ready = false;

const uint8_t* cads_explorer_net_mac(void) {
    if(!cads_net_mac_ready) {
        cads_net_mac_ready = true;
        cads_config_t cfg;
        (void)cads_config_load(&cfg); /* always leaves cfg valid, error or not */
        if(cfg.net_mac_random) {
            uint8_t random_bytes[6];
            if(cads_hal_rng_bytes(random_bytes, sizeof(random_bytes))) {
                random_bytes[0] = (uint8_t)((random_bytes[0] & 0xFEu) | 0x02u); /* unicast + locally administered */
                memcpy(cads_net_mac_value, random_bytes, sizeof(cads_net_mac_value));
            }
            /* RNG failure (SECS/CECS live-seed error, exhausted retries):
             * silently keep the fixed default rather than send an
             * uninitialised or partially-random address - see
             * cads_hal_rng_bytes()'s own contract in core/cads_hal.h. */
        }
    }
    return cads_net_mac_value;
}

/*
 * A deliberate, unambiguous TX proof, independent of whatever the LAN
 * happens to be doing. Waiting on ambient broadcast/multicast traffic (ARP,
 * mDNS, STP...) to prove RX works is honest but not reliable on a quiet
 * bench segment - it can go quiet for the whole polling window with a
 * perfectly working driver. There is no equivalent trick for RX (this
 * device cannot make another one send it something), so that half stays
 * opportunistic; see the "mmc delta rx_unicast" line below.
 *
 * EtherType 0x88B5 is IANA-registered "IEEE Std 802 - Local Experimental
 * Ethertype 1" - built exactly for traffic like this, not a made-up value.
 * Broadcast destination so it needs no ARP resolution and nothing on the
 * segment treats it as anything other than noise to ignore.
 */
static void cads_net_send_probe_frame(void) {
    uint8_t frame[60]; /* Ethernet minimum frame size, CRC excluded (the MAC appends that) */
    memset(frame, 0, sizeof(frame));
    memset(frame, 0xFFu, 6u); /* dest: broadcast */
    memcpy(frame + 6, cads_explorer_net_mac(), 6u); /* src */
    frame[12] = 0x88u;
    frame[13] = 0xB5u; /* ethertype */
    bool sent = cads_hal_eth_mac_transmit(frame, sizeof(frame));
    cads_probe_puts(sent ? "# net: probe frame queued\r\n" : "# net: probe frame FAILED to queue\r\n");
}

void cads_explorer_net_test(uint32_t seconds) {
    static bool initialised = false;
    if(!initialised) {
        const uint8_t* mac = cads_explorer_net_mac();
        cads_net_init(mac);
        initialised = true;
        char macbuf[18];
        cads_fmt_mac(macbuf, sizeof(macbuf), mac);
        cads_probe_puts("# net: initialised, mac=");
        cads_probe_puts(macbuf);
        cads_probe_puts("\r\n");
    }
    if(!seconds) seconds = 20u;

    cads_eth_mmc_counters_t mmc_before, mmc_after;
    cads_hal_eth_mmc_read(&mmc_before);

    cads_probe_puts("# net: polling ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    uint32_t start = cads_hal_ticks_ms();
    bool link_was_up = false;
    while((cads_hal_ticks_ms() - start) < seconds * 1000u) {
        cads_net_poll();

        cads_net_status_t status;
        cads_net_status(&status);
        if(status.link_up != link_was_up) {
            link_was_up = status.link_up;
            if(link_was_up) {
                cads_probe_puts("# net: link UP, speed=");
                cads_probe_put_uint(status.speed_mbit);
                cads_probe_puts(status.full_duplex ? "M full\r\n" : "M half\r\n");
                cads_net_send_probe_frame();
            } else {
                cads_probe_puts("# net: link DOWN\r\n");
            }
        }
        cads_hal_delay_us(500u);
    }

    cads_hal_eth_mmc_read(&mmc_after);

    cads_net_status_t status;
    cads_net_status(&status);
    cads_probe_puts("# net: link=");
    cads_probe_puts(status.link_up ? "UP" : "DOWN");
    cads_probe_puts(" netif rx=");
    cads_probe_put_uint(status.rx_frames);
    cads_probe_puts(" tx=");
    cads_probe_put_uint(status.tx_frames);
    cads_probe_puts(" rx_dropped=");
    cads_probe_put_uint(status.rx_dropped);
    cads_probe_puts("\r\n# net: mmc delta rx_unicast=");
    cads_probe_put_uint(mmc_after.rx_good_unicast_frames - mmc_before.rx_good_unicast_frames);
    cads_probe_puts(" tx_good=");
    cads_probe_put_uint(mmc_after.tx_good_frames - mmc_before.tx_good_frames);
    cads_probe_puts(" rx_crc_err=");
    cads_probe_put_uint(mmc_after.rx_crc_errors - mmc_before.rx_crc_errors);
    cads_probe_puts(" rx_align_err=");
    cads_probe_put_uint(mmc_after.rx_alignment_errors - mmc_before.rx_alignment_errors);
    cads_probe_puts("\r\n");
}

void cads_explorer_phy_reg(uint8_t reg, bool do_write, uint16_t value) {
    char hex[6];

    /* Same "once per boot" idempotent init cads_explorer_eth_status() (the
     * 'e' command) uses - MDIO needs SYSCFG_PMC's RMII select bit and the
     * MAC clock enabled before any transaction reads back anything but 0,
     * and this command has to work standalone, not only after 'e' or 'h'
     * happened to run first. */
    static bool initialised = false;
    if(!initialised) {
        cads_hal_eth_mdio_init();
        initialised = true;
    }

    if(do_write) {
        cads_probe_puts("# phy: writing reg ");
        cads_probe_put_uint(reg);
        cads_probe_puts(" = 0x");
        cads_fmt_hex(hex, sizeof(hex), value, 4u, false);
        cads_probe_puts(hex);
        cads_probe_puts("\r\n");
        if(!cads_hal_eth_mdio_write(CADS_ETH_PHY_ADDR, reg, value)) {
            cads_probe_puts("# phy: write failed (MDIO timeout)\r\n");
            return;
        }
    }

    uint16_t readback = 0u;
    if(!cads_hal_eth_mdio_read(CADS_ETH_PHY_ADDR, reg, &readback)) {
        cads_probe_puts("# phy: read failed (MDIO timeout)\r\n");
        return;
    }

    cads_probe_puts("# phy: reg ");
    cads_probe_put_uint(reg);
    cads_probe_puts(" = 0x");
    cads_fmt_hex(hex, sizeof(hex), readback, 4u, false);
    cads_probe_puts(hex);
    if(reg == 0u) {
        cads_probe_puts(" (BMCR: reset=");
        cads_probe_put_uint((readback >> 15) & 1u);
        cads_probe_puts(" loopback=");
        cads_probe_put_uint((readback >> 14) & 1u);
        cads_probe_puts(" an_enable=");
        cads_probe_put_uint((readback >> 12) & 1u);
        cads_probe_puts(" power_down=");
        cads_probe_put_uint((readback >> 11) & 1u);
        cads_probe_puts(" isolate=");
        cads_probe_put_uint((readback >> 10) & 1u);
        cads_probe_puts(" an_restart=");
        cads_probe_put_uint((readback >> 9) & 1u);
        cads_probe_puts(")");
    }
    cads_probe_puts("\r\n");
}

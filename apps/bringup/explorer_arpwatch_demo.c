/*
 * CaDS Zero - ARP spoofing / cache-poisoning detector (board side).
 *
 * WHY THIS DOES NOT CALL cads_net_poll() DURING CAPTURE
 * -------------------------------------------------------
 * Same reasoning as explorer_sniff_demo.c's own file header, not
 * re-derived here: cads_net_poll() drains the same RX descriptor ring
 * this file reads directly via cads_hal_eth_mac_receive(), and running
 * both at once would non-deterministically split frames between the two
 * consumers. Bring-up (the link wait below) still polls; the capture
 * loop itself has exclusive access.
 *
 * THE PARSING/BINDING POLICY ITSELF LIVES IN cads/toolbox/arpwatch.h
 * ---------------------------------------------------------------------
 * This file is only the board-specific capture loop and printout - the
 * actual ARP-claim recognition and the binding table are portable and
 * unit-tested on the host (tests/unit/test_arpwatch.c) with hand-built
 * frames, proof the parsing is correct that does not depend on this
 * bench's own traffic (or lack of it, see explorer_sniff_demo.c's own
 * note on this bench's environment).
 *
 * Uses the shared explorer capture buffer (explorer_capture_buffer.h)
 * rather than its own - see that file's own header for why.
 */

#include "explorer_arpwatch_demo.h"

#include "cads/net/net.h"
#include "cads/toolbox/arpwatch.h"
#include "cads/toolbox/fmt.h"
#include "cads_hal.h"
#include "explorer_capture_buffer.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "hal_eth_mac.h"
#include "input_probe.h"

/* A small segment's own hosts plus the gateway rarely exceeds a handful
 * of addresses within one capture run - eight is headroom, not a real
 * network's full host count, and costs little RAM either way. */
#define CADS_ARPWATCH_DEMO_MAX_BINDINGS 8u

void cads_explorer_arpwatch_demo(uint32_t seconds) {
    if(seconds == 0u) seconds = 20u;

    cads_net_init(cads_explorer_net_mac());

    /* Same reasoning (and the same bug once found and fixed there) as
     * every other explorer_*_demo.c this session: this loop must call
     * cads_net_poll() itself to actually detect the link, not just check
     * its cached status. */
    (void)cads_explorer_net_link_wait(3000u);

    static cads_arpwatch_entry_t storage[CADS_ARPWATCH_DEMO_MAX_BINDINGS];
    cads_arpwatch_table_t table;
    cads_arpwatch_table_init(&table, storage, CADS_ARPWATCH_DEMO_MAX_BINDINGS);

    cads_probe_puts("# arpwatch: tracking IP->MAC bindings for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    cads_hal_eth_mac_set_promiscuous(true);

    uint8_t* frame = cads_explorer_capture_buffer();
    uint32_t frames_seen = 0u;
    uint32_t claims_seen = 0u;
    uint32_t flips_seen = 0u;
    uint32_t start = cads_hal_ticks_ms();

    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        uint16_t length = cads_hal_eth_mac_receive(frame, CADS_EXPLORER_CAPTURE_BUFFER_SIZE);
        if(length == 0u) continue;
        frames_seen++;

        cads_arpwatch_claim_t claim;
        if(cads_arpwatch_parse(frame, length, &claim)) {
            claims_seen++;
            if(cads_arpwatch_table_learn(&table, claim.sender_ip, claim.sender_mac)) {
                flips_seen++;
            }
        }
    }

    cads_hal_eth_mac_set_promiscuous(false);

    bool suspicious = cads_arpwatch_table_any_flip(&table);

    cads_probe_puts("# arpwatch: done, ");
    cads_probe_put_uint(frames_seen);
    cads_probe_puts(" frame(s) seen, ");
    cads_probe_put_uint(claims_seen);
    cads_probe_puts(" ARP claim(s), ");
    cads_probe_put_uint((uint32_t)cads_arpwatch_table_count(&table));
    cads_probe_puts(" IP(s) tracked, ");
    cads_probe_put_uint(flips_seen);
    cads_probe_puts(" MAC change(s) - ");
    cads_probe_puts(suspicious ? "SUSPICIOUS: a binding changed MAC" : "no binding changed, nothing suspicious");
    cads_probe_puts("\r\n");

    for(size_t i = 0u; i < cads_arpwatch_table_count(&table); i++) {
        const cads_arpwatch_entry_t* entry = cads_arpwatch_table_at(&table, i);
        char ip_text[16];
        cads_fmt_ipv4(ip_text, sizeof(ip_text), entry->ip);
        char mac_text[24];
        cads_fmt_mac(mac_text, sizeof(mac_text), entry->mac);

        cads_probe_puts("#   ");
        cads_probe_puts(ip_text);
        cads_probe_puts(" -> ");
        cads_probe_puts(mac_text);
        cads_probe_puts("  sightings=");
        cads_probe_put_uint(entry->sightings);
        cads_probe_puts("  changes=");
        cads_probe_put_uint(entry->mac_changes);
        cads_probe_puts("\r\n");
    }
}

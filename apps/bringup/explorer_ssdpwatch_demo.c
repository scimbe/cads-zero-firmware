/*
 * CaDS Zero - SSDP/UPnP device discovery (board side).
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
 * THE PARSING/DEDUP POLICY ITSELF LIVES IN cads/toolbox/ssdpwatch.h
 * ---------------------------------------------------------------------
 * This file is only the board-specific capture loop and printout - the
 * actual SSDP header parsing and the dedup table are portable and
 * unit-tested on the host (tests/unit/test_ssdpwatch.c) with hand-built
 * messages, proof the parsing is correct that does not depend on this
 * bench's own traffic (or lack of it, see explorer_sniff_demo.c's own
 * note on this bench's environment).
 *
 * Uses the shared explorer capture buffer (explorer_capture_buffer.h)
 * rather than its own - see that file's own header for why.
 */

#include "explorer_ssdpwatch_demo.h"

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/ssdpwatch.h"
#include "cads_hal.h"
#include "explorer_capture_buffer.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "hal_eth_mac.h"
#include "input_probe.h"

/* A small segment rarely has more than a handful of UPnP services
 * announcing themselves within one capture run, even counting one
 * device exposing several - four is headroom, not a real network's
 * full device count. */
#define CADS_SSDPWATCH_DEMO_MAX_ENTRIES 4u

static const char* cads_ssdpwatch_kind_text(cads_ssdpwatch_kind_t kind) {
    switch(kind) {
    case CADS_SSDP_ALIVE: return "ALIVE ";
    case CADS_SSDP_BYEBYE: return "BYEBYE";
    case CADS_SSDP_RESPONSE: return "REPLY ";
    default: return "OTHER ";
    }
}

void cads_explorer_ssdpwatch_demo(uint32_t seconds) {
    if(seconds == 0u) seconds = 20u;

    cads_net_init(cads_explorer_net_mac());

    /* Same reasoning (and the same bug once found and fixed there) as
     * every other explorer_*_demo.c this session: this loop must call
     * cads_net_poll() itself to actually detect the link, not just check
     * its cached status. */
    (void)cads_explorer_net_link_wait(3000u);

    static cads_ssdpwatch_record_t storage[CADS_SSDPWATCH_DEMO_MAX_ENTRIES];
    cads_ssdpwatch_table_t table;
    cads_ssdpwatch_table_init(&table, storage, CADS_SSDPWATCH_DEMO_MAX_ENTRIES);

    cads_probe_puts("# ssdpwatch: watching UDP:1900 for SSDP/UPnP traffic for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    cads_hal_eth_mac_set_promiscuous(true);

    uint8_t* frame = cads_explorer_capture_buffer();
    uint32_t frames_seen = 0u;
    uint32_t messages_seen = 0u;
    uint32_t start = cads_hal_ticks_ms();

    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        uint16_t length = cads_hal_eth_mac_receive(frame, CADS_EXPLORER_CAPTURE_BUFFER_SIZE);
        if(length == 0u) continue;
        frames_seen++;

        cads_ssdpwatch_record_t record;
        if(cads_ssdpwatch_parse(frame, length, &record)) {
            cads_ssdpwatch_table_learn(&table, &record);
            messages_seen++;
        }
    }

    cads_hal_eth_mac_set_promiscuous(false);

    cads_probe_puts("# ssdpwatch: done, ");
    cads_probe_put_uint(frames_seen);
    cads_probe_puts(" frame(s) seen, ");
    cads_probe_put_uint(messages_seen);
    cads_probe_puts(" SSDP message(s), ");
    cads_probe_put_uint((uint32_t)cads_ssdpwatch_table_count(&table));
    cads_probe_puts(" distinct device/service(s), ");
    cads_probe_put_uint(cads_ssdpwatch_table_dropped_total(&table));
    cads_probe_puts(" dropped (table full)\r\n");

    for(size_t i = 0u; i < cads_ssdpwatch_table_count(&table); i++) {
        const cads_ssdpwatch_record_t* entry = cads_ssdpwatch_table_at(&table, i);
        char mac_text[24];
        cads_fmt_mac(mac_text, sizeof(mac_text), entry->src_mac);
        char ip_text[16];
        cads_fmt_ipv4(ip_text, sizeof(ip_text), entry->src_ip);

        cads_probe_puts("#   [");
        cads_probe_puts(cads_ssdpwatch_kind_text(entry->kind));
        cads_probe_puts("] ");
        cads_probe_puts(mac_text);
        cads_probe_puts(" (");
        cads_probe_puts(ip_text);
        cads_probe_puts(")  usn=\"");
        cads_probe_puts(entry->usn);
        cads_probe_puts("\"  location=\"");
        cads_probe_puts(entry->location);
        cads_probe_puts("\"\r\n");
    }
}

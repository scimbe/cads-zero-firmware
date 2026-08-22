/*
 * CaDS Zero - rogue-DHCP-server detector (board side).
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
 * THE PARSING/DEDUP POLICY ITSELF LIVES IN cads/toolbox/dhcpwatch.h
 * ---------------------------------------------------------------------
 * This file is only the board-specific capture loop and printout - the
 * actual DHCP-server-reply recognition and the dedup table are portable
 * and unit-tested on the host (tests/unit/test_dhcpwatch.c) with
 * hand-built frames, proof the parsing is correct that does not depend
 * on this bench's own traffic (or lack of it, see
 * explorer_sniff_demo.c's own note on this bench's environment).
 *
 * Uses the shared explorer capture buffer
 * (explorer_capture_buffer.h) rather than its own - see that file's own
 * header for why: it is the exact bug this session's own l2discover
 * task hit and fixed, not repeated here.
 */

#include "explorer_dhcpwatch_demo.h"

#include "cads/net/net.h"
#include "cads/toolbox/dhcpwatch.h"
#include "cads/toolbox/fmt.h"
#include "cads_hal.h"
#include "explorer_capture_buffer.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "hal_eth_mac.h"
#include "input_probe.h"

/* A real segment rarely has more than one legitimate DHCP server, and the
 * whole point of this table is noticing a second one - four is plenty of
 * headroom without costing real RAM. */
#define CADS_DHCPWATCH_DEMO_MAX_SERVERS 4u

static const char* cads_dhcpwatch_msg_type_text(uint8_t msg_type) {
    switch(msg_type) {
    case CADS_DHCPWATCH_OFFER: return "OFFER";
    case CADS_DHCPWATCH_ACK: return "ACK  ";
    case CADS_DHCPWATCH_NAK: return "NAK  ";
    default: return "?    ";
    }
}

void cads_explorer_dhcpwatch_demo(uint32_t seconds) {
    if(seconds == 0u) seconds = 20u;

    cads_net_init(cads_explorer_net_mac());

    /* Same reasoning (and the same bug once found and fixed there) as
     * every other explorer_*_demo.c this session: this loop must call
     * cads_net_poll() itself to actually detect the link, not just check
     * its cached status. */
    uint32_t link_wait_start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - link_wait_start < 3000u) {
        cads_net_poll();

        cads_net_status_t status;
        cads_net_status(&status);
        if(status.link_up) break;
        cads_hal_delay_ms(10u);
    }

    static cads_dhcpwatch_record_t storage[CADS_DHCPWATCH_DEMO_MAX_SERVERS];
    cads_dhcpwatch_table_t table;
    cads_dhcpwatch_table_init(&table, storage, CADS_DHCPWATCH_DEMO_MAX_SERVERS);

    cads_probe_puts("# dhcpwatch: watching for DHCPOFFER/ACK/NAK for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    cads_hal_eth_mac_set_promiscuous(true);

    uint8_t* frame = cads_explorer_capture_buffer();
    uint32_t frames_seen = 0u;
    uint32_t replies_seen = 0u;
    uint32_t start = cads_hal_ticks_ms();

    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        uint16_t length = cads_hal_eth_mac_receive(frame, CADS_EXPLORER_CAPTURE_BUFFER_SIZE);
        if(length == 0u) continue;
        frames_seen++;

        cads_dhcpwatch_record_t record;
        if(cads_dhcpwatch_parse(frame, length, &record)) {
            cads_dhcpwatch_table_learn(&table, &record);
            replies_seen++;
        }
    }

    cads_hal_eth_mac_set_promiscuous(false);

    bool rogue = cads_dhcpwatch_table_multiple_servers(&table);

    cads_probe_puts("# dhcpwatch: done, ");
    cads_probe_put_uint(frames_seen);
    cads_probe_puts(" frame(s) seen, ");
    cads_probe_put_uint(replies_seen);
    cads_probe_puts(" DHCP server repl(y/ies), ");
    cads_probe_put_uint((uint32_t)cads_dhcpwatch_table_count(&table));
    cads_probe_puts(" distinct server(s) - ");
    cads_probe_puts(rogue ? "MULTIPLE SERVERS SEEN (possible rogue DHCP)" : "one or zero servers, nothing suspicious");
    cads_probe_puts("\r\n");

    for(size_t i = 0u; i < cads_dhcpwatch_table_count(&table); i++) {
        const cads_dhcpwatch_record_t* entry = cads_dhcpwatch_table_at(&table, i);
        char mac_text[24];
        cads_fmt_mac(mac_text, sizeof(mac_text), entry->src_mac);
        char server_ip_text[16];
        cads_fmt_ipv4(server_ip_text, sizeof(server_ip_text), entry->server_ip);
        char offered_ip_text[16];
        cads_fmt_ipv4(offered_ip_text, sizeof(offered_ip_text), entry->offered_ip);

        cads_probe_puts("#   [");
        cads_probe_puts(cads_dhcpwatch_msg_type_text(entry->msg_type));
        cads_probe_puts("] ");
        cads_probe_puts(mac_text);
        cads_probe_puts("  server=");
        cads_probe_puts(server_ip_text);
        cads_probe_puts("  offered=");
        cads_probe_puts(offered_ip_text);
        cads_probe_puts("\r\n");
    }
}

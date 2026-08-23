/*
 * CaDS Zero - traffic-mix overview (board side).
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
 * WHY `stats` IS A PLAIN LOCAL, NOT `static` LIKE EVERY OTHER
 * WATCHER'S TABLE STORAGE
 * ---------------------------------------------------------------------
 * cads_trafficstats_t is eleven uint32_t fields - 44 bytes, nowhere
 * near large enough to risk this task's own stack (the HTTP status
 * page task's own history, M5's own log entries, is what a real
 * multi-KB local actually costs). Every other M5 watcher's dedup table
 * is `static` because it needs to survive past this function - this
 * one does not, and a `static` here would just be 44 bytes of .bss
 * spent for no reason, on a board where that is not free (see
 * explorer_capture_buffer.h's own header on why RAM here gets budgeted
 * this carefully).
 *
 * THE CLASSIFIER ITSELF LIVES IN cads/toolbox/trafficstats.h
 * ---------------------------------------------------------------------
 * This file is only the board-specific capture loop and printout - the
 * actual frame classification is portable and unit-tested on the host
 * (tests/unit/test_trafficstats.c) with hand-built frames, proof the
 * classification is correct that does not depend on this bench's own
 * traffic (or lack of it, see explorer_sniff_demo.c's own note on this
 * bench's environment).
 *
 * Uses the shared explorer capture buffer (explorer_capture_buffer.h)
 * rather than its own - see that file's own header for why.
 */

#include "explorer_trafficstats_demo.h"

#include "cads/net/net.h"
#include "cads/toolbox/trafficstats.h"
#include "cads_hal.h"
#include "explorer_capture_buffer.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "hal_eth_mac.h"
#include "input_probe.h"

void cads_explorer_trafficstats_demo(uint32_t seconds) {
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

    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);

    cads_probe_puts("# trafficstats: tallying traffic mix for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    cads_hal_eth_mac_set_promiscuous(true);

    uint8_t* frame = cads_explorer_capture_buffer();
    uint32_t start = cads_hal_ticks_ms();

    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        uint16_t length = cads_hal_eth_mac_receive(frame, CADS_EXPLORER_CAPTURE_BUFFER_SIZE);
        if(length == 0u) continue;
        cads_trafficstats_observe(&stats, frame, length);
    }

    cads_hal_eth_mac_set_promiscuous(false);

    cads_probe_puts("# trafficstats: done, ");
    cads_probe_put_uint(stats.total_frames);
    cads_probe_puts(" frame(s), ");
    cads_probe_put_uint(stats.total_bytes);
    cads_probe_puts(" byte(s), ");
    cads_probe_put_uint(stats.runt_frames);
    cads_probe_puts(" runt(s)\r\n");

    cads_probe_puts("#   dest: broadcast=");
    cads_probe_put_uint(stats.broadcast_frames);
    cads_probe_puts(" multicast=");
    cads_probe_put_uint(stats.multicast_frames);
    cads_probe_puts(" unicast=");
    cads_probe_put_uint(stats.unicast_frames);
    cads_probe_puts("\r\n");

    cads_probe_puts("#   ethertype: arp=");
    cads_probe_put_uint(stats.arp_frames);
    cads_probe_puts(" ipv4=");
    cads_probe_put_uint(stats.ipv4_frames);
    cads_probe_puts(" ipv6=");
    cads_probe_put_uint(stats.ipv6_frames);
    cads_probe_puts(" other=");
    cads_probe_put_uint(stats.other_ethertype_frames);
    cads_probe_puts(" vlan-tagged=");
    cads_probe_put_uint(stats.vlan_tagged_frames);
    cads_probe_puts("\r\n");
}

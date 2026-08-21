/*
 * CaDS Zero - MAC address table capture loop (board side).
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
 * WHY THE RECEIVE BUFFER IS THE FULL FRAME SIZE
 * -------------------------------------------------
 * The same "unmeasured silent loss" trap explorer_sniff_demo.c's own
 * header documents applies here too, for a different reason:
 * cads_hal_eth_mac_receive() drops a frame outright (returns 0, no
 * partial delivery) if it is longer than the buffer it is given. Even
 * the smallest real Ethernet frame (an ARP request, padded to the
 * 802.3 minimum) is well past what a "just the header" buffer would
 * hold - so anything smaller than the full frame size would silently
 * bias this table toward whichever sources happen to send only
 * undersized frames, rather than learning every source actually seen.
 *
 * THE LEARNING/AGING POLICY ITSELF LIVES IN cads/toolbox/mactable.h
 * ---------------------------------------------------------------------
 * This file is only the board-specific capture loop and printout; the
 * actual learn/refresh/evict rules are cads_mactable_t, portable and
 * unit-tested on the host (tests/unit/test_mactable.c) with a fake
 * clock - proof the aging policy is correct that does not depend on
 * this bench's own traffic (or lack of it, see explorer_sniff_demo.c's
 * own note on this bench's environment).
 */

#include "explorer_mactable_demo.h"

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/mactable.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "hal_eth_mac.h"
#include "input_probe.h"

#define CADS_MACTABLE_DEMO_MAX_ENTRIES 16u

/* A real switch typically ages a CAM entry out after ~300s of silence -
 * far longer than any one run of this command. Scaled down to 5s so the
 * "with aging" half of this bullet is something a single `M <seconds>`
 * run can actually demonstrate, not just implement; the policy itself is
 * proven correct independent of this exact number by
 * tests/unit/test_mactable.c. */
#define CADS_MACTABLE_DEMO_AGING_MS 5000u

void cads_explorer_mactable_demo(uint32_t seconds) {
    if(seconds == 0u) seconds = 15u;

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

    static cads_mactable_entry_t storage[CADS_MACTABLE_DEMO_MAX_ENTRIES];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, CADS_MACTABLE_DEMO_MAX_ENTRIES, CADS_MACTABLE_DEMO_AGING_MS);

    cads_probe_puts("# mactable: learning for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s (aging=");
    cads_probe_put_uint(CADS_MACTABLE_DEMO_AGING_MS / 1000u);
    cads_probe_puts("s, capacity=");
    cads_probe_put_uint(CADS_MACTABLE_DEMO_MAX_ENTRIES);
    cads_probe_puts(")\r\n");

    cads_hal_eth_mac_set_promiscuous(true);

    static uint8_t frame[1536]; /* full receive size - see this file's own header on why */
    uint32_t frames_seen = 0u;
    uint32_t start = cads_hal_ticks_ms();

    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        uint16_t length = cads_hal_eth_mac_receive(frame, sizeof(frame));
        uint32_t now = cads_hal_ticks_ms();

        /* Shorter than dest+src (12 bytes) is malformed - nothing to
         * learn, but still let the aging sweep below run on schedule. */
        if(length >= 12u) {
            cads_mactable_learn(&table, frame + 6, now);
            frames_seen++;
        }

        /* Run every iteration, not only when a frame arrives, so an
         * entry that has gone idle is reflected the moment it crosses
         * the aging window rather than only the next time some other
         * frame happens to show up - this bench's own near-zero ambient
         * traffic (explorer_sniff_demo.c's established finding) would
         * otherwise leave aging entirely undemonstrated on a quiet run. */
        cads_mactable_age(&table, now);
    }

    cads_hal_eth_mac_set_promiscuous(false);

    cads_probe_puts("# mactable: done, ");
    cads_probe_put_uint(frames_seen);
    cads_probe_puts(" frame(s) seen, ");
    cads_probe_put_uint((uint32_t)cads_mactable_count(&table));
    cads_probe_puts(" address(es) live, ");
    cads_probe_put_uint(cads_mactable_aged_out_total(&table));
    cads_probe_puts(" aged out, ");
    cads_probe_put_uint(cads_mactable_dropped_total(&table));
    cads_probe_puts(" dropped (table full)\r\n");

    uint32_t now = cads_hal_ticks_ms();
    for(size_t i = 0u; i < cads_mactable_count(&table); i++) {
        const cads_mactable_entry_t* entry = cads_mactable_at(&table, i);
        char mac_text[24];
        cads_fmt_mac(mac_text, sizeof(mac_text), entry->mac);
        cads_probe_puts("#   ");
        cads_probe_puts(mac_text);
        cads_probe_puts("  frames=");
        cads_probe_put_uint(entry->frame_count);
        cads_probe_puts("  idle_ms=");
        cads_probe_put_uint(now - entry->last_seen_ms);
        cads_probe_puts("\r\n");
    }
}

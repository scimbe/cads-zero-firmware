/*
 * CaDS Zero - traceroute (ICMP TTL sweep) demo.
 *
 * Thin glue over cads_net_traceroute_probe() (modules/net/src/
 * cads_net_board.c) - everything about the TTL-sweep protocol and
 * matching replies to hops lives there, this file only loops it and
 * prints what came back.
 *
 * On this project's own bench, expect every hop to report "*": ping (the
 * previous roadmap bullet) found that this device has never held a real
 * IP address here, and lwIP's ip4_route() refuses to route an outbound
 * unicast send without one - traceroute sends the exact same kind of
 * packet, just with a different TTL, so it fails the identical way. See
 * docs/ROADMAP.md's ping entry for the full finding; this file does not
 * repeat the diagnosis, only inherits it.
 */

#include "explorer_traceroute_demo.h"

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "input_probe.h"

#define CADS_TRACEROUTE_DEMO_DEFAULT_HOPS 16u
#define CADS_TRACEROUTE_DEMO_MAX_HOPS     30u
#define CADS_TRACEROUTE_DEMO_TIMEOUT_MS   1000u
#define CADS_TRACEROUTE_DEMO_GAP_MS       100u

void cads_explorer_traceroute_demo(uint32_t target, uint32_t max_hops) {
    if(max_hops == 0u) max_hops = CADS_TRACEROUTE_DEMO_DEFAULT_HOPS;
    if(max_hops > CADS_TRACEROUTE_DEMO_MAX_HOPS) max_hops = CADS_TRACEROUTE_DEMO_MAX_HOPS;

    cads_net_init(cads_explorer_net_mac());

    /* Same reasoning (and the same bug once found and fixed there) as
     * explorer_arp_demo.c/explorer_ping_demo.c: this loop must call
     * cads_net_poll() itself. */
    (void)cads_explorer_net_link_wait(3000u);

    char target_text[16];
    cads_fmt_ipv4(target_text, sizeof(target_text), target);
    cads_probe_puts("# traceroute: ");
    cads_probe_puts(target_text);
    cads_probe_puts(", up to ");
    cads_probe_put_uint(max_hops);
    cads_probe_puts(" hops\r\n");

    for(uint32_t ttl = 1u; ttl <= max_hops; ttl++) {
        uint32_t responder = 0u;
        uint32_t rtt = 0u;
        cads_net_traceroute_result_t result =
            cads_net_traceroute_probe(target, (uint8_t)ttl, CADS_TRACEROUTE_DEMO_TIMEOUT_MS, &responder, &rtt);

        cads_probe_puts("#   ");
        cads_probe_put_uint(ttl);
        cads_probe_puts(": ");

        if(result == CadsNetTracerouteNoReply) {
            cads_probe_puts("*\r\n");
        } else {
            char hop_text[16];
            cads_fmt_ipv4(hop_text, sizeof(hop_text), responder);
            cads_probe_puts(hop_text);
            cads_probe_puts(" ");
            cads_probe_put_uint(rtt);
            cads_probe_puts("ms");
            cads_probe_puts(result == CadsNetTracerouteReachedTarget ? " (target)\r\n" : "\r\n");
        }

        if(result == CadsNetTracerouteReachedTarget) {
            cads_probe_puts("# traceroute: reached target\r\n");
            return;
        }
        cads_hal_delay_ms(CADS_TRACEROUTE_DEMO_GAP_MS);
    }

    cads_probe_puts("# traceroute: max hops reached without an answer from the target\r\n");
}

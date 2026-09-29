/*
 * CaDS Zero - ARP scan of a subnet range.
 *
 * One etharp_request()/etharp_find_addr() round trip per candidate host
 * (cads_net_arp_probe(), modules/net/src/cads_net_board.c) - the standard
 * way lwIP's raw API exposes ARP resolution to application code outside
 * the stack itself. Sequential, not parallel: this device has no spare
 * RAM for tracking dozens of requests in flight at once (see
 * gui/canvas.h's and explorer_http_demo.c's own notes on how little slack
 * this firmware's RAM budget actually has), and a sequential sweep is
 * simple enough to reason about with no state machine at all.
 */

#include "explorer_arp_demo.h"

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "input_probe.h"

#define CADS_ARP_DEMO_DEFAULT_COUNT   32u
#define CADS_ARP_DEMO_MAX_COUNT       254u
#define CADS_ARP_DEMO_PROBE_TIMEOUT_MS 150u

void cads_explorer_arp_demo(uint32_t base, uint32_t count) {
    if(count == 0u) count = CADS_ARP_DEMO_DEFAULT_COUNT;
    if(count > CADS_ARP_DEMO_MAX_COUNT) count = CADS_ARP_DEMO_MAX_COUNT;

    cads_net_init(cads_explorer_net_mac());

    /* Give the PHY a moment to autonegotiate - cads_net_arp_probe() itself
     * already refuses to send anything without a link, but "no link yet"
     * and "no link at all" look identical to the caller unless this waits
     * first, the same reasoning explorer_http_demo.c's selftest uses.
     * cads_net_poll() is what actually detects the link transition
     * (cads_net_status() only reports the cached result of the last poll)
     * - a real bug here, caught by cross-checking the MAC's own MMC
     * tx_good counter after a scan and finding it had not moved at all:
     * this loop used to check cads_net_status() without ever calling
     * cads_net_poll(), so link_up could never actually become true and
     * every single probe below was silently returning false before
     * sending anything. */
    (void)cads_explorer_net_link_wait(3000u);

    char first_text[16];
    char last_text[16];
    cads_fmt_ipv4(first_text, sizeof(first_text), (base & 0xFFFFFF00u) | 1u);
    cads_fmt_ipv4(last_text, sizeof(last_text), (base & 0xFFFFFF00u) | count);
    cads_probe_puts("# arp: scanning ");
    cads_probe_puts(first_text);
    cads_probe_puts(" - ");
    cads_probe_puts(last_text);
    cads_probe_puts(", ");
    cads_probe_put_uint(CADS_ARP_DEMO_PROBE_TIMEOUT_MS);
    cads_probe_puts("ms/host\r\n");

    uint32_t found = 0u;
    for(uint32_t host = 1u; host <= count; host++) {
        uint32_t ip = (base & 0xFFFFFF00u) | host;
        uint8_t mac[6];
        if(cads_net_arp_probe(ip, CADS_ARP_DEMO_PROBE_TIMEOUT_MS, mac)) {
            found++;
            char ip_text[16];
            char mac_text[24];
            cads_fmt_ipv4(ip_text, sizeof(ip_text), ip);
            cads_fmt_mac(mac_text, sizeof(mac_text), mac);
            cads_probe_puts("#   ");
            cads_probe_puts(ip_text);
            cads_probe_puts(" -> ");
            cads_probe_puts(mac_text);
            cads_probe_puts("\r\n");
        }
    }

    cads_probe_puts("# arp: done, ");
    cads_probe_put_uint(found);
    cads_probe_puts(" host(s) answered of ");
    cads_probe_put_uint(count);
    cads_probe_puts(" probed\r\n");
}

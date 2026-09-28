/*
 * CaDS Zero - ping (ICMP echo) demo.
 *
 * Thin glue over cads_net_ping() (modules/net/src/cads_net_board.c) -
 * everything about constructing/matching echo requests lives there, this
 * file only loops it `count` times and prints what came back.
 */

#include "explorer_ping_demo.h"

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "input_probe.h"

#define CADS_PING_DEMO_DEFAULT_COUNT 4u
#define CADS_PING_DEMO_TIMEOUT_MS    1000u
#define CADS_PING_DEMO_GAP_MS        200u

void cads_explorer_ping_demo(uint32_t target, uint32_t count) {
    if(count == 0u) count = CADS_PING_DEMO_DEFAULT_COUNT;

    cads_net_init(cads_explorer_net_mac());

    /* Give the PHY a moment to autonegotiate - the same reasoning (and the
     * same bug once found and fixed there) as explorer_arp_demo.c: this
     * loop must call cads_net_poll() itself, since cads_net_status() only
     * reports the last poll's cached result and does not detect anything
     * on its own. */
    (void)cads_explorer_net_link_wait(3000u);

    char target_text[16];
    cads_fmt_ipv4(target_text, sizeof(target_text), target);
    cads_probe_puts("# ping: ");
    cads_probe_puts(target_text);
    cads_probe_puts("\r\n");

    uint32_t replies = 0u;
    uint32_t rtt_sum = 0u;

    for(uint32_t i = 0; i < count; i++) {
        uint32_t rtt = 0u;
        if(cads_net_ping(target, CADS_PING_DEMO_TIMEOUT_MS, &rtt)) {
            replies++;
            rtt_sum += rtt;
            cads_probe_puts("#   reply from ");
            cads_probe_puts(target_text);
            cads_probe_puts(": time=");
            cads_probe_put_uint(rtt);
            cads_probe_puts("ms\r\n");
        } else {
            cads_probe_puts("#   request timed out\r\n");
        }
        cads_hal_delay_ms(CADS_PING_DEMO_GAP_MS);
    }

    cads_probe_puts("# ping: ");
    cads_probe_put_uint(replies);
    cads_probe_puts("/");
    cads_probe_put_uint(count);
    cads_probe_puts(" replies");
    if(replies > 0u) {
        cads_probe_puts(", avg ");
        cads_probe_put_uint(rtt_sum / replies);
        cads_probe_puts("ms");
    }
    cads_probe_puts("\r\n");
}

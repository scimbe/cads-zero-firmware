/*
 * CaDS Zero - HTTP status page (host side).
 *
 * No network stack in the simulator (see modules/net/src/cads_net_sim.c's
 * own file header) - nothing here to serve a page over.
 */

#include "explorer_http_demo.h"

#include "input_probe.h"

void cads_explorer_http_demo(uint32_t seconds) {
    (void)seconds;
    cads_probe_puts("# http: not available in the simulator\r\n");
}

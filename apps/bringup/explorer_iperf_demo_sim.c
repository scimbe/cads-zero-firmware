/*
 * CaDS Zero - iperf throughput test (host side).
 *
 * No network stack in the simulator (modules/net/src/cads_net_sim.c's own
 * file header) - nothing here to measure throughput over.
 */

#include "explorer_iperf_demo.h"

#include "input_probe.h"

void cads_explorer_iperf_demo(uint32_t seconds) {
    (void)seconds;
    cads_probe_puts("# iperf: not available in the simulator\r\n");
}

void cads_explorer_iperf_client_demo(uint32_t target, uint32_t seconds) {
    (void)target;
    (void)seconds;
    cads_probe_puts("# iperf: not available in the simulator\r\n");
}

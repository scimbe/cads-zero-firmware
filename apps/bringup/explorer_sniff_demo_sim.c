/*
 * CaDS Zero - promiscuous packet sniffer (host side).
 *
 * Promiscuous mode and the DMA descriptor ring are both real hardware
 * (modules/net/src/cads_net_sim.c's own file header: no network stack in
 * the simulator at all) - nothing here to capture.
 */

#include "explorer_sniff_demo.h"

#include "input_probe.h"

void cads_explorer_sniff_demo(uint32_t seconds) {
    (void)seconds;
    cads_probe_puts("# sniff: not available in the simulator\r\n");
}

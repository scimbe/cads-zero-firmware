/*
 * CaDS Zero - traffic-mix overview (host side).
 *
 * Promiscuous mode and the DMA descriptor ring are both real hardware
 * (modules/net/src/cads_net_sim.c's own file header: no network stack in
 * the simulator at all) - nothing here to capture. The classifier this
 * command drives (cads/toolbox/trafficstats.h) has its own host unit
 * tests (tests/unit/test_trafficstats.c) that do not depend on this
 * command at all.
 */

#include "explorer_trafficstats_demo.h"

#include "input_probe.h"

void cads_explorer_trafficstats_demo(uint32_t seconds) {
    (void)seconds;
    cads_probe_puts("# trafficstats: not available in the simulator\r\n");
}

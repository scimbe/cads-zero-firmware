/*
 * CaDS Zero - ARP spoofing detector (host side).
 *
 * Promiscuous mode and the DMA descriptor ring are both real hardware
 * (modules/net/src/cads_net_sim.c's own file header: no network stack in
 * the simulator at all) - nothing here to capture. The parser and
 * binding table this command drives (cads/toolbox/arpwatch.h) have
 * their own host unit tests (tests/unit/test_arpwatch.c) that do not
 * depend on this command at all.
 */

#include "explorer_arpwatch_demo.h"

#include "input_probe.h"

void cads_explorer_arpwatch_demo(uint32_t seconds) {
    (void)seconds;
    cads_probe_puts("# arpwatch: not available in the simulator\r\n");
}

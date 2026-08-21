/*
 * CaDS Zero - MAC address table (host side).
 *
 * Promiscuous mode and the DMA descriptor ring are both real hardware
 * (modules/net/src/cads_net_sim.c's own file header: no network stack in
 * the simulator at all) - nothing here to learn from. The learning/aging
 * policy this command drives (cads/toolbox/mactable.h) has its own host
 * unit tests (tests/unit/test_mactable.c) that do not depend on this
 * command at all.
 */

#include "explorer_mactable_demo.h"

#include "input_probe.h"

void cads_explorer_mactable_demo(uint32_t seconds) {
    (void)seconds;
    cads_probe_puts("# mactable: not available in the simulator\r\n");
}

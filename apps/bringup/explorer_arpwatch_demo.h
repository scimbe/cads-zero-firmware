#ifndef CADS_EXPLORER_ARPWATCH_DEMO_H
#define CADS_EXPLORER_ARPWATCH_DEMO_H

#include <stdint.h>

/**
 * Passive ARP spoofing / cache-poisoning detector: `seconds` (default
 * 20) of promiscuous capture, tracking IP->MAC bindings from observed
 * ARP requests/replies (cads/toolbox/arpwatch.h owns the actual parsing
 * and binding policy, unit-tested on the host in
 * tests/unit/test_arpwatch.c) and flagging any binding that changes MAC
 * mid-run. This file is only the board-specific capture loop and
 * printout - the same split explorer_l2discover_demo.c/
 * cads/toolbox/l2discover.h already established.
 *
 * See explorer_sniff_demo.c's file header for why this does not call
 * cads_net_poll() during the capture window (same reasoning, same
 * exclusive-access argument, reused rather than re-derived here).
 *
 * Board only - promiscuous mode and the DMA descriptor ring are both real
 * hardware. explorer_arpwatch_demo_sim.c explains why the simulator
 * does not need its own version.
 */
void cads_explorer_arpwatch_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_ARPWATCH_DEMO_H */

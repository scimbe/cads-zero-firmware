#ifndef CADS_EXPLORER_L2DISCOVER_DEMO_H
#define CADS_EXPLORER_L2DISCOVER_DEMO_H

#include <stdint.h>

/**
 * Passive L2 neighbor discovery: `seconds` (default 20) of promiscuous
 * capture, decoding CDP/LLDP/STP frames (cads/toolbox/l2discover.h owns
 * the actual parsing and dedup policy, unit-tested on the host in
 * tests/unit/test_l2discover.c) and noting any distinct 802.1Q VLAN IDs
 * seen tagged on the wire. This file is only the board-specific capture
 * loop and printout - the same split explorer_mactable_demo.c/
 * cads/toolbox/mactable.h already established.
 *
 * See explorer_sniff_demo.c's file header for why this does not call
 * cads_net_poll() during the capture window (same reasoning, same
 * exclusive-access argument, reused rather than re-derived here).
 *
 * Board only - promiscuous mode and the DMA descriptor ring are both real
 * hardware. explorer_l2discover_demo_sim.c explains why the simulator
 * does not need its own version.
 */
void cads_explorer_l2discover_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_L2DISCOVER_DEMO_H */

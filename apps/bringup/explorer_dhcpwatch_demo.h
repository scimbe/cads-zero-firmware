#ifndef CADS_EXPLORER_DHCPWATCH_DEMO_H
#define CADS_EXPLORER_DHCPWATCH_DEMO_H

#include <stdint.h>

/**
 * Passive rogue-DHCP-server detector: `seconds` (default 20) of
 * promiscuous capture, watching for DHCPOFFER/DHCPACK/DHCPNAK traffic
 * (cads/toolbox/dhcpwatch.h owns the actual parsing and dedup policy,
 * unit-tested on the host in tests/unit/test_dhcpwatch.c) and flagging
 * if more than one distinct source answered as a server. This file is
 * only the board-specific capture loop and printout - the same split
 * explorer_l2discover_demo.c/cads/toolbox/l2discover.h already
 * established.
 *
 * See explorer_sniff_demo.c's file header for why this does not call
 * cads_net_poll() during the capture window (same reasoning, same
 * exclusive-access argument, reused rather than re-derived here).
 *
 * Board only - promiscuous mode and the DMA descriptor ring are both real
 * hardware. explorer_dhcpwatch_demo_sim.c explains why the simulator
 * does not need its own version.
 */
void cads_explorer_dhcpwatch_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_DHCPWATCH_DEMO_H */

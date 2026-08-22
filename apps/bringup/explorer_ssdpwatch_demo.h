#ifndef CADS_EXPLORER_SSDPWATCH_DEMO_H
#define CADS_EXPLORER_SSDPWATCH_DEMO_H

#include <stdint.h>

/**
 * Passive SSDP/UPnP device discovery: `seconds` (default 20) of
 * promiscuous capture, watching UDP port 1900 for NOTIFY (ssdp:alive/
 * ssdp:byebye) and M-SEARCH response traffic
 * (cads/toolbox/ssdpwatch.h owns the actual parsing and dedup policy,
 * unit-tested on the host in tests/unit/test_ssdpwatch.c) and listing
 * every distinct (device, service) pair seen, with its LOCATION URL
 * where available. This file is only the board-specific capture loop
 * and printout - the same split explorer_l2discover_demo.c/
 * cads/toolbox/l2discover.h already established.
 *
 * See explorer_sniff_demo.c's file header for why this does not call
 * cads_net_poll() during the capture window (same reasoning, same
 * exclusive-access argument, reused rather than re-derived here).
 *
 * Board only - promiscuous mode and the DMA descriptor ring are both real
 * hardware. explorer_ssdpwatch_demo_sim.c explains why the simulator
 * does not need its own version.
 */
void cads_explorer_ssdpwatch_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_SSDPWATCH_DEMO_H */

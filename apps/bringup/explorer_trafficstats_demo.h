#ifndef CADS_EXPLORER_TRAFFICSTATS_DEMO_H
#define CADS_EXPLORER_TRAFFICSTATS_DEMO_H

#include <stdint.h>

/**
 * Passive traffic-mix overview: `seconds` (default 20) of promiscuous
 * capture, tallying frame counts by destination class (broadcast/
 * multicast/unicast), 802.1Q tagging, and EtherType (ARP/IPv4/IPv6/
 * other) (cads/toolbox/trafficstats.h owns the actual classifier,
 * unit-tested on the host in tests/unit/test_trafficstats.c). Unlike
 * every other M5 watcher this session, keeps no per-source table at
 * all - the question this answers ("how much of what kind") only ever
 * needed counters. This file is only the board-specific capture loop
 * and printout - the same split explorer_l2discover_demo.c/
 * cads/toolbox/l2discover.h already established.
 *
 * See explorer_sniff_demo.c's file header for why this does not call
 * cads_net_poll() during the capture window (same reasoning, same
 * exclusive-access argument, reused rather than re-derived here).
 *
 * Board only - promiscuous mode and the DMA descriptor ring are both real
 * hardware. explorer_trafficstats_demo_sim.c explains why the
 * simulator does not need its own version.
 */
void cads_explorer_trafficstats_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_TRAFFICSTATS_DEMO_H */

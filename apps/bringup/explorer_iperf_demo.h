#ifndef CADS_EXPLORER_IPERF_DEMO_H
#define CADS_EXPLORER_IPERF_DEMO_H

#include <stdint.h>

/**
 * Run an iperf2-compatible TCP throughput server (lwIP's own lwiperf, port
 * 5001, the classic iperf2 default) for `seconds` (default 30). Server
 * mode only: this device answers an external `iperf -c <this device's IP>`
 * run rather than initiating one - the client role would need this device
 * to have a route out, which docs/ROADMAP.md's ping entry found this bench
 * does not provide (no DHCP server, no IP address, ever). Server mode
 * needs no outbound route and is unaffected.
 *
 * Board only - lwiperf needs lwIP. explorer_iperf_demo_sim.c explains why
 * the simulator does not need its own throughput test.
 */
void cads_explorer_iperf_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_IPERF_DEMO_H */

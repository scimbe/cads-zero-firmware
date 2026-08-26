#ifndef CADS_EXPLORER_IPERF_DEMO_H
#define CADS_EXPLORER_IPERF_DEMO_H

#include <stdint.h>

/**
 * Run an iperf2-compatible TCP throughput server (lwIP's own lwiperf, port
 * 5001, the classic iperf2 default) for `seconds` (default 30). This
 * device answers an external `iperf -c <this device's IP>` run rather than
 * initiating one.
 *
 * Board only - lwiperf needs lwIP. explorer_iperf_demo_sim.c explains why
 * the simulator does not need its own throughput test.
 */
void cads_explorer_iperf_demo(uint32_t seconds);

/**
 * Run an iperf2-compatible TCP throughput CLIENT against `target` (host
 * byte order), for up to `seconds` (default 30, aborted early if the
 * session finishes first). The counterpart to cads_explorer_iperf_demo():
 * this device connects out to `iperf -s` running elsewhere, needing
 * cads/net's own address to be usable first (see apps/netiperf's own
 * default, cads/net/net.h's cads_net_config_t - this bench's static
 * 192.168.33.99/24 makes an outbound route possible, unlike when this
 * comment was last true and the board had no address at all).
 *
 * Board only, same reasoning as cads_explorer_iperf_demo().
 */
void cads_explorer_iperf_client_demo(uint32_t target, uint32_t seconds);

#endif /* CADS_EXPLORER_IPERF_DEMO_H */

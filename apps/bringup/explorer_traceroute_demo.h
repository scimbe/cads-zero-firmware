#ifndef CADS_EXPLORER_TRACEROUTE_DEMO_H
#define CADS_EXPLORER_TRACEROUTE_DEMO_H

#include <stdint.h>

/**
 * Traceroute `target` (hex-encoded IPv4, host byte order, e.g. 0xC0A80101
 * for 192.168.1.1): one ICMP echo request per TTL, starting at 1, printing
 * whichever hop answers (or "*" for a TTL nothing answered within its
 * timeout) until the target's own echo reply arrives or `max_hops`
 * (default 16, capped at 30) is reached.
 *
 * Fully portable, no board/sim split needed: cads_net_traceroute_probe()
 * already has an honest answer on both targets (see cads/net/net.h).
 */
void cads_explorer_traceroute_demo(uint32_t target, uint32_t max_hops);

#endif /* CADS_EXPLORER_TRACEROUTE_DEMO_H */

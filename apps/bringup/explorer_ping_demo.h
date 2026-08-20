#ifndef CADS_EXPLORER_PING_DEMO_H
#define CADS_EXPLORER_PING_DEMO_H

#include <stdint.h>

/**
 * Ping `target` (hex-encoded IPv4, host byte order, e.g. 0xC0A80101 for
 * 192.168.1.1) `count` times (default 4, matching the conventional ping
 * default), one echo request/reply per second-ish, reporting round-trip
 * time or a timeout for each.
 *
 * Fully portable, no board/sim split needed: cads_net_ping() already has
 * an honest answer on both targets (see cads/net/net.h).
 */
void cads_explorer_ping_demo(uint32_t target, uint32_t count);

#endif /* CADS_EXPLORER_PING_DEMO_H */

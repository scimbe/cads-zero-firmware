#ifndef CADS_EXPLORER_ARP_DEMO_H
#define CADS_EXPLORER_ARP_DEMO_H

#include <stdint.h>

/**
 * ARP-scan a range of a subnet: `base` is a hex-encoded IPv4 address (host
 * byte order, e.g. 0xC0A80100 for 192.168.1.0) whose last octet is
 * replaced with 1..`count` in turn. `count` defaults to 32 when 0 - a
 * quick sweep rather than a full /24's worth of ~150ms-per-host waits by
 * default; pass an explicit count (up to 254) for a fuller scan.
 *
 * Fully portable, no board/sim split needed: cads_net_arp_probe() already
 * has an honest answer on both targets (see cads/net/net.h) - the
 * simulator's is simply "never resolves anything", so this reports zero
 * hosts found there rather than needing its own stub.
 */
void cads_explorer_arp_demo(uint32_t base, uint32_t count);

#endif /* CADS_EXPLORER_ARP_DEMO_H */

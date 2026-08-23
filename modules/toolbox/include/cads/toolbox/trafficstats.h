/*
 * CaDS Zero toolbox - passive traffic-mix statistics.
 *
 * A quick "what's actually on this wire" answer - how much is
 * broadcast noise vs real unicast traffic, what protocol mix (ARP/
 * IPv4/IPv6/other), whether any 802.1Q tagging is present at all -
 * without keeping a single per-source entry. Where l2discover.h/
 * dhcpwatch.h/arpwatch.h/ssdpwatch.h each answer "who is out there",
 * this answers "how much of what kind", and answering that needs only
 * counters, not a table: the RAM cost of five recon tools sharing one
 * capture buffer (explorer_capture_buffer.h) is already real (see
 * docs/ROADMAP.md's own M5 log entries), and a sixth tool that adds a
 * sixth dedup table would keep spending against the same 48K floor for
 * a question that was never about individual senders in the first
 * place.
 *
 * This file is only the classifier: a pure function over caller-
 * supplied bytes plus a caller-owned counters struct, no HAL, no lwIP,
 * unit-tested on the host with hand-built frames
 * (tests/unit/test_trafficstats.c). The board-specific capture loop
 * and printout live in apps/bringup/explorer_trafficstats_demo.c, the
 * same split every other M5 watcher this session already established.
 *
 * EVERY OFFSET IS BOUNDS-CHECKED AGAINST `length` BEFORE IT IS READ
 * ---------------------------------------------------------------------
 * Same discipline as l2discover.c/dhcpwatch.c/arpwatch.c/ssdpwatch.c,
 * for the same reason: these bytes come off the wire, from whatever
 * sent them. A frame shorter than a full Ethernet header is counted as
 * a runt, not classified further and never read past its own length.
 */

#ifndef CADS_TOOLBOX_TRAFFICSTATS_H
#define CADS_TOOLBOX_TRAFFICSTATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t total_frames;
    uint32_t total_bytes; /**< sum of every `length` passed to cads_trafficstats_observe(), including runts */
    uint32_t runt_frames; /**< shorter than a full 14-byte Ethernet header - not classified further */

    uint32_t broadcast_frames; /**< destination FF:FF:FF:FF:FF:FF */
    uint32_t multicast_frames; /**< I/G bit set, not all-FF */
    uint32_t unicast_frames;

    uint32_t vlan_tagged_frames; /**< carries an 802.1Q tag (EtherType 0x8100) */

    uint32_t arp_frames;             /**< EtherType 0x0806 (after any VLAN tag) */
    uint32_t ipv4_frames;            /**< EtherType 0x0800 */
    uint32_t ipv6_frames;            /**< EtherType 0x86DD */
    uint32_t other_ethertype_frames; /**< anything else, tallied rather than silently dropped */
} cads_trafficstats_t;

/** Zeroes every counter. `stats` must not be NULL. */
void cads_trafficstats_init(cads_trafficstats_t* stats);

/**
 * Classify one raw Ethernet frame and update `stats` in place. Safe to
 * call with any `length`, including 0 - a frame too short to have a
 * destination MAC and EtherType is still counted (total_frames,
 * total_bytes, runt_frames) but not classified further. A no-op if
 * either argument is NULL.
 */
void cads_trafficstats_observe(cads_trafficstats_t* stats, const uint8_t* frame, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_TRAFFICSTATS_H */

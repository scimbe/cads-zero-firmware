/*
 * CaDS Zero toolbox - passive ARP spoofing / cache-poisoning detector.
 *
 * ARP has no authentication at all: any host can claim to own any IP
 * address just by sending a REQUEST or REPLY naming itself as the
 * sender, and every other host's cache believes it. ARP spoofing
 * (arpspoof/ettercap-style MITM, or a rogue DHCP-adjacent attack) works
 * by repeatedly re-claiming a victim IP (usually the gateway) with the
 * attacker's own MAC. The tell is structural, not behavioural: an IP
 * address that was already bound to one MAC suddenly answering from a
 * different one. A regular laptop *can* run arpwatch(8) or
 * `arp -a` by hand, but nobody leaves one doing that unattended in a
 * wall jack for hours - the same case this session's own
 * l2discover.h/dhcpwatch.h already made for their own protocols.
 *
 * This file is only the parser and the binding table: pure functions
 * over caller-supplied bytes, no HAL, no lwIP, unit-tested on the host
 * with hand-built frames (tests/unit/test_arpwatch.c). The board-
 * specific capture loop and printout live in
 * apps/bringup/explorer_arpwatch_demo.c, the same split
 * cads/toolbox/l2discover.h/explorer_l2discover_demo.c already
 * established.
 *
 * EVERY OFFSET IS BOUNDS-CHECKED AGAINST `length` BEFORE IT IS READ
 * ---------------------------------------------------------------------
 * Same discipline as l2discover.c/dhcpwatch.c, for the same reason:
 * these bytes come off the wire, from whatever sent them.
 *
 * WHY BOTH OPCODES (REQUEST AND REPLY)
 * -----------------------------------------
 * An ARP REQUEST's sender fields are just as meaningful a claim as a
 * REPLY's - "IP X is at MAC Y" - and a gratuitous ARP announcement
 * (RFC 5227, sender IP == target IP) is normally sent as a REQUEST, not
 * a REPLY. Real ARP caches learn from both; watching only REPLYs would
 * miss exactly the announcement style some spoofing tools use.
 *
 * WHAT THIS DOES NOT CLAIM
 * -----------------------------
 * A changed IP-to-MAC binding is a strong *indicator*, not proof: DHCP
 * churn, a NIC replaced after a hardware fault, or a router's own
 * failover pair legitimately change which MAC answers for an IP too.
 * This file counts and reports changes; it does not decide intent -
 * the same honest framing real arpwatch(8) deployments already use.
 */

#ifndef CADS_TOOLBOX_ARPWATCH_H
#define CADS_TOOLBOX_ARPWATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CADS_ARPWATCH_REQUEST 1u
#define CADS_ARPWATCH_REPLY 2u

/** One "IP is at MAC" claim read off an ARP request or reply. */
typedef struct {
    uint32_t sender_ip;
    uint8_t sender_mac[6];
    uint8_t opcode; /**< CADS_ARPWATCH_REQUEST or _REPLY */
} cads_arpwatch_claim_t;

/**
 * Examine one raw Ethernet frame. If it is an Ethernet/IPv4 ARP request
 * or reply (EtherType 0x0806, hardware type 1, protocol type 0x0800,
 * hardware/protocol address lengths 6/4), fills `out` and returns true.
 * Anything else returns false with `out` untouched.
 */
bool cads_arpwatch_parse(const uint8_t* frame, uint16_t length, cads_arpwatch_claim_t* out);

/* --- binding table: one live entry per distinct sender IP seen --- */

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint32_t sightings;   /**< total times this IP has been (re)learned */
    uint32_t mac_changes; /**< times the bound MAC changed for this IP */
} cads_arpwatch_entry_t;

typedef struct {
    /* --- private --- */
    cads_arpwatch_entry_t* entries;
    size_t capacity;
    size_t count;
    uint32_t dropped_total;
} cads_arpwatch_table_t;

/** `storage` holds up to `capacity` entries and must outlive the table. */
void cads_arpwatch_table_init(cads_arpwatch_table_t* table, cads_arpwatch_entry_t* storage, size_t capacity);

/**
 * Record one "ip is at mac" claim. A first sighting of `ip` claims a
 * free slot (sightings=1, mac_changes=0), never itself a "change" - there
 * is nothing to have changed from. A later sighting of an already-known
 * `ip` with the SAME `mac` just increments sightings. A later sighting
 * with a DIFFERENT `mac` updates the binding, increments both counters,
 * and returns true - the one signal this whole file exists to raise.
 * A full table refuses a genuinely new IP, counted via
 * cads_arpwatch_table_dropped_total(), same reasoning as
 * cads_l2discover_table_learn()/cads_dhcpwatch_table_learn().
 */
bool cads_arpwatch_table_learn(cads_arpwatch_table_t* table, uint32_t ip, const uint8_t mac[6]);

size_t cads_arpwatch_table_count(const cads_arpwatch_table_t* table);

/** The `index`-th live entry, 0 <= index < cads_arpwatch_table_count(). NULL out of range. */
const cads_arpwatch_entry_t* cads_arpwatch_table_at(const cads_arpwatch_table_t* table, size_t index);

uint32_t cads_arpwatch_table_dropped_total(const cads_arpwatch_table_t* table);

/** True if any tracked IP has ever changed its bound MAC - the
 *  aggregate "something here looked like spoofing" signal. */
bool cads_arpwatch_table_any_flip(const cads_arpwatch_table_t* table);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_ARPWATCH_H */

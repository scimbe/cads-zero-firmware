/*
 * CaDS Zero toolbox - passive rogue-DHCP-server detector.
 *
 * A rogue or misconfigured DHCP server on a segment (accidental - a second
 * router plugged in with DHCP still enabled - or deliberate - a DHCP
 * starvation/MITM setup) hands out leases that silently point clients at
 * the wrong gateway or DNS. The tell is structural, not behavioural: more
 * than one distinct source answering DHCPDISCOVER with DHCPOFFER/DHCPACK
 * on the same physical segment. A regular laptop *can* watch for this with
 * Wireshark open and a human reading it, but nobody leaves a laptop
 * running that for hours unattended in a wall jack the way a small,
 * inconspicuous, always-on probe can - the same case this session's own
 * l2discover.h already made for CDP/LLDP/STP.
 *
 * This file is only the parser and the dedup table: pure functions over
 * caller-supplied bytes, no HAL, no lwIP, unit-tested on the host with
 * hand-built frames (tests/unit/test_dhcpwatch.c). The board-specific
 * capture loop and printout live in
 * apps/bringup/explorer_dhcpwatch_demo.c, the same split
 * cads/toolbox/l2discover.h/explorer_l2discover_demo.c already
 * established.
 *
 * EVERY OFFSET IS BOUNDS-CHECKED AGAINST `length` BEFORE IT IS READ
 * ---------------------------------------------------------------------
 * Same discipline as l2discover.c, for the same reason: these bytes come
 * off the wire, from whatever sent them, not from this firmware.
 *
 * WHY DHCPOFFER/DHCPACK, NOT DHCPDISCOVER/DHCPREQUEST
 * ---------------------------------------------------------------------
 * Only a server (real or rogue) ever sends OFFER or ACK - a client sends
 * DISCOVER/REQUEST/DECLINE/RELEASE/INFORM. Recognising only the two
 * server-originated message types is what makes "how many distinct
 * senders of this kind exist" the same question as "how many DHCP
 * servers exist", without needing to separately track which MACs are
 * clients to exclude them.
 */

#ifndef CADS_TOOLBOX_DHCPWATCH_H
#define CADS_TOOLBOX_DHCPWATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CADS_DHCPWATCH_OFFER 2u
#define CADS_DHCPWATCH_ACK 5u
#define CADS_DHCPWATCH_NAK 6u

/** One DHCP server reply observed on the wire. */
typedef struct {
    uint8_t src_mac[6];  /**< Ethernet source - the server (or its relay) */
    uint32_t server_ip;  /**< option 54 (Server Identifier) if present, else the IP header's source */
    uint32_t offered_ip; /**< yiaddr - the address being offered/acknowledged */
    uint8_t msg_type;    /**< CADS_DHCPWATCH_OFFER / _ACK / _NAK */
} cads_dhcpwatch_record_t;

/**
 * Examine one raw Ethernet frame. If it is IPv4/UDP, source port 67
 * (BOOTPS) and destination port 68 (BOOTPC) - a DHCP server speaking to a
 * client - and its DHCP message type option is OFFER, ACK, or NAK, fills
 * `out` and returns true. Anything else (client-originated DHCP traffic,
 * non-DHCP UDP, non-UDP, non-IPv4) returns false with `out` untouched.
 */
bool cads_dhcpwatch_parse(const uint8_t* frame, uint16_t length, cads_dhcpwatch_record_t* out);

/* --- dedup table: one live entry per distinct src_mac seen --- */

typedef struct {
    /* --- private --- */
    cads_dhcpwatch_record_t* entries;
    size_t capacity;
    size_t count;
    uint32_t dropped_total;
} cads_dhcpwatch_table_t;

/** `storage` holds up to `capacity` entries and must outlive the table. */
void cads_dhcpwatch_table_init(cads_dhcpwatch_table_t* table, cads_dhcpwatch_record_t* storage, size_t capacity);

/**
 * Record one sighting. An existing entry for the same src_mac is
 * refreshed in place (a real server's own offered/acknowledged address
 * legitimately varies sighting to sighting); a new MAC claims a free
 * slot. A full table refuses a genuinely new MAC rather than evicting a
 * still-live one, counted via cads_dhcpwatch_table_dropped_total() -
 * same reasoning as cads_l2discover_table_learn().
 */
void cads_dhcpwatch_table_learn(cads_dhcpwatch_table_t* table, const cads_dhcpwatch_record_t* record);

size_t cads_dhcpwatch_table_count(const cads_dhcpwatch_table_t* table);

/** The `index`-th live entry, 0 <= index < cads_dhcpwatch_table_count(). NULL out of range. */
const cads_dhcpwatch_record_t* cads_dhcpwatch_table_at(const cads_dhcpwatch_table_t* table, size_t index);

uint32_t cads_dhcpwatch_table_dropped_total(const cads_dhcpwatch_table_t* table);

/** More than one distinct source has answered as a DHCP server - the
 *  actual rogue-DHCP signal this whole file exists to raise. */
bool cads_dhcpwatch_table_multiple_servers(const cads_dhcpwatch_table_t* table);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_DHCPWATCH_H */

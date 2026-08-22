/*
 * CaDS Zero toolbox - passive Layer 2 neighbor discovery.
 *
 * A switch, router, or AP on the segment routinely announces itself over
 * three link-local protocols without being asked: CDP (Cisco Discovery
 * Protocol), LLDP (802.1AB, vendor-neutral), and STP/RSTP Configuration
 * BPDUs (802.1D/w, which name the root bridge and the sender's own bridge
 * ID). None of the three needs an IP address, a DHCP lease, or a
 * connection to be sent - just a NIC in promiscuous mode on the same
 * physical segment, which is exactly what explorer_sniff_demo.c/
 * explorer_mactable_demo.c's capture loop already sets up. A regular
 * laptop *can* run tcpdump and decode these by hand, but a small,
 * inconspicuous, always-on probe left plugged into a wall jack collects
 * this recon passively and continuously in a way nobody sits and does on
 * a laptop - the actual point of building it onto this board rather than
 * reaching for a normal computer.
 *
 * This file is only the parser and the dedup table: pure functions over
 * caller-supplied bytes, no HAL, no lwIP, unit-tested on the host with
 * hand-built frames (tests/unit/test_l2discover.c). The board-specific
 * capture loop and printout live in apps/bringup/explorer_l2discover_demo.c,
 * the same split explorer_mactable_demo.c/cads/toolbox/mactable.h already
 * established.
 *
 * EVERY OFFSET IS BOUNDS-CHECKED AGAINST `length` BEFORE IT IS READ
 * ---------------------------------------------------------------------
 * These bytes come straight off the wire, from whatever sent them, not
 * from this firmware - a short, truncated, or deliberately malformed
 * frame must be rejected (return false / stop the TLV walk), never read
 * past its own end.
 *
 * WHAT IS DELIBERATELY NOT DECODED
 * -----------------------------------
 * LLDP's other two, rarer multicast destinations (802.1AB permits
 * 01:80:C2:00:00:03 and 01:80:C2:00:00:00 in addition to the "nearest
 * bridge" address this file checks) are not recognised - the nearest-
 * bridge address is what essentially every real switch/AP uses. STP
 * Topology Change Notification BPDUs (type 0x80) carry no root/bridge
 * field at all and MSTP's type 0x03 payload is a different, longer
 * layout past the fields this file reads - both are left unrecognised
 * rather than guessed at. Recognising fewer frame shapes correctly beats
 * guessing at more of them.
 */

#ifndef CADS_TOOLBOX_L2DISCOVER_H
#define CADS_TOOLBOX_L2DISCOVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest displayable name/port string kept per neighbor, NUL included.
 *  Deliberately short - this record type gets multiplied by the demo's
 *  table capacity (apps/bringup/explorer_l2discover_demo.c), and RAM is
 *  the scarce resource here, not display width; a longer name/port is
 *  simply sanitized-and-truncated by cads_l2_copy_text() rather than
 *  refused. */
#define CADS_L2DISCOVER_TEXT_MAX 16u

typedef enum {
    CADS_L2_NONE = 0,
    CADS_L2_CDP,
    CADS_L2_LLDP,
    CADS_L2_STP,
} cads_l2discover_kind_t;

/**
 * One discovered neighbor. `name`/`port` are always NUL-terminated and
 * hold only printable ASCII (0x20-0x7E) - any other byte from the wire is
 * replaced with '.' before it ever reaches a caller, since these strings
 * are meant to land on a serial terminal. Fields the frame kind does not
 * carry are left zeroed, not garbage: a CDP/LLDP record's stp_* fields
 * are all-zero, an STP record's name/port are empty strings.
 */
typedef struct {
    cads_l2discover_kind_t kind;
    uint8_t src_mac[6];
    char name[CADS_L2DISCOVER_TEXT_MAX]; /**< CDP device ID / LLDP system name (falls back to chassis ID) */
    char port[CADS_L2DISCOVER_TEXT_MAX]; /**< CDP/LLDP port ID */
    uint16_t stp_root_priority;
    uint8_t stp_root_mac[6];
    uint16_t stp_bridge_priority;
    uint8_t stp_bridge_mac[6];
} cads_l2discover_record_t;

/**
 * Examine one raw Ethernet frame, destination MAC first, exactly as
 * captured off the wire. If it is recognised as CDP, LLDP, or an STP/RSTP
 * Configuration BPDU, fills `out` and returns true. Any other frame
 * (the overwhelming majority of ordinary traffic) returns false with
 * `out` untouched - a caller should not clear it first if it wants to
 * tell "not recognised" apart from "recognised, all fields empty".
 */
bool cads_l2discover_parse(const uint8_t* frame, uint16_t length, cads_l2discover_record_t* out);

/**
 * True if `frame` carries an 802.1Q VLAN tag (EtherType 0x8100 at the
 * normal, untagged offset). `vlan_id` (0-4094; 4095/0xFFF is reserved by
 * the standard and never returned) is written on success. A doubly
 * tagged (Q-in-Q, 0x88A8 outer) frame reports only the outer tag - this
 * is a passive recon signal ("a tag exists on this wire"), not a full
 * decode of nested tagging.
 */
bool cads_l2discover_vlan_tag(const uint8_t* frame, uint16_t length, uint16_t* vlan_id);

/* --- dedup table: one live entry per (kind, src_mac) pair seen --- */

typedef struct {
    /* --- private --- */
    cads_l2discover_record_t* entries;
    size_t capacity;
    size_t count;
    uint32_t dropped_total;
} cads_l2discover_table_t;

/** `storage` holds up to `capacity` entries and must outlive the table. */
void cads_l2discover_table_init(cads_l2discover_table_t* table, cads_l2discover_record_t* storage, size_t capacity);

/**
 * Record one sighting. An existing entry for the same (kind, src_mac)
 * pair is overwritten in place with `record` (a device's advertised
 * name/port can legitimately change between sightings, e.g. a port
 * renumber); a new pair claims a free slot. A full table refuses a
 * genuinely new (kind, src_mac) pair rather than evicting a still-live
 * one - this is a bounded recon snapshot of one capture run, not a table
 * that ages entries out on its own, so "refuse and count" is the honest
 * behaviour, not "silently forget someone real to make room". Counted
 * via cads_l2discover_table_dropped_total(), never silently dropped.
 */
void cads_l2discover_table_learn(cads_l2discover_table_t* table, const cads_l2discover_record_t* record);

size_t cads_l2discover_table_count(const cads_l2discover_table_t* table);

/** The `index`-th live entry, 0 <= index < cads_l2discover_table_count(). NULL out of range. */
const cads_l2discover_record_t* cads_l2discover_table_at(const cads_l2discover_table_t* table, size_t index);

uint32_t cads_l2discover_table_dropped_total(const cads_l2discover_table_t* table);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_L2DISCOVER_H */

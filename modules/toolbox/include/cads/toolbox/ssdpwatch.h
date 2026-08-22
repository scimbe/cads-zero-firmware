/*
 * CaDS Zero toolbox - passive SSDP/UPnP device discovery listener.
 *
 * SSDP (Simple Service Discovery Protocol, UDP port 1900) is how UPnP
 * devices - smart TVs, printers, media servers, routers, a growing pile
 * of consumer IoT - announce themselves on a LAN: a NOTIFY with
 * NTS: ssdp:alive when they join (ssdp:byebye when they leave), and an
 * HTTP/1.1 200 OK in reply to anyone's M-SEARCH. Both carry a USN
 * (Unique Service Name, usually a UUID) and a LOCATION URL pointing at
 * the device's own UPnP description XML - recon that needs zero active
 * probing, just listening. Same case this session's own
 * l2discover.h/dhcpwatch.h/arpwatch.h already made for their own
 * protocols: an always-on probe left plugged in collects this
 * passively and continuously in a way nobody leaves a laptop doing.
 *
 * SSDP IS TEXT, NOT TLV - THE PARSER SHAPE IS DIFFERENT ON PURPOSE
 * ---------------------------------------------------------------------
 * Unlike l2discover's CDP/LLDP/STP or dhcpwatch's BOOTP, SSDP messages
 * are HTTP-style plaintext: a start line, then "Header: value\r\n"
 * lines, ending at the first line that isn't a header. This file's
 * cads_ssdp_find_header() is a header-line scanner, not a TLV walker -
 * the honest shape for the protocol, not the same code reused because
 * it already existed.
 *
 * This file is only the parser and the dedup table: pure functions over
 * caller-supplied bytes, no HAL, no lwIP, unit-tested on the host with
 * hand-built messages (tests/unit/test_ssdpwatch.c). The board-specific
 * capture loop and printout live in
 * apps/bringup/explorer_ssdpwatch_demo.c, the same split
 * cads/toolbox/l2discover.h/explorer_l2discover_demo.c already
 * established.
 *
 * EVERY OFFSET IS BOUNDS-CHECKED AGAINST `length` BEFORE IT IS READ
 * ---------------------------------------------------------------------
 * Same discipline as l2discover.c/dhcpwatch.c/arpwatch.c, for the same
 * reason: these bytes come off the wire, from whatever sent them.
 *
 * USN/LOCATION ARE DELIBERATELY SHORT AND MAY TRUNCATE
 * -----------------------------------------------------------
 * A real USN (`uuid:xxxxxxxx-xxxx-...::urn:...`) or LOCATION URL can run
 * well past what this struct keeps - RAM is the scarce resource on this
 * board (targets/itsboard/linker/cads_itsboard.ld's own 48K floor,
 * which this session's own l2discover/dhcpwatch/arpwatch tasks have
 * each had to budget against), not display width. A longer value is
 * sanitised-and-truncated, never refused.
 */

#ifndef CADS_TOOLBOX_SSDPWATCH_H
#define CADS_TOOLBOX_SSDPWATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CADS_SSDPWATCH_USN_MAX 24u
#define CADS_SSDPWATCH_LOCATION_MAX 28u

typedef enum {
    CADS_SSDP_ALIVE = 0,  /**< NOTIFY, NTS: ssdp:alive - a device announcing itself */
    CADS_SSDP_BYEBYE,     /**< NOTIFY, NTS: ssdp:byebye - a device leaving */
    CADS_SSDP_RESPONSE,   /**< HTTP/1.1 200 OK - a reply to an M-SEARCH */
    CADS_SSDP_OTHER,      /**< NOTIFY with an NTS value this file does not classify further */
} cads_ssdpwatch_kind_t;

typedef struct {
    cads_ssdpwatch_kind_t kind;
    uint8_t src_mac[6];
    uint32_t src_ip;
    char usn[CADS_SSDPWATCH_USN_MAX];
    char location[CADS_SSDPWATCH_LOCATION_MAX];
} cads_ssdpwatch_record_t;

/**
 * Examine one raw Ethernet frame. If it is IPv4/UDP addressed to port
 * 1900 and its payload starts with "NOTIFY" or "HTTP/1.1" (case
 * insensitive), fills `out` and returns true. Anything else returns
 * false with `out` untouched.
 */
bool cads_ssdpwatch_parse(const uint8_t* frame, uint16_t length, cads_ssdpwatch_record_t* out);

/**
 * Case-insensitive header-line scan: finds a line starting with `name`
 * (include the trailing colon, e.g. "USN:") at the start of the buffer
 * or right after a `\r\n`, and copies the trimmed, sanitised value into
 * `out`. Returns false (leaving `out` untouched) if no such line
 * exists. Exposed for its own unit tests, not just used internally.
 */
bool cads_ssdpwatch_find_header(const uint8_t* payload, uint16_t payload_len, const char* name, char* out, size_t out_size);

/* --- dedup table: one live entry per distinct (src_mac, USN) pair --- */

typedef struct {
    /* --- private --- */
    cads_ssdpwatch_record_t* entries;
    size_t capacity;
    size_t count;
    uint32_t dropped_total;
} cads_ssdpwatch_table_t;

/** `storage` holds up to `capacity` entries and must outlive the table. */
void cads_ssdpwatch_table_init(cads_ssdpwatch_table_t* table, cads_ssdpwatch_record_t* storage, size_t capacity);

/**
 * Record one sighting. A device can legitimately expose more than one
 * UPnP service (and therefore more than one USN) from the same MAC, so
 * the dedup key is (src_mac, usn) together, not src_mac alone - same
 * reasoning cads_l2discover_table_learn() gives for keying on
 * (kind, src_mac) rather than src_mac alone. An existing entry is
 * overwritten in place (kind/location can legitimately change, e.g.
 * alive -> byebye); a new pair claims a free slot. A full table
 * refuses a genuinely new pair, counted via
 * cads_ssdpwatch_table_dropped_total().
 */
void cads_ssdpwatch_table_learn(cads_ssdpwatch_table_t* table, const cads_ssdpwatch_record_t* record);

size_t cads_ssdpwatch_table_count(const cads_ssdpwatch_table_t* table);

/** The `index`-th live entry, 0 <= index < cads_ssdpwatch_table_count(). NULL out of range. */
const cads_ssdpwatch_record_t* cads_ssdpwatch_table_at(const cads_ssdpwatch_table_t* table, size_t index);

uint32_t cads_ssdpwatch_table_dropped_total(const cads_ssdpwatch_table_t* table);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_SSDPWATCH_H */

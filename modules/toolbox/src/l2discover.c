/*
 * CaDS Zero toolbox - passive Layer 2 neighbor discovery, implementation.
 *
 * See cads/toolbox/l2discover.h for the why. This file is the how: three
 * small, independent frame recognisers (CDP, LLDP, STP) that share only
 * the TLV-walking shape, not code - each protocol's TLV header is a
 * different width and byte layout, so a shared "walk TLVs" helper would
 * have needed a callback and a union just to paper over three-line
 * differences. Three similar loops, written out, are easier to check
 * against a spec than one generic one.
 */

#include "cads/toolbox/l2discover.h"

#include <string.h>

/* Ethernet header layout every recognised frame shares up to this point. */
#define CADS_ETH_DST_OFFSET 0u
#define CADS_ETH_SRC_OFFSET 6u
#define CADS_ETH_LEN_OFFSET 12u /* "length/type" field: EtherType for Ethernet II, 802.3 length for LLC frames */
#define CADS_ETH_PAYLOAD_OFFSET 14u

static bool cads_l2_mac_equals(const uint8_t* frame, uint16_t offset, const uint8_t mac[6]) {
    return memcmp(frame + offset, mac, 6u) == 0;
}

/** Copies up to `out_size - 1` bytes from `src`, replacing any byte
 *  outside printable ASCII with '.', and NUL-terminates. `src_len` of 0
 *  produces an empty string, not a copy of whatever `src` points at. */
static void cads_l2_copy_text(char* out, size_t out_size, const uint8_t* src, size_t src_len) {
    if(out_size == 0u) return;

    size_t copy_len = src_len < out_size - 1u ? src_len : out_size - 1u;
    size_t i;
    for(i = 0u; i < copy_len; i++) {
        uint8_t byte = src[i];
        out[i] = (byte >= 0x20u && byte < 0x7Fu) ? (char)byte : '.';
    }
    out[i] = '\0';
}

/* ---------------------------------------------------------------- CDP */

static const uint8_t CADS_CDP_DST_MAC[6] = {0x01u, 0x00u, 0x0Cu, 0xCCu, 0xCCu, 0xCCu};
static const uint8_t CADS_CDP_SNAP_OUI[3] = {0x00u, 0x00u, 0x0Cu}; /* Cisco */
#define CADS_CDP_SNAP_PID 0x2000u
#define CADS_CDP_TLV_DEVICE_ID 1u
#define CADS_CDP_TLV_PORT_ID 3u

static bool cads_l2_parse_cdp(const uint8_t* frame, uint16_t length, cads_l2discover_record_t* out) {
    /* dst(6) src(6) len(2) llc(3: AA AA 03) snap(5: 00 00 0C 20 00) cdp-hdr(4: ver ttl cksum-hi cksum-lo) */
    const uint16_t CDP_HEADER_END = 26u;
    if(length < CDP_HEADER_END) return false;
    if(!cads_l2_mac_equals(frame, CADS_ETH_DST_OFFSET, CADS_CDP_DST_MAC)) return false;
    if(frame[14] != 0xAAu || frame[15] != 0xAAu || frame[16] != 0x03u) return false; /* LLC UI + SNAP present */
    if(memcmp(frame + 17, CADS_CDP_SNAP_OUI, 3u) != 0) return false;
    uint16_t snap_pid = ((uint16_t)frame[20] << 8) | frame[21];
    if(snap_pid != CADS_CDP_SNAP_PID) return false;

    memset(out, 0, sizeof(*out));
    out->kind = CADS_L2_CDP;
    memcpy(out->src_mac, frame + CADS_ETH_SRC_OFFSET, 6u);

    uint16_t offset = CDP_HEADER_END;
    while((uint32_t)offset + 4u <= length) {
        uint16_t tlv_type = ((uint16_t)frame[offset] << 8) | frame[offset + 1u];
        uint16_t tlv_len = ((uint16_t)frame[offset + 2u] << 8) | frame[offset + 3u]; /* includes this 4-byte header */
        if(tlv_len < 4u || (uint32_t)offset + tlv_len > length) break; /* malformed - stop, keep what was found */

        const uint8_t* value = frame + offset + 4u;
        uint16_t value_len = tlv_len - 4u;
        if(tlv_type == CADS_CDP_TLV_DEVICE_ID) {
            cads_l2_copy_text(out->name, sizeof(out->name), value, value_len);
        } else if(tlv_type == CADS_CDP_TLV_PORT_ID) {
            cads_l2_copy_text(out->port, sizeof(out->port), value, value_len);
        }
        offset = (uint16_t)(offset + tlv_len);
    }
    return true;
}

/* --------------------------------------------------------------- LLDP */

static const uint8_t CADS_LLDP_DST_MAC[6] = {0x01u, 0x80u, 0xC2u, 0x00u, 0x00u, 0x0Eu}; /* "nearest bridge" */
#define CADS_LLDP_ETHERTYPE 0x88CCu
#define CADS_LLDP_TLV_END 0u
#define CADS_LLDP_TLV_CHASSIS_ID 1u
#define CADS_LLDP_TLV_PORT_ID 2u
#define CADS_LLDP_TLV_SYSTEM_NAME 5u

static bool cads_l2_parse_lldp(const uint8_t* frame, uint16_t length, cads_l2discover_record_t* out) {
    if(length < CADS_ETH_PAYLOAD_OFFSET) return false;
    if(!cads_l2_mac_equals(frame, CADS_ETH_DST_OFFSET, CADS_LLDP_DST_MAC)) return false;
    uint16_t ethertype = ((uint16_t)frame[CADS_ETH_LEN_OFFSET] << 8) | frame[CADS_ETH_LEN_OFFSET + 1u];
    if(ethertype != CADS_LLDP_ETHERTYPE) return false;

    memset(out, 0, sizeof(*out));
    out->kind = CADS_L2_LLDP;
    memcpy(out->src_mac, frame + CADS_ETH_SRC_OFFSET, 6u);

    bool have_name = false; /* system name (preferred) beats chassis ID (fallback) once seen */
    uint16_t offset = CADS_ETH_PAYLOAD_OFFSET;
    while((uint32_t)offset + 2u <= length) {
        uint16_t tlv_header = ((uint16_t)frame[offset] << 8) | frame[offset + 1u];
        uint16_t tlv_type = tlv_header >> 9;
        uint16_t tlv_len = tlv_header & 0x01FFu;
        if(tlv_type == CADS_LLDP_TLV_END) break;
        if((uint32_t)offset + 2u + tlv_len > length) break; /* malformed - stop, keep what was found */

        const uint8_t* value = frame + offset + 2u;
        if(tlv_type == CADS_LLDP_TLV_CHASSIS_ID && tlv_len >= 1u && !have_name) {
            /* value[0] is the chassis ID subtype (MAC, IPv4, locally assigned, ...) - skipped, the
             * raw remainder is shown as-is (cads_l2_copy_text sanitises anything non-printable,
             * e.g. a subtype-4 MAC address, to dots rather than garbling the terminal). */
            cads_l2_copy_text(out->name, sizeof(out->name), value + 1u, tlv_len - 1u);
        } else if(tlv_type == CADS_LLDP_TLV_PORT_ID && tlv_len >= 1u) {
            cads_l2_copy_text(out->port, sizeof(out->port), value + 1u, tlv_len - 1u);
        } else if(tlv_type == CADS_LLDP_TLV_SYSTEM_NAME) {
            cads_l2_copy_text(out->name, sizeof(out->name), value, tlv_len);
            have_name = true;
        }
        offset = (uint16_t)(offset + 2u + tlv_len);
    }
    return true;
}

/* ---------------------------------------------------------------- STP */

static const uint8_t CADS_STP_DST_MAC[6] = {0x01u, 0x80u, 0xC2u, 0x00u, 0x00u, 0x00u};
#define CADS_STP_TYPE_CONFIG 0x00u
#define CADS_STP_TYPE_RSTP 0x02u /* RSTP/MSTP share this Configuration BPDU's layout for the fields read here */

static bool cads_l2_parse_stp(const uint8_t* frame, uint16_t length, cads_l2discover_record_t* out) {
    /* dst(6) src(6) len(2) llc(3: 42 42 03) proto-id(2) ver(1) type(1) flags(1)
     * root-id(8: prio 2 + mac 6) root-cost(4) bridge-id(8: prio 2 + mac 6) - through bridge-id is 42 bytes.
     * Port ID/timers past that are not read - the two bridge identities are the whole point of this parser. */
    const uint16_t STP_BRIDGE_ID_END = 42u;
    if(length < STP_BRIDGE_ID_END) return false;
    if(!cads_l2_mac_equals(frame, CADS_ETH_DST_OFFSET, CADS_STP_DST_MAC)) return false;
    if(frame[14] != 0x42u || frame[15] != 0x42u || frame[16] != 0x03u) return false; /* LLC DSAP/SSAP 0x42 */
    if(frame[17] != 0x00u || frame[18] != 0x00u) return false;                       /* BPDU protocol id = 0 */
    uint8_t bpdu_type = frame[20];
    if(bpdu_type != CADS_STP_TYPE_CONFIG && bpdu_type != CADS_STP_TYPE_RSTP) return false;

    memset(out, 0, sizeof(*out));
    out->kind = CADS_L2_STP;
    memcpy(out->src_mac, frame + CADS_ETH_SRC_OFFSET, 6u);
    out->stp_root_priority = ((uint16_t)frame[22] << 8) | frame[23];
    memcpy(out->stp_root_mac, frame + 24, 6u);
    out->stp_bridge_priority = ((uint16_t)frame[34] << 8) | frame[35];
    memcpy(out->stp_bridge_mac, frame + 36, 6u);
    return true;
}

/* -------------------------------------------------------------- VLAN */

#define CADS_VLAN_ETHERTYPE 0x8100u

bool cads_l2discover_vlan_tag(const uint8_t* frame, uint16_t length, uint16_t* vlan_id) {
    if(length < CADS_ETH_LEN_OFFSET + 4u) return false; /* need the tag's own 4 bytes past the length/type field */
    uint16_t ethertype = ((uint16_t)frame[CADS_ETH_LEN_OFFSET] << 8) | frame[CADS_ETH_LEN_OFFSET + 1u];
    if(ethertype != CADS_VLAN_ETHERTYPE) return false;

    uint16_t tci = ((uint16_t)frame[CADS_ETH_LEN_OFFSET + 2u] << 8) | frame[CADS_ETH_LEN_OFFSET + 3u];
    uint16_t id = tci & 0x0FFFu; /* low 12 bits; top 4 are priority (3) + DEI (1) */
    if(id == 0x0FFFu) return false; /* reserved by 802.1Q, never a real VLAN ID */
    *vlan_id = id;
    return true;
}

/* ------------------------------------------------------------ dispatch */

bool cads_l2discover_parse(const uint8_t* frame, uint16_t length, cads_l2discover_record_t* out) {
    if(frame == NULL || out == NULL || length < 12u) return false; /* shorter than dst+src is not a frame at all */
    if(cads_l2_parse_cdp(frame, length, out)) return true;
    if(cads_l2_parse_lldp(frame, length, out)) return true;
    if(cads_l2_parse_stp(frame, length, out)) return true;
    return false;
}

/* ------------------------------------------------------------- table */

void cads_l2discover_table_init(cads_l2discover_table_t* table, cads_l2discover_record_t* storage, size_t capacity) {
    if(!table) return;
    table->entries = storage;
    table->capacity = storage ? capacity : 0u;
    table->count = 0u;
    table->dropped_total = 0u;
}

void cads_l2discover_table_learn(cads_l2discover_table_t* table, const cads_l2discover_record_t* record) {
    if(!table || !record) return;
    for(size_t i = 0u; i < table->count; i++) {
        if(table->entries[i].kind == record->kind && memcmp(table->entries[i].src_mac, record->src_mac, 6u) == 0) {
            table->entries[i] = *record;
            return;
        }
    }
    if(table->count >= table->capacity) {
        table->dropped_total++;
        return;
    }
    table->entries[table->count] = *record;
    table->count++;
}

size_t cads_l2discover_table_count(const cads_l2discover_table_t* table) {
    return table ? table->count : 0u;
}

const cads_l2discover_record_t* cads_l2discover_table_at(const cads_l2discover_table_t* table, size_t index) {
    if(!table || index >= table->count) return NULL;
    return &table->entries[index];
}

uint32_t cads_l2discover_table_dropped_total(const cads_l2discover_table_t* table) {
    return table ? table->dropped_total : 0u;
}

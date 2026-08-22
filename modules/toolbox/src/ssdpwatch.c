/*
 * CaDS Zero toolbox - passive SSDP/UPnP device discovery, implementation.
 *
 * See cads/toolbox/ssdpwatch.h for the why and for why this parser is
 * shaped differently (a header-line scanner) from l2discover.c's/
 * dhcpwatch.c's TLV/fixed-field walkers - SSDP is HTTP-style plaintext,
 * not a binary protocol.
 */

#include "cads/toolbox/ssdpwatch.h"

#include <string.h>

#define CADS_ETH_PAYLOAD_OFFSET 14u
#define CADS_IPV4_ETHERTYPE 0x0800u
#define CADS_IP_PROTO_UDP 17u
#define CADS_SSDP_PORT 1900u

static uint16_t cads_read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t cads_read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/** Copies up to `out_size - 1` bytes from `src`, replacing any byte
 *  outside printable ASCII with '.', and NUL-terminates - same
 *  contract as l2discover.c's own cads_l2_copy_text(), not shared
 *  across modules for a three-line function. */
static void cads_ssdp_copy_text(char* out, size_t out_size, const uint8_t* src, size_t src_len) {
    if(out_size == 0u) return;
    size_t copy_len = src_len < out_size - 1u ? src_len : out_size - 1u;
    size_t i;
    for(i = 0u; i < copy_len; i++) {
        uint8_t byte = src[i];
        out[i] = (byte >= 0x20u && byte < 0x7Fu) ? (char)byte : '.';
    }
    out[i] = '\0';
}

static bool cads_ci_starts_with(const uint8_t* data, uint16_t data_len, const char* prefix) {
    size_t plen = strlen(prefix);
    if(data_len < plen) return false;
    for(size_t k = 0u; k < plen; k++) {
        uint8_t c = data[k];
        if(c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        char p = prefix[k];
        if(p >= 'a' && p <= 'z') p = (char)(p - 32);
        if(c != (uint8_t)p) return false;
    }
    return true;
}

bool cads_ssdpwatch_find_header(
    const uint8_t* payload, uint16_t payload_len, const char* name, char* out, size_t out_size) {
    if(!payload || !name || !out || out_size == 0u) return false;
    size_t name_len = strlen(name);

    for(uint16_t i = 0u; (uint32_t)i + name_len <= payload_len; i++) {
        bool at_line_start = (i == 0u) || (i >= 2u && payload[i - 2u] == '\r' && payload[i - 1u] == '\n');
        if(!at_line_start) continue;
        if(!cads_ci_starts_with(payload + i, (uint16_t)(payload_len - i), name)) continue;

        uint16_t value_start = (uint16_t)(i + name_len);
        while(value_start < payload_len && payload[value_start] == ' ') value_start++;
        uint16_t value_end = value_start;
        while(value_end < payload_len && payload[value_end] != '\r') value_end++;

        cads_ssdp_copy_text(out, out_size, payload + value_start, (size_t)(value_end - value_start));
        return true;
    }
    return false;
}

bool cads_ssdpwatch_parse(const uint8_t* frame, uint16_t length, cads_ssdpwatch_record_t* out) {
    if(frame == NULL || out == NULL || length < CADS_ETH_PAYLOAD_OFFSET) return false;
    if(cads_read_be16(frame + 12u) != CADS_IPV4_ETHERTYPE) return false;

    /* --- IPv4 header: IHL read, not assumed - same reasoning as dhcpwatch.c --- */
    uint16_t ip_offset = CADS_ETH_PAYLOAD_OFFSET;
    if((uint32_t)ip_offset + 20u > length) return false;
    uint8_t version = frame[ip_offset] >> 4;
    uint8_t ihl_words = frame[ip_offset] & 0x0Fu;
    if(version != 4u || ihl_words < 5u) return false;
    uint16_t ip_header_len = (uint16_t)(ihl_words * 4u);
    if((uint32_t)ip_offset + ip_header_len > length) return false;
    if(frame[ip_offset + 9u] != CADS_IP_PROTO_UDP) return false;
    uint32_t ip_src = cads_read_be32(frame + ip_offset + 12u);

    /* --- UDP header --- */
    uint16_t udp_offset = (uint16_t)(ip_offset + ip_header_len);
    if((uint32_t)udp_offset + 8u > length) return false;
    if(cads_read_be16(frame + udp_offset + 2u) != CADS_SSDP_PORT) return false; /* dst port */

    uint16_t payload_offset = (uint16_t)(udp_offset + 8u);
    if(payload_offset >= length) return false;
    const uint8_t* payload = frame + payload_offset;
    uint16_t payload_len = (uint16_t)(length - payload_offset);
    if(payload_len < 8u) return false;

    cads_ssdpwatch_kind_t kind;
    if(cads_ci_starts_with(payload, payload_len, "NOTIFY")) {
        char nts[16];
        if(cads_ssdpwatch_find_header(payload, payload_len, "NTS:", nts, sizeof(nts))) {
            if(strstr(nts, "ssdp:alive") != NULL) {
                kind = CADS_SSDP_ALIVE;
            } else if(strstr(nts, "ssdp:byebye") != NULL) {
                kind = CADS_SSDP_BYEBYE;
            } else {
                kind = CADS_SSDP_OTHER;
            }
        } else {
            kind = CADS_SSDP_OTHER;
        }
    } else if(cads_ci_starts_with(payload, payload_len, "HTTP/1.1")) {
        kind = CADS_SSDP_RESPONSE;
    } else {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->kind = kind;
    memcpy(out->src_mac, frame + 6u, 6u);
    out->src_ip = ip_src;
    cads_ssdpwatch_find_header(payload, payload_len, "USN:", out->usn, sizeof(out->usn));
    cads_ssdpwatch_find_header(payload, payload_len, "LOCATION:", out->location, sizeof(out->location));
    return true;
}

/* ------------------------------------------------------------- table */

void cads_ssdpwatch_table_init(cads_ssdpwatch_table_t* table, cads_ssdpwatch_record_t* storage, size_t capacity) {
    if(!table) return;
    table->entries = storage;
    table->capacity = storage ? capacity : 0u;
    table->count = 0u;
    table->dropped_total = 0u;
}

void cads_ssdpwatch_table_learn(cads_ssdpwatch_table_t* table, const cads_ssdpwatch_record_t* record) {
    if(!table || !record) return;
    for(size_t i = 0u; i < table->count; i++) {
        cads_ssdpwatch_record_t* entry = &table->entries[i];
        if(memcmp(entry->src_mac, record->src_mac, 6u) == 0 && strcmp(entry->usn, record->usn) == 0) {
            *entry = *record;
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

size_t cads_ssdpwatch_table_count(const cads_ssdpwatch_table_t* table) {
    return table ? table->count : 0u;
}

const cads_ssdpwatch_record_t* cads_ssdpwatch_table_at(const cads_ssdpwatch_table_t* table, size_t index) {
    if(!table || index >= table->count) return NULL;
    return &table->entries[index];
}

uint32_t cads_ssdpwatch_table_dropped_total(const cads_ssdpwatch_table_t* table) {
    return table ? table->dropped_total : 0u;
}

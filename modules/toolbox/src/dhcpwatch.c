/*
 * CaDS Zero toolbox - passive rogue-DHCP-server detector, implementation.
 *
 * See cads/toolbox/dhcpwatch.h for the why. This walks four nested
 * headers - Ethernet, IPv4 (variable length: IHL is read, not assumed
 * to be the common 20 bytes), UDP, BOOTP/DHCP - each offset checked
 * against `length` before it is read, the same discipline
 * cads/toolbox/l2discover.c already established for a different set of
 * protocols.
 */

#include "cads/toolbox/dhcpwatch.h"

#include <string.h>

#define CADS_ETH_SRC_OFFSET 6u
#define CADS_ETH_ETHERTYPE_OFFSET 12u
#define CADS_ETH_PAYLOAD_OFFSET 14u
#define CADS_IPV4_ETHERTYPE 0x0800u
#define CADS_IP_PROTO_UDP 17u
#define CADS_DHCP_SERVER_PORT 67u
#define CADS_DHCP_CLIENT_PORT 68u

#define CADS_DHCP_OPT_MESSAGE_TYPE 53u
#define CADS_DHCP_OPT_SERVER_ID 54u
#define CADS_DHCP_OPT_END 255u
#define CADS_DHCP_OPT_PAD 0u

static const uint8_t CADS_DHCP_MAGIC_COOKIE[4] = {0x63u, 0x82u, 0x53u, 0x63u};

static uint32_t cads_read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t cads_read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

bool cads_dhcpwatch_parse(const uint8_t* frame, uint16_t length, cads_dhcpwatch_record_t* out) {
    if(frame == NULL || out == NULL || length < CADS_ETH_PAYLOAD_OFFSET) return false;

    if(cads_read_be16(frame + CADS_ETH_ETHERTYPE_OFFSET) != CADS_IPV4_ETHERTYPE) return false;

    /* --- IPv4 header: IHL is read, not assumed - options are rare on LAN
     * DHCP traffic but a wrong assumption here would misalign every field
     * that follows, silently. */
    uint16_t ip_offset = CADS_ETH_PAYLOAD_OFFSET;
    if((uint32_t)ip_offset + 20u > length) return false; /* shortest possible IPv4 header */
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
    uint16_t src_port = cads_read_be16(frame + udp_offset);
    uint16_t dst_port = cads_read_be16(frame + udp_offset + 2u);
    if(src_port != CADS_DHCP_SERVER_PORT || dst_port != CADS_DHCP_CLIENT_PORT) return false;

    /* --- BOOTP/DHCP fixed fields (236 B) + magic cookie (4 B) --- */
    uint16_t dhcp_offset = (uint16_t)(udp_offset + 8u);
    const uint16_t CADS_DHCP_FIXED_LEN = 236u;
    if((uint32_t)dhcp_offset + CADS_DHCP_FIXED_LEN + 4u > length) return false;
    if(memcmp(frame + dhcp_offset + CADS_DHCP_FIXED_LEN, CADS_DHCP_MAGIC_COOKIE, 4u) != 0) return false;

    uint32_t yiaddr = cads_read_be32(frame + dhcp_offset + 16u);

    /* --- options: walk for message type (53) and server identifier (54) --- */
    uint16_t offset = (uint16_t)(dhcp_offset + CADS_DHCP_FIXED_LEN + 4u);
    bool have_msg_type = false;
    uint8_t msg_type = 0u;
    bool have_server_id = false;
    uint32_t server_id = 0u;

    while(offset < length) {
        uint8_t opt_type = frame[offset];
        if(opt_type == CADS_DHCP_OPT_END) break;
        if(opt_type == CADS_DHCP_OPT_PAD) {
            offset++;
            continue;
        }
        if((uint32_t)offset + 2u > length) break; /* malformed - stop, keep what was found */
        uint8_t opt_len = frame[offset + 1u];
        if((uint32_t)offset + 2u + opt_len > length) break; /* malformed - stop, keep what was found */

        if(opt_type == CADS_DHCP_OPT_MESSAGE_TYPE && opt_len == 1u) {
            msg_type = frame[offset + 2u];
            have_msg_type = true;
        } else if(opt_type == CADS_DHCP_OPT_SERVER_ID && opt_len == 4u) {
            server_id = cads_read_be32(frame + offset + 2u);
            have_server_id = true;
        }
        offset = (uint16_t)(offset + 2u + opt_len);
    }

    if(!have_msg_type) return false;
    if(msg_type != CADS_DHCPWATCH_OFFER && msg_type != CADS_DHCPWATCH_ACK && msg_type != CADS_DHCPWATCH_NAK) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    memcpy(out->src_mac, frame + CADS_ETH_SRC_OFFSET, 6u);
    out->server_ip = have_server_id ? server_id : ip_src;
    out->offered_ip = yiaddr;
    out->msg_type = msg_type;
    return true;
}

/* ------------------------------------------------------------- table */

void cads_dhcpwatch_table_init(cads_dhcpwatch_table_t* table, cads_dhcpwatch_record_t* storage, size_t capacity) {
    if(!table) return;
    table->entries = storage;
    table->capacity = storage ? capacity : 0u;
    table->count = 0u;
    table->dropped_total = 0u;
}

void cads_dhcpwatch_table_learn(cads_dhcpwatch_table_t* table, const cads_dhcpwatch_record_t* record) {
    if(!table || !record) return;
    for(size_t i = 0u; i < table->count; i++) {
        if(memcmp(table->entries[i].src_mac, record->src_mac, 6u) == 0) {
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

size_t cads_dhcpwatch_table_count(const cads_dhcpwatch_table_t* table) {
    return table ? table->count : 0u;
}

const cads_dhcpwatch_record_t* cads_dhcpwatch_table_at(const cads_dhcpwatch_table_t* table, size_t index) {
    if(!table || index >= table->count) return NULL;
    return &table->entries[index];
}

uint32_t cads_dhcpwatch_table_dropped_total(const cads_dhcpwatch_table_t* table) {
    return table ? table->dropped_total : 0u;
}

bool cads_dhcpwatch_table_multiple_servers(const cads_dhcpwatch_table_t* table) {
    return table && table->count > 1u;
}

/*
 * CaDS Zero toolbox - passive ARP spoofing detector, implementation.
 *
 * See cads/toolbox/arpwatch.h for the why. The ARP header itself is the
 * simplest of the three protocols this session parsed off the wire
 * (l2discover's CDP/LLDP/STP, dhcpwatch's BOOTP) - fixed-width fields,
 * no TLVs, no variable-length options - so this file is correspondingly
 * short.
 */

#include "cads/toolbox/arpwatch.h"

#include <string.h>

#define CADS_ETH_PAYLOAD_OFFSET 14u
#define CADS_ARP_ETHERTYPE 0x0806u
#define CADS_ARP_HTYPE_ETHERNET 1u
#define CADS_ARP_PTYPE_IPV4 0x0800u
#define CADS_ARP_HEADER_LEN 28u /* htype2 ptype2 hlen1 plen1 op2 smac6 sip4 tmac6 tip4 */

static uint16_t cads_read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t cads_read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

bool cads_arpwatch_parse(const uint8_t* frame, uint16_t length, cads_arpwatch_claim_t* out) {
    if(frame == NULL || out == NULL) return false;
    if((uint32_t)CADS_ETH_PAYLOAD_OFFSET + CADS_ARP_HEADER_LEN > length) return false;

    if(cads_read_be16(frame + 12u) != CADS_ARP_ETHERTYPE) return false;

    const uint8_t* arp = frame + CADS_ETH_PAYLOAD_OFFSET;
    if(cads_read_be16(arp + 0u) != CADS_ARP_HTYPE_ETHERNET) return false;
    if(cads_read_be16(arp + 2u) != CADS_ARP_PTYPE_IPV4) return false;
    if(arp[4u] != 6u || arp[5u] != 4u) return false; /* hardware/protocol address lengths */

    uint16_t opcode = cads_read_be16(arp + 6u);
    if(opcode != CADS_ARPWATCH_REQUEST && opcode != CADS_ARPWATCH_REPLY) return false;

    memset(out, 0, sizeof(*out));
    out->opcode = (uint8_t)opcode;
    memcpy(out->sender_mac, arp + 8u, 6u);
    out->sender_ip = cads_read_be32(arp + 14u);
    return true;
}

/* ------------------------------------------------------------- table */

void cads_arpwatch_table_init(cads_arpwatch_table_t* table, cads_arpwatch_entry_t* storage, size_t capacity) {
    if(!table) return;
    table->entries = storage;
    table->capacity = storage ? capacity : 0u;
    table->count = 0u;
    table->dropped_total = 0u;
}

bool cads_arpwatch_table_learn(cads_arpwatch_table_t* table, uint32_t ip, const uint8_t mac[6]) {
    if(!table || !mac) return false;

    for(size_t i = 0u; i < table->count; i++) {
        cads_arpwatch_entry_t* entry = &table->entries[i];
        if(entry->ip != ip) continue;
        entry->sightings++;
        if(memcmp(entry->mac, mac, 6u) == 0) return false; /* same binding, no change */
        memcpy(entry->mac, mac, 6u);
        entry->mac_changes++;
        return true;
    }

    if(table->count >= table->capacity) {
        table->dropped_total++;
        return false;
    }

    cads_arpwatch_entry_t* fresh = &table->entries[table->count];
    fresh->ip = ip;
    memcpy(fresh->mac, mac, 6u);
    fresh->sightings = 1u;
    fresh->mac_changes = 0u;
    table->count++;
    return false; /* a first sighting is not a change */
}

size_t cads_arpwatch_table_count(const cads_arpwatch_table_t* table) {
    return table ? table->count : 0u;
}

const cads_arpwatch_entry_t* cads_arpwatch_table_at(const cads_arpwatch_table_t* table, size_t index) {
    if(!table || index >= table->count) return NULL;
    return &table->entries[index];
}

uint32_t cads_arpwatch_table_dropped_total(const cads_arpwatch_table_t* table) {
    return table ? table->dropped_total : 0u;
}

bool cads_arpwatch_table_any_flip(const cads_arpwatch_table_t* table) {
    if(!table) return false;
    for(size_t i = 0u; i < table->count; i++) {
        if(table->entries[i].mac_changes > 0u) return true;
    }
    return false;
}

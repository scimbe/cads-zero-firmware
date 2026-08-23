/*
 * CaDS Zero toolbox - passive traffic-mix statistics, implementation.
 *
 * See cads/toolbox/trafficstats.h for the why. No TLV, no header-line
 * scan, no table - just the Ethernet destination address's own I/G bit
 * and one, maybe two, EtherType reads.
 */

#include "cads/toolbox/trafficstats.h"

#include <string.h>

#define CADS_ETH_HEADER_LEN 14u
#define CADS_ETHERTYPE_OFFSET 12u
#define CADS_VLAN_ETHERTYPE 0x8100u
#define CADS_VLAN_TAG_LEN 4u
#define CADS_ETHERTYPE_ARP 0x0806u
#define CADS_ETHERTYPE_IPV4 0x0800u
#define CADS_ETHERTYPE_IPV6 0x86DDu

static uint16_t cads_read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

void cads_trafficstats_init(cads_trafficstats_t* stats) {
    if(!stats) return;
    memset(stats, 0, sizeof(*stats));
}

void cads_trafficstats_observe(cads_trafficstats_t* stats, const uint8_t* frame, uint16_t length) {
    if(!stats || !frame) return;

    stats->total_frames++;
    stats->total_bytes += length;

    if(length < CADS_ETH_HEADER_LEN) {
        stats->runt_frames++;
        return;
    }

    /* --- destination address: broadcast, multicast, or unicast --- */
    static const uint8_t BROADCAST[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    if(memcmp(frame, BROADCAST, 6u) == 0) {
        stats->broadcast_frames++;
    } else if(frame[0] & 0x01u) { /* I/G bit: 1 = group (multicast) address */
        stats->multicast_frames++;
    } else {
        stats->unicast_frames++;
    }

    /* --- EtherType, stepping past one 802.1Q tag if present --- */
    uint16_t ethertype_offset = CADS_ETHERTYPE_OFFSET;
    uint16_t ethertype = cads_read_be16(frame + ethertype_offset);

    if(ethertype == CADS_VLAN_ETHERTYPE) {
        stats->vlan_tagged_frames++;
        ethertype_offset = (uint16_t)(ethertype_offset + CADS_VLAN_TAG_LEN);
        if((uint32_t)ethertype_offset + 2u > length) return; /* tag present, too short for the inner EtherType */
        ethertype = cads_read_be16(frame + ethertype_offset);
    }

    if(ethertype == CADS_ETHERTYPE_ARP) {
        stats->arp_frames++;
    } else if(ethertype == CADS_ETHERTYPE_IPV4) {
        stats->ipv4_frames++;
    } else if(ethertype == CADS_ETHERTYPE_IPV6) {
        stats->ipv6_frames++;
    } else {
        stats->other_ethertype_frames++;
    }
}

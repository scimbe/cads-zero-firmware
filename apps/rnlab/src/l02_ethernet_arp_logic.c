/*
 * CaDS Zero - rnlab L02 (Ethernet und ARP): pure logic, host-testable.
 * See l02_ethernet_arp_logic.h.
 */

#include "l02_ethernet_arp_logic.h"

#include <string.h>

static inline uint16_t rnlab_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline bool rnlab_ip_equal(const uint8_t a[4], const uint8_t b[4]) {
    return memcmp(a, b, 4u) == 0;
}

static inline bool rnlab_ip_is_zero(const uint8_t a[4]) {
    return (a[0] | a[1] | a[2] | a[3]) == 0u;
}

rnlab_arp_result_t rnlab_parse_arp(const uint8_t* frame, size_t len, rnlab_arp_packet_t* out) {
    /* TODO(L02): ARP-Paket aus dem Ethernet-Rahmen lesen (RFC 826, Skript 5.15).
     *
     * Aufbau ab frame[0]: Ziel-MAC (6), Quell-MAC (6), EtherType (2, 0x0806),
     * danach das ARP-Paket (28 B): htype (2), ptype (2), hlen (1), plen (1),
     * oper (2), sha (6), spa (4), tha (6), tpa (4). Mehrbyte-Felder sind
     * big-endian (rnlab_be16 hilft). Prueft in dieser Reihenfolge:
     *   1. frame/out NULL oder len < 14 + 28      -> RNLAB_ARP_ERR_TRUNCATED
     *   2. EtherType != 0x0806                     -> RNLAB_ARP_ERR_NOT_ARP
     *   3. htype != 1, ptype != 0x0800, hlen != 6
     *      oder plen != 4                          -> RNLAB_ARP_ERR_UNSUPPORTED
     *   4. oper weder 1 (Request) noch 2 (Reply)   -> RNLAB_ARP_ERR_BAD_OPER
     * Nur bei RNLAB_ARP_OK *out beschreiben. Bytes nach dem ARP-Paket
     * (Padding auf 60 B) ignorieren. */
    (void)frame;
    (void)len;
    (void)out;
    return RNLAB_ARP_ERR_NOT_ARP;
}

rnlab_arp_kind_t rnlab_arp_classify(const rnlab_arp_packet_t* packet) {
    /* TODO(L02): Paket einordnen (rnlab_ip_equal, rnlab_ip_is_zero helfen).
     *   NULL oder oper weder 1 noch 2              -> RNLAB_ARP_KIND_INVALID
     *   Request mit spa 0.0.0.0 (RFC 5227)         -> RNLAB_ARP_KIND_PROBE
     *   spa == tpa (Request oder Reply)            -> RNLAB_ARP_KIND_GRATUITOUS
     *   sonst                                      -> RNLAB_ARP_KIND_REQUEST / _REPLY */
    (void)packet;
    (void)rnlab_ip_is_zero; /* Helfer, bis ihr ihn benutzt */
    return RNLAB_ARP_KIND_INVALID;
}

const char* rnlab_arp_kind_name(rnlab_arp_kind_t kind) {
    switch(kind) {
    case RNLAB_ARP_KIND_REQUEST:
        return "Request";
    case RNLAB_ARP_KIND_REPLY:
        return "Reply";
    case RNLAB_ARP_KIND_GRATUITOUS:
        return "Gratuitous";
    case RNLAB_ARP_KIND_PROBE:
        return "Probe";
    default:
        return "ungueltig";
    }
}

void rnlab_arp_count(rnlab_arp_counters_t* counters, const uint8_t* frame, size_t len) {
    if(counters == NULL || frame == NULL) return;
    rnlab_arp_packet_t packet = {0};
    rnlab_arp_result_t result = rnlab_parse_arp(frame, len, &packet);
    if(result == RNLAB_ARP_ERR_NOT_ARP) return;
    if(result == RNLAB_ARP_ERR_TRUNCATED) {
        /* A runt that still carries the ARP EtherType is a broken ARP frame;
         * one too short for a EtherType at all is just not ours. */
        if(len >= RNLAB_ETH_HDR_LEN && rnlab_be16(&frame[12]) == RNLAB_ETHERTYPE_ARP) counters->invalid++;
        return;
    }
    if(result != RNLAB_ARP_OK) {
        counters->invalid++;
        return;
    }
    switch(rnlab_arp_classify(&packet)) {
    case RNLAB_ARP_KIND_REQUEST:
        counters->requests++;
        break;
    case RNLAB_ARP_KIND_REPLY:
        counters->replies++;
        break;
    case RNLAB_ARP_KIND_GRATUITOUS:
        counters->gratuitous++;
        break;
    case RNLAB_ARP_KIND_PROBE:
        counters->probes++;
        break;
    default:
        counters->invalid++;
        break;
    }
}

void rnlab_arp_timing_reset(rnlab_arp_timing_t* timing) {
    if(timing == NULL) return;
    memset(timing, 0, sizeof(*timing));
}

void rnlab_arp_timing_on_tx(rnlab_arp_timing_t* timing, const uint8_t* frame, size_t len, uint64_t now_us) {
    if(timing == NULL) return;
    rnlab_arp_packet_t packet = {0};
    if(rnlab_parse_arp(frame, len, &packet) != RNLAB_ARP_OK) return;
    if(rnlab_arp_classify(&packet) != RNLAB_ARP_KIND_REQUEST) return;
    /* lwIP repeats a pending request once per second; timing from the
     * latest one measures one exchange, not the whole retry history. */
    memcpy(timing->target, packet.tpa, 4u);
    timing->sent_us = now_us;
    timing->pending = true;
}

bool rnlab_arp_timing_on_rx(rnlab_arp_timing_t* timing, const uint8_t* frame, size_t len, uint64_t now_us,
                            uint32_t* latency_us) {
    if(timing == NULL || !timing->pending) return false;
    rnlab_arp_packet_t packet = {0};
    if(rnlab_parse_arp(frame, len, &packet) != RNLAB_ARP_OK) return false;
    if(packet.oper != RNLAB_ARP_OP_REPLY || !rnlab_ip_equal(packet.spa, timing->target)) return false;

    uint64_t delta = now_us - timing->sent_us;
    uint32_t us = delta > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)delta;
    timing->pending = false;
    timing->last_us = us;
    if(timing->count == 0u || us < timing->min_us) timing->min_us = us;
    if(us > timing->max_us) timing->max_us = us;
    timing->count++;
    if(latency_us) *latency_us = us;
    return true;
}

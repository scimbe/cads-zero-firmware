/*
 * CaDS Zero - rnlab L02 (Ethernet und ARP): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l02.c links this file
 * directly on the host. Board integration lives in l02_ethernet_arp.c.
 *
 * Everything works on raw Ethernet frames as the driver's hooks see them:
 * from the destination MAC on, without FCS (RFC 826 for the ARP layout,
 * RFC 5227 for probes and announcements).
 */

#ifndef RNLAB_L02_ETHERNET_ARP_LOGIC_H
#define RNLAB_L02_ETHERNET_ARP_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNLAB_ETH_HDR_LEN     14u     /* Ziel-MAC, Quell-MAC, EtherType */
#define RNLAB_ETHERTYPE_ARP   0x0806u
#define RNLAB_ARP_LEN         28u     /* ARP fuer Ethernet/IPv4 */
#define RNLAB_ARP_HTYPE_ETH   1u
#define RNLAB_ARP_PTYPE_IPV4  0x0800u
#define RNLAB_ARP_OP_REQUEST  1u
#define RNLAB_ARP_OP_REPLY    2u

/** An ARP packet for Ethernet/IPv4, fields in host byte order; the
 *  addresses stay byte arrays in wire order (spa[0] is the first octet). */
typedef struct {
    uint16_t htype; /* Hardware Type, 1 = Ethernet */
    uint16_t ptype; /* Protocol Type, 0x0800 = IPv4 */
    uint8_t hlen;   /* Hardware Address Length, 6 */
    uint8_t plen;   /* Protocol Address Length, 4 */
    uint16_t oper;  /* Operation, 1 = Request, 2 = Reply */
    uint8_t sha[6]; /* Sender Hardware Address */
    uint8_t spa[4]; /* Sender Protocol Address */
    uint8_t tha[6]; /* Target Hardware Address */
    uint8_t tpa[4]; /* Target Protocol Address */
} rnlab_arp_packet_t;

typedef enum {
    RNLAB_ARP_OK = 0,
    RNLAB_ARP_ERR_TRUNCATED,   /* kuerzer als 14 B Ethernet + 28 B ARP */
    RNLAB_ARP_ERR_NOT_ARP,     /* EtherType ist nicht 0x0806 */
    RNLAB_ARP_ERR_UNSUPPORTED, /* nicht Ethernet/IPv4 (htype, ptype, hlen, plen) */
    RNLAB_ARP_ERR_BAD_OPER,    /* Operation weder Request (1) noch Reply (2) */
} rnlab_arp_result_t;

typedef enum {
    RNLAB_ARP_KIND_INVALID = 0, /* kein gueltiges Paket (NULL, falsche Operation) */
    RNLAB_ARP_KIND_REQUEST,     /* "who-has tpa tell spa" */
    RNLAB_ARP_KIND_REPLY,       /* "spa is-at sha" */
    RNLAB_ARP_KIND_GRATUITOUS,  /* spa == tpa: Ankuendigung der eigenen Adresse */
    RNLAB_ARP_KIND_PROBE,       /* Request mit spa 0.0.0.0: Adresskonflikt pruefen */
} rnlab_arp_kind_t;

/**
 * Parse the ARP packet in an Ethernet frame (from the destination MAC, no
 * FCS). Trailing bytes after the 28-byte ARP packet (Ethernet padding to
 * 60 B) are ignored. `out` is written only on RNLAB_ARP_OK; the checks run
 * in the order of the result enum, so a truncated frame reports TRUNCATED
 * even if its EtherType would not be ARP.
 */
rnlab_arp_result_t rnlab_parse_arp(const uint8_t* frame, size_t len, rnlab_arp_packet_t* out);

/** Classify a parsed packet. PROBE wins over GRATUITOUS (a probe has
 *  spa 0.0.0.0, never equal to a real tpa); GRATUITOUS covers both the
 *  request and the reply form. */
rnlab_arp_kind_t rnlab_arp_classify(const rnlab_arp_packet_t* packet);

/** Short German name of a kind for the console ("Request", ...). */
const char* rnlab_arp_kind_name(rnlab_arp_kind_t kind);

/* --- lab 02 watch: counters per direction --------------------------------- */

typedef struct {
    uint32_t requests;
    uint32_t replies;
    uint32_t gratuitous;
    uint32_t probes;
    uint32_t invalid; /* EtherType ARP, aber nicht auswertbar */
} rnlab_arp_counters_t;

/** Count one frame. Non-ARP frames are ignored (not counted as invalid). */
void rnlab_arp_count(rnlab_arp_counters_t* counters, const uint8_t* frame, size_t len);

/* --- Resolution time: own request -> matching reply ----------------------- */

typedef struct {
    bool pending;       /* Request gesendet, Reply noch offen */
    uint8_t target[4];  /* angefragte IPv4-Adresse */
    uint64_t sent_us;   /* Zeitpunkt des Requests */
    uint32_t count;     /* abgeschlossene Messungen */
    uint32_t last_us;
    uint32_t min_us;
    uint32_t max_us;
} rnlab_arp_timing_t;

void rnlab_arp_timing_reset(rnlab_arp_timing_t* timing);

/** A frame the board sends: a request (not a probe, not gratuitous) starts
 *  a measurement for its tpa; a newer request restarts it. */
void rnlab_arp_timing_on_tx(rnlab_arp_timing_t* timing, const uint8_t* frame, size_t len, uint64_t now_us);

/** A frame the board receives: a reply whose spa is the pending target
 *  completes the measurement. Returns true and stores the latency in
 *  *latency_us (may be NULL) exactly then. */
bool rnlab_arp_timing_on_rx(rnlab_arp_timing_t* timing, const uint8_t* frame, size_t len, uint64_t now_us,
                            uint32_t* latency_us);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L02_ETHERNET_ARP_LOGIC_H */

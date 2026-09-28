/*
 * CaDS Zero - rnlab L01 (Schichten und Kapselung): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l01.c links this file
 * directly on the host. Board integration lives in l01_schichten_kapselung.c.
 *
 * rnlab_decode_frame() peels one received or sent Ethernet frame layer by
 * layer (Ethernet II -> IPv4/ARP -> ICMP/UDP/TCP -> Nutzdaten) and records,
 * per layer, where its header starts and how long it is. rnlab_l01_overhead()
 * turns that into the protocol efficiency of the frame on the wire.
 */

#ifndef RNLAB_L01_SCHICHTEN_KAPSELUNG_LOGIC_H
#define RNLAB_L01_SCHICHTEN_KAPSELUNG_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ethernet II (IEEE 802.3), as the MAC hands it to the driver: from the
 * destination MAC, without preamble/SFD and without the FCS. */
#define RNLAB_L01_ETH_HDR_LEN      14u
#define RNLAB_L01_ETH_MIN_FRAME    60u /* shortest frame without FCS; shorter ones get padded */
#define RNLAB_L01_ETH_FCS_LEN      4u
#define RNLAB_L01_ETH_PREAMBLE_SFD 8u  /* 7 B preamble + 1 B start frame delimiter */
#define RNLAB_L01_ETH_IFG          12u /* inter-frame gap: 96 bit times of silence */

#define RNLAB_L01_ETHERTYPE_IPV4 0x0800u
#define RNLAB_L01_ETHERTYPE_ARP  0x0806u
#define RNLAB_L01_ETHERTYPE_VLAN 0x8100u
#define RNLAB_L01_ETHERTYPE_IPV6 0x86DDu

#define RNLAB_L01_IPV4_MIN_HDR 20u
#define RNLAB_L01_ARP_LEN      28u /* Ethernet/IPv4 ARP packet */
#define RNLAB_L01_ICMP_HDR_LEN 8u
#define RNLAB_L01_UDP_HDR_LEN  8u
#define RNLAB_L01_TCP_MIN_HDR  20u

#define RNLAB_L01_IPPROTO_ICMP 1u
#define RNLAB_L01_IPPROTO_TCP  6u
#define RNLAB_L01_IPPROTO_UDP  17u

/** How far rnlab_decode_frame() got. Everything before the failing layer is
 *  filled in, so a trace can still show the layers that were intact. */
typedef enum {
    RNLAB_L01_OK = 0,              /* every known layer decoded */
    RNLAB_L01_ERR_NOT_DECODED,     /* nothing decoded (initial value) */
    RNLAB_L01_ERR_TRUNC_ETH,       /* shorter than the 14-byte Ethernet header */
    RNLAB_L01_ERR_TRUNC_NET,       /* IPv4 header or ARP packet cut off */
    RNLAB_L01_ERR_BAD_NET,         /* IPv4 version != 4, IHL < 5 or total length < header */
    RNLAB_L01_ERR_TRUNC_UPPER,     /* ICMP/UDP/TCP header cut off */
    RNLAB_L01_ERR_BAD_UPPER,       /* TCP data offset < 5 or UDP length < 8 */
    RNLAB_L01_ERR_TRUNC_PAYLOAD,   /* headers fine, but IPv4 total length > frame */
} rnlab_l01_status_t;

/** What the EtherType announces (OSI layer 3 / TCP/IP Internetschicht). */
typedef enum {
    RNLAB_L01_NET_NONE = 0, /* not decoded */
    RNLAB_L01_NET_IPV4,
    RNLAB_L01_NET_ARP,
    RNLAB_L01_NET_OTHER,    /* IPv6, 802.1Q-VLAN, ... - recognised, not decoded */
} rnlab_l01_net_t;

/** What IPv4's protocol field announces. ICMP belongs to layer 3 but is
 *  carried inside IPv4 like a transport protocol. */
typedef enum {
    RNLAB_L01_UPPER_NONE = 0, /* not decoded (ARP, non-first fragment, ...) */
    RNLAB_L01_UPPER_ICMP,
    RNLAB_L01_UPPER_UDP,
    RNLAB_L01_UPPER_TCP,
    RNLAB_L01_UPPER_OTHER,    /* any other IP protocol - its bytes count as payload */
} rnlab_l01_upper_t;

/**
 * One decoded frame. Offsets are byte positions inside the frame, lengths
 * are header lengths as the frame says them; multi-byte fields are in host
 * byte order, addresses as bytes in wire order.
 */
typedef struct {
    rnlab_l01_status_t status;
    uint16_t frame_len; /* bytes handed to the decoder (no FCS) */

    /* Ethernet II - Sicherungsschicht (OSI 2) */
    uint8_t eth_dst[6];
    uint8_t eth_src[6];
    uint16_t ethertype;
    uint16_t eth_hdr_len; /* 14 once decoded, else 0 */

    /* Internetschicht (OSI 3) */
    rnlab_l01_net_t net;
    uint16_t net_off;     /* = eth_hdr_len */
    uint16_t net_hdr_len; /* IPv4: IHL * 4; ARP: the whole 28-byte packet */
    uint8_t ip_src[4];
    uint8_t ip_dst[4];
    uint8_t ip_ttl;
    uint8_t ip_proto;
    uint16_t ip_total_len;
    bool ip_fragment;     /* MF set or fragment offset != 0 */
    uint16_t arp_oper;    /* 1 = request, 2 = reply */

    /* ICMP / UDP / TCP */
    rnlab_l01_upper_t upper;
    uint16_t upper_off;
    uint16_t upper_hdr_len; /* ICMP 8, UDP 8, TCP data offset * 4 */
    uint16_t src_port;      /* UDP/TCP */
    uint16_t dst_port;
    uint8_t icmp_type;
    uint8_t icmp_code;
    uint8_t tcp_flags;      /* FIN 0x01 SYN 0x02 RST 0x04 PSH 0x08 ACK 0x10 URG 0x20 */

    /* Nutzdaten der obersten dekodierten Schicht (OSI 5-7 for UDP/TCP) */
    uint16_t payload_off;
    uint16_t payload_len;
    uint16_t pad_len; /* bytes behind the IPv4 packet / ARP packet: Ethernet padding */
} rnlab_frame_info_t;

/**
 * Student task (TODO(L01) in l01_schichten_kapselung_logic.c): decode `frame` (`len` bytes, from the
 * destination MAC, without FCS) into `info`.
 *
 * Always fills `info` as far as the frame allows and sets info->status;
 * never reads outside frame[0 .. len-1]. Returns true exactly when
 * info->status == RNLAB_L01_OK.
 */
bool rnlab_decode_frame(const uint8_t* frame, size_t len, rnlab_frame_info_t* info);

/** Byte counts and efficiency of one frame on a 100 Mbit/s Ethernet wire. */
typedef struct {
    uint32_t payload; /* Nutzdaten */
    uint32_t headers; /* Ethernet + IPv4/ARP + ICMP/UDP/TCP header bytes */
    uint32_t padding; /* filled up to 60 B by the sending MAC */
    uint32_t frame;   /* Ethernet frame incl. padding and FCS (>= 64 B) */
    uint32_t wire;    /* frame + preamble/SFD + inter-frame gap */
    uint32_t eff_frame_bp; /* payload / frame, in 0.01 % (basis points), rounded */
    uint32_t eff_wire_bp;  /* payload / wire, in 0.01 %, rounded */
} rnlab_l01_overhead_t;

/**
 * Student task (TODO(L01)): fill `out` for a frame that
 * rnlab_decode_frame() decoded (any status but RNLAB_L01_ERR_NOT_DECODED and
 * RNLAB_L01_ERR_TRUNC_ETH). The frame counts as the MAC sends it: at least
 * 60 B plus 4 B FCS; on the wire 8 B preamble/SFD and 12 B gap on top.
 * Returns false (and zeroes `out`) when `info` has no decoded Ethernet header.
 */
bool rnlab_l01_overhead(const rnlab_frame_info_t* info, rnlab_l01_overhead_t* out);

/** The same numbers for an ICMP echo (ping) with `icmp_payload` bytes of
 *  data - what `ping -s <n>` sends - built on rnlab_l01_overhead(). */
void rnlab_l01_ping_overhead(uint32_t icmp_payload, rnlab_l01_overhead_t* out);

/** "12.34 %" from basis points (0.01 %). Returns the characters written. */
size_t rnlab_l01_format_percent(char* out, size_t size, uint32_t basis_points);

/** Short names for the trace: "IPv4", "ARP", "ICMP", "Echo Request", ... */
const char* rnlab_l01_net_name(rnlab_l01_net_t net);
const char* rnlab_l01_upper_name(rnlab_l01_upper_t upper);
const char* rnlab_l01_icmp_type_name(uint8_t type);
const char* rnlab_l01_status_text(rnlab_l01_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L01_SCHICHTEN_KAPSELUNG_LOGIC_H */

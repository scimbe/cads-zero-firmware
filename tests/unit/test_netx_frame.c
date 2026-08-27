/*
 * Golden-byte coverage for modules/netx frame builders (M9).
 *
 * The builders are pure functions over caller-owned buffers with no HAL/lwIP
 * deps, so they are tested on the host exactly the way the passive detectors
 * in modules/toolbox are (test_arpwatch.c et al.). Two kinds of assertion:
 *
 *  - Exact byte/length comparison for the deterministic frames (Ethernet,
 *    single/double VLAN, ARP reply, gratuitous ARP): no checksums, so every
 *    byte is pinned.
 *  - For the frames that carry a one's-complement checksum (IPv4, UDP, TCP
 *    RST, ICMPv6 RA): the fixed fields (addresses, ethertype, flags, lengths,
 *    opcodes) are pinned byte-for-byte, and each checksum is verified by the
 *    defining property of a correct Internet checksum - folding the sum of
 *    the bytes it covers (including the checksum field itself) yields 0xFFFF
 *    (RFC 1071). This proves the builder computed a valid checksum without
 *    hand-encoding a magic value that could drift with a typo.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/netx/frame.h"

void setUp(void) {
}

void tearDown(void) {
}

static const uint8_t MAC_SRC[6] = {0x02, 0xCA, 0xD5, 0x5E, 0x00, 0x01};
static const uint8_t MAC_DST[6] = {0x02, 0xCA, 0xD5, 0x5E, 0x00, 0x99};
#define IP_HOST 0xC0A82101u /* 192.168.33.1 in host order */

/* RFC 1071 one's-complement sum over a byte range, returned UNfolded. Mirrors
 * the builder's own sum_bytes()/fold16() but is independently written here so
 * a bug in either side would show up as a mismatch rather than both agreeing
 * on the same wrong answer. */
static uint32_t sum_bytes(const uint8_t* p, size_t len) {
    uint32_t sum = 0u;
    size_t i = 0u;
    while((len - i) >= 2u) {
        sum += ((uint32_t)p[i] << 8) | (uint32_t)p[i + 1];
        i += 2u;
    }
    if(i < len) sum += (uint32_t)p[i] << 8;
    return sum;
}

static uint16_t fold16(uint32_t sum) {
    while(sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFu);
}

/* A correct Internet checksum, with the checksum field included in its own
 * sum, folds (uncomplemented) to 0xFFFF - RFC 1071's "the result should be
 * all 1s". fold16() here complements (it is the same shape the builder uses
 * to *produce* a checksum), so a verified checksum comes back as 0x0000. */
static bool checksum_valid(const uint8_t* p, size_t len) {
    return fold16(sum_bytes(p, len)) == 0x0000u;
}

/* --- Ethernet ------------------------------------------------------------- */

static void test_eth_layout(void) {
    uint8_t out[64];
    uint16_t n = cads_netx_build_eth(out, sizeof(out),
        MAC_DST, MAC_SRC, 0x0800u, NULL, 0u);
    TEST_ASSERT_EQUAL_UINT16(14u, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_DST, out + 0u, 6u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_SRC, out + 6u, 6u);
    TEST_ASSERT_EQUAL_UINT8(0x08u, out[12u]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[13u]);
}

static void test_eth_overflow_is_silent_noop(void) {
    uint8_t out[10]; /* too small for even the 14-byte header */
    uint16_t n = cads_netx_build_eth(out, sizeof(out),
        MAC_DST, MAC_SRC, 0x0800u, NULL, 0u);
    TEST_ASSERT_EQUAL_UINT16(0u, n);
}

/* --- VLAN (single + QinQ) ------------------------------------------------- */

static void test_vlan_single_tag_layout(void) {
    uint8_t out[64];
    uint16_t n = cads_netx_build_vlan(out, sizeof(out),
        MAC_DST, MAC_SRC, 100u, 0x0800u, NULL, 0u);
    /* 14 eth (TPID 0x8100 in the ethertype slot) + 2 TCI + 2 ethertype = 18.
     * An 802.1Q tag is TPID-in-ethertype-slot + TCI + real ethertype - NOT a
     * second 4-byte TPID+TCI (which would parse as VID 0x100 + garbage type). */
    TEST_ASSERT_EQUAL_UINT16(18u, n);
    TEST_ASSERT_EQUAL_UINT8(0x81u, out[12u]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[13u]); /* TPID 0x8100 in ethertype slot */
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[14u]);
    TEST_ASSERT_EQUAL_UINT8(0x64u, out[15u]); /* TCI: VID 100, PCP/DEI zero */
    TEST_ASSERT_EQUAL_UINT8(0x08u, out[16u]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[17u]); /* inner ethertype 0x0800 */
}

static void test_vlan_vid_masks_to_12_bits(void) {
    uint8_t out[64];
    uint16_t n = cads_netx_build_vlan(out, sizeof(out),
        MAC_DST, MAC_SRC, 0x1FFFu, 0x0800u, NULL, 0u);
    TEST_ASSERT_EQUAL_UINT16(18u, n);
    TEST_ASSERT_EQUAL_UINT8(0x0Fu, out[14u]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[15u]); /* 0x0FFF, PCP/DEI stripped */
}

static void test_vlan_qinq_layout(void) {
    uint8_t out[64];
    uint16_t n = cads_netx_build_vlan_qinq(out, sizeof(out),
        MAC_DST, MAC_SRC, 10u, 20u, 0x0800u, NULL, 0u);
    /* 14 eth + 2 outer TCI + 2 inner TPID + 2 inner TCI + 2 ethertype = 22 */
    TEST_ASSERT_EQUAL_UINT16(22u, n);
    TEST_ASSERT_EQUAL_UINT8(0x81u, out[12u]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[13u]); /* outer TPID 0x8100 */
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[14u]);
    TEST_ASSERT_EQUAL_UINT8(0x0Au, out[15u]); /* outer TCI: VID 10 */
    TEST_ASSERT_EQUAL_UINT8(0x81u, out[16u]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[17u]); /* inner TPID 0x8100 */
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[18u]);
    TEST_ASSERT_EQUAL_UINT8(0x14u, out[19u]); /* inner TCI: VID 20 */
    TEST_ASSERT_EQUAL_UINT8(0x08u, out[20u]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[21u]); /* carried ethertype 0x0800 */
}

/* --- ARP ------------------------------------------------------------------ */

static void test_arp_reply_layout(void) {
    uint8_t out[64];
    uint16_t n = cads_netx_build_arp_reply(out, sizeof(out),
        MAC_DST, MAC_SRC, MAC_SRC, IP_HOST, 0xC0A82102u);
    TEST_ASSERT_EQUAL_UINT16(42u, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_DST, out + 0u, 6u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_SRC, out + 6u, 6u);
    TEST_ASSERT_EQUAL_UINT8(0x08u, out[12u]);
    TEST_ASSERT_EQUAL_UINT8(0x06u, out[13u]); /* ARP */
    uint8_t* arp = out + 14u;
    TEST_ASSERT_EQUAL_UINT8(0x00u, arp[0u]); TEST_ASSERT_EQUAL_UINT8(0x01u, arp[1u]); /* htype */
    TEST_ASSERT_EQUAL_UINT8(0x08u, arp[2u]); TEST_ASSERT_EQUAL_UINT8(0x00u, arp[3u]); /* ptype */
    TEST_ASSERT_EQUAL_UINT8(6u, arp[4u]); /* hlen */
    TEST_ASSERT_EQUAL_UINT8(4u, arp[5u]); /* plen */
    TEST_ASSERT_EQUAL_UINT8(0x00u, arp[6u]); TEST_ASSERT_EQUAL_UINT8(0x02u, arp[7u]); /* REPLY */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_SRC, arp + 8u, 6u); /* sender mac */
    TEST_ASSERT_EQUAL_UINT8(0xC0u, arp[14u]); /* sender ip 192.168.33.1 */
    TEST_ASSERT_EQUAL_UINT8(0xA8u, arp[15u]);
    TEST_ASSERT_EQUAL_UINT8(0x21u, arp[16u]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, arp[17u]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_SRC, arp + 18u, 6u); /* target mac = src */
    TEST_ASSERT_EQUAL_UINT8(0xC0u, arp[24u]); /* target ip 192.168.33.2 */
    TEST_ASSERT_EQUAL_UINT8(0xA8u, arp[25u]);
    TEST_ASSERT_EQUAL_UINT8(0x21u, arp[26u]);
    TEST_ASSERT_EQUAL_UINT8(0x02u, arp[27u]);
}

static void test_arp_gratuitous_is_broadcast_reply_with_equal_ips(void) {
    uint8_t out[64];
    uint16_t n = cads_netx_build_arp_gratuitous(out, sizeof(out), MAC_SRC, IP_HOST);
    TEST_ASSERT_EQUAL_UINT16(42u, n);
    /* destination MAC is broadcast */
    for(int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8(0xFFu, out[i]);
    /* opcode is REPLY */
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[20u]);
    TEST_ASSERT_EQUAL_UINT8(0x02u, out[21u]);
    /* sender IP == target IP (gratuitous) */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(out + 14u + 14u, out + 14u + 24u, 4u);
    /* sender MAC == src MAC */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_SRC, out + 14u + 8u, 6u);
}

/* --- IPv4 / UDP / TCP (checksum validity) -------------------------------- */

static void test_ipv4_header_and_checksum(void) {
    static const uint8_t payload[4] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
    uint8_t out[64];
    uint16_t n = cads_netx_build_ipv4(out, sizeof(out),
        MAC_DST, MAC_SRC, 17u, IP_HOST, 0xC0A82102u, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT16(14u + 20u + 4u, n);
    TEST_ASSERT_EQUAL_UINT8(0x08u, out[12u]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[13u]); /* ethertype IPv4 */
    uint8_t* ip = out + 14u;
    TEST_ASSERT_EQUAL_UINT8(0x45u, ip[0u]); /* v4, IHL 5 */
    TEST_ASSERT_EQUAL_UINT8(0x00u, ip[1u]); /* DSCP/ECN */
    TEST_ASSERT_EQUAL_UINT8(0x00u, ip[2u]); TEST_ASSERT_EQUAL_UINT8(0x18u, ip[3u]); /* total len 24 */
    TEST_ASSERT_EQUAL_UINT8(0x40u, ip[6u]); TEST_ASSERT_EQUAL_UINT8(0x00u, ip[7u]); /* DF */
    TEST_ASSERT_EQUAL_UINT8(64u, ip[8u]); /* TTL */
    TEST_ASSERT_EQUAL_UINT8(17u, ip[9u]); /* proto UDP */
    /* The IPv4 header checksum covers exactly the 20-byte header (including
     * its own checksum field), so a correct one folds to 0xFFFF. */
    TEST_ASSERT_TRUE(checksum_valid(ip, 20u));
    /* Payload copied verbatim after the header. */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out + 14u + 20u, sizeof(payload));
}

static void test_udp_checksums_both_valid(void) {
    static const uint8_t payload[5] = {1u, 2u, 3u, 4u, 5u};
    uint8_t out[64];
    uint16_t n = cads_netx_build_udp(out, sizeof(out),
        MAC_DST, MAC_SRC, IP_HOST, 0xC0A82102u, 0x1234u, 5678u, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT16(14u + 20u + 8u + 5u, n);
    uint8_t* ip = out + 14u;
    uint8_t* udp = out + 14u + 20u;
    TEST_ASSERT_EQUAL_UINT8(0x12u, udp[0u]); TEST_ASSERT_EQUAL_UINT8(0x34u, udp[1u]); /* src port */
    TEST_ASSERT_EQUAL_UINT8(0x16u, udp[2u]); TEST_ASSERT_EQUAL_UINT8(0x2Eu, udp[3u]); /* dst port 5678 */
    TEST_ASSERT_EQUAL_UINT8(0x00u, udp[4u]); TEST_ASSERT_EQUAL_UINT8(0x0Du, udp[5u]); /* udp len 13 */
    /* Both the IP header checksum and the UDP checksum must be valid. The UDP
     * checksum covers the IPv4 pseudo-header + UDP header + payload; we verify
     * it by reconstructing that span and folding, which is exactly what a
     * receiver's CHECKSUM_CHECK_UDP does. */
    TEST_ASSERT_TRUE(checksum_valid(ip, 20u));
    uint32_t s = 0u;
    s += (IP_HOST >> 16) & 0xFFFFu;
    s += IP_HOST & 0xFFFFu;
    s += (0xC0A82102u >> 16) & 0xFFFFu;
    s += 0xC0A82102u & 0xFFFFu;
    s += 17u; /* proto */
    s += 13u; /* udp length */
    s += sum_bytes(udp, 8u);
    s += sum_bytes(udp + 8u, 5u);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, fold16(s));
}

static void test_tcp_rst_has_only_rst_flag_and_valid_checksums(void) {
    uint8_t out[64];
    uint16_t n = cads_netx_build_tcp_rst(out, sizeof(out),
        MAC_DST, MAC_SRC, IP_HOST, 0xC0A82102u, 0x1234u, 5678u, 0x00010002u);
    TEST_ASSERT_EQUAL_UINT16(14u + 20u + 20u, n); /* header-only RST */
    uint8_t* tcp = out + 14u + 20u;
    TEST_ASSERT_EQUAL_UINT8(0x12u, tcp[0u]); TEST_ASSERT_EQUAL_UINT8(0x34u, tcp[1u]); /* src port */
    TEST_ASSERT_EQUAL_UINT8(0x16u, tcp[2u]); TEST_ASSERT_EQUAL_UINT8(0x2Eu, tcp[3u]); /* dst port */
    TEST_ASSERT_EQUAL_UINT8(0x00u, tcp[4u]); TEST_ASSERT_EQUAL_UINT8(0x01u, tcp[5u]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, tcp[6u]); TEST_ASSERT_EQUAL_UINT8(0x02u, tcp[7u]); /* seq 0x00010002 */
    TEST_ASSERT_EQUAL_UINT8(0x50u, tcp[12u]); /* data offset 5 */
    TEST_ASSERT_EQUAL_UINT8(0x04u, tcp[13u]); /* flags: RST only */
    /* IP + TCP checksums both valid. */
    TEST_ASSERT_TRUE(checksum_valid(out + 14u, 20u));
    uint32_t s = 0u;
    s += (IP_HOST >> 16) & 0xFFFFu;
    s += IP_HOST & 0xFFFFu;
    s += (0xC0A82102u >> 16) & 0xFFFFu;
    s += 0xC0A82102u & 0xFFFFu;
    s += 6u;  /* proto */
    s += 20u; /* tcp length */
    s += sum_bytes(tcp, 20u);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, fold16(s));
}

/* --- ICMPv6 RA ----------------------------------------------------------- */

static void test_icmpv6_ra_addresses_and_checksum(void) {
    static const uint8_t prefix[16] = {
        0xFD, 0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t out[128];
    uint16_t n = cads_netx_build_icmpv6_ra(out, sizeof(out),
        MAC_SRC, prefix, 1800u, 1800u, 1500u);
    /* 14 eth + 40 ipv6 + 16 RA + 32 prefix + 8 MTU = 110 */
    TEST_ASSERT_EQUAL_UINT16(110u, n);
    /* Ethernet dst is the all-nodes multicast 33:33::1. */
    TEST_ASSERT_EQUAL_UINT8(0x33u, out[0u]);
    TEST_ASSERT_EQUAL_UINT8(0x33u, out[1u]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[2u]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, out[5u]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_SRC, out + 6u, 6u);
    TEST_ASSERT_EQUAL_UINT8(0x86u, out[12u]);
    TEST_ASSERT_EQUAL_UINT8(0xDDu, out[13u]); /* ethertype IPv6 */
    uint8_t* ip6 = out + 14u;
    TEST_ASSERT_EQUAL_UINT8(0x60u, ip6[0u]); /* version 6 */
    TEST_ASSERT_EQUAL_UINT8(58u, ip6[6u]);  /* next header: ICMPv6 */
    TEST_ASSERT_EQUAL_UINT8(255u, ip6[7u]); /* hop limit 255 (RA requirement) */
    /* Source is fe80::EUI-64(MAC). */
    TEST_ASSERT_EQUAL_UINT8(0xFEu, ip6[8u]);
    TEST_ASSERT_EQUAL_UINT8(0x80u, ip6[9u]);
    TEST_ASSERT_EQUAL_UINT8(MAC_SRC[0u] ^ 0x02u, ip6[16u]); /* EUI-64, U/L flipped */
    TEST_ASSERT_EQUAL_UINT8(MAC_SRC[5u], ip6[23u]);
    /* Destination is ff02::1. */
    TEST_ASSERT_EQUAL_UINT8(0xFFu, ip6[24u]);
    TEST_ASSERT_EQUAL_UINT8(0x02u, ip6[25u]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, ip6[39u]);
    uint8_t* icmp = out + 14u + 40u;
    TEST_ASSERT_EQUAL_UINT8(134u, icmp[0u]); /* type RA */
    TEST_ASSERT_EQUAL_UINT8(64u, icmp[4u]);  /* cur hop limit */
    TEST_ASSERT_EQUAL_UINT8(0xC0u, icmp[16u + 3u]); /* prefix info L+A flags */
    TEST_ASSERT_EQUAL_UINT8(64u, icmp[16u + 2u]);  /* prefix length */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(prefix, icmp + 16u + 16u, 16u);
    TEST_ASSERT_EQUAL_UINT8(5u, icmp[16u + 32u + 0u]); /* MTU option type */
    TEST_ASSERT_EQUAL_UINT8(1u, icmp[16u + 32u + 1u]); /* MTU option length */

    /* ICMPv6 checksum over the IPv6 pseudo-header + ICMPv6 message. */
    uint16_t icmp_len = n - (14u + 40u);
    uint32_t s = 0u;
    for(int i = 0; i < 16; i += 2) s += ((uint32_t)ip6[8u + i] << 8) | (uint32_t)ip6[8u + i + 1];
    for(int i = 0; i < 16; i += 2) s += ((uint32_t)ip6[24u + i] << 8) | (uint32_t)ip6[24u + i + 1];
    s += (uint32_t)icmp_len;
    s += 58u;
    s += sum_bytes(icmp, icmp_len);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, fold16(s));
}

static void test_icmpv6_ra_mtu_zero_suppresses_mtu_option(void) {
    static const uint8_t prefix[16] = {0};
    uint8_t out[128];
    uint16_t n = cads_netx_build_icmpv6_ra(out, sizeof(out),
        MAC_SRC, prefix, 1800u, 1800u, 0u);
    /* 14 + 40 + 16 RA + 32 prefix, no MTU option = 102 */
    TEST_ASSERT_EQUAL_UINT16(102u, n);
}

/* --- ARP REQUEST (VLAN-hop probe payload) -------------------------------- */

static void test_arp_request_layout(void) {
    static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t out[64];
    uint16_t n = cads_netx_build_arp_request(out, sizeof(out),
        bcast, MAC_SRC, MAC_SRC, IP_HOST, 0xC0A82164u);
    TEST_ASSERT_EQUAL_UINT16(42u, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bcast, out + 0u, 6u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_SRC, out + 6u, 6u);
    TEST_ASSERT_EQUAL_UINT8(0x08u, out[12u]);
    TEST_ASSERT_EQUAL_UINT8(0x06u, out[13u]); /* ARP */
    uint8_t* arp = out + 14u;
    TEST_ASSERT_EQUAL_UINT8(0x00u, arp[0u]); TEST_ASSERT_EQUAL_UINT8(0x01u, arp[1u]); /* htype */
    TEST_ASSERT_EQUAL_UINT8(0x08u, arp[2u]); TEST_ASSERT_EQUAL_UINT8(0x00u, arp[3u]); /* ptype */
    TEST_ASSERT_EQUAL_UINT8(6u, arp[4u]); TEST_ASSERT_EQUAL_UINT8(4u, arp[5u]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, arp[6u]); TEST_ASSERT_EQUAL_UINT8(0x01u, arp[7u]); /* REQUEST */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_SRC, arp + 8u, 6u); /* sender mac */
    TEST_ASSERT_EQUAL_UINT8(0xC0u, arp[14u]); /* sender ip 192.168.33.1 */
    TEST_ASSERT_EQUAL_UINT8(0xA8u, arp[15u]);
    TEST_ASSERT_EQUAL_UINT8(0x21u, arp[16u]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, arp[17u]);
    for(int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8(0u, arp[18u + i]); /* target MAC zero */
    TEST_ASSERT_EQUAL_UINT8(0xC0u, arp[24u]); /* target ip 192.168.33.100 */
    TEST_ASSERT_EQUAL_UINT8(0xA8u, arp[25u]);
    TEST_ASSERT_EQUAL_UINT8(0x21u, arp[26u]);
    TEST_ASSERT_EQUAL_UINT8(0x64u, arp[27u]);
}

/* --- CoAP GET /.well-known/core (reverse beacon) -------------------------- */

static void test_coap_get_well_known(void) {
    uint8_t out[32];
    uint16_t n = cads_netx_build_coap_get(out, sizeof(out), 0x0001u, ".well-known/core");
    /* 4 header + 1 option byte + 11 (.well-known) + 1 option byte + 4 (core) = 21 */
    TEST_ASSERT_EQUAL_UINT16(21u, n);
    TEST_ASSERT_EQUAL_UINT8(0x40u, out[0u]); /* Ver1 CON TKL0 */
    TEST_ASSERT_EQUAL_UINT8(0x01u, out[1u]); /* GET */
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[2u]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, out[3u]); /* Message ID 1 */
    TEST_ASSERT_EQUAL_UINT8(0xBBu, out[4u]); /* delta 11, len 11 */
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t*)".well-known", out + 5u, 11u);
    TEST_ASSERT_EQUAL_UINT8(0x04u, out[16u]); /* delta 0, len 4 */
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t*)"core", out + 17u, 4u);
}

static void test_coap_get_no_path_is_bare_header(void) {
    /* A "/" or empty path yields just the 4-byte header - no URI-Path options.
     * Still a valid CoAP message (an empty-path GET). */
    uint8_t out[16];
    uint16_t n = cads_netx_build_coap_get(out, sizeof(out), 0xABCDu, "/");
    TEST_ASSERT_EQUAL_UINT16(4u, n);
    TEST_ASSERT_EQUAL_UINT8(0x40u, out[0u]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, out[1u]);
    TEST_ASSERT_EQUAL_UINT8(0xABu, out[2u]);
    TEST_ASSERT_EQUAL_UINT8(0xCDu, out[3u]);
}

static void test_coap_get_long_segment_is_build_error(void) {
    /* Segment > 12 needs the extended-length form, which this builder does
     * not emit - it returns 0 rather than a malformed option. */
    uint8_t out[64];
    uint16_t n = cads_netx_build_coap_get(out, sizeof(out), 1u, "this-is-too-long");
    TEST_ASSERT_EQUAL_UINT16(0u, n);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_eth_layout);
    RUN_TEST(test_eth_overflow_is_silent_noop);
    RUN_TEST(test_vlan_single_tag_layout);
    RUN_TEST(test_vlan_vid_masks_to_12_bits);
    RUN_TEST(test_vlan_qinq_layout);
    RUN_TEST(test_arp_reply_layout);
    RUN_TEST(test_arp_gratuitous_is_broadcast_reply_with_equal_ips);
    RUN_TEST(test_arp_request_layout);
    RUN_TEST(test_ipv4_header_and_checksum);
    RUN_TEST(test_udp_checksums_both_valid);
    RUN_TEST(test_tcp_rst_has_only_rst_flag_and_valid_checksums);
    RUN_TEST(test_icmpv6_ra_addresses_and_checksum);
    RUN_TEST(test_icmpv6_ra_mtu_zero_suppresses_mtu_option);
    RUN_TEST(test_coap_get_well_known);
    RUN_TEST(test_coap_get_no_path_is_bare_header);
    RUN_TEST(test_coap_get_long_segment_is_build_error);
    return UNITY_END();
}
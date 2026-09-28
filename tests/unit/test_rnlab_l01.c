/* rnlab L01 (Schichten und Kapselung): host tests for l01_schichten_kapselung_logic.c
 * - the layer-by-layer frame decoder and the protocol-efficiency arithmetic.
 * ctest label rnlab-L01; red on the student stub until the TODO(L01)s are done.
 *
 * The cap_* frames are real: captured with `tcpdump -i en13 -xx` on the Mac
 * (192.168.33.1, a0:ce:c8:61:5d:08) talking to the ITS-BRD (192.168.33.99,
 * random MAC de:5f:af:a4:8a:6a that boot) on 2026-09-28 - ping -s 64/-s 0,
 * a UDP datagram to a closed port (-> ICMP port unreachable) and a telnet
 * session to the lab CLI on TCP 4242. Frames sent by the Mac were captured
 * before its MAC padded them (42 B), the board's arrive padded (60 B). */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "l01_schichten_kapselung_logic.h"

/* ethertype ARP (0x0806), length 42: Request who-has 192.168.33.99 (de:5f:af:a4:8a:6a) tell 192.1 */
static const uint8_t cap_arp_request[42] = {
    0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08,
    0x08, 0x06, 0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0xa0, 0xce,
    0xc8, 0x61, 0x5d, 0x08, 0xc0, 0xa8, 0x21, 0x01, 0xde, 0x5f, 0xaf, 0xa4,
    0x8a, 0x6a, 0xc0, 0xa8, 0x21, 0x63,
};
/* ethertype ARP (0x0806), length 60: Reply 192.168.33.99 is-at de:5f:af:a4:8a:6a, length 46 */
static const uint8_t cap_arp_reply_padded[60] = {
    0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08, 0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a,
    0x08, 0x06, 0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x02, 0xde, 0x5f,
    0xaf, 0xa4, 0x8a, 0x6a, 0xc0, 0xa8, 0x21, 0x63, 0xa0, 0xce, 0xc8, 0x61,
    0x5d, 0x08, 0xc0, 0xa8, 0x21, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
/* ethertype IPv4 (0x0800), length 106: 192.168.33.1 > 192.168.33.99: ICMP echo request, id 24382, */
static const uint8_t cap_icmp_echo_req_64[106] = {
    0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08,
    0x08, 0x00, 0x45, 0x00, 0x00, 0x5c, 0x1d, 0xef, 0x00, 0x00, 0x40, 0x01,
    0x98, 0xfd, 0xc0, 0xa8, 0x21, 0x01, 0xc0, 0xa8, 0x21, 0x63, 0x08, 0x00,
    0x86, 0x53, 0x5f, 0x3e, 0x00, 0x00, 0x6a, 0xba, 0x64, 0xea, 0x00, 0x0e,
    0x6a, 0xc7, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
    0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
    0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
};
/* ethertype IPv4 (0x0800), length 106: 192.168.33.99 > 192.168.33.1: ICMP echo reply, id 24382, s */
static const uint8_t cap_icmp_echo_rep_64[106] = {
    0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08, 0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a,
    0x08, 0x00, 0x45, 0x00, 0x00, 0x5c, 0x1d, 0xef, 0x00, 0x00, 0xff, 0x01,
    0xd9, 0xfc, 0xc0, 0xa8, 0x21, 0x63, 0xc0, 0xa8, 0x21, 0x01, 0x00, 0x00,
    0x8e, 0x53, 0x5f, 0x3e, 0x00, 0x00, 0x6a, 0xba, 0x64, 0xea, 0x00, 0x0e,
    0x6a, 0xc7, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
    0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
    0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
};
/* ethertype IPv4 (0x0800), length 42: 192.168.33.1 > 192.168.33.99: ICMP echo request, id 24638,  */
static const uint8_t cap_icmp_echo_req_0[42] = {
    0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08,
    0x08, 0x00, 0x45, 0x00, 0x00, 0x1c, 0x31, 0xcb, 0x00, 0x00, 0x40, 0x01,
    0x85, 0x61, 0xc0, 0xa8, 0x21, 0x01, 0xc0, 0xa8, 0x21, 0x63, 0x08, 0x00,
    0x97, 0xc1, 0x60, 0x3e, 0x00, 0x00,
};
/* ethertype IPv4 (0x0800), length 60: 192.168.33.99 > 192.168.33.1: ICMP echo reply, id 24638, se */
static const uint8_t cap_icmp_echo_rep_0[60] = {
    0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08, 0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a,
    0x08, 0x00, 0x45, 0x00, 0x00, 0x1c, 0x31, 0xcb, 0x00, 0x00, 0xff, 0x01,
    0xc6, 0x60, 0xc0, 0xa8, 0x21, 0x63, 0xc0, 0xa8, 0x21, 0x01, 0x00, 0x00,
    0x9f, 0xc1, 0x60, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
/* ethertype IPv4 (0x0800), length 60: 192.168.33.1.60823 > 192.168.33.99.7007:  [|rx] (18) */
static const uint8_t cap_udp_18[60] = {
    0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08,
    0x08, 0x00, 0x45, 0x00, 0x00, 0x2e, 0xe8, 0x83, 0x00, 0x00, 0x40, 0x11,
    0xce, 0x86, 0xc0, 0xa8, 0x21, 0x01, 0xc0, 0xa8, 0x21, 0x63, 0xed, 0x97,
    0x1b, 0x5f, 0x00, 0x1a, 0xb6, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
    0x5c, 0x8a, 0xa6, 0x63, 0x79, 0xd3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
/* ethertype IPv4 (0x0800), length 70: 192.168.33.99 > 192.168.33.1: ICMP 192.168.33.99 udp port 7 */
static const uint8_t cap_icmp_port_unreach[70] = {
    0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08, 0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a,
    0x08, 0x00, 0x45, 0x00, 0x00, 0x38, 0x00, 0x5b, 0x00, 0x00, 0xff, 0x01,
    0xf7, 0xb4, 0xc0, 0xa8, 0x21, 0x63, 0xc0, 0xa8, 0x21, 0x01, 0x03, 0x03,
    0x3d, 0xa5, 0x00, 0x00, 0x00, 0x00, 0x45, 0x00, 0x00, 0x2e, 0xe8, 0x83,
    0x00, 0x00, 0x40, 0x11, 0xce, 0x86, 0xc0, 0xa8, 0x21, 0x01, 0xc0, 0xa8,
    0x21, 0x63, 0xed, 0x97, 0x1b, 0x5f, 0x00, 0x1a, 0xb6, 0x46,
};
/* ethertype IPv4 (0x0800), length 78: 192.168.33.1.63337 > 192.168.33.99.4242: Flags [SEW], seq 1 */
static const uint8_t cap_tcp_syn[78] = {
    0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08,
    0x08, 0x00, 0x45, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x00, 0x40, 0x06,
    0x77, 0x03, 0xc0, 0xa8, 0x21, 0x01, 0xc0, 0xa8, 0x21, 0x63, 0xf7, 0x69,
    0x10, 0x92, 0x3d, 0x81, 0x3a, 0x34, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xc2,
    0xff, 0xff, 0xc5, 0xae, 0x00, 0x00, 0x02, 0x04, 0x05, 0xb4, 0x01, 0x03,
    0x03, 0x06, 0x01, 0x01, 0x08, 0x0a, 0x5d, 0xc5, 0xcf, 0x61, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x02, 0x00, 0x00,
};
/* ethertype IPv4 (0x0800), length 60: 192.168.33.99.4242 > 192.168.33.1.63337: Flags [S.], seq 71 */
static const uint8_t cap_tcp_synack[60] = {
    0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08, 0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a,
    0x08, 0x00, 0x45, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0xff, 0x06,
    0xf8, 0x16, 0xc0, 0xa8, 0x21, 0x63, 0xc0, 0xa8, 0x21, 0x01, 0x10, 0x92,
    0xf7, 0x69, 0x00, 0x00, 0x1b, 0xe2, 0x3d, 0x81, 0x3a, 0x35, 0x60, 0x12,
    0x10, 0xc0, 0x2b, 0xa9, 0x00, 0x00, 0x02, 0x04, 0x02, 0x18, 0x00, 0x00,
};
/* ethertype IPv4 (0x0800), length 93: 192.168.33.99.4242 > 192.168.33.1.63337: Flags [P.], seq 1: */
static const uint8_t cap_tcp_psh_39[93] = {
    0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08, 0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a,
    0x08, 0x00, 0x45, 0x00, 0x00, 0x4f, 0x00, 0x5c, 0x00, 0x00, 0xff, 0x06,
    0xf7, 0x97, 0xc0, 0xa8, 0x21, 0x63, 0xc0, 0xa8, 0x21, 0x01, 0x10, 0x92,
    0xf7, 0x69, 0x00, 0x00, 0x1b, 0xe3, 0x3d, 0x81, 0x3a, 0x35, 0x50, 0x18,
    0x10, 0xc0, 0x73, 0xb6, 0x00, 0x00, 0x43, 0x61, 0x44, 0x53, 0x20, 0x5a,
    0x65, 0x72, 0x6f, 0x20, 0x43, 0x4c, 0x49, 0x20, 0x2d, 0x20, 0x27, 0x68,
    0x65, 0x6c, 0x70, 0x27, 0x20, 0x66, 0x6f, 0x72, 0x20, 0x63, 0x6f, 0x6d,
    0x6d, 0x61, 0x6e, 0x64, 0x73, 0x0d, 0x0a, 0x3e, 0x20,
};

static const uint8_t mac_host[6] = {0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08};
static const uint8_t mac_board[6] = {0xde, 0x5f, 0xaf, 0xa4, 0x8a, 0x6a};
static const uint8_t ip_host[4] = {192, 168, 33, 1};
static const uint8_t ip_board[4] = {192, 168, 33, 99};

void setUp(void) {
}

void tearDown(void) {
}

/* Decode from an exactly-sized heap copy, so a sanitizer (or a lucky crash)
 * notices any read past the end of the frame. */
static bool decode_copy(const uint8_t* frame, size_t len, rnlab_frame_info_t* info) {
    uint8_t* copy = malloc(len > 0u ? len : 1u);
    TEST_ASSERT_NOT_NULL(copy);
    if(len > 0u) memcpy(copy, frame, len);
    bool ok = rnlab_decode_frame(copy, len, info);
    free(copy);
    return ok;
}

/* A mutable copy of a captured frame for the "what if this byte were
 * different" tests. */
static uint8_t scratch[128];

static uint8_t* mutable_copy(const uint8_t* frame, size_t len) {
    TEST_ASSERT_TRUE(len <= sizeof(scratch));
    memcpy(scratch, frame, len);
    return scratch;
}

/* Every decoded byte belongs to exactly one place: a header, the payload,
 * or the padding behind the IPv4/ARP packet. */
static void assert_bytes_add_up(const rnlab_frame_info_t* info) {
    TEST_ASSERT_EQUAL_UINT32(info->frame_len, (uint32_t)info->eth_hdr_len + info->net_hdr_len +
                                                  info->upper_hdr_len + info->payload_len +
                                                  info->pad_len);
}

/* --- Ethernet II + ARP ---------------------------------------------------- */

static void test_arp_request(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(cap_arp_request, sizeof(cap_arp_request), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_OK, info.status);
    TEST_ASSERT_EQUAL_UINT16(42u, info.frame_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac_board, info.eth_dst, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac_host, info.eth_src, 6);
    TEST_ASSERT_EQUAL_HEX16(0x0806u, info.ethertype);
    TEST_ASSERT_EQUAL_UINT16(14u, info.eth_hdr_len);
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_NET_ARP, info.net);
    TEST_ASSERT_EQUAL_UINT16(14u, info.net_off);
    TEST_ASSERT_EQUAL_UINT16(28u, info.net_hdr_len);
    TEST_ASSERT_EQUAL_UINT16(1u, info.arp_oper);
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_UPPER_NONE, info.upper);
    TEST_ASSERT_EQUAL_UINT16(0u, info.payload_len);
    TEST_ASSERT_EQUAL_UINT16(0u, info.pad_len);
    assert_bytes_add_up(&info);
}

static void test_arp_reply_padded(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(cap_arp_reply_padded, sizeof(cap_arp_reply_padded), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_NET_ARP, info.net);
    TEST_ASSERT_EQUAL_UINT16(2u, info.arp_oper);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac_host, info.eth_dst, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac_board, info.eth_src, 6);
    /* 60 B on the wire, 42 B of it Ethernet + ARP: 18 B padding */
    TEST_ASSERT_EQUAL_UINT16(18u, info.pad_len);
    TEST_ASSERT_EQUAL_UINT16(0u, info.payload_len);
    assert_bytes_add_up(&info);
}

static void test_arp_truncated(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_FALSE(decode_copy(cap_arp_request, 41u, &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_TRUNC_NET, info.status);
    /* layer 2 is still there */
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_NET_ARP, info.net);
    TEST_ASSERT_EQUAL_HEX16(0x0806u, info.ethertype);
    TEST_ASSERT_EQUAL_UINT16(14u, info.eth_hdr_len);
}

static void test_other_ethertypes_stop_at_layer2(void) {
    rnlab_frame_info_t info;
    uint8_t* f = mutable_copy(cap_icmp_echo_req_64, sizeof(cap_icmp_echo_req_64));
    f[12] = 0x86;
    f[13] = 0xDD; /* IPv6 */
    TEST_ASSERT_TRUE(decode_copy(f, sizeof(cap_icmp_echo_req_64), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_NET_OTHER, info.net);
    TEST_ASSERT_EQUAL_HEX16(0x86DDu, info.ethertype);
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_UPPER_NONE, info.upper);
    TEST_ASSERT_EQUAL_UINT16(14u, info.payload_off);
    TEST_ASSERT_EQUAL_UINT16(106u - 14u, info.payload_len);
    assert_bytes_add_up(&info);

    f[12] = 0x81;
    f[13] = 0x00; /* 802.1Q tag: recognised, not unwrapped */
    TEST_ASSERT_TRUE(decode_copy(f, sizeof(cap_icmp_echo_req_64), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_NET_OTHER, info.net);
    TEST_ASSERT_EQUAL_HEX16(0x8100u, info.ethertype);
}

/* --- IPv4 + ICMP ---------------------------------------------------------- */

static void test_icmp_echo_request_64(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(cap_icmp_echo_req_64, sizeof(cap_icmp_echo_req_64), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_NET_IPV4, info.net);
    TEST_ASSERT_EQUAL_HEX16(0x0800u, info.ethertype);
    TEST_ASSERT_EQUAL_UINT16(20u, info.net_hdr_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ip_host, info.ip_src, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ip_board, info.ip_dst, 4);
    TEST_ASSERT_EQUAL_UINT8(64u, info.ip_ttl);
    TEST_ASSERT_EQUAL_UINT8(1u, info.ip_proto);
    TEST_ASSERT_EQUAL_UINT16(92u, info.ip_total_len);
    TEST_ASSERT_FALSE(info.ip_fragment);
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_UPPER_ICMP, info.upper);
    TEST_ASSERT_EQUAL_UINT16(34u, info.upper_off);
    TEST_ASSERT_EQUAL_UINT16(8u, info.upper_hdr_len);
    TEST_ASSERT_EQUAL_UINT8(8u, info.icmp_type);
    TEST_ASSERT_EQUAL_UINT8(0u, info.icmp_code);
    TEST_ASSERT_EQUAL_UINT16(42u, info.payload_off);
    TEST_ASSERT_EQUAL_UINT16(64u, info.payload_len);
    TEST_ASSERT_EQUAL_UINT16(0u, info.pad_len);
    assert_bytes_add_up(&info);
}

static void test_icmp_echo_reply_64(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(cap_icmp_echo_rep_64, sizeof(cap_icmp_echo_rep_64), &info));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ip_board, info.ip_src, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ip_host, info.ip_dst, 4);
    TEST_ASSERT_EQUAL_UINT8(255u, info.ip_ttl); /* lwIP's ICMP_TTL */
    TEST_ASSERT_EQUAL_UINT8(0u, info.icmp_type);
    TEST_ASSERT_EQUAL_UINT16(64u, info.payload_len);
    assert_bytes_add_up(&info);
}

static void test_icmp_echo_zero_payload(void) {
    rnlab_frame_info_t info;
    /* sent by the Mac: 42 B, the MAC pads it only after the capture point */
    TEST_ASSERT_TRUE(decode_copy(cap_icmp_echo_req_0, sizeof(cap_icmp_echo_req_0), &info));
    TEST_ASSERT_EQUAL_UINT16(28u, info.ip_total_len);
    TEST_ASSERT_EQUAL_UINT16(0u, info.payload_len);
    TEST_ASSERT_EQUAL_UINT16(0u, info.pad_len);
    assert_bytes_add_up(&info);

    /* answered by the board: arrives padded to 60 B */
    TEST_ASSERT_TRUE(decode_copy(cap_icmp_echo_rep_0, sizeof(cap_icmp_echo_rep_0), &info));
    TEST_ASSERT_EQUAL_UINT8(0u, info.icmp_type);
    TEST_ASSERT_EQUAL_UINT16(28u, info.ip_total_len);
    TEST_ASSERT_EQUAL_UINT16(0u, info.payload_len);
    TEST_ASSERT_EQUAL_UINT16(18u, info.pad_len);
    assert_bytes_add_up(&info);
}

static void test_icmp_port_unreachable(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(cap_icmp_port_unreach, sizeof(cap_icmp_port_unreach), &info));
    TEST_ASSERT_EQUAL_UINT8(3u, info.icmp_type);
    TEST_ASSERT_EQUAL_UINT8(3u, info.icmp_code);
    /* the ICMP error quotes the original IPv4 header + 8 B of UDP */
    TEST_ASSERT_EQUAL_UINT16(28u, info.payload_len);
    assert_bytes_add_up(&info);
}

/* --- UDP / TCP ------------------------------------------------------------ */

static void test_udp_datagram(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(cap_udp_18, sizeof(cap_udp_18), &info));
    TEST_ASSERT_EQUAL_UINT8(17u, info.ip_proto);
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_UPPER_UDP, info.upper);
    TEST_ASSERT_EQUAL_UINT16(8u, info.upper_hdr_len);
    TEST_ASSERT_EQUAL_UINT16(60823u, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(7007u, info.dst_port);
    TEST_ASSERT_EQUAL_UINT16(42u, info.payload_off);
    TEST_ASSERT_EQUAL_UINT16(18u, info.payload_len);
    TEST_ASSERT_EQUAL_UINT16(0u, info.pad_len); /* exactly 60 B, nothing to pad */
    assert_bytes_add_up(&info);
}

static void test_tcp_syn_with_options(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(cap_tcp_syn, sizeof(cap_tcp_syn), &info));
    TEST_ASSERT_FALSE(info.ip_fragment); /* DF is set, but DF is not fragmenting */
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_UPPER_TCP, info.upper);
    TEST_ASSERT_EQUAL_UINT16(63337u, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(4242u, info.dst_port);
    /* data offset 11: 20 B fixed header + 24 B options (MSS, WS, TS, SACK) */
    TEST_ASSERT_EQUAL_UINT16(44u, info.upper_hdr_len);
    TEST_ASSERT_EQUAL_HEX8(0x02u, info.tcp_flags); /* SYN (ECE/CWR not reported) */
    TEST_ASSERT_EQUAL_UINT16(78u, info.payload_off);
    TEST_ASSERT_EQUAL_UINT16(0u, info.payload_len);
    assert_bytes_add_up(&info);
}

static void test_tcp_synack_padded(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(cap_tcp_synack, sizeof(cap_tcp_synack), &info));
    TEST_ASSERT_EQUAL_UINT16(4242u, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(24u, info.upper_hdr_len); /* MSS option only */
    TEST_ASSERT_EQUAL_HEX8(0x12u, info.tcp_flags);     /* SYN+ACK */
    TEST_ASSERT_EQUAL_UINT16(44u, info.ip_total_len);
    TEST_ASSERT_EQUAL_UINT16(0u, info.payload_len);
    TEST_ASSERT_EQUAL_UINT16(2u, info.pad_len); /* 58 B -> 60 B */
    assert_bytes_add_up(&info);
}

static void test_tcp_data_segment(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(cap_tcp_psh_39, sizeof(cap_tcp_psh_39), &info));
    TEST_ASSERT_EQUAL_HEX8(0x18u, info.tcp_flags); /* PSH+ACK */
    TEST_ASSERT_EQUAL_UINT16(20u, info.upper_hdr_len);
    TEST_ASSERT_EQUAL_UINT16(54u, info.payload_off);
    TEST_ASSERT_EQUAL_UINT16(39u, info.payload_len);
    /* the payload is the CLI's greeting, i.e. OSI 5-7 */
    TEST_ASSERT_EQUAL_MEMORY("CaDS Zero CLI", &cap_tcp_psh_39[info.payload_off], 13);
    assert_bytes_add_up(&info);
}

/* --- robustness ----------------------------------------------------------- */

static void test_null_arguments(void) {
    rnlab_frame_info_t info;
    TEST_ASSERT_FALSE(rnlab_decode_frame(cap_udp_18, sizeof(cap_udp_18), NULL));
    TEST_ASSERT_FALSE(rnlab_decode_frame(NULL, 60u, &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_TRUNC_ETH, info.status);
}

/* Cut the TCP SYN after every length: the status must name the layer that
 * is incomplete, and nothing may be read past the cut. */
static void test_every_truncation_of_tcp_syn(void) {
    for(size_t len = 0; len < sizeof(cap_tcp_syn); len++) {
        rnlab_frame_info_t info;
        TEST_ASSERT_FALSE(decode_copy(cap_tcp_syn, len, &info));
        TEST_ASSERT_EQUAL_UINT16(len, info.frame_len);
        rnlab_l01_status_t want;
        if(len < 14u) want = RNLAB_L01_ERR_TRUNC_ETH;
        else if(len < 34u) want = RNLAB_L01_ERR_TRUNC_NET;
        else want = RNLAB_L01_ERR_TRUNC_UPPER; /* 34 .. 77: TCP header (44 B) incomplete */
        TEST_ASSERT_EQUAL_INT_MESSAGE(want, info.status, "status for this length");
    }
}

static void test_every_truncation_of_echo_request(void) {
    for(size_t len = 0; len < sizeof(cap_icmp_echo_req_64); len++) {
        rnlab_frame_info_t info;
        TEST_ASSERT_FALSE(decode_copy(cap_icmp_echo_req_64, len, &info));
        if(len >= 42u) {
            /* all headers intact, the data is short: keep what is there */
            TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_TRUNC_PAYLOAD, info.status);
            TEST_ASSERT_EQUAL_UINT8(8u, info.icmp_type);
            TEST_ASSERT_EQUAL_UINT16(len - 42u, info.payload_len);
            TEST_ASSERT_EQUAL_UINT16(0u, info.pad_len);
        } else if(len >= 34u) {
            TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_TRUNC_UPPER, info.status);
            TEST_ASSERT_EQUAL_INT(RNLAB_L01_UPPER_ICMP, info.upper);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(ip_board, info.ip_dst, 4);
        } else if(len >= 14u) {
            TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_TRUNC_NET, info.status);
            TEST_ASSERT_EQUAL_INT(RNLAB_L01_NET_IPV4, info.net);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(mac_board, info.eth_dst, 6);
        } else {
            TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_TRUNC_ETH, info.status);
        }
    }
}

static void test_bad_ipv4_headers(void) {
    rnlab_frame_info_t info;
    uint8_t* f = mutable_copy(cap_icmp_echo_req_64, sizeof(cap_icmp_echo_req_64));
    f[14] = 0x65; /* version 6 inside an IPv4 EtherType */
    TEST_ASSERT_FALSE(decode_copy(f, sizeof(cap_icmp_echo_req_64), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_BAD_NET, info.status);

    f = mutable_copy(cap_icmp_echo_req_64, sizeof(cap_icmp_echo_req_64));
    f[14] = 0x44; /* IHL 4 = 16 B, below the 20-byte minimum */
    TEST_ASSERT_FALSE(decode_copy(f, sizeof(cap_icmp_echo_req_64), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_BAD_NET, info.status);

    f = mutable_copy(cap_icmp_echo_req_64, sizeof(cap_icmp_echo_req_64));
    f[16] = 0x00;
    f[17] = 0x13; /* total length 19 < header length 20 */
    TEST_ASSERT_FALSE(decode_copy(f, sizeof(cap_icmp_echo_req_64), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_BAD_NET, info.status);

    f = mutable_copy(cap_icmp_echo_req_64, sizeof(cap_icmp_echo_req_64));
    f[14] = 0x4F; /* IHL 15 = 60 B, but a 60-B frame leaves only 46 B for IPv4 */
    TEST_ASSERT_FALSE(decode_copy(f, 60u, &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_TRUNC_NET, info.status);
}

static void test_ipv4_options_move_the_upper_layer(void) {
    /* Echo request with a 4-byte IPv4 option (4x NOP): IHL 6. */
    uint8_t f[110];
    memcpy(f, cap_icmp_echo_req_64, 34u);
    f[14] = 0x46;
    f[16] = 0x00;
    f[17] = 96u; /* total length 92 + 4 */
    memset(&f[34], 0x01, 4u);
    memcpy(&f[38], &cap_icmp_echo_req_64[34], 72u);
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(f, sizeof(f), &info));
    TEST_ASSERT_EQUAL_UINT16(24u, info.net_hdr_len);
    TEST_ASSERT_EQUAL_UINT16(38u, info.upper_off);
    TEST_ASSERT_EQUAL_UINT8(8u, info.icmp_type);
    TEST_ASSERT_EQUAL_UINT16(46u, info.payload_off);
    TEST_ASSERT_EQUAL_UINT16(64u, info.payload_len);
    assert_bytes_add_up(&info);
}

static void test_bad_transport_headers(void) {
    rnlab_frame_info_t info;
    uint8_t* f = mutable_copy(cap_tcp_psh_39, sizeof(cap_tcp_psh_39));
    f[46] = 0x40; /* TCP data offset 4 = 16 B */
    TEST_ASSERT_FALSE(decode_copy(f, sizeof(cap_tcp_psh_39), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_BAD_UPPER, info.status);
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_UPPER_TCP, info.upper);

    f = mutable_copy(cap_tcp_psh_39, sizeof(cap_tcp_psh_39));
    f[46] = 0xF0; /* data offset 15 = 60 B, but only 59 B of TCP in the packet */
    TEST_ASSERT_FALSE(decode_copy(f, sizeof(cap_tcp_psh_39), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_TRUNC_UPPER, info.status);

    f = mutable_copy(cap_udp_18, sizeof(cap_udp_18));
    f[38] = 0x00;
    f[39] = 0x07; /* UDP length 7 < 8 */
    TEST_ASSERT_FALSE(decode_copy(f, sizeof(cap_udp_18), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_BAD_UPPER, info.status);
}

/* Until praktikum/start 41d52e9 the board's RX hook handed over Ethernet II
 * frames with the 4-byte FCS still attached (a 106-B ping arrived as
 * 110 B). Whatever follows the IPv4 packet - padding or such a trailer -
 * must end up behind it, never in a header or the payload. */
static void test_trailing_fcs_stays_behind_the_packet(void) {
    uint8_t f[sizeof(cap_icmp_echo_req_64) + 4u];
    memcpy(f, cap_icmp_echo_req_64, sizeof(cap_icmp_echo_req_64));
    const uint8_t fcs[4] = {0x5a, 0x1c, 0x9e, 0x03};
    memcpy(&f[sizeof(cap_icmp_echo_req_64)], fcs, 4u);
    rnlab_frame_info_t info;
    TEST_ASSERT_TRUE(decode_copy(f, sizeof(f), &info));
    TEST_ASSERT_EQUAL_UINT16(64u, info.payload_len);
    TEST_ASSERT_EQUAL_UINT16(4u, info.pad_len);
    assert_bytes_add_up(&info);
}

static void test_padding_is_not_a_header(void) {
    /* The SYN-ACK's TCP header ends at byte 58; a data offset reaching into
     * the 2 padding bytes must count as truncated, not as options. */
    rnlab_frame_info_t info;
    uint8_t* f = mutable_copy(cap_tcp_synack, sizeof(cap_tcp_synack));
    f[46] = 0x70; /* 28 B, but the IPv4 packet has only 24 B of TCP */
    TEST_ASSERT_FALSE(decode_copy(f, sizeof(cap_tcp_synack), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_ERR_TRUNC_UPPER, info.status);
}

static void test_fragments(void) {
    rnlab_frame_info_t info;
    uint8_t* f = mutable_copy(cap_icmp_echo_req_64, sizeof(cap_icmp_echo_req_64));
    f[20] = 0x20; /* first fragment: MF set, offset 0 - ICMP header is there */
    TEST_ASSERT_TRUE(decode_copy(f, sizeof(cap_icmp_echo_req_64), &info));
    TEST_ASSERT_TRUE(info.ip_fragment);
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_UPPER_ICMP, info.upper);

    f[20] = 0x00;
    f[21] = 0xB9; /* offset 185 * 8 = 1480: a later fragment, data only */
    TEST_ASSERT_TRUE(decode_copy(f, sizeof(cap_icmp_echo_req_64), &info));
    TEST_ASSERT_TRUE(info.ip_fragment);
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_UPPER_NONE, info.upper);
    TEST_ASSERT_EQUAL_UINT16(34u, info.payload_off);
    TEST_ASSERT_EQUAL_UINT16(72u, info.payload_len);
    assert_bytes_add_up(&info);
}

static void test_unknown_ip_protocol(void) {
    rnlab_frame_info_t info;
    uint8_t* f = mutable_copy(cap_udp_18, sizeof(cap_udp_18));
    f[23] = 89u; /* OSPF */
    TEST_ASSERT_TRUE(decode_copy(f, sizeof(cap_udp_18), &info));
    TEST_ASSERT_EQUAL_INT(RNLAB_L01_UPPER_OTHER, info.upper);
    TEST_ASSERT_EQUAL_UINT16(0u, info.upper_hdr_len);
    TEST_ASSERT_EQUAL_UINT16(34u, info.payload_off);
    TEST_ASSERT_EQUAL_UINT16(26u, info.payload_len);
}

/* --- protocol efficiency ---------------------------------------------------- */

static void test_ping_overhead_zero(void) {
    rnlab_l01_overhead_t o;
    rnlab_l01_ping_overhead(0u, &o);
    TEST_ASSERT_EQUAL_UINT32(0u, o.payload);
    TEST_ASSERT_EQUAL_UINT32(42u, o.headers); /* 14 + 20 + 8 */
    TEST_ASSERT_EQUAL_UINT32(18u, o.padding);
    TEST_ASSERT_EQUAL_UINT32(64u, o.frame);   /* minimum frame incl. FCS */
    TEST_ASSERT_EQUAL_UINT32(84u, o.wire);    /* + 8 preamble/SFD + 12 IFG */
    TEST_ASSERT_EQUAL_UINT32(0u, o.eff_frame_bp);
    TEST_ASSERT_EQUAL_UINT32(0u, o.eff_wire_bp);
}

static void test_ping_overhead_64(void) {
    rnlab_l01_overhead_t o;
    rnlab_l01_ping_overhead(64u, &o);
    TEST_ASSERT_EQUAL_UINT32(0u, o.padding);
    TEST_ASSERT_EQUAL_UINT32(110u, o.frame);
    TEST_ASSERT_EQUAL_UINT32(130u, o.wire);
    TEST_ASSERT_EQUAL_UINT32(5818u, o.eff_frame_bp); /* 64/110 = 58.18 % */
    TEST_ASSERT_EQUAL_UINT32(4923u, o.eff_wire_bp);  /* 64/130 = 49.23 % */
}

static void test_ping_overhead_1472(void) {
    rnlab_l01_overhead_t o;
    rnlab_l01_ping_overhead(1472u, &o);
    TEST_ASSERT_EQUAL_UINT32(1518u, o.frame); /* the largest untagged frame */
    TEST_ASSERT_EQUAL_UINT32(1538u, o.wire);
    TEST_ASSERT_EQUAL_UINT32(9697u, o.eff_frame_bp); /* 96.97 % */
    TEST_ASSERT_EQUAL_UINT32(9571u, o.eff_wire_bp);  /* 95.71 % */
}

static void test_ping_overhead_padding_boundary(void) {
    rnlab_l01_overhead_t o;
    rnlab_l01_ping_overhead(18u, &o); /* 14 + 28 + 18 = 60: no padding */
    TEST_ASSERT_EQUAL_UINT32(0u, o.padding);
    TEST_ASSERT_EQUAL_UINT32(64u, o.frame);
    rnlab_l01_ping_overhead(17u, &o);
    TEST_ASSERT_EQUAL_UINT32(1u, o.padding);
    TEST_ASSERT_EQUAL_UINT32(64u, o.frame);
    rnlab_l01_ping_overhead(19u, &o);
    TEST_ASSERT_EQUAL_UINT32(65u, o.frame);
}

/* The measured frames must give the same numbers as the prediction - for
 * the RX side (already padded) and the TX side (padded later by the MAC). */
static void test_overhead_of_captured_frames_matches_prediction(void) {
    rnlab_frame_info_t info;
    rnlab_l01_overhead_t measured, predicted;

    TEST_ASSERT_TRUE(decode_copy(cap_icmp_echo_rep_64, sizeof(cap_icmp_echo_rep_64), &info));
    TEST_ASSERT_TRUE(rnlab_l01_overhead(&info, &measured));
    rnlab_l01_ping_overhead(64u, &predicted);
    TEST_ASSERT_EQUAL_MEMORY(&predicted, &measured, sizeof(measured));

    TEST_ASSERT_TRUE(decode_copy(cap_icmp_echo_rep_0, sizeof(cap_icmp_echo_rep_0), &info));
    TEST_ASSERT_TRUE(rnlab_l01_overhead(&info, &measured));
    rnlab_l01_ping_overhead(0u, &predicted);
    TEST_ASSERT_EQUAL_MEMORY(&predicted, &measured, sizeof(measured));

    TEST_ASSERT_TRUE(decode_copy(cap_icmp_echo_req_0, sizeof(cap_icmp_echo_req_0), &info));
    TEST_ASSERT_TRUE(rnlab_l01_overhead(&info, &measured));
    TEST_ASSERT_EQUAL_MEMORY(&predicted, &measured, sizeof(measured));
}

static void test_overhead_adds_up_for_all_frames(void) {
    const struct {
        const uint8_t* frame;
        size_t len;
    } frames[] = {
        {cap_arp_request, sizeof(cap_arp_request)},
        {cap_arp_reply_padded, sizeof(cap_arp_reply_padded)},
        {cap_icmp_echo_req_64, sizeof(cap_icmp_echo_req_64)},
        {cap_icmp_echo_req_0, sizeof(cap_icmp_echo_req_0)},
        {cap_icmp_echo_rep_0, sizeof(cap_icmp_echo_rep_0)},
        {cap_udp_18, sizeof(cap_udp_18)},
        {cap_icmp_port_unreach, sizeof(cap_icmp_port_unreach)},
        {cap_tcp_syn, sizeof(cap_tcp_syn)},
        {cap_tcp_synack, sizeof(cap_tcp_synack)},
        {cap_tcp_psh_39, sizeof(cap_tcp_psh_39)},
    };
    for(size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
        rnlab_frame_info_t info;
        rnlab_l01_overhead_t o;
        TEST_ASSERT_TRUE(decode_copy(frames[i].frame, frames[i].len, &info));
        TEST_ASSERT_TRUE(rnlab_l01_overhead(&info, &o));
        TEST_ASSERT_EQUAL_UINT32(o.frame, o.headers + o.payload + o.padding + 4u);
        TEST_ASSERT_EQUAL_UINT32(o.frame + 20u, o.wire);
        TEST_ASSERT_TRUE(o.frame >= 64u);
    }
}

static void test_overhead_arp_and_tcp_values(void) {
    rnlab_frame_info_t info;
    rnlab_l01_overhead_t o;
    TEST_ASSERT_TRUE(decode_copy(cap_arp_request, sizeof(cap_arp_request), &info));
    TEST_ASSERT_TRUE(rnlab_l01_overhead(&info, &o));
    TEST_ASSERT_EQUAL_UINT32(42u, o.headers);
    TEST_ASSERT_EQUAL_UINT32(0u, o.eff_frame_bp); /* ARP: pure control data */

    TEST_ASSERT_TRUE(decode_copy(cap_tcp_psh_39, sizeof(cap_tcp_psh_39), &info));
    TEST_ASSERT_TRUE(rnlab_l01_overhead(&info, &o));
    TEST_ASSERT_EQUAL_UINT32(54u, o.headers);
    TEST_ASSERT_EQUAL_UINT32(97u, o.frame);
    TEST_ASSERT_EQUAL_UINT32(4021u, o.eff_frame_bp); /* 39/97 = 40.21 % */
}

static void test_overhead_needs_a_decoded_frame(void) {
    rnlab_frame_info_t info;
    rnlab_l01_overhead_t o;
    TEST_ASSERT_FALSE(decode_copy(cap_udp_18, 10u, &info));
    TEST_ASSERT_FALSE(rnlab_l01_overhead(&info, &o));
    TEST_ASSERT_EQUAL_UINT32(0u, o.frame);
    TEST_ASSERT_FALSE(rnlab_l01_overhead(NULL, &o));
    TEST_ASSERT_FALSE(rnlab_l01_overhead(&info, NULL));
}

/* --- helpers for the board output ------------------------------------------ */

static void test_format_percent(void) {
    char text[16];
    TEST_ASSERT_EQUAL_size_t(7u, rnlab_l01_format_percent(text, sizeof(text), 9697u));
    TEST_ASSERT_EQUAL_STRING("96,97 %", text);
    rnlab_l01_format_percent(text, sizeof(text), 0u);
    TEST_ASSERT_EQUAL_STRING("0,00 %", text);
    rnlab_l01_format_percent(text, sizeof(text), 5u);
    TEST_ASSERT_EQUAL_STRING("0,05 %", text);
    rnlab_l01_format_percent(text, sizeof(text), 10000u);
    TEST_ASSERT_EQUAL_STRING("100,00 %", text);
    TEST_ASSERT_EQUAL_size_t(0u, rnlab_l01_format_percent(text, 6u, 9697u));
    TEST_ASSERT_EQUAL_STRING("", text);
}

static void test_names(void) {
    TEST_ASSERT_EQUAL_STRING("IPv4", rnlab_l01_net_name(RNLAB_L01_NET_IPV4));
    TEST_ASSERT_EQUAL_STRING("ARP", rnlab_l01_net_name(RNLAB_L01_NET_ARP));
    TEST_ASSERT_EQUAL_STRING("TCP", rnlab_l01_upper_name(RNLAB_L01_UPPER_TCP));
    TEST_ASSERT_EQUAL_STRING("Echo Request", rnlab_l01_icmp_type_name(8u));
    TEST_ASSERT_EQUAL_STRING("Echo Reply", rnlab_l01_icmp_type_name(0u));
    TEST_ASSERT_EQUAL_STRING("ok", rnlab_l01_status_text(RNLAB_L01_OK));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_arp_request);
    RUN_TEST(test_arp_reply_padded);
    RUN_TEST(test_arp_truncated);
    RUN_TEST(test_other_ethertypes_stop_at_layer2);
    RUN_TEST(test_icmp_echo_request_64);
    RUN_TEST(test_icmp_echo_reply_64);
    RUN_TEST(test_icmp_echo_zero_payload);
    RUN_TEST(test_icmp_port_unreachable);
    RUN_TEST(test_udp_datagram);
    RUN_TEST(test_tcp_syn_with_options);
    RUN_TEST(test_tcp_synack_padded);
    RUN_TEST(test_tcp_data_segment);
    RUN_TEST(test_null_arguments);
    RUN_TEST(test_every_truncation_of_tcp_syn);
    RUN_TEST(test_every_truncation_of_echo_request);
    RUN_TEST(test_bad_ipv4_headers);
    RUN_TEST(test_ipv4_options_move_the_upper_layer);
    RUN_TEST(test_bad_transport_headers);
    RUN_TEST(test_trailing_fcs_stays_behind_the_packet);
    RUN_TEST(test_padding_is_not_a_header);
    RUN_TEST(test_fragments);
    RUN_TEST(test_unknown_ip_protocol);
    RUN_TEST(test_ping_overhead_zero);
    RUN_TEST(test_ping_overhead_64);
    RUN_TEST(test_ping_overhead_1472);
    RUN_TEST(test_ping_overhead_padding_boundary);
    RUN_TEST(test_overhead_of_captured_frames_matches_prediction);
    RUN_TEST(test_overhead_adds_up_for_all_frames);
    RUN_TEST(test_overhead_arp_and_tcp_values);
    RUN_TEST(test_overhead_needs_a_decoded_frame);
    RUN_TEST(test_format_percent);
    RUN_TEST(test_names);
    return UNITY_END();
}

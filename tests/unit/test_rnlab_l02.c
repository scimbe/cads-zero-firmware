/* rnlab L02 (Ethernet und ARP): host tests for l02_ethernet_arp_logic.c.
 * ctest label rnlab-L02.
 *
 * The frames are real: captured with `tcpdump -i en13 -xx arp` on the lab
 * link (Mac a0:ce:c8:61:5d:08 = 192.168.33.1, board 82:b4:0a:e7:63:91 =
 * 192.168.33.99) on 2026-09-28. As the hooks see them: from the
 * destination MAC on, without FCS. The board's frames reach the wire padded
 * to 60 B, the Mac's are captured on the sender before padding (42 B) - the
 * parser has to accept both. */

#include <string.h>

#include "unity.h"

#include "l02_ethernet_arp_logic.h"

static const uint8_t MAC_HOST[6] = {0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08};
static const uint8_t MAC_BOARD[6] = {0x82, 0xb4, 0x0a, 0xe7, 0x63, 0x91};
static const uint8_t MAC_BOARD_EARLIER[6] = {0xfa, 0x55, 0xa3, 0x75, 0x35, 0x49};
static const uint8_t IP_HOST[4] = {192, 168, 33, 1};
static const uint8_t IP_BOARD[4] = {192, 168, 33, 99};
static const uint8_t MAC_ZERO[6] = {0, 0, 0, 0, 0, 0};
static const uint8_t IP_ZERO[4] = {0, 0, 0, 0};

/* Mac -> broadcast: "who-has 192.168.33.99 tell 192.168.33.1", 42 B as
 * captured on the sending host (the NIC pads to 60 B only on the wire). */
static const uint8_t FRAME_HOST_REQUEST_BCAST[42] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08, 0x08, 0x06,
    0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08,
    0xc0, 0xa8, 0x21, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x21, 0x63,
};

/* Mac -> board, unicast: the refresh request macOS sends before its cache
 * entry expires - tha already filled in (captured while the board still
 * ran an earlier build with MAC fa:55:a3:75:35:49). */
static const uint8_t FRAME_HOST_REQUEST_UNICAST[42] = {
    0xfa, 0x55, 0xa3, 0x75, 0x35, 0x49, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08, 0x08, 0x06,
    0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08,
    0xc0, 0xa8, 0x21, 0x01, 0xfa, 0x55, 0xa3, 0x75, 0x35, 0x49, 0xc0, 0xa8, 0x21, 0x63,
};

/* Board -> broadcast: `lab 02 garp`, "who-has 192.168.33.99 tell
 * 192.168.33.99", padded to 60 B. */
static const uint8_t FRAME_BOARD_GARP[60] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x82, 0xb4, 0x0a, 0xe7, 0x63, 0x91, 0x08, 0x06,
    0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0x82, 0xb4, 0x0a, 0xe7, 0x63, 0x91,
    0xc0, 0xa8, 0x21, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x21, 0x63,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

/* Board -> broadcast: `lab 02 resolve 192.168.33.1`, padded to 60 B. */
static const uint8_t FRAME_BOARD_REQUEST[60] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x82, 0xb4, 0x0a, 0xe7, 0x63, 0x91, 0x08, 0x06,
    0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0x82, 0xb4, 0x0a, 0xe7, 0x63, 0x91,
    0xc0, 0xa8, 0x21, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x21, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

/* Mac -> board: the answer to FRAME_BOARD_REQUEST, "192.168.33.1 is-at
 * a0:ce:c8:61:5d:08", 107 us later on the Mac's clock. */
static const uint8_t FRAME_HOST_REPLY[42] = {
    0x82, 0xb4, 0x0a, 0xe7, 0x63, 0x91, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08, 0x08, 0x06,
    0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x02, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08,
    0xc0, 0xa8, 0x21, 0x01, 0x82, 0xb4, 0x0a, 0xe7, 0x63, 0x91, 0xc0, 0xa8, 0x21, 0x63,
};

/* Mac -> board: an ICMP echo request (`ping -s 0`), Ethernet + IPv4 + ICMP,
 * captured on the same link (board then ran a build with MAC
 * da:71:d5:87:27:fe) - not ARP at all. */
static const uint8_t FRAME_HOST_PING[42] = {
    0xda, 0x71, 0xd5, 0x87, 0x27, 0xfe, 0xa0, 0xce, 0xc8, 0x61, 0x5d, 0x08, 0x08, 0x00,
    0x45, 0x00, 0x00, 0x1c, 0x2e, 0x54, 0x00, 0x00, 0x40, 0x01, 0x88, 0xd8, 0xc0, 0xa8,
    0x21, 0x01, 0xc0, 0xa8, 0x21, 0x63, 0x08, 0x00, 0xb6, 0xd7, 0x41, 0x28, 0x00, 0x00,
};

static rnlab_arp_packet_t packet;

void setUp(void) {
    memset(&packet, 0xAA, sizeof(packet));
}

void tearDown(void) {
}

/* Build a packet by hand for the classifier tests. */
static rnlab_arp_packet_t make_packet(uint16_t oper, const uint8_t spa[4], const uint8_t tpa[4]) {
    rnlab_arp_packet_t p;
    memset(&p, 0, sizeof(p));
    p.htype = RNLAB_ARP_HTYPE_ETH;
    p.ptype = RNLAB_ARP_PTYPE_IPV4;
    p.hlen = 6;
    p.plen = 4;
    p.oper = oper;
    memcpy(p.sha, MAC_HOST, 6);
    memcpy(p.spa, spa, 4);
    memcpy(p.tpa, tpa, 4);
    return p;
}

/* --- rnlab_parse_arp: real frames ------------------------------------------ */

static void test_parse_host_broadcast_request(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_OK,
                      rnlab_parse_arp(FRAME_HOST_REQUEST_BCAST, sizeof(FRAME_HOST_REQUEST_BCAST), &packet));
    TEST_ASSERT_EQUAL_HEX16(1, packet.htype);
    TEST_ASSERT_EQUAL_HEX16(0x0800, packet.ptype);
    TEST_ASSERT_EQUAL_UINT8(6, packet.hlen);
    TEST_ASSERT_EQUAL_UINT8(4, packet.plen);
    TEST_ASSERT_EQUAL_UINT16(RNLAB_ARP_OP_REQUEST, packet.oper);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAC_HOST, packet.sha, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IP_HOST, packet.spa, 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAC_ZERO, packet.tha, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IP_BOARD, packet.tpa, 4);
}

static void test_parse_host_unicast_request_has_tha(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_OK,
                      rnlab_parse_arp(FRAME_HOST_REQUEST_UNICAST, sizeof(FRAME_HOST_REQUEST_UNICAST), &packet));
    TEST_ASSERT_EQUAL_UINT16(RNLAB_ARP_OP_REQUEST, packet.oper);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAC_BOARD_EARLIER, packet.tha, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IP_BOARD, packet.tpa, 4);
}

static void test_parse_host_reply(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_OK, rnlab_parse_arp(FRAME_HOST_REPLY, sizeof(FRAME_HOST_REPLY), &packet));
    TEST_ASSERT_EQUAL_UINT16(RNLAB_ARP_OP_REPLY, packet.oper);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAC_HOST, packet.sha, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IP_HOST, packet.spa, 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAC_BOARD, packet.tha, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IP_BOARD, packet.tpa, 4);
}

static void test_parse_padded_board_request_ignores_padding(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_OK, rnlab_parse_arp(FRAME_BOARD_REQUEST, sizeof(FRAME_BOARD_REQUEST), &packet));
    TEST_ASSERT_EQUAL_UINT16(RNLAB_ARP_OP_REQUEST, packet.oper);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAC_BOARD, packet.sha, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IP_BOARD, packet.spa, 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAC_ZERO, packet.tha, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IP_HOST, packet.tpa, 4);
}

static void test_parse_padding_content_does_not_matter(void) {
    uint8_t frame[60];
    memcpy(frame, FRAME_BOARD_REQUEST, sizeof(frame));
    memset(&frame[42], 0x5A, sizeof(frame) - 42);
    TEST_ASSERT_EQUAL(RNLAB_ARP_OK, rnlab_parse_arp(frame, sizeof(frame), &packet));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IP_HOST, packet.tpa, 4);
}

static void test_parse_board_garp(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_OK, rnlab_parse_arp(FRAME_BOARD_GARP, sizeof(FRAME_BOARD_GARP), &packet));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IP_BOARD, packet.spa, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IP_BOARD, packet.tpa, 4);
}

/* --- rnlab_parse_arp: rejects ------------------------------------------------ */

static void test_parse_rejects_ipv4_frame(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_NOT_ARP, rnlab_parse_arp(FRAME_HOST_PING, sizeof(FRAME_HOST_PING), &packet));
}

static void test_parse_truncated_by_one_byte(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_TRUNCATED, rnlab_parse_arp(FRAME_HOST_REPLY, 41, &packet));
}

static void test_parse_exact_minimum_length(void) {
    /* 14 + 28 is enough; the padded board frame cut to 42 still parses. */
    TEST_ASSERT_EQUAL(RNLAB_ARP_OK, rnlab_parse_arp(FRAME_BOARD_REQUEST, 42, &packet));
}

static void test_parse_truncated_every_length(void) {
    for(size_t len = 0; len < 42; len++) {
        TEST_ASSERT_EQUAL_MESSAGE(RNLAB_ARP_ERR_TRUNCATED, rnlab_parse_arp(FRAME_HOST_REPLY, len, &packet),
                                  "every length below 42 B must be TRUNCATED");
    }
}

static void test_parse_truncated_wins_over_not_arp(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_TRUNCATED, rnlab_parse_arp(FRAME_HOST_PING, 20, &packet));
}

static void test_parse_null_arguments(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_TRUNCATED, rnlab_parse_arp(NULL, 60, &packet));
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_TRUNCATED, rnlab_parse_arp(FRAME_HOST_REPLY, sizeof(FRAME_HOST_REPLY), NULL));
}

static void test_parse_rejects_wrong_htype(void) {
    uint8_t frame[42];
    memcpy(frame, FRAME_HOST_REPLY, sizeof(frame));
    frame[15] = 0x06; /* IEEE 802 instead of Ethernet */
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_UNSUPPORTED, rnlab_parse_arp(frame, sizeof(frame), &packet));
}

static void test_parse_rejects_wrong_ptype(void) {
    uint8_t frame[42];
    memcpy(frame, FRAME_HOST_REPLY, sizeof(frame));
    frame[16] = 0x86; /* 0x86dd = IPv6 */
    frame[17] = 0xdd;
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_UNSUPPORTED, rnlab_parse_arp(frame, sizeof(frame), &packet));
}

static void test_parse_rejects_wrong_hlen(void) {
    uint8_t frame[42];
    memcpy(frame, FRAME_HOST_REPLY, sizeof(frame));
    frame[18] = 8;
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_UNSUPPORTED, rnlab_parse_arp(frame, sizeof(frame), &packet));
}

static void test_parse_rejects_wrong_plen(void) {
    uint8_t frame[42];
    memcpy(frame, FRAME_HOST_REPLY, sizeof(frame));
    frame[19] = 16;
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_UNSUPPORTED, rnlab_parse_arp(frame, sizeof(frame), &packet));
}

static void test_parse_rejects_rarp_operation(void) {
    uint8_t frame[42];
    memcpy(frame, FRAME_HOST_REPLY, sizeof(frame));
    frame[21] = 3; /* RARP request (RFC 903) */
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_BAD_OPER, rnlab_parse_arp(frame, sizeof(frame), &packet));
    frame[21] = 0;
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_BAD_OPER, rnlab_parse_arp(frame, sizeof(frame), &packet));
}

static void test_parse_operation_uses_both_bytes(void) {
    uint8_t frame[42];
    memcpy(frame, FRAME_HOST_REPLY, sizeof(frame));
    frame[20] = 0x01; /* 0x0102, not 2: big-endian, both bytes count */
    TEST_ASSERT_EQUAL(RNLAB_ARP_ERR_BAD_OPER, rnlab_parse_arp(frame, sizeof(frame), &packet));
}

static void test_parse_leaves_output_untouched_on_error(void) {
    rnlab_arp_packet_t before;
    memset(&before, 0xAA, sizeof(before));
    (void)rnlab_parse_arp(FRAME_HOST_PING, sizeof(FRAME_HOST_PING), &packet);
    TEST_ASSERT_EQUAL_MEMORY(&before, &packet, sizeof(packet));
}

/* --- rnlab_arp_classify ------------------------------------------------------ */

static void test_classify_real_frames(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_OK, rnlab_parse_arp(FRAME_HOST_REQUEST_BCAST, 42, &packet));
    TEST_ASSERT_EQUAL(RNLAB_ARP_KIND_REQUEST, rnlab_arp_classify(&packet));
    TEST_ASSERT_EQUAL(RNLAB_ARP_OK, rnlab_parse_arp(FRAME_HOST_REPLY, 42, &packet));
    TEST_ASSERT_EQUAL(RNLAB_ARP_KIND_REPLY, rnlab_arp_classify(&packet));
    TEST_ASSERT_EQUAL(RNLAB_ARP_OK, rnlab_parse_arp(FRAME_BOARD_GARP, 60, &packet));
    TEST_ASSERT_EQUAL(RNLAB_ARP_KIND_GRATUITOUS, rnlab_arp_classify(&packet));
}

static void test_classify_gratuitous_reply(void) {
    rnlab_arp_packet_t p = make_packet(RNLAB_ARP_OP_REPLY, IP_HOST, IP_HOST);
    TEST_ASSERT_EQUAL(RNLAB_ARP_KIND_GRATUITOUS, rnlab_arp_classify(&p));
}

static void test_classify_probe(void) {
    rnlab_arp_packet_t p = make_packet(RNLAB_ARP_OP_REQUEST, IP_ZERO, IP_BOARD);
    TEST_ASSERT_EQUAL(RNLAB_ARP_KIND_PROBE, rnlab_arp_classify(&p));
}

static void test_classify_reply_from_zero_is_not_probe(void) {
    /* A probe is a request by definition (RFC 5227, 2.1.1). */
    rnlab_arp_packet_t p = make_packet(RNLAB_ARP_OP_REPLY, IP_ZERO, IP_BOARD);
    TEST_ASSERT_EQUAL(RNLAB_ARP_KIND_REPLY, rnlab_arp_classify(&p));
}

static void test_classify_invalid(void) {
    TEST_ASSERT_EQUAL(RNLAB_ARP_KIND_INVALID, rnlab_arp_classify(NULL));
    rnlab_arp_packet_t p = make_packet(3, IP_HOST, IP_BOARD);
    TEST_ASSERT_EQUAL(RNLAB_ARP_KIND_INVALID, rnlab_arp_classify(&p));
}

static void test_kind_names(void) {
    TEST_ASSERT_EQUAL_STRING("Request", rnlab_arp_kind_name(RNLAB_ARP_KIND_REQUEST));
    TEST_ASSERT_EQUAL_STRING("Reply", rnlab_arp_kind_name(RNLAB_ARP_KIND_REPLY));
    TEST_ASSERT_EQUAL_STRING("Gratuitous", rnlab_arp_kind_name(RNLAB_ARP_KIND_GRATUITOUS));
    TEST_ASSERT_EQUAL_STRING("Probe", rnlab_arp_kind_name(RNLAB_ARP_KIND_PROBE));
    TEST_ASSERT_EQUAL_STRING("ungueltig", rnlab_arp_kind_name(RNLAB_ARP_KIND_INVALID));
}

/* --- rnlab_arp_count --------------------------------------------------------- */

static void test_count_mixed_traffic(void) {
    rnlab_arp_counters_t c;
    memset(&c, 0, sizeof(c));
    rnlab_arp_count(&c, FRAME_HOST_REQUEST_BCAST, 42);
    rnlab_arp_count(&c, FRAME_HOST_REQUEST_UNICAST, 42);
    rnlab_arp_count(&c, FRAME_HOST_REPLY, 42);
    rnlab_arp_count(&c, FRAME_BOARD_GARP, 60);
    rnlab_arp_count(&c, FRAME_HOST_PING, 42); /* not ARP: ignored */
    TEST_ASSERT_EQUAL_UINT32(2, c.requests);
    TEST_ASSERT_EQUAL_UINT32(1, c.replies);
    TEST_ASSERT_EQUAL_UINT32(1, c.gratuitous);
    TEST_ASSERT_EQUAL_UINT32(0, c.probes);
    TEST_ASSERT_EQUAL_UINT32(0, c.invalid);
}

static void test_count_broken_arp_is_invalid(void) {
    rnlab_arp_counters_t c;
    memset(&c, 0, sizeof(c));
    uint8_t frame[42];
    memcpy(frame, FRAME_HOST_REPLY, sizeof(frame));
    frame[19] = 16;
    rnlab_arp_count(&c, frame, sizeof(frame));       /* unsupported */
    rnlab_arp_count(&c, FRAME_HOST_REPLY, 30);       /* runt with ARP EtherType */
    rnlab_arp_count(&c, FRAME_HOST_REPLY, 10);       /* too short for a EtherType */
    TEST_ASSERT_EQUAL_UINT32(2, c.invalid);
    TEST_ASSERT_EQUAL_UINT32(0, c.replies);
}

/* --- rnlab_arp_timing -------------------------------------------------------- */

static void test_timing_request_then_reply(void) {
    rnlab_arp_timing_t t;
    rnlab_arp_timing_reset(&t);
    uint32_t latency = 0;
    rnlab_arp_timing_on_tx(&t, FRAME_BOARD_REQUEST, 60, 1000000u);
    TEST_ASSERT_TRUE(t.pending);
    TEST_ASSERT_TRUE(rnlab_arp_timing_on_rx(&t, FRAME_HOST_REPLY, 42, 1000400u, &latency));
    TEST_ASSERT_EQUAL_UINT32(400, latency);
    TEST_ASSERT_FALSE(t.pending);
    TEST_ASSERT_EQUAL_UINT32(1, t.count);
    TEST_ASSERT_EQUAL_UINT32(400, t.min_us);
    TEST_ASSERT_EQUAL_UINT32(400, t.max_us);
}

static void test_timing_min_max_over_several(void) {
    rnlab_arp_timing_t t;
    rnlab_arp_timing_reset(&t);
    const uint32_t delays[3] = {416, 400, 10227};
    for(int i = 0; i < 3; i++) {
        uint64_t t0 = 5000000u * (uint64_t)(i + 1);
        rnlab_arp_timing_on_tx(&t, FRAME_BOARD_REQUEST, 60, t0);
        TEST_ASSERT_TRUE(rnlab_arp_timing_on_rx(&t, FRAME_HOST_REPLY, 42, t0 + delays[i], NULL));
    }
    TEST_ASSERT_EQUAL_UINT32(3, t.count);
    TEST_ASSERT_EQUAL_UINT32(10227, t.last_us);
    TEST_ASSERT_EQUAL_UINT32(400, t.min_us);
    TEST_ASSERT_EQUAL_UINT32(10227, t.max_us);
}

static void test_timing_ignores_reply_without_request(void) {
    rnlab_arp_timing_t t;
    rnlab_arp_timing_reset(&t);
    TEST_ASSERT_FALSE(rnlab_arp_timing_on_rx(&t, FRAME_HOST_REPLY, 42, 1234u, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, t.count);
}

static void test_timing_ignores_reply_from_other_host(void) {
    rnlab_arp_timing_t t;
    rnlab_arp_timing_reset(&t);
    rnlab_arp_timing_on_tx(&t, FRAME_BOARD_REQUEST, 60, 100u);
    uint8_t frame[42];
    memcpy(frame, FRAME_HOST_REPLY, sizeof(frame));
    frame[31] = 0x02; /* spa 192.168.33.2 */
    TEST_ASSERT_FALSE(rnlab_arp_timing_on_rx(&t, frame, sizeof(frame), 500u, NULL));
    TEST_ASSERT_TRUE(t.pending);
}

static void test_timing_ignores_requests_as_answer(void) {
    rnlab_arp_timing_t t;
    rnlab_arp_timing_reset(&t);
    rnlab_arp_timing_on_tx(&t, FRAME_BOARD_REQUEST, 60, 100u);
    /* The Mac's own request carries spa 192.168.33.1 too, but answers nothing. */
    TEST_ASSERT_FALSE(rnlab_arp_timing_on_rx(&t, FRAME_HOST_REQUEST_BCAST, 42, 500u, NULL));
    TEST_ASSERT_TRUE(t.pending);
}

static void test_timing_gratuitous_starts_nothing(void) {
    rnlab_arp_timing_t t;
    rnlab_arp_timing_reset(&t);
    rnlab_arp_timing_on_tx(&t, FRAME_BOARD_GARP, 60, 100u);
    TEST_ASSERT_FALSE(t.pending);
}

static void test_timing_retry_restarts_clock(void) {
    rnlab_arp_timing_t t;
    rnlab_arp_timing_reset(&t);
    uint32_t latency = 0;
    rnlab_arp_timing_on_tx(&t, FRAME_BOARD_REQUEST, 60, 0u);
    rnlab_arp_timing_on_tx(&t, FRAME_BOARD_REQUEST, 60, 1000000u); /* lwIP's retry 1 s later */
    TEST_ASSERT_TRUE(rnlab_arp_timing_on_rx(&t, FRAME_HOST_REPLY, 42, 1000300u, &latency));
    TEST_ASSERT_EQUAL_UINT32(300, latency);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_host_broadcast_request);
    RUN_TEST(test_parse_host_unicast_request_has_tha);
    RUN_TEST(test_parse_host_reply);
    RUN_TEST(test_parse_padded_board_request_ignores_padding);
    RUN_TEST(test_parse_padding_content_does_not_matter);
    RUN_TEST(test_parse_board_garp);
    RUN_TEST(test_parse_rejects_ipv4_frame);
    RUN_TEST(test_parse_truncated_by_one_byte);
    RUN_TEST(test_parse_exact_minimum_length);
    RUN_TEST(test_parse_truncated_every_length);
    RUN_TEST(test_parse_truncated_wins_over_not_arp);
    RUN_TEST(test_parse_null_arguments);
    RUN_TEST(test_parse_rejects_wrong_htype);
    RUN_TEST(test_parse_rejects_wrong_ptype);
    RUN_TEST(test_parse_rejects_wrong_hlen);
    RUN_TEST(test_parse_rejects_wrong_plen);
    RUN_TEST(test_parse_rejects_rarp_operation);
    RUN_TEST(test_parse_operation_uses_both_bytes);
    RUN_TEST(test_parse_leaves_output_untouched_on_error);
    RUN_TEST(test_classify_real_frames);
    RUN_TEST(test_classify_gratuitous_reply);
    RUN_TEST(test_classify_probe);
    RUN_TEST(test_classify_reply_from_zero_is_not_probe);
    RUN_TEST(test_classify_invalid);
    RUN_TEST(test_kind_names);
    RUN_TEST(test_count_mixed_traffic);
    RUN_TEST(test_count_broken_arp_is_invalid);
    RUN_TEST(test_timing_request_then_reply);
    RUN_TEST(test_timing_min_max_over_several);
    RUN_TEST(test_timing_ignores_reply_without_request);
    RUN_TEST(test_timing_ignores_reply_from_other_host);
    RUN_TEST(test_timing_ignores_requests_as_answer);
    RUN_TEST(test_timing_gratuitous_starts_nothing);
    RUN_TEST(test_timing_retry_restarts_clock);
    return UNITY_END();
}

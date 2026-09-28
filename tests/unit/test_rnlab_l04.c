/* rnlab L04 (ICMP): host tests for l04_icmp_logic.c.
 * ctest label rnlab-L04. On the stub (praktikum/start + TODOs) these fail by
 * design - they are the students' specification. */

#include "unity.h"

#include "l04_icmp_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

/* RFC 1071 section 3's worked example: the words 0001 f203 f4f5 f6f7 sum
 * (with end-around carry) to 0xddf2, so the checksum is ~0xddf2 = 0x220d. */
static void test_checksum_rfc1071_example(void) {
    const uint8_t data[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
    TEST_ASSERT_EQUAL_HEX16(0x220d, rnlab_inet_checksum(data, sizeof(data)));
}

static void test_checksum_odd_length_pads_with_zero(void) {
    const uint8_t odd[] = {0x01, 0x02, 0x03};
    const uint8_t even[] = {0x01, 0x02, 0x03, 0x00};
    TEST_ASSERT_EQUAL_HEX16(rnlab_inet_checksum(even, sizeof(even)), rnlab_inet_checksum(odd, sizeof(odd)));
    TEST_ASSERT_EQUAL_HEX16(0xFBFD, rnlab_inet_checksum(odd, sizeof(odd)));
}

static void test_checksum_edge_cases(void) {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, rnlab_inet_checksum(NULL, 0u));
    const uint8_t ones[] = {0xFF, 0xFF, 0xFF, 0xFF};
    /* 0xFFFF + 0xFFFF = 0x1FFFE -> fold -> 0xFFFF -> complement 0. */
    TEST_ASSERT_EQUAL_HEX16(0x0000, rnlab_inet_checksum(ones, sizeof(ones)));
}

/* A real IPv4 header (Wikipedia/RFC example): its checksum field 0xb861
 * makes the whole header sum to 0xFFFF, i.e. the checksum over it is 0. */
static void test_checksum_verifies_real_ip_header(void) {
    uint8_t hdr[] = {0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
                     0xb8, 0x61, 0xc0, 0xa8, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0xc7};
    TEST_ASSERT_EQUAL_HEX16(0x0000, rnlab_inet_checksum(hdr, sizeof(hdr)));
    hdr[10] = 0u;
    hdr[11] = 0u;
    TEST_ASSERT_EQUAL_HEX16(0xb861, rnlab_inet_checksum(hdr, sizeof(hdr)));
}

/* Echo request as macOS `ping` sends it: id 0x1234, seq 1, 4 data bytes. */
static void make_request(uint8_t* pkt) {
    const uint8_t req[] = {0x08, 0x00, 0x00, 0x00, 0x12, 0x34, 0x00, 0x01, 'a', 'b', 'c', 'd'};
    for(unsigned i = 0; i < sizeof(req); i++) pkt[i] = req[i];
    uint16_t sum = rnlab_inet_checksum(pkt, sizeof(req));
    pkt[2] = (uint8_t)(sum >> 8);
    pkt[3] = (uint8_t)sum;
}

static void test_echo_to_reply(void) {
    uint8_t pkt[12];
    make_request(pkt);
    TEST_ASSERT_TRUE(rnlab_l04_echo_to_reply(pkt, sizeof(pkt)));
    TEST_ASSERT_EQUAL_UINT8(0u, pkt[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, pkt[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, pkt[4]);
    TEST_ASSERT_EQUAL_HEX8(0x34, pkt[5]);
    TEST_ASSERT_EQUAL_HEX8(0x01, pkt[7]);
    TEST_ASSERT_EQUAL_UINT8('d', pkt[11]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, rnlab_inet_checksum(pkt, sizeof(pkt)));
    /* Type 8 -> 0 lowers the first word by 0x0800, so the checksum rises
     * by 0x0800 - the incremental rule of RFC 1624. */
    uint8_t req[12];
    make_request(req);
    uint16_t before = (uint16_t)((req[2] << 8) | req[3]);
    uint16_t after = (uint16_t)((pkt[2] << 8) | pkt[3]);
    TEST_ASSERT_EQUAL_HEX16((uint16_t)(before + 0x0800u), after);
}

static void test_echo_to_reply_odd_length(void) {
    uint8_t pkt[9] = {0x08, 0x00, 0x00, 0x00, 0xAB, 0xCD, 0x00, 0x07, 0x5A};
    uint16_t sum = rnlab_inet_checksum(pkt, sizeof(pkt));
    pkt[2] = (uint8_t)(sum >> 8);
    pkt[3] = (uint8_t)sum;
    TEST_ASSERT_TRUE(rnlab_l04_echo_to_reply(pkt, sizeof(pkt)));
    TEST_ASSERT_EQUAL_UINT8(0u, pkt[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, rnlab_inet_checksum(pkt, sizeof(pkt)));
}

static void test_echo_to_reply_rejects(void) {
    uint8_t pkt[12];
    make_request(pkt);
    pkt[9] ^= 0x01; /* corrupted in transit */
    TEST_ASSERT_FALSE(rnlab_l04_echo_to_reply(pkt, sizeof(pkt)));
    TEST_ASSERT_EQUAL_UINT8(8u, pkt[0]); /* untouched */

    make_request(pkt);
    TEST_ASSERT_FALSE(rnlab_l04_echo_to_reply(pkt, 7u)); /* shorter than the header */

    uint8_t reply[12];
    make_request(reply);
    TEST_ASSERT_TRUE(rnlab_l04_echo_to_reply(reply, sizeof(reply)));
    TEST_ASSERT_FALSE(rnlab_l04_echo_to_reply(reply, sizeof(reply))); /* already a reply */

    uint8_t code1[12];
    make_request(code1);
    code1[1] = 1u;
    uint16_t sum;
    code1[2] = code1[3] = 0u;
    sum = rnlab_inet_checksum(code1, sizeof(code1));
    code1[2] = (uint8_t)(sum >> 8);
    code1[3] = (uint8_t)sum;
    TEST_ASSERT_FALSE(rnlab_l04_echo_to_reply(code1, sizeof(code1)));
}

static void test_hist_bin(void) {
    TEST_ASSERT_EQUAL_UINT(0u, rnlab_l04_hist_bin(0u));
    TEST_ASSERT_EQUAL_UINT(0u, rnlab_l04_hist_bin(249u));
    TEST_ASSERT_EQUAL_UINT(1u, rnlab_l04_hist_bin(250u));
    TEST_ASSERT_EQUAL_UINT(1u, rnlab_l04_hist_bin(499u));
    TEST_ASSERT_EQUAL_UINT(2u, rnlab_l04_hist_bin(500u));
    TEST_ASSERT_EQUAL_UINT(3u, rnlab_l04_hist_bin(1000u));
    TEST_ASSERT_EQUAL_UINT(5u, rnlab_l04_hist_bin(5000u));
    TEST_ASSERT_EQUAL_UINT(6u, rnlab_l04_hist_bin(8000u));
    TEST_ASSERT_EQUAL_UINT(6u, rnlab_l04_hist_bin(10000u));
    TEST_ASSERT_EQUAL_UINT(8u, rnlab_l04_hist_bin(63999u));
    TEST_ASSERT_EQUAL_UINT(9u, rnlab_l04_hist_bin(64000u));
    TEST_ASSERT_EQUAL_UINT(9u, rnlab_l04_hist_bin(UINT32_MAX));
}

static void test_stats(void) {
    rnlab_l04_stats_t s;
    rnlab_l04_stats_init(&s);
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_l04_stats_avg_us(&s));
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_l04_stats_stddev_us(&s));

    /* 2, 4, 4, 4, 5, 5, 7, 9 ms: mean 5 ms, population stddev exactly 2 ms. */
    const uint32_t ms[] = {2, 4, 4, 4, 5, 5, 7, 9};
    s.sent = 10u;
    for(unsigned i = 0; i < 8u; i++) rnlab_l04_stats_add(&s, ms[i] * 1000u);
    TEST_ASSERT_EQUAL_UINT32(8u, s.received);
    TEST_ASSERT_EQUAL_UINT32(2000u, s.min_us);
    TEST_ASSERT_EQUAL_UINT32(9000u, s.max_us);
    TEST_ASSERT_EQUAL_UINT32(5000u, rnlab_l04_stats_avg_us(&s));
    TEST_ASSERT_EQUAL_UINT32(2000u, rnlab_l04_stats_stddev_us(&s));
    TEST_ASSERT_EQUAL_UINT32(20u, rnlab_l04_stats_loss_pct(&s));
    /* 2 ms -> bin 4 [2,4) ms; 4,4,4,5,5,7 -> bin 5 [4,8) ms; 9 -> bin 6. */
    TEST_ASSERT_EQUAL_UINT32(1u, s.hist[4]);
    TEST_ASSERT_EQUAL_UINT32(6u, s.hist[5]);
    TEST_ASSERT_EQUAL_UINT32(1u, s.hist[6]);
}

static void test_stats_identical_samples_have_zero_stddev(void) {
    rnlab_l04_stats_t s;
    rnlab_l04_stats_init(&s);
    for(unsigned i = 0; i < 100u; i++) rnlab_l04_stats_add(&s, 333u);
    TEST_ASSERT_EQUAL_UINT32(333u, rnlab_l04_stats_avg_us(&s));
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_l04_stats_stddev_us(&s));
    TEST_ASSERT_EQUAL_UINT32(333u, s.min_us);
    TEST_ASSERT_EQUAL_UINT32(333u, s.max_us);
}

static void test_stats_uniform_poll_delay(void) {
    /* The shape the lesson predicts for a 10 ms poll loop: RTT uniform over
     * 0..10 ms -> mean 5 ms, stddev 10/sqrt(12) = 2.887 ms. */
    rnlab_l04_stats_t s;
    rnlab_l04_stats_init(&s);
    for(uint32_t us = 0; us < 10000u; us += 10u) rnlab_l04_stats_add(&s, us);
    TEST_ASSERT_UINT32_WITHIN(5u, 4995u, rnlab_l04_stats_avg_us(&s));
    TEST_ASSERT_UINT32_WITHIN(5u, 2887u, rnlab_l04_stats_stddev_us(&s));
}

static void test_provided_helpers(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_l04_isqrt64(0u));
    TEST_ASSERT_EQUAL_UINT32(3u, rnlab_l04_isqrt64(15u));
    TEST_ASSERT_EQUAL_UINT32(4u, rnlab_l04_isqrt64(16u));
    TEST_ASSERT_EQUAL_UINT32(1000000u, rnlab_l04_isqrt64(1000000000000ull));

    uint32_t p = 0u;
    TEST_ASSERT_TRUE(rnlab_l04_parse_percent("25%", &p));
    TEST_ASSERT_EQUAL_UINT32(25u, p);
    TEST_ASSERT_TRUE(rnlab_l04_parse_percent("0", &p));
    TEST_ASSERT_FALSE(rnlab_l04_parse_percent("101", &p));
    TEST_ASSERT_FALSE(rnlab_l04_parse_percent("x", &p));

    uint32_t state = 12345u;
    unsigned drops = 0u;
    for(unsigned i = 0; i < 10000u; i++) drops += rnlab_l04_should_drop(&state, 30u) ? 1u : 0u;
    TEST_ASSERT_UINT_WITHIN(300u, 3000u, drops);
    TEST_ASSERT_FALSE(rnlab_l04_should_drop(&state, 0u));
    TEST_ASSERT_TRUE(rnlab_l04_should_drop(&state, 100u));

    uint8_t req[8 + 56];
    rnlab_l04_build_echo_request(req, 0xBEEF, 7u, 56u);
    TEST_ASSERT_EQUAL_UINT8(8u, req[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, req[4]);
    TEST_ASSERT_EQUAL_UINT8(7u, req[7]);
    TEST_ASSERT_EQUAL_UINT8(55u, req[8 + 55]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, rnlab_inet_checksum(req, sizeof(req)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_checksum_rfc1071_example);
    RUN_TEST(test_checksum_odd_length_pads_with_zero);
    RUN_TEST(test_checksum_edge_cases);
    RUN_TEST(test_checksum_verifies_real_ip_header);
    RUN_TEST(test_echo_to_reply);
    RUN_TEST(test_echo_to_reply_odd_length);
    RUN_TEST(test_echo_to_reply_rejects);
    RUN_TEST(test_hist_bin);
    RUN_TEST(test_stats);
    RUN_TEST(test_stats_identical_samples_have_zero_stddev);
    RUN_TEST(test_stats_uniform_poll_delay);
    RUN_TEST(test_provided_helpers);
    return UNITY_END();
}

/* rnlab L03 (IPv4 und Subnetting): host tests for l03_ipv4_subnetting_logic.c.
 * ctest label rnlab-L03. On the stub (praktikum/start + TODOs) these fail by
 * design - they are the students' specification. */

#include "unity.h"

#include "l03_ipv4_subnetting_logic.h"

#define IP4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

void setUp(void) {
}

void tearDown(void) {
}

static void test_mask_valid(void) {
    TEST_ASSERT_TRUE(rnlab_l03_mask_valid(IP4(255, 255, 255, 0)));
    TEST_ASSERT_TRUE(rnlab_l03_mask_valid(IP4(255, 255, 255, 128)));
    TEST_ASSERT_TRUE(rnlab_l03_mask_valid(IP4(255, 255, 0, 0)));
    TEST_ASSERT_TRUE(rnlab_l03_mask_valid(IP4(255, 255, 255, 252)));
    TEST_ASSERT_TRUE(rnlab_l03_mask_valid(0u));
    TEST_ASSERT_TRUE(rnlab_l03_mask_valid(0xFFFFFFFFu));
    /* Holes in the ones, ones after zeros, a single stray bit. */
    TEST_ASSERT_FALSE(rnlab_l03_mask_valid(IP4(255, 0, 255, 0)));
    TEST_ASSERT_FALSE(rnlab_l03_mask_valid(IP4(255, 255, 255, 1)));
    TEST_ASSERT_FALSE(rnlab_l03_mask_valid(IP4(0, 255, 255, 255)));
    TEST_ASSERT_FALSE(rnlab_l03_mask_valid(IP4(255, 255, 253, 0)));
    TEST_ASSERT_FALSE(rnlab_l03_mask_valid(1u));
}

static void test_mask_to_prefix(void) {
    TEST_ASSERT_EQUAL_INT(24, rnlab_l03_mask_to_prefix(IP4(255, 255, 255, 0)));
    TEST_ASSERT_EQUAL_INT(25, rnlab_l03_mask_to_prefix(IP4(255, 255, 255, 128)));
    TEST_ASSERT_EQUAL_INT(16, rnlab_l03_mask_to_prefix(IP4(255, 255, 0, 0)));
    TEST_ASSERT_EQUAL_INT(30, rnlab_l03_mask_to_prefix(IP4(255, 255, 255, 252)));
    TEST_ASSERT_EQUAL_INT(0, rnlab_l03_mask_to_prefix(0u));
    TEST_ASSERT_EQUAL_INT(32, rnlab_l03_mask_to_prefix(0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_INT(-1, rnlab_l03_mask_to_prefix(IP4(255, 0, 255, 0)));
}

static void test_prefix_to_mask(void) {
    TEST_ASSERT_EQUAL_HEX32(IP4(255, 255, 255, 0), rnlab_l03_prefix_to_mask(24));
    TEST_ASSERT_EQUAL_HEX32(IP4(255, 255, 255, 128), rnlab_l03_prefix_to_mask(25));
    TEST_ASSERT_EQUAL_HEX32(IP4(255, 255, 0, 0), rnlab_l03_prefix_to_mask(16));
    TEST_ASSERT_EQUAL_HEX32(IP4(255, 255, 255, 252), rnlab_l03_prefix_to_mask(30));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, rnlab_l03_prefix_to_mask(32));
    TEST_ASSERT_EQUAL_HEX32(0u, rnlab_l03_prefix_to_mask(0));
    TEST_ASSERT_EQUAL_HEX32(0u, rnlab_l03_prefix_to_mask(33));
}

static void test_prefix_mask_round_trip(void) {
    for(unsigned prefix = 0; prefix <= 32u; prefix++) {
        uint32_t mask = rnlab_l03_prefix_to_mask(prefix);
        TEST_ASSERT_TRUE(rnlab_l03_mask_valid(mask));
        TEST_ASSERT_EQUAL_INT((int)prefix, rnlab_l03_mask_to_prefix(mask));
    }
}

static void test_network_and_broadcast(void) {
    uint32_t board = IP4(192, 168, 33, 99);
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 0), rnlab_l03_network(board, rnlab_l03_prefix_to_mask(24)));
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 255), rnlab_l03_broadcast(board, rnlab_l03_prefix_to_mask(24)));
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 0), rnlab_l03_network(board, rnlab_l03_prefix_to_mask(25)));
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 127), rnlab_l03_broadcast(board, rnlab_l03_prefix_to_mask(25)));
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 0, 0), rnlab_l03_network(board, rnlab_l03_prefix_to_mask(16)));
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 255, 255), rnlab_l03_broadcast(board, rnlab_l03_prefix_to_mask(16)));
    /* The /30 surprise: .99 is the broadcast address of 192.168.33.96/30. */
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 96), rnlab_l03_network(board, rnlab_l03_prefix_to_mask(30)));
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 99), rnlab_l03_broadcast(board, rnlab_l03_prefix_to_mask(30)));
}

static void test_same_subnet(void) {
    uint32_t board = IP4(192, 168, 33, 99);
    uint32_t mac = IP4(192, 168, 33, 10);
    uint32_t m24 = IP4(255, 255, 255, 0);
    TEST_ASSERT_TRUE(rnlab_same_subnet(board, mac, m24));
    TEST_ASSERT_FALSE(rnlab_same_subnet(board, IP4(192, 168, 34, 10), m24));
    TEST_ASSERT_TRUE(rnlab_same_subnet(board, mac, IP4(255, 255, 255, 128)));
    TEST_ASSERT_FALSE(rnlab_same_subnet(board, IP4(192, 168, 33, 200), IP4(255, 255, 255, 128)));
    TEST_ASSERT_TRUE(rnlab_same_subnet(board, IP4(192, 168, 34, 10), IP4(255, 255, 0, 0)));
    TEST_ASSERT_FALSE(rnlab_same_subnet(board, mac, IP4(255, 255, 255, 252)));
    TEST_ASSERT_TRUE(rnlab_same_subnet(board, IP4(192, 168, 33, 97), IP4(255, 255, 255, 252)));
    /* /0: everything is on-link; /32: only the address itself. */
    TEST_ASSERT_TRUE(rnlab_same_subnet(board, IP4(8, 8, 8, 8), 0u));
    TEST_ASSERT_FALSE(rnlab_same_subnet(board, IP4(192, 168, 33, 98), 0xFFFFFFFFu));
    TEST_ASSERT_TRUE(rnlab_same_subnet(board, board, 0xFFFFFFFFu));
}

static void test_classify_dst(void) {
    uint32_t board = IP4(192, 168, 33, 99);
    uint32_t m24 = IP4(255, 255, 255, 0);
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_OWN, rnlab_l03_classify_dst(board, board, m24));
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_BROADCAST, rnlab_l03_classify_dst(IP4(192, 168, 33, 255), board, m24));
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_BROADCAST, rnlab_l03_classify_dst(0xFFFFFFFFu, board, m24));
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_MULTICAST, rnlab_l03_classify_dst(IP4(224, 0, 0, 251), board, m24));
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_MULTICAST, rnlab_l03_classify_dst(IP4(239, 255, 255, 250), board, m24));
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_OTHER, rnlab_l03_classify_dst(IP4(192, 168, 33, 10), board, m24));
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_OTHER, rnlab_l03_classify_dst(IP4(240, 0, 0, 1), board, m24));
    /* Same packet, other mask: .255 is a plain (foreign) host under /25 and /16. */
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_OTHER,
                      rnlab_l03_classify_dst(IP4(192, 168, 33, 255), board, IP4(255, 255, 255, 128)));
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_BROADCAST,
                      rnlab_l03_classify_dst(IP4(192, 168, 33, 127), board, IP4(255, 255, 255, 128)));
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_OTHER, rnlab_l03_classify_dst(IP4(192, 168, 33, 255), board, IP4(255, 255, 0, 0)));
    /* /30: our own address is the subnet broadcast - own wins. */
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_OWN, rnlab_l03_classify_dst(board, board, IP4(255, 255, 255, 252)));
    /* /31 has no broadcast (RFC 3021). */
    TEST_ASSERT_EQUAL(RNLAB_L03_DST_OTHER,
                      rnlab_l03_classify_dst(IP4(192, 168, 33, 99), IP4(192, 168, 33, 98), 0xFFFFFFFEu));
}

static void test_next_hop(void) {
    uint32_t board = IP4(192, 168, 33, 99);
    uint32_t gw = IP4(192, 168, 33, 1);
    uint32_t mac = IP4(192, 168, 33, 10);
    uint32_t far = IP4(192, 168, 34, 10);
    /* /24: the Mac directly, the other network via the gateway. */
    TEST_ASSERT_EQUAL_HEX32(mac, rnlab_l03_next_hop(board, rnlab_l03_prefix_to_mask(24), gw, mac));
    TEST_ASSERT_EQUAL_HEX32(gw, rnlab_l03_next_hop(board, rnlab_l03_prefix_to_mask(24), gw, far));
    /* /16: 192.168.34.10 looks on-link - the board ARPs for it directly. */
    TEST_ASSERT_EQUAL_HEX32(far, rnlab_l03_next_hop(board, rnlab_l03_prefix_to_mask(16), gw, far));
    /* /30: even the Mac next door is "far away" - reply goes to the gateway. */
    TEST_ASSERT_EQUAL_HEX32(gw, rnlab_l03_next_hop(board, rnlab_l03_prefix_to_mask(30), gw, mac));
    /* /25: .200 is in the upper half, not ours. */
    TEST_ASSERT_EQUAL_HEX32(
        gw, rnlab_l03_next_hop(board, rnlab_l03_prefix_to_mask(25), gw, IP4(192, 168, 33, 200)));
    /* Limited broadcast never needs a gateway; no gateway set -> 0. */
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, rnlab_l03_next_hop(board, rnlab_l03_prefix_to_mask(30), gw, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_HEX32(0u, rnlab_l03_next_hop(board, rnlab_l03_prefix_to_mask(24), 0u, far));
}

static void test_parse_mask(void) {
    uint32_t mask = 0u;
    TEST_ASSERT_TRUE(rnlab_l03_parse_mask("255.255.255.128", &mask));
    TEST_ASSERT_EQUAL_HEX32(IP4(255, 255, 255, 128), mask);
    TEST_ASSERT_TRUE(rnlab_l03_parse_mask("/16", &mask));
    TEST_ASSERT_EQUAL_HEX32(IP4(255, 255, 0, 0), mask);
    TEST_ASSERT_TRUE(rnlab_l03_parse_mask("30", &mask));
    TEST_ASSERT_EQUAL_HEX32(IP4(255, 255, 255, 252), mask);
    TEST_ASSERT_FALSE(rnlab_l03_parse_mask("255.0.255.0", &mask));
    TEST_ASSERT_FALSE(rnlab_l03_parse_mask("/33", &mask));
    TEST_ASSERT_FALSE(rnlab_l03_parse_mask("24x", &mask));
    TEST_ASSERT_FALSE(rnlab_l03_parse_mask("", &mask));
}

static void test_parse_cidr(void) {
    uint32_t net = 0u, mask = 0u;
    TEST_ASSERT_TRUE(rnlab_l03_parse_cidr("192.168.33.0/24", &net, &mask));
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 0), net);
    TEST_ASSERT_EQUAL_HEX32(IP4(255, 255, 255, 0), mask);
    /* Host bits are cleared, like `ip route` does. */
    TEST_ASSERT_TRUE(rnlab_l03_parse_cidr("192.168.33.10/30", &net, &mask));
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 8), net);
    TEST_ASSERT_FALSE(rnlab_l03_parse_cidr("192.168.33.0", &net, &mask));
    TEST_ASSERT_FALSE(rnlab_l03_parse_cidr("192.168.33.0/", &net, &mask));
    TEST_ASSERT_FALSE(rnlab_l03_parse_cidr("192.168.333.0/24", &net, &mask));
}

static void test_header_addrs(void) {
    const uint8_t ping[20] = {0x45, 0x00, 0x00, 0x54, 0x12, 0x34, 0x00, 0x00, 0x40, 0x01,
                              0x00, 0x00, 192,  168,  33,   10,   192,  168,  33,   99};
    uint32_t src = 0u, dst = 0u;
    TEST_ASSERT_TRUE(rnlab_l03_header_addrs(ping, sizeof(ping), &src, &dst));
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 10), src);
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 99), dst);
    TEST_ASSERT_FALSE(rnlab_l03_header_addrs(ping, 19u, &src, &dst));
    uint8_t v6[20];
    for(int i = 0; i < 20; i++) v6[i] = ping[i];
    v6[0] = 0x60;
    TEST_ASSERT_FALSE(rnlab_l03_header_addrs(v6, sizeof(v6), &src, &dst));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mask_valid);
    RUN_TEST(test_mask_to_prefix);
    RUN_TEST(test_prefix_to_mask);
    RUN_TEST(test_prefix_mask_round_trip);
    RUN_TEST(test_network_and_broadcast);
    RUN_TEST(test_same_subnet);
    RUN_TEST(test_classify_dst);
    RUN_TEST(test_next_hop);
    RUN_TEST(test_parse_mask);
    RUN_TEST(test_parse_cidr);
    RUN_TEST(test_header_addrs);
    return UNITY_END();
}

/* cads_net_mac_from_uid: the per-board default MAC - fixed prefix, the
 * unicast + locally-administered bits, deterministic per UID, different
 * between UIDs (including ones differing in a single low bit, like two dies
 * from neighbouring wafer positions). */

#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/net/mac.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_prefix_and_address_bits(void) {
    static const uint32_t uid[3] = {0x00460036u, 0x3438510Au, 0x31383830u};
    uint8_t mac[6];
    cads_net_mac_from_uid(uid, mac);
    TEST_ASSERT_EQUAL_HEX8(0x02u, mac[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, mac[0] & 0x02u); /* locally administered */
    TEST_ASSERT_EQUAL_HEX8(0x00u, mac[0] & 0x01u); /* unicast */
    TEST_ASSERT_EQUAL_HEX8(0xCAu, mac[1]);
    TEST_ASSERT_EQUAL_HEX8(0xD5u, mac[2]);
}

static void test_same_uid_same_mac(void) {
    static const uint32_t uid[3] = {1u, 2u, 3u};
    uint8_t a[6], b[6];
    cads_net_mac_from_uid(uid, a);
    cads_net_mac_from_uid(uid, b);
    TEST_ASSERT_EQUAL_MEMORY(a, b, 6u);
}

static void test_neighbouring_uids_differ(void) {
    static const uint32_t u1[3] = {0x00460036u, 0x3438510Au, 0x31383830u};
    static const uint32_t u2[3] = {0x00460037u, 0x3438510Au, 0x31383830u};
    static const uint32_t u3[3] = {0x00460036u, 0x3438510Au, 0x31383831u};
    uint8_t a[6], b[6], c[6];
    cads_net_mac_from_uid(u1, a);
    cads_net_mac_from_uid(u2, b);
    cads_net_mac_from_uid(u3, c);
    TEST_ASSERT_FALSE(memcmp(a + 3, b + 3, 3u) == 0);
    TEST_ASSERT_FALSE(memcmp(a + 3, c + 3, 3u) == 0);
}

/* The old shared default must not come out for a typical UID either. */
static void test_not_the_old_fixed_default(void) {
    static const uint32_t uid[3] = {0x00460036u, 0x3438510Au, 0x31383830u};
    static const uint8_t old_default[6] = {0x02, 0xCA, 0xD5, 0x5E, 0x00, 0x01};
    uint8_t mac[6];
    cads_net_mac_from_uid(uid, mac);
    TEST_ASSERT_FALSE(memcmp(mac, old_default, 6u) == 0);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_prefix_and_address_bits);
    RUN_TEST(test_same_uid_same_mac);
    RUN_TEST(test_neighbouring_uids_differ);
    RUN_TEST(test_not_the_old_fixed_default);
    return UNITY_END();
}

/* cads_trafficstats: the M5 "passive traffic-mix statistics" line in
 * docs/ROADMAP.md - the portable classifier, independent of whether the
 * board this runs on has ever seen a single real frame. Deliberately no
 * table anywhere in this file, unlike every other M5 watcher's own
 * tests - the whole point of this one is that it needs none. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/toolbox/trafficstats.h"

void setUp(void) {
}

void tearDown(void) {
}

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/** Builds a minimal 14-byte Ethernet header (no payload) with the given
 *  destination and EtherType. */
static void build_eth_header(uint8_t* frame, const uint8_t dst[6], uint16_t ethertype) {
    memcpy(frame, dst, 6u);
    memset(frame + 6u, 0x02u, 6u); /* src: arbitrary, not classified */
    put_be16(frame + 12u, ethertype);
}

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t MULTICAST_IPV6[6] = {0x33, 0x33, 0x00, 0x00, 0x00, 0x01}; /* IPv6 multicast MAC range */
static const uint8_t UNICAST[6] = {0x02, 0xCA, 0xD5, 0x00, 0x00, 0x01};

static void test_init_zeroes_everything(void) {
    cads_trafficstats_t stats;
    memset(&stats, 0xAA, sizeof(stats)); /* poison first, to prove init actually clears it */
    cads_trafficstats_init(&stats);

    TEST_ASSERT_EQUAL_UINT32(0u, stats.total_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.total_bytes);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.ipv4_frames);
}

static void test_broadcast_arp_frame(void) {
    uint8_t frame[14];
    build_eth_header(frame, BROADCAST, 0x0806u);

    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);
    cads_trafficstats_observe(&stats, frame, sizeof(frame));

    TEST_ASSERT_EQUAL_UINT32(1u, stats.total_frames);
    TEST_ASSERT_EQUAL_UINT32(14u, stats.total_bytes);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.broadcast_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.multicast_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.unicast_frames);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.arp_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.runt_frames);
}

static void test_unicast_ipv4_frame(void) {
    uint8_t frame[14];
    build_eth_header(frame, UNICAST, 0x0800u);

    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);
    cads_trafficstats_observe(&stats, frame, sizeof(frame));

    TEST_ASSERT_EQUAL_UINT32(1u, stats.unicast_frames);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.ipv4_frames);
}

static void test_multicast_ipv6_frame(void) {
    uint8_t frame[14];
    build_eth_header(frame, MULTICAST_IPV6, 0x86DDu);

    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);
    cads_trafficstats_observe(&stats, frame, sizeof(frame));

    TEST_ASSERT_EQUAL_UINT32(1u, stats.multicast_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.broadcast_frames); /* multicast, not broadcast, despite both having bit0 set */
    TEST_ASSERT_EQUAL_UINT32(1u, stats.ipv6_frames);
}

static void test_vlan_tagged_ipv4_frame(void) {
    uint8_t frame[18];
    memcpy(frame, UNICAST, 6u);
    memset(frame + 6u, 0x02u, 6u);
    put_be16(frame + 12u, 0x8100u); /* VLAN tag */
    put_be16(frame + 14u, 0x0064u); /* TCI: VLAN 100 - not decoded here, just skipped over */
    put_be16(frame + 16u, 0x0800u); /* inner EtherType: IPv4 */

    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);
    cads_trafficstats_observe(&stats, frame, sizeof(frame));

    TEST_ASSERT_EQUAL_UINT32(1u, stats.vlan_tagged_frames);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.ipv4_frames); /* the INNER ethertype, not 0x8100 itself */
    TEST_ASSERT_EQUAL_UINT32(0u, stats.other_ethertype_frames);
}

static void test_vlan_tag_too_short_for_inner_ethertype(void) {
    uint8_t frame[16]; /* tag present (bytes 12-15) but no room for the inner EtherType */
    memcpy(frame, UNICAST, 6u);
    memset(frame + 6u, 0x02u, 6u);
    put_be16(frame + 12u, 0x8100u);
    put_be16(frame + 14u, 0x0064u);

    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);
    cads_trafficstats_observe(&stats, frame, sizeof(frame));

    TEST_ASSERT_EQUAL_UINT32(1u, stats.vlan_tagged_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.ipv4_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.arp_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.ipv6_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.other_ethertype_frames); /* not tallied either - genuinely unknown, not "other" */
}

static void test_unknown_ethertype_is_tallied_not_dropped(void) {
    uint8_t frame[14];
    build_eth_header(frame, UNICAST, 0x88B5u); /* IEEE 802 Local Experimental Ethertype 1 */

    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);
    cads_trafficstats_observe(&stats, frame, sizeof(frame));

    TEST_ASSERT_EQUAL_UINT32(1u, stats.other_ethertype_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.arp_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.ipv4_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.ipv6_frames);
}

static void test_runt_frame_counted_but_not_classified(void) {
    uint8_t frame[8] = {0}; /* shorter than a full Ethernet header */

    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);
    cads_trafficstats_observe(&stats, frame, sizeof(frame));

    TEST_ASSERT_EQUAL_UINT32(1u, stats.total_frames);
    TEST_ASSERT_EQUAL_UINT32(8u, stats.total_bytes);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.runt_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.broadcast_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.unicast_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.arp_frames);
}

static void test_zero_length_frame_counted_as_runt(void) {
    uint8_t frame[1] = {0};

    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);
    cads_trafficstats_observe(&stats, frame, 0u);

    TEST_ASSERT_EQUAL_UINT32(1u, stats.total_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.total_bytes);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.runt_frames);
}

static void test_repeated_observations_accumulate(void) {
    uint8_t arp_frame[14];
    build_eth_header(arp_frame, BROADCAST, 0x0806u);
    uint8_t ipv4_frame[14];
    build_eth_header(ipv4_frame, UNICAST, 0x0800u);

    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);
    cads_trafficstats_observe(&stats, arp_frame, sizeof(arp_frame));
    cads_trafficstats_observe(&stats, ipv4_frame, sizeof(ipv4_frame));
    cads_trafficstats_observe(&stats, ipv4_frame, sizeof(ipv4_frame));

    TEST_ASSERT_EQUAL_UINT32(3u, stats.total_frames);
    TEST_ASSERT_EQUAL_UINT32(42u, stats.total_bytes);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.arp_frames);
    TEST_ASSERT_EQUAL_UINT32(2u, stats.ipv4_frames);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.broadcast_frames);
    TEST_ASSERT_EQUAL_UINT32(2u, stats.unicast_frames);
}

static void test_null_arguments_are_refused(void) {
    cads_trafficstats_t stats;
    cads_trafficstats_init(&stats);
    uint8_t frame[14];
    build_eth_header(frame, UNICAST, 0x0800u);

    cads_trafficstats_observe(&stats, NULL, 14u);
    cads_trafficstats_observe(NULL, frame, 14u);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.total_frames);

    cads_trafficstats_init(NULL); /* must not crash */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_zeroes_everything);
    RUN_TEST(test_broadcast_arp_frame);
    RUN_TEST(test_unicast_ipv4_frame);
    RUN_TEST(test_multicast_ipv6_frame);
    RUN_TEST(test_vlan_tagged_ipv4_frame);
    RUN_TEST(test_vlan_tag_too_short_for_inner_ethertype);
    RUN_TEST(test_unknown_ethertype_is_tallied_not_dropped);
    RUN_TEST(test_runt_frame_counted_but_not_classified);
    RUN_TEST(test_zero_length_frame_counted_as_runt);
    RUN_TEST(test_repeated_observations_accumulate);
    RUN_TEST(test_null_arguments_are_refused);
    return UNITY_END();
}

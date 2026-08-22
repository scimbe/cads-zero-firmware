/* cads_arpwatch: the M5 "passive ARP spoofing / cache-poisoning
 * detector" line in docs/ROADMAP.md - the portable frame parser and
 * binding table, independent of whether the board this runs on has
 * ever seen a real ARP exchange, let alone a spoofed one. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/toolbox/arpwatch.h"

void setUp(void) {
}

void tearDown(void) {
}

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/** Builds a 42-byte Ethernet/ARP frame and returns its length. */
static uint16_t build_arp_frame(uint8_t* frame, const uint8_t sender_mac[6], uint32_t sender_ip, uint16_t opcode) {
    memset(frame, 0xFF, 6u); /* dst: broadcast */
    memcpy(frame + 6u, sender_mac, 6u);
    put_be16(frame + 12u, 0x0806u); /* EtherType: ARP */

    uint8_t* arp = frame + 14u;
    put_be16(arp + 0u, 1u);    /* htype: Ethernet */
    put_be16(arp + 2u, 0x0800u); /* ptype: IPv4 */
    arp[4u] = 6u;              /* hlen */
    arp[5u] = 4u;              /* plen */
    put_be16(arp + 6u, opcode);
    memcpy(arp + 8u, sender_mac, 6u);
    put_be32(arp + 14u, sender_ip);
    memset(arp + 18u, 0u, 6u); /* target mac: unknown/unused by the parser */
    put_be32(arp + 24u, 0u);  /* target ip: unused by the parser */

    return 14u + 28u;
}

static const uint8_t MAC_A[6] = {0x02, 0xCA, 0xD5, 0x00, 0x00, 0x01};
static const uint8_t MAC_B[6] = {0x02, 0xCA, 0xD5, 0x00, 0x00, 0x02};
#define IP_GATEWAY 0xC0A80101u /* 192.168.1.1 */

/* ---------------------------------------------------------------- parse */

static void test_reply_recognized(void) {
    uint8_t frame[42];
    uint16_t len = build_arp_frame(frame, MAC_A, IP_GATEWAY, CADS_ARPWATCH_REPLY);

    cads_arpwatch_claim_t claim;
    bool ok = cads_arpwatch_parse(frame, len, &claim);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_A, claim.sender_mac, 6u);
    TEST_ASSERT_EQUAL_UINT32(IP_GATEWAY, claim.sender_ip);
    TEST_ASSERT_EQUAL_UINT8(CADS_ARPWATCH_REPLY, claim.opcode);
}

static void test_request_recognized(void) {
    uint8_t frame[42];
    uint16_t len = build_arp_frame(frame, MAC_A, IP_GATEWAY, CADS_ARPWATCH_REQUEST);

    cads_arpwatch_claim_t claim;
    TEST_ASSERT_TRUE(cads_arpwatch_parse(frame, len, &claim));
    TEST_ASSERT_EQUAL_UINT8(CADS_ARPWATCH_REQUEST, claim.opcode);
}

static void test_wrong_ethertype_not_recognized(void) {
    uint8_t frame[42];
    uint16_t len = build_arp_frame(frame, MAC_A, IP_GATEWAY, CADS_ARPWATCH_REPLY);
    put_be16(frame + 12u, 0x0800u); /* IPv4, not ARP */

    cads_arpwatch_claim_t claim;
    TEST_ASSERT_FALSE(cads_arpwatch_parse(frame, len, &claim));
}

static void test_wrong_htype_not_recognized(void) {
    uint8_t frame[42];
    uint16_t len = build_arp_frame(frame, MAC_A, IP_GATEWAY, CADS_ARPWATCH_REPLY);
    put_be16(frame + 14u, 6u); /* not Ethernet */

    cads_arpwatch_claim_t claim;
    TEST_ASSERT_FALSE(cads_arpwatch_parse(frame, len, &claim));
}

static void test_wrong_addr_lengths_not_recognized(void) {
    uint8_t frame[42];
    uint16_t len = build_arp_frame(frame, MAC_A, IP_GATEWAY, CADS_ARPWATCH_REPLY);
    frame[14u + 4u] = 8u; /* hlen should be 6 */

    cads_arpwatch_claim_t claim;
    TEST_ASSERT_FALSE(cads_arpwatch_parse(frame, len, &claim));
}

static void test_unknown_opcode_not_recognized(void) {
    uint8_t frame[42];
    uint16_t len = build_arp_frame(frame, MAC_A, IP_GATEWAY, 3u); /* RARP request - not tracked */

    cads_arpwatch_claim_t claim;
    TEST_ASSERT_FALSE(cads_arpwatch_parse(frame, len, &claim));
}

static void test_truncated_frame_not_recognized(void) {
    uint8_t frame[42];
    uint16_t len = build_arp_frame(frame, MAC_A, IP_GATEWAY, CADS_ARPWATCH_REPLY);

    cads_arpwatch_claim_t claim;
    TEST_ASSERT_FALSE(cads_arpwatch_parse(frame, (uint16_t)(len - 5u), &claim));
}

static void test_parse_null_arguments_are_refused(void) {
    uint8_t frame[42];
    uint16_t len = build_arp_frame(frame, MAC_A, IP_GATEWAY, CADS_ARPWATCH_REPLY);
    cads_arpwatch_claim_t claim;
    TEST_ASSERT_FALSE(cads_arpwatch_parse(NULL, len, &claim));
    TEST_ASSERT_FALSE(cads_arpwatch_parse(frame, len, NULL));
}

/* ---------------------------------------------------------------- table */

static void test_first_sighting_is_not_a_change(void) {
    cads_arpwatch_entry_t storage[4];
    cads_arpwatch_table_t table;
    cads_arpwatch_table_init(&table, storage, 4u);

    bool changed = cads_arpwatch_table_learn(&table, IP_GATEWAY, MAC_A);

    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_EQUAL_UINT(1u, cads_arpwatch_table_count(&table));
    const cads_arpwatch_entry_t* entry = cads_arpwatch_table_at(&table, 0u);
    TEST_ASSERT_EQUAL_UINT32(1u, entry->sightings);
    TEST_ASSERT_EQUAL_UINT32(0u, entry->mac_changes);
}

static void test_repeated_same_mac_is_not_a_change(void) {
    cads_arpwatch_entry_t storage[4];
    cads_arpwatch_table_t table;
    cads_arpwatch_table_init(&table, storage, 4u);

    cads_arpwatch_table_learn(&table, IP_GATEWAY, MAC_A);
    bool changed = cads_arpwatch_table_learn(&table, IP_GATEWAY, MAC_A);

    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_EQUAL_UINT(1u, cads_arpwatch_table_count(&table));
    TEST_ASSERT_EQUAL_UINT32(2u, cads_arpwatch_table_at(&table, 0u)->sightings);
    TEST_ASSERT_EQUAL_UINT32(0u, cads_arpwatch_table_at(&table, 0u)->mac_changes);
    TEST_ASSERT_FALSE(cads_arpwatch_table_any_flip(&table));
}

static void test_different_mac_is_a_change(void) {
    cads_arpwatch_entry_t storage[4];
    cads_arpwatch_table_t table;
    cads_arpwatch_table_init(&table, storage, 4u);

    cads_arpwatch_table_learn(&table, IP_GATEWAY, MAC_A);
    bool changed = cads_arpwatch_table_learn(&table, IP_GATEWAY, MAC_B);

    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL_UINT(1u, cads_arpwatch_table_count(&table)); /* still one entry, rebound */
    const cads_arpwatch_entry_t* entry = cads_arpwatch_table_at(&table, 0u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_B, entry->mac, 6u);
    TEST_ASSERT_EQUAL_UINT32(1u, entry->mac_changes);
    TEST_ASSERT_TRUE(cads_arpwatch_table_any_flip(&table));
}

static void test_distinct_ips_are_separate_entries(void) {
    cads_arpwatch_entry_t storage[4];
    cads_arpwatch_table_t table;
    cads_arpwatch_table_init(&table, storage, 4u);

    cads_arpwatch_table_learn(&table, IP_GATEWAY, MAC_A);
    cads_arpwatch_table_learn(&table, IP_GATEWAY + 1u, MAC_B);

    TEST_ASSERT_EQUAL_UINT(2u, cads_arpwatch_table_count(&table));
    TEST_ASSERT_FALSE(cads_arpwatch_table_any_flip(&table));
}

static void test_full_table_drops_and_counts_new_ip(void) {
    cads_arpwatch_entry_t storage[1];
    cads_arpwatch_table_t table;
    cads_arpwatch_table_init(&table, storage, 1u);

    cads_arpwatch_table_learn(&table, IP_GATEWAY, MAC_A);
    cads_arpwatch_table_learn(&table, IP_GATEWAY + 1u, MAC_B);

    TEST_ASSERT_EQUAL_UINT(1u, cads_arpwatch_table_count(&table));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_arpwatch_table_dropped_total(&table));
}

static void test_table_at_out_of_range_is_null(void) {
    cads_arpwatch_entry_t storage[4];
    cads_arpwatch_table_t table;
    cads_arpwatch_table_init(&table, storage, 4u);
    TEST_ASSERT_NULL(cads_arpwatch_table_at(&table, 0u));
    TEST_ASSERT_NULL(cads_arpwatch_table_at(&table, 99u));
}

static void test_table_null_arguments_are_refused(void) {
    cads_arpwatch_entry_t storage[4];
    cads_arpwatch_table_t table;
    cads_arpwatch_table_init(&table, storage, 4u);

    TEST_ASSERT_FALSE(cads_arpwatch_table_learn(&table, IP_GATEWAY, NULL));
    TEST_ASSERT_FALSE(cads_arpwatch_table_learn(NULL, IP_GATEWAY, MAC_A));
    TEST_ASSERT_EQUAL_UINT(0u, cads_arpwatch_table_count(&table));
    TEST_ASSERT_EQUAL_UINT(0u, cads_arpwatch_table_count(NULL));
    TEST_ASSERT_NULL(cads_arpwatch_table_at(NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_arpwatch_table_dropped_total(NULL));
    TEST_ASSERT_FALSE(cads_arpwatch_table_any_flip(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_reply_recognized);
    RUN_TEST(test_request_recognized);
    RUN_TEST(test_wrong_ethertype_not_recognized);
    RUN_TEST(test_wrong_htype_not_recognized);
    RUN_TEST(test_wrong_addr_lengths_not_recognized);
    RUN_TEST(test_unknown_opcode_not_recognized);
    RUN_TEST(test_truncated_frame_not_recognized);
    RUN_TEST(test_parse_null_arguments_are_refused);
    RUN_TEST(test_first_sighting_is_not_a_change);
    RUN_TEST(test_repeated_same_mac_is_not_a_change);
    RUN_TEST(test_different_mac_is_a_change);
    RUN_TEST(test_distinct_ips_are_separate_entries);
    RUN_TEST(test_full_table_drops_and_counts_new_ip);
    RUN_TEST(test_table_at_out_of_range_is_null);
    RUN_TEST(test_table_null_arguments_are_refused);
    return UNITY_END();
}

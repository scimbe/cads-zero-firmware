/* cads_dhcpwatch: the M5 "passive rogue-DHCP-server detector" line in
 * docs/ROADMAP.md - the portable frame parser and dedup table,
 * independent of whether the board this runs on has ever seen a real
 * DHCP exchange. Frames are built by cads_test_build_dhcp_frame() below
 * (byte-for-byte against RFC 2131/2132's BOOTP/DHCP layout, not
 * captured off a network) rather than hand-counted literal arrays - the
 * fixed BOOTP section alone is 236 bytes, too easy to miscount by hand
 * and have the mistake go unnoticed. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/toolbox/dhcpwatch.h"

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

/**
 * Fills `frame` with a server->client DHCP frame (src port 67, dst port
 * 68) and returns its total length. `server_mac`/`ip_src` are the
 * Ethernet/IP source (the answering server); `yiaddr` is the offered
 * address; `msg_type` is the DHCP option 53 value; `server_id_ip`, if
 * `include_server_id` is true, becomes option 54.
 */
static uint16_t build_dhcp_frame(
    uint8_t* frame, const uint8_t server_mac[6], uint32_t ip_src, uint8_t msg_type, uint32_t yiaddr,
    bool include_server_id, uint32_t server_id_ip) {
    uint16_t offset = 0u;

    /* --- Ethernet: broadcast dst, server src, IPv4 --- */
    memset(frame + offset, 0xFF, 6u);
    offset += 6u;
    memcpy(frame + offset, server_mac, 6u);
    offset += 6u;
    put_be16(frame + offset, 0x0800u);
    offset += 2u;

    uint16_t ip_header_offset = offset;
    /* --- IPv4 header, 20 B, no options --- */
    frame[offset + 0u] = 0x45u; /* version 4, IHL 5 */
    frame[offset + 1u] = 0x00u;
    /* total_length filled in below, once the payload size is known */
    frame[offset + 4u] = 0x00u;
    frame[offset + 5u] = 0x00u;
    frame[offset + 6u] = 0x00u;
    frame[offset + 7u] = 0x00u;
    frame[offset + 8u] = 0x40u;
    frame[offset + 9u] = 17u; /* UDP */
    frame[offset + 10u] = 0x00u;
    frame[offset + 11u] = 0x00u;
    put_be32(frame + offset + 12u, ip_src);
    put_be32(frame + offset + 16u, 0xFFFFFFFFu); /* broadcast dst */
    offset += 20u;

    uint16_t udp_offset = offset;
    /* --- UDP header --- */
    put_be16(frame + offset + 0u, 67u); /* src port: server */
    put_be16(frame + offset + 2u, 68u); /* dst port: client */
    /* length filled in below */
    frame[offset + 6u] = 0x00u;
    frame[offset + 7u] = 0x00u;
    offset += 8u;

    /* --- BOOTP fixed fields, 236 B --- */
    memset(frame + offset, 0, 236u);
    frame[offset + 0u] = 2u; /* op: BOOTREPLY */
    frame[offset + 1u] = 1u; /* htype: Ethernet */
    frame[offset + 2u] = 6u; /* hlen */
    put_be32(frame + offset + 16u, yiaddr); /* yiaddr at +16 */
    offset += 236u;

    /* --- magic cookie --- */
    frame[offset + 0u] = 0x63u;
    frame[offset + 1u] = 0x82u;
    frame[offset + 2u] = 0x53u;
    frame[offset + 3u] = 0x63u;
    offset += 4u;

    /* --- options: message type (53), optionally server id (54), end --- */
    frame[offset + 0u] = 53u;
    frame[offset + 1u] = 1u;
    frame[offset + 2u] = msg_type;
    offset += 3u;

    if(include_server_id) {
        frame[offset + 0u] = 54u;
        frame[offset + 1u] = 4u;
        put_be32(frame + offset + 2u, server_id_ip);
        offset += 6u;
    }

    frame[offset] = 255u; /* end */
    offset += 1u;

    uint16_t udp_len = (uint16_t)(offset - udp_offset);
    put_be16(frame + udp_offset + 4u, udp_len);
    uint16_t ip_total_len = (uint16_t)(offset - ip_header_offset);
    put_be16(frame + ip_header_offset + 2u, ip_total_len);

    return offset;
}

static const uint8_t SERVER_A[6] = {0x02, 0xCA, 0xD5, 0x00, 0x01, 0x00};
static const uint8_t SERVER_B[6] = {0x02, 0xCA, 0xD5, 0x00, 0x01, 0x01};

/* ---------------------------------------------------------------- parse */

static void test_offer_recognized_with_server_id(void) {
    uint8_t frame[300];
    uint16_t len = build_dhcp_frame(
        frame, SERVER_A, 0xC0A80101u, CADS_DHCPWATCH_OFFER, 0xC0A80164u, true, 0xC0A80101u);

    cads_dhcpwatch_record_t record;
    bool ok = cads_dhcpwatch_parse(frame, len, &record);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_A, record.src_mac, 6u);
    TEST_ASSERT_EQUAL_UINT32(0xC0A80101u, record.server_ip);
    TEST_ASSERT_EQUAL_UINT32(0xC0A80164u, record.offered_ip);
    TEST_ASSERT_EQUAL_UINT8(CADS_DHCPWATCH_OFFER, record.msg_type);
}

static void test_ack_without_server_id_falls_back_to_ip_source(void) {
    uint8_t frame[300];
    uint16_t len = build_dhcp_frame(frame, SERVER_B, 0x0A000001u, CADS_DHCPWATCH_ACK, 0x0A000064u, false, 0u);

    cads_dhcpwatch_record_t record;
    bool ok = cads_dhcpwatch_parse(frame, len, &record);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(0x0A000001u, record.server_ip); /* fell back to IP header src */
    TEST_ASSERT_EQUAL_UINT8(CADS_DHCPWATCH_ACK, record.msg_type);
}

static void test_nak_recognized(void) {
    uint8_t frame[300];
    uint16_t len = build_dhcp_frame(frame, SERVER_A, 0xC0A80101u, CADS_DHCPWATCH_NAK, 0u, false, 0u);

    cads_dhcpwatch_record_t record;
    TEST_ASSERT_TRUE(cads_dhcpwatch_parse(frame, len, &record));
    TEST_ASSERT_EQUAL_UINT8(CADS_DHCPWATCH_NAK, record.msg_type);
}

static void test_client_originated_discover_not_recognized(void) {
    /* DHCPDISCOVER (type 1) is a client message - even if it somehow
     * carried server ports, this is not what this file watches for. */
    uint8_t frame[300];
    uint16_t len = build_dhcp_frame(frame, SERVER_A, 0xC0A80101u, 1u /* DISCOVER */, 0u, false, 0u);

    cads_dhcpwatch_record_t record;
    TEST_ASSERT_FALSE(cads_dhcpwatch_parse(frame, len, &record));
}

static void test_wrong_ports_not_recognized(void) {
    uint8_t frame[300];
    uint16_t len = build_dhcp_frame(frame, SERVER_A, 0xC0A80101u, CADS_DHCPWATCH_OFFER, 0xC0A80164u, false, 0u);
    /* Flip to client->server ports (68->67) - this file only watches
     * server->client (67->68) traffic. */
    put_be16(frame + 14u + 20u + 0u, 68u);
    put_be16(frame + 14u + 20u + 2u, 67u);

    cads_dhcpwatch_record_t record;
    TEST_ASSERT_FALSE(cads_dhcpwatch_parse(frame, len, &record));
}

static void test_wrong_magic_cookie_not_recognized(void) {
    uint8_t frame[300];
    uint16_t len = build_dhcp_frame(frame, SERVER_A, 0xC0A80101u, CADS_DHCPWATCH_OFFER, 0xC0A80164u, false, 0u);
    frame[14u + 20u + 8u + 236u] = 0x00u; /* corrupt the magic cookie's first byte */

    cads_dhcpwatch_record_t record;
    TEST_ASSERT_FALSE(cads_dhcpwatch_parse(frame, len, &record));
}

static void test_non_udp_not_recognized(void) {
    uint8_t frame[300];
    uint16_t len = build_dhcp_frame(frame, SERVER_A, 0xC0A80101u, CADS_DHCPWATCH_OFFER, 0xC0A80164u, false, 0u);
    frame[14u + 9u] = 6u; /* TCP, not UDP */

    cads_dhcpwatch_record_t record;
    TEST_ASSERT_FALSE(cads_dhcpwatch_parse(frame, len, &record));
}

static void test_truncated_frame_not_recognized(void) {
    uint8_t frame[300];
    uint16_t len = build_dhcp_frame(frame, SERVER_A, 0xC0A80101u, CADS_DHCPWATCH_OFFER, 0xC0A80164u, false, 0u);

    cads_dhcpwatch_record_t record;
    TEST_ASSERT_FALSE(cads_dhcpwatch_parse(frame, (uint16_t)(len - 20u), &record));
}

static void test_parse_null_arguments_are_refused(void) {
    uint8_t frame[300];
    uint16_t len = build_dhcp_frame(frame, SERVER_A, 0xC0A80101u, CADS_DHCPWATCH_OFFER, 0xC0A80164u, false, 0u);
    cads_dhcpwatch_record_t record;
    TEST_ASSERT_FALSE(cads_dhcpwatch_parse(NULL, len, &record));
    TEST_ASSERT_FALSE(cads_dhcpwatch_parse(frame, len, NULL));
}

/* ---------------------------------------------------------------- table */

static void test_table_learn_then_appears(void) {
    cads_dhcpwatch_record_t storage[4];
    cads_dhcpwatch_table_t table;
    cads_dhcpwatch_table_init(&table, storage, 4u);

    cads_dhcpwatch_record_t record;
    memset(&record, 0, sizeof(record));
    memcpy(record.src_mac, SERVER_A, 6u);
    record.server_ip = 0xC0A80101u;

    cads_dhcpwatch_table_learn(&table, &record);

    TEST_ASSERT_EQUAL_UINT(1u, cads_dhcpwatch_table_count(&table));
    TEST_ASSERT_FALSE(cads_dhcpwatch_table_multiple_servers(&table));
}

static void test_table_same_mac_refreshes_not_duplicates(void) {
    cads_dhcpwatch_record_t storage[4];
    cads_dhcpwatch_table_t table;
    cads_dhcpwatch_table_init(&table, storage, 4u);

    cads_dhcpwatch_record_t record;
    memset(&record, 0, sizeof(record));
    memcpy(record.src_mac, SERVER_A, 6u);
    record.offered_ip = 0xC0A80164u;
    cads_dhcpwatch_table_learn(&table, &record);

    record.offered_ip = 0xC0A80165u; /* a second lease offered by the same server */
    cads_dhcpwatch_table_learn(&table, &record);

    TEST_ASSERT_EQUAL_UINT(1u, cads_dhcpwatch_table_count(&table));
    TEST_ASSERT_EQUAL_UINT32(0xC0A80165u, cads_dhcpwatch_table_at(&table, 0u)->offered_ip);
}

static void test_table_two_distinct_macs_trip_multiple_servers(void) {
    cads_dhcpwatch_record_t storage[4];
    cads_dhcpwatch_table_t table;
    cads_dhcpwatch_table_init(&table, storage, 4u);

    cads_dhcpwatch_record_t record;
    memset(&record, 0, sizeof(record));
    memcpy(record.src_mac, SERVER_A, 6u);
    cads_dhcpwatch_table_learn(&table, &record);

    TEST_ASSERT_FALSE(cads_dhcpwatch_table_multiple_servers(&table));

    memcpy(record.src_mac, SERVER_B, 6u);
    cads_dhcpwatch_table_learn(&table, &record);

    TEST_ASSERT_EQUAL_UINT(2u, cads_dhcpwatch_table_count(&table));
    TEST_ASSERT_TRUE(cads_dhcpwatch_table_multiple_servers(&table));
}

static void test_table_full_drops_and_counts_new_entries(void) {
    cads_dhcpwatch_record_t storage[1];
    cads_dhcpwatch_table_t table;
    cads_dhcpwatch_table_init(&table, storage, 1u);

    cads_dhcpwatch_record_t record;
    memset(&record, 0, sizeof(record));
    memcpy(record.src_mac, SERVER_A, 6u);
    cads_dhcpwatch_table_learn(&table, &record);
    memcpy(record.src_mac, SERVER_B, 6u);
    cads_dhcpwatch_table_learn(&table, &record);

    TEST_ASSERT_EQUAL_UINT(1u, cads_dhcpwatch_table_count(&table));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_dhcpwatch_table_dropped_total(&table));
}

static void test_table_at_out_of_range_is_null(void) {
    cads_dhcpwatch_record_t storage[4];
    cads_dhcpwatch_table_t table;
    cads_dhcpwatch_table_init(&table, storage, 4u);
    TEST_ASSERT_NULL(cads_dhcpwatch_table_at(&table, 0u));
    TEST_ASSERT_NULL(cads_dhcpwatch_table_at(&table, 99u));
}

static void test_table_null_arguments_are_refused(void) {
    cads_dhcpwatch_record_t storage[4];
    cads_dhcpwatch_table_t table;
    cads_dhcpwatch_table_init(&table, storage, 4u);

    cads_dhcpwatch_record_t record;
    memset(&record, 0, sizeof(record));

    cads_dhcpwatch_table_learn(&table, NULL);
    cads_dhcpwatch_table_learn(NULL, &record);
    TEST_ASSERT_EQUAL_UINT(0u, cads_dhcpwatch_table_count(&table));
    TEST_ASSERT_EQUAL_UINT(0u, cads_dhcpwatch_table_count(NULL));
    TEST_ASSERT_NULL(cads_dhcpwatch_table_at(NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_dhcpwatch_table_dropped_total(NULL));
    TEST_ASSERT_FALSE(cads_dhcpwatch_table_multiple_servers(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_offer_recognized_with_server_id);
    RUN_TEST(test_ack_without_server_id_falls_back_to_ip_source);
    RUN_TEST(test_nak_recognized);
    RUN_TEST(test_client_originated_discover_not_recognized);
    RUN_TEST(test_wrong_ports_not_recognized);
    RUN_TEST(test_wrong_magic_cookie_not_recognized);
    RUN_TEST(test_non_udp_not_recognized);
    RUN_TEST(test_truncated_frame_not_recognized);
    RUN_TEST(test_parse_null_arguments_are_refused);
    RUN_TEST(test_table_learn_then_appears);
    RUN_TEST(test_table_same_mac_refreshes_not_duplicates);
    RUN_TEST(test_table_two_distinct_macs_trip_multiple_servers);
    RUN_TEST(test_table_full_drops_and_counts_new_entries);
    RUN_TEST(test_table_at_out_of_range_is_null);
    RUN_TEST(test_table_null_arguments_are_refused);
    return UNITY_END();
}

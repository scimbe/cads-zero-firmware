/* cads_ssdpwatch: the M5 "passive SSDP/UPnP device discovery" line in
 * docs/ROADMAP.md - the portable text-header parser and dedup table,
 * independent of whether the board this runs on has ever seen a real
 * UPnP device announce itself. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/toolbox/ssdpwatch.h"

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

/** Builds an Ethernet/IPv4/UDP frame carrying `payload` at `dst_port` and
 *  returns its total length. `frame` must have room for 42 + payload_len. */
static uint16_t build_udp_frame(
    uint8_t* frame, const uint8_t src_mac[6], uint32_t ip_src, uint16_t dst_port, const char* payload,
    size_t payload_len) {
    memset(frame, 0xFF, 6u); /* dst: broadcast/multicast, not checked */
    memcpy(frame + 6u, src_mac, 6u);
    put_be16(frame + 12u, 0x0800u);

    uint16_t ip_offset = 14u;
    frame[ip_offset + 0u] = 0x45u;
    frame[ip_offset + 1u] = 0x00u;
    frame[ip_offset + 4u] = 0x00u;
    frame[ip_offset + 5u] = 0x00u;
    frame[ip_offset + 6u] = 0x00u;
    frame[ip_offset + 7u] = 0x00u;
    frame[ip_offset + 8u] = 0x04u;
    frame[ip_offset + 9u] = 17u; /* UDP */
    frame[ip_offset + 10u] = 0x00u;
    frame[ip_offset + 11u] = 0x00u;
    put_be32(frame + ip_offset + 12u, ip_src);
    put_be32(frame + ip_offset + 16u, 0xEFFFFFFAu); /* 239.255.255.250 */

    uint16_t udp_offset = (uint16_t)(ip_offset + 20u);
    put_be16(frame + udp_offset + 0u, 55000u); /* src port: arbitrary high port */
    put_be16(frame + udp_offset + 2u, dst_port);
    frame[udp_offset + 6u] = 0x00u;
    frame[udp_offset + 7u] = 0x00u;

    uint16_t payload_offset = (uint16_t)(udp_offset + 8u);
    memcpy(frame + payload_offset, payload, payload_len);

    uint16_t total = (uint16_t)(payload_offset + payload_len);
    uint16_t udp_len = (uint16_t)(total - udp_offset);
    put_be16(frame + udp_offset + 4u, udp_len);
    uint16_t ip_total_len = (uint16_t)(total - ip_offset);
    put_be16(frame + ip_offset + 2u, ip_total_len);

    return total;
}

static const uint8_t DEVICE_MAC[6] = {0x02, 0xCA, 0xD5, 0x00, 0x02, 0x00};
#define DEVICE_IP 0xC0A80132u /* 192.168.1.50 */

static const char NOTIFY_ALIVE[] =
    "NOTIFY * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "CACHE-CONTROL: max-age=1800\r\n"
    "LOCATION: http://192.168.1.50/d\r\n"
    "NT: upnp:rootdevice\r\n"
    "NTS: ssdp:alive\r\n"
    "USN: short-usn-1\r\n"
    "\r\n";

static const char NOTIFY_BYEBYE[] =
    "NOTIFY * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "NT: upnp:rootdevice\r\n"
    "NTS: ssdp:byebye\r\n"
    "USN: short-usn-1\r\n"
    "\r\n";

static const char SEARCH_RESPONSE[] =
    "HTTP/1.1 200 OK\r\n"
    "CACHE-CONTROL: max-age=1800\r\n"
    "LOCATION: http://192.168.1.50:8080/d.xml\r\n"
    "USN: short-usn-1\r\n"
    "\r\n";

/* ---------------------------------------------------------------- parse */

static void test_notify_alive_recognized(void) {
    uint8_t frame[300];
    uint16_t len = build_udp_frame(frame, DEVICE_MAC, DEVICE_IP, 1900u, NOTIFY_ALIVE, strlen(NOTIFY_ALIVE));

    cads_ssdpwatch_record_t record;
    bool ok = cads_ssdpwatch_parse(frame, len, &record);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(CADS_SSDP_ALIVE, record.kind);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DEVICE_MAC, record.src_mac, 6u);
    TEST_ASSERT_EQUAL_UINT32(DEVICE_IP, record.src_ip);
    TEST_ASSERT_EQUAL_STRING("short-usn-1", record.usn);
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50/d", record.location);
}

static void test_long_location_is_truncated_not_refused(void) {
    static const char notify_long_location[] =
        "NOTIFY * HTTP/1.1\r\n"
        "LOCATION: http://192.168.1.50:8080/description.xml\r\n"
        "NTS: ssdp:alive\r\n"
        "USN: x\r\n"
        "\r\n";
    uint8_t frame[300];
    uint16_t len =
        build_udp_frame(frame, DEVICE_MAC, DEVICE_IP, 1900u, notify_long_location, strlen(notify_long_location));

    cads_ssdpwatch_record_t record;
    TEST_ASSERT_TRUE(cads_ssdpwatch_parse(frame, len, &record));
    /* CADS_SSDPWATCH_LOCATION_MAX is 28 - 27 characters kept, then NUL. */
    TEST_ASSERT_EQUAL_UINT(27u, strlen(record.location));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50:8080/de", record.location);
}

static void test_notify_byebye_recognized(void) {
    uint8_t frame[300];
    uint16_t len = build_udp_frame(frame, DEVICE_MAC, DEVICE_IP, 1900u, NOTIFY_BYEBYE, strlen(NOTIFY_BYEBYE));

    cads_ssdpwatch_record_t record;
    TEST_ASSERT_TRUE(cads_ssdpwatch_parse(frame, len, &record));
    TEST_ASSERT_EQUAL_INT(CADS_SSDP_BYEBYE, record.kind);
}

static void test_search_response_recognized(void) {
    uint8_t frame[300];
    uint16_t len = build_udp_frame(frame, DEVICE_MAC, DEVICE_IP, 1900u, SEARCH_RESPONSE, strlen(SEARCH_RESPONSE));

    cads_ssdpwatch_record_t record;
    TEST_ASSERT_TRUE(cads_ssdpwatch_parse(frame, len, &record));
    TEST_ASSERT_EQUAL_INT(CADS_SSDP_RESPONSE, record.kind);
    TEST_ASSERT_EQUAL_STRING("short-usn-1", record.usn);
}

static void test_wrong_port_not_recognized(void) {
    uint8_t frame[300];
    uint16_t len = build_udp_frame(frame, DEVICE_MAC, DEVICE_IP, 5353u, NOTIFY_ALIVE, strlen(NOTIFY_ALIVE));

    cads_ssdpwatch_record_t record;
    TEST_ASSERT_FALSE(cads_ssdpwatch_parse(frame, len, &record));
}

static void test_non_ssdp_payload_not_recognized(void) {
    static const char junk[] = "not an ssdp message at all\r\n";
    uint8_t frame[300];
    uint16_t len = build_udp_frame(frame, DEVICE_MAC, DEVICE_IP, 1900u, junk, strlen(junk));

    cads_ssdpwatch_record_t record;
    TEST_ASSERT_FALSE(cads_ssdpwatch_parse(frame, len, &record));
}

static void test_notify_without_nts_is_other(void) {
    static const char notify_no_nts[] = "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n\r\n";
    uint8_t frame[300];
    uint16_t len = build_udp_frame(frame, DEVICE_MAC, DEVICE_IP, 1900u, notify_no_nts, strlen(notify_no_nts));

    cads_ssdpwatch_record_t record;
    TEST_ASSERT_TRUE(cads_ssdpwatch_parse(frame, len, &record));
    TEST_ASSERT_EQUAL_INT(CADS_SSDP_OTHER, record.kind);
}

static void test_truncated_frame_not_recognized(void) {
    uint8_t frame[300];
    build_udp_frame(frame, DEVICE_MAC, DEVICE_IP, 1900u, NOTIFY_ALIVE, strlen(NOTIFY_ALIVE));

    cads_ssdpwatch_record_t record;
    TEST_ASSERT_FALSE(cads_ssdpwatch_parse(frame, 20u, &record)); /* cut off mid-IP-header */
}

static void test_parse_null_arguments_are_refused(void) {
    uint8_t frame[300];
    uint16_t len = build_udp_frame(frame, DEVICE_MAC, DEVICE_IP, 1900u, NOTIFY_ALIVE, strlen(NOTIFY_ALIVE));
    cads_ssdpwatch_record_t record;
    TEST_ASSERT_FALSE(cads_ssdpwatch_parse(NULL, len, &record));
    TEST_ASSERT_FALSE(cads_ssdpwatch_parse(frame, len, NULL));
}

/* ------------------------------------------------------- find_header */

static void test_find_header_case_insensitive(void) {
    static const char payload[] = "NOTIFY * HTTP/1.1\r\nusn: lowercase-header-name\r\n\r\n";
    char out[32];
    bool ok = cads_ssdpwatch_find_header((const uint8_t*)payload, (uint16_t)strlen(payload), "USN:", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("lowercase-header-name", out);
}

static void test_find_header_missing_returns_false(void) {
    static const char payload[] = "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n\r\n";
    char out[32] = "untouched";
    bool ok = cads_ssdpwatch_find_header((const uint8_t*)payload, (uint16_t)strlen(payload), "USN:", out, sizeof(out));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_STRING("untouched", out); /* out is left alone on a miss */
}

static void test_find_header_does_not_match_mid_line(void) {
    /* "MY-USN:" contains "USN:" but not at a line start - must not match. */
    static const char payload[] = "NOTIFY * HTTP/1.1\r\nMY-USN: should-not-match\r\n\r\n";
    char out[32];
    bool ok = cads_ssdpwatch_find_header((const uint8_t*)payload, (uint16_t)strlen(payload), "USN:", out, sizeof(out));
    TEST_ASSERT_FALSE(ok);
}

/* ---------------------------------------------------------------- table */

static cads_ssdpwatch_record_t make_record(const uint8_t mac[6], const char* usn) {
    cads_ssdpwatch_record_t record;
    memset(&record, 0, sizeof(record));
    record.kind = CADS_SSDP_ALIVE;
    memcpy(record.src_mac, mac, 6u);
    strncpy(record.usn, usn, sizeof(record.usn) - 1u);
    return record;
}

static void test_table_learn_then_appears(void) {
    cads_ssdpwatch_record_t storage[4];
    cads_ssdpwatch_table_t table;
    cads_ssdpwatch_table_init(&table, storage, 4u);

    cads_ssdpwatch_record_t record = make_record(DEVICE_MAC, "usn-a");
    cads_ssdpwatch_table_learn(&table, &record);

    TEST_ASSERT_EQUAL_UINT(1u, cads_ssdpwatch_table_count(&table));
    TEST_ASSERT_EQUAL_STRING("usn-a", cads_ssdpwatch_table_at(&table, 0u)->usn);
}

static void test_table_same_mac_and_usn_refreshes_not_duplicates(void) {
    cads_ssdpwatch_record_t storage[4];
    cads_ssdpwatch_table_t table;
    cads_ssdpwatch_table_init(&table, storage, 4u);

    cads_ssdpwatch_record_t record = make_record(DEVICE_MAC, "usn-a");
    record.kind = CADS_SSDP_ALIVE;
    cads_ssdpwatch_table_learn(&table, &record);

    record.kind = CADS_SSDP_BYEBYE; /* the same device announcing it is leaving */
    cads_ssdpwatch_table_learn(&table, &record);

    TEST_ASSERT_EQUAL_UINT(1u, cads_ssdpwatch_table_count(&table));
    TEST_ASSERT_EQUAL_INT(CADS_SSDP_BYEBYE, cads_ssdpwatch_table_at(&table, 0u)->kind);
}

static void test_table_same_mac_different_usn_is_a_separate_entry(void) {
    /* One device can expose more than one UPnP service. */
    cads_ssdpwatch_record_t storage[4];
    cads_ssdpwatch_table_t table;
    cads_ssdpwatch_table_init(&table, storage, 4u);

    cads_ssdpwatch_record_t a = make_record(DEVICE_MAC, "usn-a");
    cads_ssdpwatch_record_t b = make_record(DEVICE_MAC, "usn-b");
    cads_ssdpwatch_table_learn(&table, &a);
    cads_ssdpwatch_table_learn(&table, &b);

    TEST_ASSERT_EQUAL_UINT(2u, cads_ssdpwatch_table_count(&table));
}

static void test_table_full_drops_and_counts_new_entries(void) {
    cads_ssdpwatch_record_t storage[1];
    cads_ssdpwatch_table_t table;
    cads_ssdpwatch_table_init(&table, storage, 1u);

    cads_ssdpwatch_record_t a = make_record(DEVICE_MAC, "usn-a");
    cads_ssdpwatch_record_t b = make_record(DEVICE_MAC, "usn-b");
    cads_ssdpwatch_table_learn(&table, &a);
    cads_ssdpwatch_table_learn(&table, &b);

    TEST_ASSERT_EQUAL_UINT(1u, cads_ssdpwatch_table_count(&table));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_ssdpwatch_table_dropped_total(&table));
}

static void test_table_at_out_of_range_is_null(void) {
    cads_ssdpwatch_record_t storage[4];
    cads_ssdpwatch_table_t table;
    cads_ssdpwatch_table_init(&table, storage, 4u);
    TEST_ASSERT_NULL(cads_ssdpwatch_table_at(&table, 0u));
    TEST_ASSERT_NULL(cads_ssdpwatch_table_at(&table, 99u));
}

static void test_table_null_arguments_are_refused(void) {
    cads_ssdpwatch_record_t storage[4];
    cads_ssdpwatch_table_t table;
    cads_ssdpwatch_table_init(&table, storage, 4u);

    cads_ssdpwatch_record_t record = make_record(DEVICE_MAC, "usn-a");
    cads_ssdpwatch_table_learn(&table, NULL);
    cads_ssdpwatch_table_learn(NULL, &record);
    TEST_ASSERT_EQUAL_UINT(0u, cads_ssdpwatch_table_count(&table));
    TEST_ASSERT_EQUAL_UINT(0u, cads_ssdpwatch_table_count(NULL));
    TEST_ASSERT_NULL(cads_ssdpwatch_table_at(NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_ssdpwatch_table_dropped_total(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_notify_alive_recognized);
    RUN_TEST(test_long_location_is_truncated_not_refused);
    RUN_TEST(test_notify_byebye_recognized);
    RUN_TEST(test_search_response_recognized);
    RUN_TEST(test_wrong_port_not_recognized);
    RUN_TEST(test_non_ssdp_payload_not_recognized);
    RUN_TEST(test_notify_without_nts_is_other);
    RUN_TEST(test_truncated_frame_not_recognized);
    RUN_TEST(test_parse_null_arguments_are_refused);
    RUN_TEST(test_find_header_case_insensitive);
    RUN_TEST(test_find_header_missing_returns_false);
    RUN_TEST(test_find_header_does_not_match_mid_line);
    RUN_TEST(test_table_learn_then_appears);
    RUN_TEST(test_table_same_mac_and_usn_refreshes_not_duplicates);
    RUN_TEST(test_table_same_mac_different_usn_is_a_separate_entry);
    RUN_TEST(test_table_full_drops_and_counts_new_entries);
    RUN_TEST(test_table_at_out_of_range_is_null);
    RUN_TEST(test_table_null_arguments_are_refused);
    return UNITY_END();
}

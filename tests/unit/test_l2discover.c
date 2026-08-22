/* cads_l2discover: the M5 "passive L2 neighbor discovery (CDP/LLDP/STP/
 * VLAN)" line in docs/ROADMAP.md - the portable frame parser and dedup
 * table, independent of whether the board this runs on has ever seen a
 * real CDP/LLDP/STP frame to parse. Every test frame below is hand-built
 * byte-for-byte against the real protocol layouts (Cisco's CDP, IEEE
 * 802.1AB LLDP, IEEE 802.1D/w STP Configuration/RST BPDUs, 802.1Q), not
 * captured off a network - see cads/toolbox/l2discover.c's own header
 * for the exact offsets each parser checks. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/toolbox/l2discover.h"

void setUp(void) {
}

void tearDown(void) {
}

/* ---------------------------------------------------------------- CDP */

static void test_cdp_recognized_with_device_and_port_id(void) {
    uint8_t frame[46] = {
        0x01, 0x00, 0x0C, 0xCC, 0xCC, 0xCC, /* dst: CDP multicast */
        0x02, 0xCA, 0xD5, 0x00, 0x00, 0x01, /* src */
        0x00, 0x00,                         /* 802.3 length (not checked) */
        0xAA, 0xAA, 0x03,                   /* LLC UI */
        0x00, 0x00, 0x0C, 0x20, 0x00,       /* SNAP: Cisco OUI, PID=CDP */
        0x02, 0xB4, 0x00, 0x00,             /* CDP: version=2, ttl=180, checksum (unchecked) */
        0x00, 0x01, 0x00, 0x0B, 'S', 'w', 'i', 't', 'c', 'h', '1', /* TLV 1: Device ID */
        0x00, 0x03, 0x00, 0x09, 'G', 'i', '0', '/', '1',           /* TLV 3: Port ID */
    };

    cads_l2discover_record_t record;
    bool ok = cads_l2discover_parse(frame, sizeof(frame), &record);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(CADS_L2_CDP, record.kind);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame + 6, record.src_mac, 6u);
    TEST_ASSERT_EQUAL_STRING("Switch1", record.name);
    TEST_ASSERT_EQUAL_STRING("Gi0/1", record.port);
}

static void test_cdp_wrong_snap_pid_not_recognized(void) {
    /* Same dst/LLC/SNAP-OUI as real CDP, but PID 0x2003 is VTP, not CDP
     * (0x2000) - the two share a vendor but not a protocol. */
    uint8_t frame[26] = {
        0x01, 0x00, 0x0C, 0xCC, 0xCC, 0xCC, 0x02, 0xCA, 0xD5, 0x00, 0x00, 0x01, 0x00, 0x00,
        0xAA, 0xAA, 0x03, 0x00, 0x00, 0x0C, 0x20, 0x03, 0x01, 0x00, 0x00, 0x00,
    };

    cads_l2discover_record_t record;
    TEST_ASSERT_FALSE(cads_l2discover_parse(frame, sizeof(frame), &record));
}

static void test_cdp_malformed_tlv_length_stops_without_overrun(void) {
    /* A TLV claiming a length far past the frame's own end must stop the
     * walk, not read past `length` - the frame is still recognised as
     * CDP (the header matched), just with no name/port filled in. */
    uint8_t frame[30] = {
        0x01, 0x00, 0x0C, 0xCC, 0xCC, 0xCC, 0x02, 0xCA, 0xD5, 0x00, 0x00, 0x01, 0x00, 0x00,
        0xAA, 0xAA, 0x03, 0x00, 0x00, 0x0C, 0x20, 0x00, 0x02, 0xB4, 0x00, 0x00,
        0x00, 0x01, 0xFF, 0xFF, /* type=Device ID, length=65535 (impossible) */
    };

    cads_l2discover_record_t record;
    bool ok = cads_l2discover_parse(frame, sizeof(frame), &record);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(CADS_L2_CDP, record.kind);
    TEST_ASSERT_EQUAL_STRING("", record.name);
    TEST_ASSERT_EQUAL_STRING("", record.port);
}

/* --------------------------------------------------------------- LLDP */

static void test_lldp_chassis_fallback_is_sanitized(void) {
    /* Chassis ID subtype 4 = MAC address (raw binary, not text) - with no
     * System Name TLV, the fallback path must still produce a safe,
     * printable string rather than raw control/high bytes. */
    uint8_t frame[] = {
        0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E, /* dst: LLDP nearest bridge */
        0x02, 0xCA, 0xD5, 0x00, 0x00, 0x02, /* src */
        0x88, 0xCC,                         /* EtherType: LLDP */
        /* Chassis ID TLV: type=1, len=7 (1 subtype + 6 mac), subtype=4 */
        0x02, 0x07, 0x04, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        /* Port ID TLV: type=2, len=9 (1 subtype + 8), subtype=7 (locally assigned) */
        0x04, 0x09, 0x07, 'G', 'i', '1', '/', '0', '/', '2', '4',
        /* End TLV */
        0x00, 0x00,
    };

    cads_l2discover_record_t record;
    bool ok = cads_l2discover_parse(frame, sizeof(frame), &record);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(CADS_L2_LLDP, record.kind);
    /* 0x00 and 0x11 are not printable ASCII -> '.'; 0x22 '"' 0x33 '3' 0x44 'D' 0x55 'U' are. */
    TEST_ASSERT_EQUAL_STRING("..\"3DU", record.name);
    TEST_ASSERT_EQUAL_STRING("Gi1/0/24", record.port);
}

static void test_lldp_system_name_overrides_chassis_id(void) {
    uint8_t frame[] = {
        0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E, 0x02, 0xCA, 0xD5, 0x00, 0x00, 0x02, 0x88, 0xCC,
        /* Chassis ID TLV: subtype=7, "chassis-a" (9 chars) -> len=10 */
        0x02, 0x0A, 0x07, 'c', 'h', 'a', 's', 's', 'i', 's', '-', 'a',
        /* System Name TLV: type=5, "core-sw1" (8 chars) -> len=8 */
        0x0A, 0x08, 'c', 'o', 'r', 'e', '-', 's', 'w', '1',
        0x00, 0x00, /* End */
    };

    cads_l2discover_record_t record;
    bool ok = cads_l2discover_parse(frame, sizeof(frame), &record);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("core-sw1", record.name);
}

static void test_lldp_wrong_ethertype_not_recognized(void) {
    uint8_t frame[14] = {
        0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E, 0x02, 0xCA, 0xD5, 0x00, 0x00, 0x02, 0x08, 0x00,
    };
    cads_l2discover_record_t record;
    TEST_ASSERT_FALSE(cads_l2discover_parse(frame, sizeof(frame), &record));
}

/* ---------------------------------------------------------------- STP */

static void test_stp_root_and_bridge_ids_are_distinct(void) {
    uint8_t frame[42] = {
        0x01, 0x80, 0xC2, 0x00, 0x00, 0x00,       /* dst: STP multicast */
        0x02, 0xCA, 0xD5, 0x00, 0x00, 0x03,       /* src: this bridge's own MAC */
        0x00, 0x26,                               /* 802.3 length (unchecked) */
        0x42, 0x42, 0x03,                         /* LLC */
        0x00, 0x00,                               /* BPDU protocol id = 0 */
        0x02,                                     /* version 2 = RSTP */
        0x02,                                     /* bpdu type = RST */
        0x00,                                     /* flags */
        0x80, 0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, /* root id: prio 32768, mac AA:BB:CC:DD:EE:FF */
        0x00, 0x00, 0x00, 0x04,                   /* root path cost = 4 */
        0x80, 0x00, 0x02, 0xCA, 0xD5, 0x00, 0x00, 0x03, /* bridge id: prio 32768, mac = src (not root) */
    };

    cads_l2discover_record_t record;
    bool ok = cads_l2discover_parse(frame, sizeof(frame), &record);

    static const uint8_t root_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    static const uint8_t bridge_mac[6] = {0x02, 0xCA, 0xD5, 0x00, 0x00, 0x03};

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(CADS_L2_STP, record.kind);
    TEST_ASSERT_EQUAL_UINT16(0x8000u, record.stp_root_priority);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(root_mac, record.stp_root_mac, 6u);
    TEST_ASSERT_EQUAL_UINT16(0x8000u, record.stp_bridge_priority);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bridge_mac, record.stp_bridge_mac, 6u);
}

static void test_stp_tcn_type_not_recognized(void) {
    /* A Topology Change Notification BPDU is only 4 bytes past the LLC
     * header and carries no root/bridge id at all - not a shorter version
     * of the same layout, a completely different, shorter frame. */
    uint8_t frame[21] = {
        0x01, 0x80, 0xC2, 0x00, 0x00, 0x00, 0x02, 0xCA, 0xD5, 0x00, 0x00, 0x03, 0x00, 0x04,
        0x42, 0x42, 0x03, 0x00, 0x00, 0x00, 0x80,
    };
    cads_l2discover_record_t record;
    TEST_ASSERT_FALSE(cads_l2discover_parse(frame, sizeof(frame), &record));
}

static void test_stp_too_short_not_recognized(void) {
    uint8_t frame[41] = {0}; /* one byte short of STP_BRIDGE_ID_END */
    memcpy(frame, (uint8_t[]){0x01, 0x80, 0xC2, 0x00, 0x00, 0x00}, 6u);
    cads_l2discover_record_t record;
    TEST_ASSERT_FALSE(cads_l2discover_parse(frame, sizeof(frame), &record));
}

/* --------------------------------------------------------------- VLAN */

static void test_vlan_tag_extracts_id(void) {
    uint8_t frame[18] = {
        0x02, 0xCA, 0xD5, 0x00, 0x00, 0x04, 0x02, 0xCA, 0xD5, 0x00, 0x00, 0x05,
        0x81, 0x00, /* EtherType: 802.1Q */
        0x00, 0x64, /* TCI: priority=0 DEI=0 VLAN=100 */
        0x08, 0x00, /* inner EtherType: IPv4 */
    };
    uint16_t vlan_id = 0xFFFFu;
    TEST_ASSERT_TRUE(cads_l2discover_vlan_tag(frame, sizeof(frame), &vlan_id));
    TEST_ASSERT_EQUAL_UINT16(100u, vlan_id);
}

static void test_vlan_reserved_id_rejected(void) {
    uint8_t frame[18] = {
        0x02, 0xCA, 0xD5, 0x00, 0x00, 0x04, 0x02, 0xCA, 0xD5, 0x00, 0x00, 0x05,
        0x81, 0x00, 0x0F, 0xFF, /* VLAN = 0xFFF, reserved */
        0x08, 0x00,
    };
    uint16_t vlan_id = 0u;
    TEST_ASSERT_FALSE(cads_l2discover_vlan_tag(frame, sizeof(frame), &vlan_id));
}

static void test_vlan_untagged_frame_returns_false(void) {
    uint8_t frame[18] = {
        0x02, 0xCA, 0xD5, 0x00, 0x00, 0x04, 0x02, 0xCA, 0xD5, 0x00, 0x00, 0x05,
        0x08, 0x00, 0x45, 0x00, 0x00, 0x00, /* ordinary IPv4 EtherType, no tag */
    };
    uint16_t vlan_id = 0u;
    TEST_ASSERT_FALSE(cads_l2discover_vlan_tag(frame, sizeof(frame), &vlan_id));
}

/* -------------------------------------------------------- misuse/edges */

static void test_parse_null_and_too_short_are_refused(void) {
    uint8_t short_frame[11] = {0}; /* shorter than dst+src */
    cads_l2discover_record_t record;
    TEST_ASSERT_FALSE(cads_l2discover_parse(NULL, 100u, &record));
    TEST_ASSERT_FALSE(cads_l2discover_parse(short_frame, sizeof(short_frame), &record));
    TEST_ASSERT_FALSE(cads_l2discover_parse(short_frame, sizeof(short_frame), NULL));
}

/* ---------------------------------------------------------------- table */

static void test_table_learn_then_appears(void) {
    cads_l2discover_record_t storage[4];
    cads_l2discover_table_t table;
    cads_l2discover_table_init(&table, storage, 4u);

    cads_l2discover_record_t record;
    memset(&record, 0, sizeof(record));
    record.kind = CADS_L2_LLDP;
    memcpy(record.src_mac, (uint8_t[]){0x02, 0xCA, 0xD5, 0, 0, 1}, 6u);
    strcpy(record.name, "sw1");

    cads_l2discover_table_learn(&table, &record);

    TEST_ASSERT_EQUAL_UINT(1u, cads_l2discover_table_count(&table));
    const cads_l2discover_record_t* at0 = cads_l2discover_table_at(&table, 0u);
    TEST_ASSERT_NOT_NULL(at0);
    TEST_ASSERT_EQUAL_STRING("sw1", at0->name);
}

static void test_table_same_kind_and_mac_refreshes_not_duplicates(void) {
    cads_l2discover_record_t storage[4];
    cads_l2discover_table_t table;
    cads_l2discover_table_init(&table, storage, 4u);

    cads_l2discover_record_t record;
    memset(&record, 0, sizeof(record));
    record.kind = CADS_L2_CDP;
    memcpy(record.src_mac, (uint8_t[]){0x02, 0xCA, 0xD5, 0, 0, 1}, 6u);
    strcpy(record.name, "first-seen");
    cads_l2discover_table_learn(&table, &record);

    strcpy(record.name, "port-renamed");
    cads_l2discover_table_learn(&table, &record);

    TEST_ASSERT_EQUAL_UINT(1u, cads_l2discover_table_count(&table));
    TEST_ASSERT_EQUAL_STRING("port-renamed", cads_l2discover_table_at(&table, 0u)->name);
}

static void test_table_same_mac_different_kind_is_a_separate_entry(void) {
    /* A device can legitimately speak both CDP and LLDP off the same
     * port/MAC - these must not collide into one slot. */
    cads_l2discover_record_t storage[4];
    cads_l2discover_table_t table;
    cads_l2discover_table_init(&table, storage, 4u);

    cads_l2discover_record_t record;
    memset(&record, 0, sizeof(record));
    memcpy(record.src_mac, (uint8_t[]){0x02, 0xCA, 0xD5, 0, 0, 1}, 6u);

    record.kind = CADS_L2_CDP;
    cads_l2discover_table_learn(&table, &record);
    record.kind = CADS_L2_LLDP;
    cads_l2discover_table_learn(&table, &record);

    TEST_ASSERT_EQUAL_UINT(2u, cads_l2discover_table_count(&table));
}

static void test_table_full_drops_and_counts_new_entries(void) {
    cads_l2discover_record_t storage[2];
    cads_l2discover_table_t table;
    cads_l2discover_table_init(&table, storage, 2u);

    cads_l2discover_record_t record;
    memset(&record, 0, sizeof(record));
    record.kind = CADS_L2_STP;

    memcpy(record.src_mac, (uint8_t[]){0x02, 0xCA, 0xD5, 0, 0, 1}, 6u);
    cads_l2discover_table_learn(&table, &record);
    memcpy(record.src_mac, (uint8_t[]){0x02, 0xCA, 0xD5, 0, 0, 2}, 6u);
    cads_l2discover_table_learn(&table, &record);
    memcpy(record.src_mac, (uint8_t[]){0x02, 0xCA, 0xD5, 0, 0, 3}, 6u);
    cads_l2discover_table_learn(&table, &record);

    TEST_ASSERT_EQUAL_UINT(2u, cads_l2discover_table_count(&table));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_l2discover_table_dropped_total(&table));
}

static void test_table_at_out_of_range_is_null(void) {
    cads_l2discover_record_t storage[4];
    cads_l2discover_table_t table;
    cads_l2discover_table_init(&table, storage, 4u);
    TEST_ASSERT_NULL(cads_l2discover_table_at(&table, 0u));
    TEST_ASSERT_NULL(cads_l2discover_table_at(&table, 99u));
}

static void test_table_null_arguments_are_refused(void) {
    cads_l2discover_record_t storage[4];
    cads_l2discover_table_t table;
    cads_l2discover_table_init(&table, storage, 4u);

    cads_l2discover_record_t record;
    memset(&record, 0, sizeof(record));

    cads_l2discover_table_learn(&table, NULL);
    cads_l2discover_table_learn(NULL, &record);
    TEST_ASSERT_EQUAL_UINT(0u, cads_l2discover_table_count(&table));
    TEST_ASSERT_EQUAL_UINT(0u, cads_l2discover_table_count(NULL));
    TEST_ASSERT_NULL(cads_l2discover_table_at(NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_l2discover_table_dropped_total(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cdp_recognized_with_device_and_port_id);
    RUN_TEST(test_cdp_wrong_snap_pid_not_recognized);
    RUN_TEST(test_cdp_malformed_tlv_length_stops_without_overrun);
    RUN_TEST(test_lldp_chassis_fallback_is_sanitized);
    RUN_TEST(test_lldp_system_name_overrides_chassis_id);
    RUN_TEST(test_lldp_wrong_ethertype_not_recognized);
    RUN_TEST(test_stp_root_and_bridge_ids_are_distinct);
    RUN_TEST(test_stp_tcn_type_not_recognized);
    RUN_TEST(test_stp_too_short_not_recognized);
    RUN_TEST(test_vlan_tag_extracts_id);
    RUN_TEST(test_vlan_reserved_id_rejected);
    RUN_TEST(test_vlan_untagged_frame_returns_false);
    RUN_TEST(test_parse_null_and_too_short_are_refused);
    RUN_TEST(test_table_learn_then_appears);
    RUN_TEST(test_table_same_kind_and_mac_refreshes_not_duplicates);
    RUN_TEST(test_table_same_mac_different_kind_is_a_separate_entry);
    RUN_TEST(test_table_full_drops_and_counts_new_entries);
    RUN_TEST(test_table_at_out_of_range_is_null);
    RUN_TEST(test_table_null_arguments_are_refused);
    return UNITY_END();
}

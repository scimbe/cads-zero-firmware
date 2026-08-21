/* cads_mactable: the M5 "MAC address table (switch-style learning with
 * aging)" line in docs/ROADMAP.md - the portable learning/aging policy,
 * independent of whether the board this runs on has ever seen a live
 * frame to learn from. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/toolbox/mactable.h"

void setUp(void) {
}

void tearDown(void) {
}

static const uint8_t MAC_A[6] = {0x02, 0xCA, 0xD5, 0x00, 0x00, 0x01};
static const uint8_t MAC_B[6] = {0x02, 0xCA, 0xD5, 0x00, 0x00, 0x02};
static const uint8_t MAC_C[6] = {0x02, 0xCA, 0xD5, 0x00, 0x00, 0x03};

static void test_learn_then_appears_in_table(void) {
    cads_mactable_entry_t storage[4];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, 4u, 5000u);

    cads_mactable_learn(&table, MAC_A, 1000u);

    TEST_ASSERT_EQUAL_UINT(1u, cads_mactable_count(&table));
    const cads_mactable_entry_t* entry = cads_mactable_at(&table, 0u);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_A, entry->mac, 6u);
    TEST_ASSERT_EQUAL_UINT32(1000u, entry->last_seen_ms);
    TEST_ASSERT_EQUAL_UINT32(1u, entry->frame_count);
}

static void test_repeated_sighting_refreshes_not_duplicates(void) {
    cads_mactable_entry_t storage[4];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, 4u, 5000u);

    cads_mactable_learn(&table, MAC_A, 1000u);
    cads_mactable_learn(&table, MAC_A, 2000u);
    cads_mactable_learn(&table, MAC_A, 3000u);

    TEST_ASSERT_EQUAL_UINT(1u, cads_mactable_count(&table));
    const cads_mactable_entry_t* entry = cads_mactable_at(&table, 0u);
    TEST_ASSERT_EQUAL_UINT32(3000u, entry->last_seen_ms);
    TEST_ASSERT_EQUAL_UINT32(3u, entry->frame_count);
}

static void test_distinct_addresses_get_distinct_entries(void) {
    cads_mactable_entry_t storage[4];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, 4u, 5000u);

    cads_mactable_learn(&table, MAC_A, 1000u);
    cads_mactable_learn(&table, MAC_B, 1001u);

    TEST_ASSERT_EQUAL_UINT(2u, cads_mactable_count(&table));
}

static void test_full_table_refuses_new_address_when_nothing_aged(void) {
    cads_mactable_entry_t storage[2];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, 2u, 5000u);

    cads_mactable_learn(&table, MAC_A, 1000u);
    cads_mactable_learn(&table, MAC_B, 1000u);
    /* Both entries are 1ms old at t=1001, nowhere near the 5000ms aging
     * window - a third address must be refused, not evict a live entry. */
    cads_mactable_learn(&table, MAC_C, 1001u);

    TEST_ASSERT_EQUAL_UINT(2u, cads_mactable_count(&table));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_mactable_dropped_total(&table));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_mactable_aged_out_total(&table));
}

static void test_full_table_evicts_the_aged_entry_for_a_new_address(void) {
    cads_mactable_entry_t storage[2];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, 2u, 5000u);

    cads_mactable_learn(&table, MAC_A, 0u);
    cads_mactable_learn(&table, MAC_B, 4000u);
    /* At t=6000, MAC_A (idle 6000ms) has crossed the 5000ms aging window;
     * MAC_B (idle 2000ms) has not. MAC_C should evict MAC_A specifically. */
    cads_mactable_learn(&table, MAC_C, 6000u);

    TEST_ASSERT_EQUAL_UINT(2u, cads_mactable_count(&table));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_mactable_aged_out_total(&table));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_mactable_dropped_total(&table));

    bool saw_b = false, saw_c = false, saw_a = false;
    for(size_t i = 0u; i < cads_mactable_count(&table); i++) {
        const cads_mactable_entry_t* entry = cads_mactable_at(&table, i);
        if(memcmp(entry->mac, MAC_A, 6u) == 0) saw_a = true;
        if(memcmp(entry->mac, MAC_B, 6u) == 0) saw_b = true;
        if(memcmp(entry->mac, MAC_C, 6u) == 0) saw_c = true;
    }
    TEST_ASSERT_FALSE(saw_a);
    TEST_ASSERT_TRUE(saw_b);
    TEST_ASSERT_TRUE(saw_c);
}

static void test_age_evicts_idle_entries_without_a_new_sighting(void) {
    cads_mactable_entry_t storage[4];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, 4u, 5000u);

    cads_mactable_learn(&table, MAC_A, 0u);
    cads_mactable_learn(&table, MAC_B, 4000u);

    uint32_t evicted = cads_mactable_age(&table, 6000u);

    TEST_ASSERT_EQUAL_UINT32(1u, evicted);
    TEST_ASSERT_EQUAL_UINT(1u, cads_mactable_count(&table));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_mactable_aged_out_total(&table));
    const cads_mactable_entry_t* remaining = cads_mactable_at(&table, 0u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_B, remaining->mac, 6u);
}

static void test_age_below_the_window_evicts_nothing(void) {
    cads_mactable_entry_t storage[4];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, 4u, 5000u);

    cads_mactable_learn(&table, MAC_A, 0u);

    uint32_t evicted = cads_mactable_age(&table, 4999u);

    TEST_ASSERT_EQUAL_UINT32(0u, evicted);
    TEST_ASSERT_EQUAL_UINT(1u, cads_mactable_count(&table));
}

static void test_at_out_of_range_is_null(void) {
    cads_mactable_entry_t storage[4];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, 4u, 5000u);

    cads_mactable_learn(&table, MAC_A, 0u);

    TEST_ASSERT_NULL(cads_mactable_at(&table, 1u));
    TEST_ASSERT_NULL(cads_mactable_at(&table, 99u));
}

static void test_evicted_slot_is_reusable(void) {
    cads_mactable_entry_t storage[1];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, 1u, 1000u);

    cads_mactable_learn(&table, MAC_A, 0u);
    cads_mactable_age(&table, 1000u);
    TEST_ASSERT_EQUAL_UINT(0u, cads_mactable_count(&table));

    cads_mactable_learn(&table, MAC_B, 1000u);
    TEST_ASSERT_EQUAL_UINT(1u, cads_mactable_count(&table));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MAC_B, cads_mactable_at(&table, 0u)->mac, 6u);
}

static void test_null_arguments_are_refused(void) {
    cads_mactable_entry_t storage[4];
    cads_mactable_t table;
    cads_mactable_init(&table, storage, 4u, 5000u);

    cads_mactable_learn(&table, NULL, 0u);
    cads_mactable_learn(NULL, MAC_A, 0u);
    TEST_ASSERT_EQUAL_UINT(0u, cads_mactable_count(&table));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_mactable_age(NULL, 0u));
    TEST_ASSERT_NULL(cads_mactable_at(NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_mactable_aged_out_total(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_mactable_dropped_total(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_learn_then_appears_in_table);
    RUN_TEST(test_repeated_sighting_refreshes_not_duplicates);
    RUN_TEST(test_distinct_addresses_get_distinct_entries);
    RUN_TEST(test_full_table_refuses_new_address_when_nothing_aged);
    RUN_TEST(test_full_table_evicts_the_aged_entry_for_a_new_address);
    RUN_TEST(test_age_evicts_idle_entries_without_a_new_sighting);
    RUN_TEST(test_age_below_the_window_evicts_nothing);
    RUN_TEST(test_at_out_of_range_is_null);
    RUN_TEST(test_evicted_slot_is_reusable);
    RUN_TEST(test_null_arguments_are_refused);
    return UNITY_END();
}

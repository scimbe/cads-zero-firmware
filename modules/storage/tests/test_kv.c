/* cads/storage/kv.h - a client of cads/storage/storage.h like any other, so
 * this needs a mounted, formatted volume the same way test_storage.c does,
 * not a fake of the layer beneath it. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/storage/kv.h"
#include "cads/storage/storage.h"

void setUp(void) {
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_format());
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_open("/settings.kv"));
}

void tearDown(void) {
    cads_storage_unmount();
}

static void test_open_of_a_missing_file_starts_empty(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, cads_kv_count());
    TEST_ASSERT_FALSE(cads_kv_has("backlight"));
}

static void test_set_and_get_each_type(void) {
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_set_i32("backlight", 80));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_set_bool("fast_clock", false));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_set_str("board_name", "ITSboard"));

    int32_t i32 = 0;
    bool boolean = true;
    char str[32] = {0};
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_get_i32("backlight", &i32));
    TEST_ASSERT_EQUAL_INT32(80, i32);
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_get_bool("fast_clock", &boolean));
    TEST_ASSERT_FALSE(boolean);
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_get_str("board_name", str, sizeof(str)));
    TEST_ASSERT_EQUAL_STRING("ITSboard", str);

    TEST_ASSERT_EQUAL_UINT32(3u, cads_kv_count());
}

static void test_set_again_updates_in_place_not_a_new_entry(void) {
    cads_kv_set_i32("backlight", 80);
    cads_kv_set_i32("backlight", 25);
    TEST_ASSERT_EQUAL_UINT32(1u, cads_kv_count());

    int32_t value = 0;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_get_i32("backlight", &value));
    TEST_ASSERT_EQUAL_INT32(25, value);
}

static void test_values_survive_save_and_a_fresh_open(void) {
    cads_kv_set_i32("backlight", 80);
    cads_kv_set_bool("fast_clock", true);
    cads_kv_set_str("board_name", "ITSboard (NUCLEO-F429ZI)");
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_save());

    /* A fresh cads_kv_open() discards the in-memory table (kv.h's own
     * documented contract - one table, not one per path) and reloads from
     * the file this time, exercising the header + entry parse path rather
     * than just reading back RAM the setters already wrote. */
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_open("/settings.kv"));
    TEST_ASSERT_EQUAL_UINT32(3u, cads_kv_count());

    int32_t i32 = 0;
    bool boolean = false;
    char str[32] = {0};
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_get_i32("backlight", &i32));
    TEST_ASSERT_EQUAL_INT32(80, i32);
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_get_bool("fast_clock", &boolean));
    TEST_ASSERT_TRUE(boolean);
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_get_str("board_name", str, sizeof(str)));
    TEST_ASSERT_EQUAL_STRING("ITSboard (NUCLEO-F429ZI)", str);
}

static void test_wrong_type_getter_is_refused(void) {
    cads_kv_set_i32("backlight", 80);

    bool boolean = false;
    char str[8] = {0};
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_INVAL, cads_kv_get_bool("backlight", &boolean));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_INVAL, cads_kv_get_str("backlight", str, sizeof(str)));
}

static void test_missing_key_is_noent(void) {
    int32_t value = 0;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_NOENT, cads_kv_get_i32("nope", &value));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_NOENT, cads_kv_type("nope"));
}

static void test_remove_drops_the_entry(void) {
    cads_kv_set_i32("a", 1);
    cads_kv_set_i32("b", 2);
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_remove("a"));
    TEST_ASSERT_FALSE(cads_kv_has("a"));
    TEST_ASSERT_TRUE(cads_kv_has("b"));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_kv_count());
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_NOENT, cads_kv_remove("a"));
}

static void test_a_string_value_longer_than_the_limit_is_refused(void) {
    char too_long[CADS_KV_STR_MAX + 2u];
    memset(too_long, 'x', sizeof(too_long) - 1u);
    too_long[sizeof(too_long) - 1u] = '\0';

    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_INVAL, cads_kv_set_str("long", too_long));
    TEST_ASSERT_FALSE(cads_kv_has("long"));
}

static void test_get_str_truncates_to_the_callers_buffer(void) {
    cads_kv_set_str("board_name", "ITSboard (NUCLEO-F429ZI)");

    char small[5];
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_get_str("board_name", small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("ITSb", small); /* 4 chars + NUL, never overrunning `small` */
}

static void test_a_corrupt_file_reports_corrupt_and_leaves_the_table_empty(void) {
    cads_kv_set_i32("a", 1);
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_save());

    /* Overwrite the saved file with a bad magic number directly through
     * cads/storage/storage.h - deliberately not through cads_kv.h, which has
     * no API for writing garbage on purpose. */
    cads_storage_file_t* file = NULL;
    TEST_ASSERT_EQUAL_INT(
        CADS_STORAGE_OK,
        cads_storage_open(&file, "/settings.kv", CADS_STORAGE_WRONLY | CADS_STORAGE_TRUNC));
    uint32_t garbage[3] = {0xDEADBEEFu, 1u, 1u};
    cads_storage_write(file, garbage, sizeof(garbage));
    cads_storage_close(file);

    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_CORRUPT, cads_kv_open("/settings.kv"));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_kv_count());
}

static void test_an_unterminated_key_on_disk_reports_corrupt(void) {
    cads_kv_set_i32("a", 1);
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_save());

    /* Rewrite the one entry's key array with no terminator anywhere in it:
     * header (12 bytes) then the entry, whose key comes first. */
    cads_storage_file_t* file = NULL;
    TEST_ASSERT_EQUAL_INT(
        CADS_STORAGE_OK, cads_storage_open(&file, "/settings.kv", CADS_STORAGE_WRONLY));
    TEST_ASSERT_EQUAL_INT32(12, cads_storage_seek(file, 12, CADS_STORAGE_SEEK_SET));
    char key[CADS_KV_KEY_MAX + 1u];
    memset(key, 'k', sizeof(key));
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(key), cads_storage_write(file, key, sizeof(key)));
    cads_storage_close(file);

    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_CORRUPT, cads_kv_open("/settings.kv"));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_kv_count());
}

static void test_entry_pool_is_exhausted_not_grown(void) {
    char key[CADS_KV_KEY_MAX + 1u];
    for(uint32_t i = 0; i < CADS_KV_MAX_ENTRIES; i++) {
        key[0] = 'k';
        /* Decimal, two digits max - CADS_KV_MAX_ENTRIES is 32 by default. */
        key[1] = (char)('0' + (i / 10u));
        key[2] = (char)('0' + (i % 10u));
        key[3] = '\0';
        TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_kv_set_i32(key, (int32_t)i));
    }
    TEST_ASSERT_EQUAL_UINT32(CADS_KV_MAX_ENTRIES, cads_kv_count());
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_NOSLOT, cads_kv_set_i32("one_more", 0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_open_of_a_missing_file_starts_empty);
    RUN_TEST(test_set_and_get_each_type);
    RUN_TEST(test_set_again_updates_in_place_not_a_new_entry);
    RUN_TEST(test_values_survive_save_and_a_fresh_open);
    RUN_TEST(test_wrong_type_getter_is_refused);
    RUN_TEST(test_missing_key_is_noent);
    RUN_TEST(test_remove_drops_the_entry);
    RUN_TEST(test_a_string_value_longer_than_the_limit_is_refused);
    RUN_TEST(test_get_str_truncates_to_the_callers_buffer);
    RUN_TEST(test_a_corrupt_file_reports_corrupt_and_leaves_the_table_empty);
    RUN_TEST(test_an_unterminated_key_on_disk_reports_corrupt);
    RUN_TEST(test_entry_pool_is_exhausted_not_grown);
    return UNITY_END();
}

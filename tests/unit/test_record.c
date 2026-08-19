/* cads_record: the M2 service-registry line in docs/ROADMAP.md, the
 * named-lookup half. */

#include <stddef.h>
#include <stdint.h>

#include "unity.h"

#include "cads/toolbox/record.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_register_then_lookup(void) {
    cads_record_entry_t storage[4];
    cads_record_t registry;
    cads_record_init(&registry, storage, 4u);

    int eth_service = 1;
    TEST_ASSERT_TRUE(cads_record_register(&registry, "eth", &eth_service));
    TEST_ASSERT_EQUAL_PTR(&eth_service, cads_record_lookup(&registry, "eth"));
}

static void test_lookup_of_unregistered_name_is_null(void) {
    cads_record_entry_t storage[4];
    cads_record_t registry;
    cads_record_init(&registry, storage, 4u);

    TEST_ASSERT_NULL(cads_record_lookup(&registry, "nothing"));
}

static void test_duplicate_name_is_refused(void) {
    cads_record_entry_t storage[4];
    cads_record_t registry;
    cads_record_init(&registry, storage, 4u);

    int first = 1, second = 2;
    TEST_ASSERT_TRUE(cads_record_register(&registry, "eth", &first));
    /* A second registration under the same name would let two unrelated
     * callers silently collide - refused, not overwritten. */
    TEST_ASSERT_FALSE(cads_record_register(&registry, "eth", &second));
    TEST_ASSERT_EQUAL_PTR(&first, cads_record_lookup(&registry, "eth"));
}

static void test_registry_full_is_refused(void) {
    cads_record_entry_t storage[2];
    cads_record_t registry;
    cads_record_init(&registry, storage, 2u);

    int a = 1, b = 2, c = 3;
    TEST_ASSERT_TRUE(cads_record_register(&registry, "a", &a));
    TEST_ASSERT_TRUE(cads_record_register(&registry, "b", &b));
    TEST_ASSERT_FALSE(cads_record_register(&registry, "c", &c));
    TEST_ASSERT_NULL(cads_record_lookup(&registry, "c"));
}

static void test_name_too_long_is_refused(void) {
    cads_record_entry_t storage[4];
    cads_record_t registry;
    cads_record_init(&registry, storage, 4u);

    /* CADS_RECORD_NAME_MAX is 15; this is 16 characters, one over. */
    int instance = 1;
    TEST_ASSERT_FALSE(
        cads_record_register(&registry, "0123456789abcdef", &instance));
}

static void test_unregister_then_reuse_the_slot(void) {
    cads_record_entry_t storage[2];
    cads_record_t registry;
    cads_record_init(&registry, storage, 2u);

    int a = 1, b = 2;
    TEST_ASSERT_TRUE(cads_record_register(&registry, "a", &a));
    TEST_ASSERT_TRUE(cads_record_unregister(&registry, "a"));
    TEST_ASSERT_NULL(cads_record_lookup(&registry, "a"));

    /* The freed slot is available again. */
    TEST_ASSERT_TRUE(cads_record_register(&registry, "b", &b));
    TEST_ASSERT_EQUAL_PTR(&b, cads_record_lookup(&registry, "b"));
}

static void test_unregister_unknown_name_fails(void) {
    cads_record_entry_t storage[4];
    cads_record_t registry;
    cads_record_init(&registry, storage, 4u);

    TEST_ASSERT_FALSE(cads_record_unregister(&registry, "nothing"));
}

static void test_names_are_exact_matches(void) {
    cads_record_entry_t storage[4];
    cads_record_t registry;
    cads_record_init(&registry, storage, 4u);

    int instance = 1;
    TEST_ASSERT_TRUE(cads_record_register(&registry, "eth", &instance));
    TEST_ASSERT_NULL(cads_record_lookup(&registry, "et"));
    TEST_ASSERT_NULL(cads_record_lookup(&registry, "eth0"));
    TEST_ASSERT_NULL(cads_record_lookup(&registry, "Eth"));
}

static void test_null_arguments_are_refused(void) {
    cads_record_entry_t storage[4];
    cads_record_t registry;
    cads_record_init(&registry, storage, 4u);
    int instance = 1;

    TEST_ASSERT_FALSE(cads_record_register(&registry, NULL, &instance));
    TEST_ASSERT_FALSE(cads_record_register(NULL, "a", &instance));
    TEST_ASSERT_NULL(cads_record_lookup(&registry, NULL));
    TEST_ASSERT_NULL(cads_record_lookup(NULL, "a"));
    TEST_ASSERT_FALSE(cads_record_unregister(&registry, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_register_then_lookup);
    RUN_TEST(test_lookup_of_unregistered_name_is_null);
    RUN_TEST(test_duplicate_name_is_refused);
    RUN_TEST(test_registry_full_is_refused);
    RUN_TEST(test_name_too_long_is_refused);
    RUN_TEST(test_unregister_then_reuse_the_slot);
    RUN_TEST(test_unregister_unknown_name_fails);
    RUN_TEST(test_names_are_exact_matches);
    RUN_TEST(test_null_arguments_are_refused);
    return UNITY_END();
}

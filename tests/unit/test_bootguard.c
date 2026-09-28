/* modules/diag/src/cads_bootguard.c - the crash-loop guard. The storage is
 * static (CCM on target), so every test starts by forcing a known state
 * through the public API rather than relying on test order. */

#include "unity.h"

#include "cads/diag/bootguard.h"

void setUp(void) {
    cads_bootguard_boot(false); /* a clean (non-watchdog) boot resets the count */
}

void tearDown(void) {
}

static void test_clean_boot_is_not_tripped(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, cads_bootguard_count());
    TEST_ASSERT_FALSE(cads_bootguard_tripped());
}

static void test_trips_after_limit_consecutive_watchdog_resets(void) {
    for(uint32_t i = 1u; i < CADS_BOOTGUARD_LIMIT; i++) {
        cads_bootguard_boot(true);
        TEST_ASSERT_EQUAL_UINT32(i, cads_bootguard_count());
        TEST_ASSERT_FALSE(cads_bootguard_tripped());
    }
    cads_bootguard_boot(true);
    TEST_ASSERT_TRUE(cads_bootguard_tripped());
}

static void test_any_other_reset_starts_over(void) {
    for(uint32_t i = 0u; i < CADS_BOOTGUARD_LIMIT; i++) cads_bootguard_boot(true);
    TEST_ASSERT_TRUE(cads_bootguard_tripped());
    cads_bootguard_boot(false); /* NRST: the operator's retry */
    TEST_ASSERT_FALSE(cads_bootguard_tripped());
    TEST_ASSERT_EQUAL_UINT32(0u, cads_bootguard_count());
}

static void test_stable_session_forgives_earlier_watchdog_resets(void) {
    for(uint32_t i = 1u; i < CADS_BOOTGUARD_LIMIT; i++) cads_bootguard_boot(true);
    cads_bootguard_stable();
    cads_bootguard_boot(true); /* a later, unrelated hang */
    TEST_ASSERT_EQUAL_UINT32(1u, cads_bootguard_count());
    TEST_ASSERT_FALSE(cads_bootguard_tripped());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_clean_boot_is_not_tripped);
    RUN_TEST(test_trips_after_limit_consecutive_watchdog_resets);
    RUN_TEST(test_any_other_reset_starts_over);
    RUN_TEST(test_stable_session_forgives_earlier_watchdog_resets);
    return UNITY_END();
}

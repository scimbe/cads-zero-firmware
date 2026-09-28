/*
 * modules/diag/src/cads_forensic.c - the crash-forensics ring buffer.
 *
 * All of this runs identically on host as on the board: cads_forensic.c
 * only calls cads_hal_ticks_ms() (fake_hal.c's own clock variable here) and
 * uses CADS_CCM_SECTION, which is a no-op attribute off-target. The ring
 * eviction/ordering logic under test is exactly the code that will run on
 * real hardware inside a fault handler - proving it here, where a bug is a
 * failed assertion instead of a mysteriously wrong crash report six months
 * from now, is the entire point.
 */

#include <string.h>

#include "unity.h"

#include "cads/diag/forensic.h"
#include "fake_hal.h"

void setUp(void) {
    cads_fake_reset();
    /* No cads_forensic_init() to call - see forensic.h's own header: the
     * ring's validity comes from per-slot magic numbers, checked fresh on
     * every read, not from a one-time initialisation step. What setUp()
     * DOES need to do is start each test from a clean ring, since the
     * storage is static (module-level, matching how it lives in CCM on
     * target - persistent by design). Fill it past capacity with junk
     * records first so every test starts from a known, fully-evicted state
     * rather than depending on test execution order. */
    for(uint32_t i = 0; i < CADS_FORENSIC_RING_DEPTH; i++) {
        cads_forensic_record("setUp-flush", NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);
    }
}

void tearDown(void) {
}

static void test_empty_ring_reports_zero(void) {
    /* This only proves "no test has recorded anything since setUp's own
     * flush", not "the ring has literally never been written" - setUp()
     * already wrote CADS_FORENSIC_RING_DEPTH records, all with reason
     * "setUp-flush", precisely so this test does not depend on being the
     * first one Unity happens to run. */
    TEST_ASSERT_EQUAL_UINT32(CADS_FORENSIC_RING_DEPTH, cads_forensic_count());
    cads_forensic_record_t record;
    TEST_ASSERT_TRUE(cads_forensic_get(0u, &record));
    TEST_ASSERT_EQUAL_STRING("setUp-flush", record.reason);
    TEST_ASSERT_FALSE(cads_forensic_get(CADS_FORENSIC_RING_DEPTH, &record));
}

static void test_single_record_with_frame(void) {
    cads_forensic_frame_t frame = {
        .r0 = 0x11111111u, .r1 = 0x22222222u, .r2 = 0x33333333u, .r3 = 0x44444444u,
        .r12 = 0x55555555u, .lr = 0x66666666u, .pc = 0x77777777u, .xpsr = 0x88888888u};
    cads_forensic_record("HardFault", &frame, 0xCAFEu, 0xBEEFu, true, 0xAAAAu, true, 0xBBBBu, 0u, 0u);

    cads_forensic_record_t out;
    TEST_ASSERT_TRUE(cads_forensic_get(0u, &out));
    TEST_ASSERT_EQUAL_STRING("HardFault", out.reason);
    TEST_ASSERT_TRUE(out.has_frame);
    TEST_ASSERT_EQUAL_UINT32(0x77777777u, out.frame.pc);
    TEST_ASSERT_EQUAL_UINT32(0x66666666u, out.frame.lr);
    TEST_ASSERT_EQUAL_UINT32(0xCAFEu, out.cfsr);
    TEST_ASSERT_EQUAL_UINT32(0xBEEFu, out.hfsr);
    TEST_ASSERT_TRUE(out.mmfar_valid);
    TEST_ASSERT_EQUAL_UINT32(0xAAAAu, out.mmfar);
    TEST_ASSERT_TRUE(out.bfar_valid);
    TEST_ASSERT_EQUAL_UINT32(0xBBBBu, out.bfar);
}

static void test_panic_record_has_no_frame(void) {
    cads_forensic_record("stack overflow", NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);

    cads_forensic_record_t out;
    TEST_ASSERT_TRUE(cads_forensic_get(0u, &out));
    TEST_ASSERT_EQUAL_STRING("stack overflow", out.reason);
    TEST_ASSERT_FALSE(out.has_frame);
    TEST_ASSERT_FALSE(out.mmfar_valid);
    TEST_ASSERT_FALSE(out.bfar_valid);
}

static void test_get_orders_newest_first(void) {
    cads_forensic_record("first", NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);
    cads_forensic_record("second", NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);
    cads_forensic_record("third", NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);

    cads_forensic_record_t out;
    TEST_ASSERT_TRUE(cads_forensic_get(0u, &out));
    TEST_ASSERT_EQUAL_STRING("third", out.reason);
    TEST_ASSERT_TRUE(cads_forensic_get(1u, &out));
    TEST_ASSERT_EQUAL_STRING("second", out.reason);
    TEST_ASSERT_TRUE(cads_forensic_get(2u, &out));
    TEST_ASSERT_EQUAL_STRING("first", out.reason);
}

static void test_sequence_is_monotonic_across_the_ring(void) {
    uint32_t first_sequence = 0u;
    for(uint32_t i = 0u; i < CADS_FORENSIC_RING_DEPTH + 3u; i++) {
        cads_forensic_record("seq", NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);
        cads_forensic_record_t out;
        TEST_ASSERT_TRUE(cads_forensic_get(0u, &out));
        if(i == 0u) first_sequence = out.sequence;
        TEST_ASSERT_EQUAL_UINT32(first_sequence + i, out.sequence);
    }
}

static void test_eviction_beyond_depth_keeps_newest_and_count_capped(void) {
    /* setUp() already wrote CADS_FORENSIC_RING_DEPTH "setUp-flush" records;
     * every one of them must be gone once at least that many more real
     * records have been written - proves eviction genuinely rotates
     * through every slot, not just the one it happened to pick last time. */
    for(uint32_t i = 0u; i < CADS_FORENSIC_RING_DEPTH; i++) {
        cads_forensic_record("fresh", NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);
    }

    TEST_ASSERT_EQUAL_UINT32(CADS_FORENSIC_RING_DEPTH, cads_forensic_count());
    for(uint32_t i = 0u; i < CADS_FORENSIC_RING_DEPTH; i++) {
        cads_forensic_record_t out;
        TEST_ASSERT_TRUE(cads_forensic_get(i, &out));
        TEST_ASSERT_EQUAL_STRING("fresh", out.reason);
    }
}

static void test_uptime_is_captured_at_record_time(void) {
    cads_fake_set_ms(12345u);
    cads_forensic_record("timed", NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);

    cads_forensic_record_t out;
    TEST_ASSERT_TRUE(cads_forensic_get(0u, &out));
    TEST_ASSERT_EQUAL_UINT32(12345u, out.uptime_ms);
}

static void test_clear_empties_the_ring_and_recording_resumes(void) {
    TEST_ASSERT_EQUAL_UINT32(CADS_FORENSIC_RING_DEPTH, cads_forensic_count());
    cads_forensic_clear();
    TEST_ASSERT_EQUAL_UINT32(0u, cads_forensic_count());
    cads_forensic_record_t out;
    TEST_ASSERT_FALSE(cads_forensic_get(0u, &out));

    cads_forensic_record("after-clear", NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_UINT32(1u, cads_forensic_count());
    TEST_ASSERT_TRUE(cads_forensic_get(0u, &out));
    TEST_ASSERT_EQUAL_STRING("after-clear", out.reason);
}

static void test_reason_is_copied_not_referenced(void) {
    /* cads_kernel_assert() formats its file:line into a .bss buffer that the
     * next Reset_Handler zeroes - the record must hold its own copy. */
    char location[16] = "queue.c:1234";
    cads_forensic_record(location, NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);
    location[0] = '\0';

    cads_forensic_record_t out;
    TEST_ASSERT_TRUE(cads_forensic_get(0u, &out));
    TEST_ASSERT_EQUAL_STRING("queue.c:1234", out.reason);
}

static void test_long_reason_keeps_its_tail(void) {
    const char* location =
        "/Users/dev/some/very/long/checkout/path/lib/FreeRTOS-Kernel/queue.c:1234";
    cads_forensic_record(location, NULL, 0u, 0u, false, 0u, false, 0u, 0u, 0u);

    cads_forensic_record_t out;
    TEST_ASSERT_TRUE(cads_forensic_get(0u, &out));
    TEST_ASSERT_EQUAL_UINT32(CADS_FORENSIC_REASON_MAX - 1u, (uint32_t)strlen(out.reason));
    TEST_ASSERT_EQUAL_STRING(location + strlen(location) - (CADS_FORENSIC_REASON_MAX - 1u), out.reason);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_ring_reports_zero);
    RUN_TEST(test_single_record_with_frame);
    RUN_TEST(test_panic_record_has_no_frame);
    RUN_TEST(test_get_orders_newest_first);
    RUN_TEST(test_sequence_is_monotonic_across_the_ring);
    RUN_TEST(test_eviction_beyond_depth_keeps_newest_and_count_capped);
    RUN_TEST(test_uptime_is_captured_at_record_time);
    RUN_TEST(test_clear_empties_the_ring_and_recording_resumes);
    RUN_TEST(test_reason_is_copied_not_referenced);
    RUN_TEST(test_long_reason_keeps_its_tail);
    return UNITY_END();
}

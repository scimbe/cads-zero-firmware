/*
 * cads_tap: byte-for-byte the format apps/bringup/bringup.c emits, because
 * scripts/board_test.py parses it to decide whether a milestone passed. These
 * assertions are deliberately literal - a changed separator here would break
 * the hardware gate, and nothing else would notice.
 */

#include <stddef.h>
#include <string.h>

#include "unity.h"

#include "cads/toolbox/tap.h"

#define SINK_BYTES 1024

static char sink[SINK_BYTES];
static size_t sink_used;
static uint32_t sink_calls;
static cads_tap_t tap;

static void sink_write(void* context, const char* text, size_t length) {
    TEST_ASSERT_EQUAL_PTR(&sink_used, context);
    TEST_ASSERT_TRUE(sink_used + length < SINK_BYTES);
    memcpy(&sink[sink_used], text, length);
    sink_used += length;
    sink[sink_used] = '\0';
    sink_calls++;
}

void setUp(void) {
    sink_used = 0u;
    sink_calls = 0u;
    sink[0] = '\0';
    cads_tap_init(&tap, sink_write, &sink_used);
}

void tearDown(void) {
}

static void test_plan_is_emitted_first_and_verbatim(void) {
    cads_tap_plan(&tap, 10u);
    TEST_ASSERT_EQUAL_STRING("1..10\r\n", sink);
}

static void test_results_are_numbered_from_one(void) {
    cads_tap_ok(&tap, "SysTick advances at 1 kHz");
    cads_tap_not_ok(&tap, "DWT microsecond clock agrees");
    cads_tap_ok(&tap, "canvas 4 bpp pixel round trip");

    TEST_ASSERT_EQUAL_STRING(
        "ok 1 - SysTick advances at 1 kHz\r\n"
        "not ok 2 - DWT microsecond clock agrees\r\n"
        "ok 3 - canvas 4 bpp pixel round trip\r\n",
        sink);

    TEST_ASSERT_EQUAL_UINT32(3u, cads_tap_count(&tap));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_tap_failures(&tap));
}

static void test_check_returns_the_verdict_it_printed(void) {
    TEST_ASSERT_TRUE(cads_tap_check(&tap, 1 == 1, "arithmetic still works"));
    TEST_ASSERT_FALSE(cads_tap_check(&tap, 1 == 2, "and still does not lie"));

    TEST_ASSERT_EQUAL_STRING(
        "ok 1 - arithmetic still works\r\n"
        "not ok 2 - and still does not lie\r\n",
        sink);
}

static void test_diagnostics_never_count_as_assertions(void) {
    cads_tap_diag(&tap, "entering interactive loop");
    cads_tap_diag_uint(&tap, "flush_pixels", 153600u);
    cads_tap_diag_uint(&tap, "dropped", 0u);

    TEST_ASSERT_EQUAL_STRING(
        "# entering interactive loop\r\n"
        "# flush_pixels: 153600\r\n"
        "# dropped: 0\r\n",
        sink);
    TEST_ASSERT_EQUAL_UINT32(0u, cads_tap_count(&tap));
}

static void test_finish_reports_pass(void) {
    cads_tap_plan(&tap, 2u);
    cads_tap_ok(&tap, "one");
    cads_tap_ok(&tap, "two");
    sink_used = 0u; /* only the summary is under test here */
    sink[0] = '\0';

    TEST_ASSERT_TRUE(cads_tap_finish(&tap));
    TEST_ASSERT_EQUAL_STRING("# 2/2 passed\r\n# RESULT: PASS\r\n", sink);
}

static void test_finish_reports_fail(void) {
    cads_tap_plan(&tap, 3u);
    cads_tap_ok(&tap, "one");
    cads_tap_not_ok(&tap, "two");
    cads_tap_ok(&tap, "three");
    sink_used = 0u;
    sink[0] = '\0';

    TEST_ASSERT_FALSE(cads_tap_finish(&tap));
    TEST_ASSERT_EQUAL_STRING("# 2/3 passed\r\n# RESULT: FAIL\r\n", sink);
}

static void test_a_run_that_stopped_early_fails_even_with_no_failures(void) {
    /* This is the case a plan exists for: firmware that resets half way
     * through would otherwise report nothing but "ok" lines. */
    cads_tap_plan(&tap, 10u);
    cads_tap_ok(&tap, "one");
    cads_tap_ok(&tap, "two");
    sink_used = 0u;
    sink[0] = '\0';

    TEST_ASSERT_FALSE(cads_tap_finish(&tap));
    TEST_ASSERT_EQUAL_STRING(
        "# 2/2 passed\r\n"
        "# planned 10, ran 2\r\n"
        "# RESULT: FAIL\r\n",
        sink);
}

static void test_without_a_plan_any_count_finishes(void) {
    cads_tap_ok(&tap, "one");
    TEST_ASSERT_TRUE(cads_tap_finish(&tap));
}

static void test_no_writer_still_counts(void) {
    cads_tap_t quiet;
    cads_tap_init(&quiet, NULL, NULL);

    cads_tap_plan(&quiet, 2u);
    cads_tap_ok(&quiet, "one");
    cads_tap_not_ok(&quiet, "two");
    cads_tap_diag_uint(&quiet, "key", 1u);

    TEST_ASSERT_EQUAL_UINT32(2u, cads_tap_count(&quiet));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_tap_failures(&quiet));
    TEST_ASSERT_FALSE(cads_tap_finish(&quiet));
    TEST_ASSERT_EQUAL_UINT32(0u, sink_calls);
}

static void test_null_tap_is_survivable(void) {
    cads_tap_plan(NULL, 1u);
    cads_tap_ok(NULL, "nobody");
    cads_tap_diag(NULL, "nobody");
    TEST_ASSERT_FALSE(cads_tap_check(NULL, true, "nobody"));
    TEST_ASSERT_FALSE(cads_tap_finish(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_tap_count(NULL));
}

static void test_the_stream_matches_the_bringup_format(void) {
    /* The whole shape scripts/board_test.py expects, in one piece. */
    cads_tap_plan(&tap, 2u);
    cads_tap_check(&tap, true, "SysTick advances at 1 kHz");
    cads_tap_diag_uint(&tap, "systick_ms_over_50ms", 50u);
    cads_tap_check(&tap, false, "faster SPI divider roughly doubles throughput");
    cads_tap_finish(&tap);

    TEST_ASSERT_EQUAL_STRING(
        "1..2\r\n"
        "ok 1 - SysTick advances at 1 kHz\r\n"
        "# systick_ms_over_50ms: 50\r\n"
        "not ok 2 - faster SPI divider roughly doubles throughput\r\n"
        "# 1/2 passed\r\n"
        "# RESULT: FAIL\r\n",
        sink);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_plan_is_emitted_first_and_verbatim);
    RUN_TEST(test_results_are_numbered_from_one);
    RUN_TEST(test_check_returns_the_verdict_it_printed);
    RUN_TEST(test_diagnostics_never_count_as_assertions);
    RUN_TEST(test_finish_reports_pass);
    RUN_TEST(test_finish_reports_fail);
    RUN_TEST(test_a_run_that_stopped_early_fails_even_with_no_failures);
    RUN_TEST(test_without_a_plan_any_count_finishes);
    RUN_TEST(test_no_writer_still_counts);
    RUN_TEST(test_null_tap_is_survivable);
    RUN_TEST(test_the_stream_matches_the_bringup_format);
    return UNITY_END();
}

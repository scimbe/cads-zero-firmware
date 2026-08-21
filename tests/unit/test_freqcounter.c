/* cads_freqcounter: the M6 "Frequency/period counter on an INT line via
 * timer input capture" bullet's own capture-to-period math, tested with
 * a scripted sequence of raw counter values instead of a real TIM2 -
 * this project has no way to drive a known test frequency into CN8 pin
 * 5 (PB10) without a physical jumper wire, so this is the only real
 * proof the wraparound and missed-edge handling are correct rather than
 * assumed. */

#include <stddef.h>
#include <stdint.h>

#include "unity.h"

#include "cads/toolbox/freqcounter.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_first_capture_reports_nothing(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    uint32_t period;
    TEST_ASSERT_FALSE(cads_freqcounter_capture(&fc, 1000u, false, &period));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_period_count(&fc));
}

static void test_second_capture_reports_the_delta(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    uint32_t period = 0u;
    cads_freqcounter_capture(&fc, 1000u, false, &period);
    TEST_ASSERT_TRUE(cads_freqcounter_capture(&fc, 1500u, false, &period));

    TEST_ASSERT_EQUAL_UINT32(500u, period);
    TEST_ASSERT_EQUAL_UINT32(1u, cads_freqcounter_period_count(&fc));
}

static void test_delta_is_correct_across_a_counter_wraparound(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    uint32_t period = 0u;
    /* 100 ticks before the 32-bit counter's own maximum value. */
    cads_freqcounter_capture(&fc, 0xFFFFFFFFu - 100u, false, &period);
    /* Wrapped past 0 and 199 ticks further - true elapsed time is
     * 100 (up to the wrap) + 1 (the wrap itself) + 199 = 300 ticks. */
    TEST_ASSERT_TRUE(cads_freqcounter_capture(&fc, 199u, false, &period));

    TEST_ASSERT_EQUAL_UINT32(300u, period);
}

static void test_overcapture_resyncs_without_reporting_a_period(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    uint32_t period = 0u;
    cads_freqcounter_capture(&fc, 1000u, false, &period);
    /* An edge was missed before this one arrived - the delta from 1000
     * to 5000 does not mean "one 4000-tick period". */
    TEST_ASSERT_FALSE(cads_freqcounter_capture(&fc, 5000u, true, &period));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_freqcounter_missed_count(&fc));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_period_count(&fc));

    /* The next clean capture measures against the resync point (5000),
     * not the stale one from before the gap. */
    TEST_ASSERT_TRUE(cads_freqcounter_capture(&fc, 5100u, false, &period));
    TEST_ASSERT_EQUAL_UINT32(100u, period);
}

static void test_min_max_avg_track_across_varying_periods(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    uint32_t period = 0u;
    cads_freqcounter_capture(&fc, 0u, false, &period);
    cads_freqcounter_capture(&fc, 100u, false, &period);   /* period 100 */
    cads_freqcounter_capture(&fc, 350u, false, &period);   /* period 250 */
    cads_freqcounter_capture(&fc, 450u, false, &period);   /* period 100 */

    TEST_ASSERT_EQUAL_UINT32(3u, cads_freqcounter_period_count(&fc));
    TEST_ASSERT_EQUAL_UINT32(100u, cads_freqcounter_min_period_ticks(&fc));
    TEST_ASSERT_EQUAL_UINT32(250u, cads_freqcounter_max_period_ticks(&fc));
    TEST_ASSERT_EQUAL_UINT32(150u, cads_freqcounter_avg_period_ticks(&fc)); /* (100+250+100)/3 */
}

static void test_empty_counter_reports_zero_not_garbage(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_period_count(&fc));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_missed_count(&fc));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_min_period_ticks(&fc));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_max_period_ticks(&fc));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_avg_period_ticks(&fc));
}

static void test_null_arguments_are_refused(void) {
    uint32_t period = 0u;
    TEST_ASSERT_FALSE(cads_freqcounter_capture(NULL, 100u, false, &period));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_period_count(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_missed_count(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_min_period_ticks(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_max_period_ticks(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_avg_period_ticks(NULL));

    /* cads_freqcounter_init(NULL) must not crash either. */
    cads_freqcounter_init(NULL);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_capture_reports_nothing);
    RUN_TEST(test_second_capture_reports_the_delta);
    RUN_TEST(test_delta_is_correct_across_a_counter_wraparound);
    RUN_TEST(test_overcapture_resyncs_without_reporting_a_period);
    RUN_TEST(test_min_max_avg_track_across_varying_periods);
    RUN_TEST(test_empty_counter_reports_zero_not_garbage);
    RUN_TEST(test_null_arguments_are_refused);
    return UNITY_END();
}

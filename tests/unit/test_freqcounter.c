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

/* --- duty cycle (falling edge, "the second capture compare register") --- */

static void test_falling_edge_before_any_rising_edge_reports_nothing(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    uint32_t high = 0u;
    TEST_ASSERT_FALSE(cads_freqcounter_capture_high(&fc, 500u, false, &high));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_high_count(&fc));
    /* Not a loss - there was never a rising edge to lose. */
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_missed_high_count(&fc));
}

static void test_falling_edge_reports_high_time_since_the_rising_edge(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    uint32_t period = 0u, high = 0u;
    cads_freqcounter_capture(&fc, 1000u, false, &period); /* rising */
    TEST_ASSERT_TRUE(cads_freqcounter_capture_high(&fc, 1300u, false, &high)); /* falling */

    TEST_ASSERT_EQUAL_UINT32(300u, high);
    TEST_ASSERT_EQUAL_UINT32(1u, cads_freqcounter_high_count(&fc));
}

static void test_high_time_is_correct_across_a_counter_wraparound(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    uint32_t period = 0u, high = 0u;
    cads_freqcounter_capture(&fc, 0xFFFFFFFFu - 50u, false, &period); /* rising */
    TEST_ASSERT_TRUE(cads_freqcounter_capture_high(&fc, 49u, false, &high)); /* falling, past the wrap */

    TEST_ASSERT_EQUAL_UINT32(100u, high); /* 50 up to the wrap + 1 for the wrap + 49 */
}

static void test_falling_edge_overcapture_does_not_report_and_is_counted(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    uint32_t period = 0u, high = 0u;
    cads_freqcounter_capture(&fc, 1000u, false, &period); /* rising */
    TEST_ASSERT_FALSE(cads_freqcounter_capture_high(&fc, 1900u, true, &high));

    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_high_count(&fc));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_freqcounter_missed_high_count(&fc));
    /* The rising-edge side is a separate counter, untouched by this. */
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_missed_count(&fc));
}

static void test_duty_cycle_min_max_avg_track_across_varying_cycles(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    uint32_t period = 0u, high = 0u;
    cads_freqcounter_capture(&fc, 0u, false, &period);
    cads_freqcounter_capture_high(&fc, 25u, false, &high);   /* high 25 of period 100 */
    cads_freqcounter_capture(&fc, 100u, false, &period);
    cads_freqcounter_capture_high(&fc, 175u, false, &high);  /* high 75 of period 100 */
    cads_freqcounter_capture(&fc, 200u, false, &period);

    TEST_ASSERT_EQUAL_UINT32(2u, cads_freqcounter_high_count(&fc));
    TEST_ASSERT_EQUAL_UINT32(25u, cads_freqcounter_min_high_ticks(&fc));
    TEST_ASSERT_EQUAL_UINT32(75u, cads_freqcounter_max_high_ticks(&fc));
    TEST_ASSERT_EQUAL_UINT32(50u, cads_freqcounter_avg_high_ticks(&fc));
    TEST_ASSERT_EQUAL_UINT32(100u, cads_freqcounter_avg_period_ticks(&fc));
}

static void test_empty_high_stats_report_zero_not_garbage(void) {
    cads_freqcounter_t fc;
    cads_freqcounter_init(&fc);

    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_high_count(&fc));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_missed_high_count(&fc));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_min_high_ticks(&fc));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_max_high_ticks(&fc));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_avg_high_ticks(&fc));
}

static void test_null_arguments_are_refused_for_high_time_too(void) {
    uint32_t high = 0u;
    TEST_ASSERT_FALSE(cads_freqcounter_capture_high(NULL, 100u, false, &high));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_high_count(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_missed_high_count(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_min_high_ticks(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_max_high_ticks(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_freqcounter_avg_high_ticks(NULL));
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
    RUN_TEST(test_falling_edge_before_any_rising_edge_reports_nothing);
    RUN_TEST(test_falling_edge_reports_high_time_since_the_rising_edge);
    RUN_TEST(test_high_time_is_correct_across_a_counter_wraparound);
    RUN_TEST(test_falling_edge_overcapture_does_not_report_and_is_counted);
    RUN_TEST(test_duty_cycle_min_max_avg_track_across_varying_cycles);
    RUN_TEST(test_empty_high_stats_report_zero_not_garbage);
    RUN_TEST(test_null_arguments_are_refused_for_high_time_too);
    return UNITY_END();
}

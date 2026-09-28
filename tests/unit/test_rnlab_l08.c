/* rnlab L08 (TCP-Flusskontrolle): host tests for l08_tcp_flusskontrolle_logic.c -
 * the goodput model (window, link, application limit) and the read-rate
 * limiter behind `lab 08 sink rate`. ctest label rnlab-L08. */

#include <stdint.h>

#include "unity.h"

#include "l08_tcp_flusskontrolle_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

#define ASSERT_U64(expected, actual) TEST_ASSERT_TRUE_MESSAGE((uint64_t)(expected) == (actual), #actual)

/* --- throughput model ------------------------------------------------------ */

static void test_window_limit(void) {
    /* 8 x 536 B = 4288 B per 1 ms: 34.304 Mbit/s */
    ASSERT_U64(34304000u, rnlab_l08_window_limit_bps(8u * 536u, 1000u));
    /* 2 x 536 B per 6 ms: 1.429 Mbit/s (floor) */
    ASSERT_U64(1429333u, rnlab_l08_window_limit_bps(2u * 536u, 6000u));
}

static void test_window_limit_no_overflow(void) {
    /* 65535 B per 1 us would overflow 32 bit on the way. */
    ASSERT_U64(524280000000ull, rnlab_l08_window_limit_bps(65535u, 1u));
}

static void test_window_limit_zero_rtt(void) {
    ASSERT_U64(0u, rnlab_l08_window_limit_bps(4288u, 0u));
}

static void test_link_limit(void) {
    /* 100 Mbit/s, 1460 B payload in 1538 B on the wire */
    ASSERT_U64(94928478u, rnlab_l08_link_limit_bps(100000000u, 1460u));
    /* 536 B payload in 614 B */
    ASSERT_U64(87296416u, rnlab_l08_link_limit_bps(100000000u, 536u));
    ASSERT_U64(0u, rnlab_l08_link_limit_bps(100000000u, 0u));
}

static void test_predict_window_bound(void) {
    ASSERT_U64(1429333u, rnlab_l08_predict_bps(1072u, 6000u, 100000000u, 536u, 0u));
}

static void test_predict_link_bound(void) {
    /* 16 x 1460 B per 0.5 ms would be 373 Mbit/s - the link caps it. */
    ASSERT_U64(94928478u, rnlab_l08_predict_bps(16u * 1460u, 500u, 100000000u, 1460u, 0u));
}

static void test_predict_app_bound(void) {
    /* The application reads 50 kB/s = 400 kbit/s, far below both limits. */
    ASSERT_U64(400000u, rnlab_l08_predict_bps(4288u, 1000u, 100000000u, 536u, 50000u));
}

static void test_predict_invalid(void) {
    ASSERT_U64(0u, rnlab_l08_predict_bps(4288u, 0u, 100000000u, 536u, 0u));
    ASSERT_U64(0u, rnlab_l08_predict_bps(4288u, 1000u, 100000000u, 0u, 0u));
}

/* --- read-rate limiter ----------------------------------------------------- */

static void test_limiter_unlimited(void) {
    rnlab_l08_limiter_t l;
    rnlab_l08_limiter_init(&l, 0u, 0u);
    TEST_ASSERT_EQUAL_UINT32(5000u, rnlab_l08_limiter_release(&l, 0u, 5000u));
}

static void test_limiter_rate(void) {
    rnlab_l08_limiter_t l;
    rnlab_l08_limiter_init(&l, 10000u, 1000u); /* 10 kB/s = 10 B/ms */
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_l08_limiter_release(&l, 1000u, 5000u));
    TEST_ASSERT_EQUAL_UINT32(100u, rnlab_l08_limiter_release(&l, 1010u, 5000u));
    TEST_ASSERT_EQUAL_UINT32(200u, rnlab_l08_limiter_release(&l, 1030u, 5000u));
}

static void test_limiter_keeps_unused_credit(void) {
    rnlab_l08_limiter_t l;
    rnlab_l08_limiter_init(&l, 10000u, 0u);
    /* 100 B earned, only 40 waiting: 60 B stay banked. */
    TEST_ASSERT_EQUAL_UINT32(40u, rnlab_l08_limiter_release(&l, 10u, 40u));
    TEST_ASSERT_EQUAL_UINT32(160u, rnlab_l08_limiter_release(&l, 20u, 1000u));
}

static void test_limiter_cap(void) {
    rnlab_l08_limiter_t l;
    rnlab_l08_limiter_init(&l, 10000u, 0u);
    /* 10 s idle would be 100 kB - only RNLAB_L08_BUCKET_MS (100 ms = 1000 B) count. */
    TEST_ASSERT_EQUAL_UINT32(1000u, rnlab_l08_limiter_release(&l, 10000u, 1000000u));
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_l08_limiter_release(&l, 10000u, 1000000u));
}

static void test_limiter_slow_rate_accumulates(void) {
    /* 50 B/s with a 10 ms timer: 0.5 B per call must not round to zero forever. */
    rnlab_l08_limiter_t l;
    rnlab_l08_limiter_init(&l, 50u, 0u);
    uint32_t total = 0u;
    for(uint32_t t = 10u; t <= 1000u; t += 10u) total += rnlab_l08_limiter_release(&l, t, 1000u);
    TEST_ASSERT_EQUAL_UINT32(50u, total);
}

static void test_limiter_very_slow_rate(void) {
    /* 1 B/s: the cap (0.1 B at 100 ms) must still let one byte through per second. */
    rnlab_l08_limiter_t l;
    rnlab_l08_limiter_init(&l, 1u, 0u);
    uint32_t total = 0u;
    for(uint32_t t = 10u; t <= 3000u; t += 10u) total += rnlab_l08_limiter_release(&l, t, 1000u);
    TEST_ASSERT_EQUAL_UINT32(3u, total);
}

static void test_limiter_wrap(void) {
    rnlab_l08_limiter_t l;
    rnlab_l08_limiter_init(&l, 10000u, 0xFFFFFFF0u);
    /* 0xFFFFFFF0 -> 0x0000000A is 26 ms, not minus four billion. */
    TEST_ASSERT_EQUAL_UINT32(260u, rnlab_l08_limiter_release(&l, 0x0000000Au, 5000u));
}

static void test_limiter_nothing_pending(void) {
    rnlab_l08_limiter_t l;
    rnlab_l08_limiter_init(&l, 10000u, 0u);
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_l08_limiter_release(&l, 50u, 0u));
    TEST_ASSERT_EQUAL_UINT32(500u, rnlab_l08_limiter_release(&l, 50u, 800u));
}

static void test_limiter_null(void) {
    rnlab_l08_limiter_init(NULL, 1u, 0u);
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_l08_limiter_release(NULL, 10u, 10u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_window_limit);
    RUN_TEST(test_window_limit_no_overflow);
    RUN_TEST(test_window_limit_zero_rtt);
    RUN_TEST(test_link_limit);
    RUN_TEST(test_predict_window_bound);
    RUN_TEST(test_predict_link_bound);
    RUN_TEST(test_predict_app_bound);
    RUN_TEST(test_predict_invalid);
    RUN_TEST(test_limiter_unlimited);
    RUN_TEST(test_limiter_rate);
    RUN_TEST(test_limiter_keeps_unused_credit);
    RUN_TEST(test_limiter_cap);
    RUN_TEST(test_limiter_slow_rate_accumulates);
    RUN_TEST(test_limiter_very_slow_rate);
    RUN_TEST(test_limiter_wrap);
    RUN_TEST(test_limiter_nothing_pending);
    RUN_TEST(test_limiter_null);
    return UNITY_END();
}

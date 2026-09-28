/* rnlab L07 (UDP-Transport): host tests for l07_udp_transport_logic.c -
 * the rnlab header decoder and the sequence tracker (loss, reordering,
 * duplicates, 32-bit wraparound). ctest label rnlab-L07. */

#include <stdint.h>

#include "unity.h"

#include "l07_udp_transport_logic.h"

static rnlab_seq_tracker_t t;

void setUp(void) {
    rnlab_seq_reset(&t);
}

void tearDown(void) {
}

static void feed(const uint32_t* seqs, int n) {
    for(int i = 0; i < n; i++) rnlab_seq_update(&t, seqs[i]);
}

/* --- header ---------------------------------------------------------------- */

static void test_header_big_endian(void) {
    /* seq 0x01020304, time 0x1122334455667788 - what rnlab.py packs with "!IQ" */
    const uint8_t d[14] = {0x01, 0x02, 0x03, 0x04, 0x11, 0x22, 0x33, 0x44,
                           0x55, 0x66, 0x77, 0x88, 0x00, 0x00};
    uint32_t seq = 0u;
    uint64_t us = 0u;
    TEST_ASSERT_TRUE(rnlab_l07_parse_header(d, sizeof(d), &seq, &us));
    TEST_ASSERT_EQUAL_HEX32(0x01020304u, seq);
    TEST_ASSERT_TRUE(us == 0x1122334455667788ull);
}

static void test_header_exact_minimum(void) {
    const uint8_t d[12] = {0xFF, 0xFF, 0xFF, 0xFE, 0, 0, 0, 0, 0, 0, 0, 1};
    uint32_t seq = 0u;
    uint64_t us = 0u;
    TEST_ASSERT_TRUE(rnlab_l07_parse_header(d, sizeof(d), &seq, &us));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEu, seq);
    TEST_ASSERT_TRUE(us == 1u);
}

static void test_header_too_short(void) {
    const uint8_t d[11] = {0};
    uint32_t seq = 77u;
    uint64_t us = 77u;
    TEST_ASSERT_FALSE(rnlab_l07_parse_header(d, sizeof(d), &seq, &us));
    TEST_ASSERT_EQUAL_UINT32(77u, seq);
    TEST_ASSERT_TRUE(us == 77u);
}

/* --- tracker --------------------------------------------------------------- */

static void test_first_then_in_order(void) {
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_FIRST, rnlab_seq_update(&t, 0u));
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_IN_ORDER, rnlab_seq_update(&t, 1u));
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_IN_ORDER, rnlab_seq_update(&t, 2u));
    TEST_ASSERT_EQUAL_UINT32(3u, t.received);
    TEST_ASSERT_EQUAL_UINT32(3u, rnlab_seq_expected(&t));
    TEST_ASSERT_EQUAL_UINT32(0u, t.lost);
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_seq_loss_bp(&t));
}

static void test_first_need_not_be_zero(void) {
    /* Joining a running stream: nothing before the first datagram is "lost". */
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_FIRST, rnlab_seq_update(&t, 500u));
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_IN_ORDER, rnlab_seq_update(&t, 501u));
    TEST_ASSERT_EQUAL_UINT32(2u, rnlab_seq_expected(&t));
    TEST_ASSERT_EQUAL_UINT32(0u, t.lost);
}

static void test_gap_counts_lost(void) {
    const uint32_t s[] = {0u, 1u, 5u, 6u};
    feed(s, 4);
    TEST_ASSERT_EQUAL_UINT32(3u, t.lost); /* 2, 3, 4 */
    TEST_ASSERT_EQUAL_UINT32(7u, rnlab_seq_expected(&t));
    TEST_ASSERT_EQUAL_UINT32(4285u, rnlab_seq_loss_bp(&t)); /* 3/7 = 42.85 % */
    TEST_ASSERT_EQUAL_UINT32(0u, t.reordered);
}

static void test_gap_event(void) {
    rnlab_seq_update(&t, 10u);
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_GAP, rnlab_seq_update(&t, 12u));
}

static void test_late_fills_gap(void) {
    const uint32_t s[] = {0u, 1u, 3u};
    feed(s, 3);
    TEST_ASSERT_EQUAL_UINT32(1u, t.lost);
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_LATE, rnlab_seq_update(&t, 2u));
    TEST_ASSERT_EQUAL_UINT32(0u, t.lost);
    TEST_ASSERT_EQUAL_UINT32(1u, t.reordered);
    TEST_ASSERT_EQUAL_UINT32(4u, rnlab_seq_expected(&t));
}

static void test_duplicate_of_highest(void) {
    const uint32_t s[] = {0u, 1u};
    feed(s, 2);
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_DUPLICATE, rnlab_seq_update(&t, 1u));
    TEST_ASSERT_EQUAL_UINT32(1u, t.duplicates);
    TEST_ASSERT_EQUAL_UINT32(3u, t.received);
    TEST_ASSERT_EQUAL_UINT32(0u, t.lost);
}

static void test_duplicate_of_late(void) {
    /* The late arrival is remembered - its repeat is a duplicate, not a second fill. */
    const uint32_t s[] = {0u, 2u, 1u};
    feed(s, 3);
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_DUPLICATE, rnlab_seq_update(&t, 1u));
    TEST_ASSERT_EQUAL_UINT32(1u, t.reordered);
    TEST_ASSERT_EQUAL_UINT32(0u, t.lost);
}

static void test_duplicate_of_first(void) {
    rnlab_seq_update(&t, 0u);
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_DUPLICATE, rnlab_seq_update(&t, 0u));
}

static void test_window_edge(void) {
    /* 0, then 31 (1..30 lost). The window reaches back exactly 32 numbers:
     * 0 is still inside at age 31, and falls out once 32 arrives. */
    rnlab_seq_update(&t, 0u);
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_GAP, rnlab_seq_update(&t, 31u));
    TEST_ASSERT_EQUAL_UINT32(30u, t.lost);
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_LATE, rnlab_seq_update(&t, 1u));      /* age 30 */
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_DUPLICATE, rnlab_seq_update(&t, 0u)); /* age 31, last bit */
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_IN_ORDER, rnlab_seq_update(&t, 32u));
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_STALE, rnlab_seq_update(&t, 0u));     /* age 32 */
    TEST_ASSERT_EQUAL_UINT32(1u, t.stale);
    TEST_ASSERT_EQUAL_UINT32(29u, t.lost);
}

static void test_big_jump_clears_window(void) {
    rnlab_seq_update(&t, 0u);
    rnlab_seq_update(&t, 100u);
    TEST_ASSERT_EQUAL_UINT32(99u, t.lost);
    /* 99 is age 1 - inside the new window and never seen: late. */
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_LATE, rnlab_seq_update(&t, 99u));
    TEST_ASSERT_EQUAL_UINT32(98u, t.lost);
    /* 0 is far behind: stale, not a duplicate and not a fill. */
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_STALE, rnlab_seq_update(&t, 0u));
    TEST_ASSERT_EQUAL_UINT32(98u, t.lost);
}

static void test_wrap_in_order(void) {
    const uint32_t s[] = {0xFFFFFFFEu, 0xFFFFFFFFu, 0u, 1u};
    feed(s, 4);
    TEST_ASSERT_EQUAL_UINT32(0u, t.lost);
    TEST_ASSERT_EQUAL_UINT32(0u, t.reordered);
    TEST_ASSERT_EQUAL_UINT32(4u, rnlab_seq_expected(&t));
    TEST_ASSERT_EQUAL_UINT32(2u, t.next);
}

static void test_wrap_gap(void) {
    rnlab_seq_update(&t, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_GAP, rnlab_seq_update(&t, 2u));
    TEST_ASSERT_EQUAL_UINT32(2u, t.lost); /* 0 and 1 */
    TEST_ASSERT_EQUAL_UINT32(4u, rnlab_seq_expected(&t));
}

static void test_wrap_late(void) {
    const uint32_t s[] = {0xFFFFFFFEu, 1u};
    feed(s, 2);
    TEST_ASSERT_EQUAL_UINT32(2u, t.lost); /* 0xFFFFFFFF and 0 */
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_LATE, rnlab_seq_update(&t, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_LATE, rnlab_seq_update(&t, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, t.lost);
    TEST_ASSERT_EQUAL_UINT32(2u, t.reordered);
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_DUPLICATE, rnlab_seq_update(&t, 0xFFFFFFFEu));
}

static void test_restart_from_zero_is_stale(void) {
    /* A second udp-send run starts at 0 again: without `lab 07 udp reset` the
     * tracker sees an old number, never "new" or "lost". */
    for(uint32_t s = 0u; s < 1000u; s++) rnlab_seq_update(&t, s);
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_STALE, rnlab_seq_update(&t, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, t.lost);
}

static void test_reset(void) {
    const uint32_t s[] = {0u, 5u};
    feed(s, 2);
    rnlab_seq_reset(&t);
    TEST_ASSERT_EQUAL_UINT32(0u, t.received);
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_seq_expected(&t));
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_FIRST, rnlab_seq_update(&t, 9u));
}

static void test_null_safe(void) {
    TEST_ASSERT_EQUAL_INT(RNLAB_SEQ_ERROR, rnlab_seq_update(NULL, 1u));
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_seq_expected(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_seq_loss_bp(NULL));
}

static void test_mixed_stream(void) {
    /* 0..9 with 3 lost, 5/6 swapped and 8 doubled. */
    const uint32_t s[] = {0u, 1u, 2u, 4u, 6u, 5u, 7u, 8u, 8u, 9u};
    feed(s, 10);
    TEST_ASSERT_EQUAL_UINT32(10u, t.received);
    TEST_ASSERT_EQUAL_UINT32(10u, rnlab_seq_expected(&t));
    TEST_ASSERT_EQUAL_UINT32(1u, t.lost);
    TEST_ASSERT_EQUAL_UINT32(1u, t.reordered);
    TEST_ASSERT_EQUAL_UINT32(1u, t.duplicates);
    TEST_ASSERT_EQUAL_UINT32(1000u, rnlab_seq_loss_bp(&t));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_header_big_endian);
    RUN_TEST(test_header_exact_minimum);
    RUN_TEST(test_header_too_short);
    RUN_TEST(test_first_then_in_order);
    RUN_TEST(test_first_need_not_be_zero);
    RUN_TEST(test_gap_counts_lost);
    RUN_TEST(test_gap_event);
    RUN_TEST(test_late_fills_gap);
    RUN_TEST(test_duplicate_of_highest);
    RUN_TEST(test_duplicate_of_late);
    RUN_TEST(test_duplicate_of_first);
    RUN_TEST(test_window_edge);
    RUN_TEST(test_big_jump_clears_window);
    RUN_TEST(test_wrap_in_order);
    RUN_TEST(test_wrap_gap);
    RUN_TEST(test_wrap_late);
    RUN_TEST(test_restart_from_zero_is_stale);
    RUN_TEST(test_reset);
    RUN_TEST(test_null_safe);
    RUN_TEST(test_mixed_stream);
    return UNITY_END();
}

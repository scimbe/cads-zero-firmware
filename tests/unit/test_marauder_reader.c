/* apps/marauder's line reader: Marauder's plaintext CLI response parser,
 * independent of any HAL/GUI - see apps/marauder/cads_marauder_reader.h. */

#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads_marauder_reader.h"

void setUp(void) {
}

void tearDown(void) {
}

static void feed_str(cads_marauder_reader_t* r, const char* s) {
    cads_marauder_reader_feed(r, (const uint8_t*)s, strlen(s));
}

static void test_reset_is_empty(void) {
    cads_marauder_reader_t r;
    cads_marauder_reader_reset(&r);
    TEST_ASSERT_EQUAL_UINT8(0u, r.count);
}

static void test_single_line(void) {
    cads_marauder_reader_t r;
    cads_marauder_reader_reset(&r);
    feed_str(&r, "hello\n");
    TEST_ASSERT_EQUAL_UINT8(1u, r.count);
    TEST_ASSERT_EQUAL_STRING("hello", cads_marauder_reader_line(&r, 0));
}

static void test_crlf_strips_cr(void) {
    cads_marauder_reader_t r;
    cads_marauder_reader_reset(&r);
    feed_str(&r, "hello\r\n");
    TEST_ASSERT_EQUAL_UINT8(1u, r.count);
    TEST_ASSERT_EQUAL_STRING("hello", cads_marauder_reader_line(&r, 0));
}

static void test_multiple_lines_in_order(void) {
    cads_marauder_reader_t r;
    cads_marauder_reader_reset(&r);
    feed_str(&r, "one\ntwo\nthree\n");
    TEST_ASSERT_EQUAL_UINT8(3u, r.count);
    TEST_ASSERT_EQUAL_STRING("one", cads_marauder_reader_line(&r, 0));
    TEST_ASSERT_EQUAL_STRING("two", cads_marauder_reader_line(&r, 1));
    TEST_ASSERT_EQUAL_STRING("three", cads_marauder_reader_line(&r, 2));
}

/* A command can arrive split across several UART reads (bytes trickle in) -
 * the partial buffer must carry state across separate feed() calls. */
static void test_line_split_across_feeds(void) {
    cads_marauder_reader_t r;
    cads_marauder_reader_reset(&r);
    feed_str(&r, "hel");
    TEST_ASSERT_EQUAL_UINT8(0u, r.count);
    feed_str(&r, "lo\n");
    TEST_ASSERT_EQUAL_UINT8(1u, r.count);
    TEST_ASSERT_EQUAL_STRING("hello", cads_marauder_reader_line(&r, 0));
}

/* A live scanall response mixing AP scan lines and station-association
 * lines - shaped after hardware capture 2026-08-27/28. Real Marauder AP
 * lines ("-76 Ch: 2 fc:34:97:30:ad:21 ESSID: persepolis-XI 11 14", 57
 * chars) run well past CADS_MARAUDER_LINE_LEN (30, chosen for RAM, not for
 * matching every possible CLI line) - the first line here is trimmed to fit
 * in one segment so this test isolates "multiple distinct lines arrive
 * correctly", not overflow-splitting (test_overlong_line_is_truncated_not_
 * lost covers that on its own). */
static void test_realistic_scanall_burst(void) {
    cads_marauder_reader_t r;
    cads_marauder_reader_reset(&r);
    feed_str(&r,
        "#scanall\n"
        "Scanning. Stop with stopscan\n"
        "-76 Ch:2 ESSID: persepolis\n"
        "1: ap->sta assoc\n");
    TEST_ASSERT_EQUAL_UINT8(4u, r.count);
    TEST_ASSERT_EQUAL_STRING("#scanall", cads_marauder_reader_line(&r, 0));
    TEST_ASSERT_EQUAL_STRING("Scanning. Stop with stopscan", cads_marauder_reader_line(&r, 1));
}

/* Past OUT_LINES (5) lines, the ring drops the oldest - only the most
 * recent lines matter for a live status view. */
static void test_ring_drops_oldest_past_capacity(void) {
    cads_marauder_reader_t r;
    cads_marauder_reader_reset(&r);
    feed_str(&r, "l1\nl2\nl3\nl4\nl5\nl6\nl7\n");
    TEST_ASSERT_EQUAL_UINT8(CADS_MARAUDER_OUT_LINES, r.count);
    TEST_ASSERT_EQUAL_STRING("l3", cads_marauder_reader_line(&r, 0));
    TEST_ASSERT_EQUAL_STRING("l7", cads_marauder_reader_line(&r, (uint8_t)(CADS_MARAUDER_OUT_LINES - 1u)));
}

/* A line longer than the display width is truncated, not overrun or lost -
 * the buffer stays bounded (no dynamic allocation) and output keeps
 * flowing rather than blocking on an oversized line. Exactly one buffer's
 * worth of non-newline bytes (LINE_LEN-1) triggers exactly one overflow
 * flush before the real newline arrives - a longer run would flush more
 * than once, which is correct (bounded, never silently drops bytes) but
 * not what this test isolates. */
static void test_overlong_line_is_truncated_not_lost(void) {
    cads_marauder_reader_t r;
    cads_marauder_reader_reset(&r);
    char long_line[CADS_MARAUDER_LINE_LEN - 1u + 1u]; /* LINE_LEN-1 'x's + NUL */
    memset(long_line, 'x', CADS_MARAUDER_LINE_LEN - 1u);
    long_line[CADS_MARAUDER_LINE_LEN - 1u] = '\0';
    feed_str(&r, long_line);
    feed_str(&r, "\nshort\n");
    TEST_ASSERT_EQUAL_UINT8(2u, r.count);
    TEST_ASSERT_EQUAL_UINT32(
        (uint32_t)(CADS_MARAUDER_LINE_LEN - 1u), (uint32_t)strlen(cads_marauder_reader_line(&r, 0)));
    TEST_ASSERT_EQUAL_STRING("short", cads_marauder_reader_line(&r, 1));
}

/* A run well past one buffer's worth (no newline) flushes more than once,
 * bounded each time, rather than growing or dropping data. */
static void test_very_overlong_run_flushes_multiple_segments(void) {
    cads_marauder_reader_t r;
    cads_marauder_reader_reset(&r);
    char long_line[80];
    memset(long_line, 'x', sizeof(long_line) - 1u);
    long_line[sizeof(long_line) - 1u] = '\0';
    feed_str(&r, long_line);
    feed_str(&r, "\nshort\n");
    /* 79 bytes / (LINE_LEN-1=29 per segment) = 3 flush segments, + "short". */
    TEST_ASSERT_EQUAL_UINT8(4u, r.count);
    TEST_ASSERT_EQUAL_STRING("short", cads_marauder_reader_line(&r, 3));
}

static void test_lines_total_counts_every_line_ever(void) {
    cads_marauder_reader_t r;
    cads_marauder_reader_reset(&r);
    feed_str(&r, "a\nb\nc\nd\ne\nf\n");
    TEST_ASSERT_EQUAL_UINT32(6u, r.lines_total);
    TEST_ASSERT_EQUAL_UINT8(CADS_MARAUDER_OUT_LINES, r.count); /* ring still capped */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_reset_is_empty);
    RUN_TEST(test_single_line);
    RUN_TEST(test_crlf_strips_cr);
    RUN_TEST(test_multiple_lines_in_order);
    RUN_TEST(test_line_split_across_feeds);
    RUN_TEST(test_realistic_scanall_burst);
    RUN_TEST(test_ring_drops_oldest_past_capacity);
    RUN_TEST(test_overlong_line_is_truncated_not_lost);
    RUN_TEST(test_very_overlong_run_flushes_multiple_segments);
    RUN_TEST(test_lines_total_counts_every_line_ever);
    return UNITY_END();
}

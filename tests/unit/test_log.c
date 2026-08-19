/* cads_log: the "with levels, routed to the console" half of M2's roadmap
 * line. cads_log is a global sink (see cads/toolbox/log.h for why, unlike
 * every other caller-owned-instance module in this toolbox), so setUp()
 * re-initialises it before every test rather than constructing a fresh
 * instance the way test_tap.c does. */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unity.h"

#include "cads/toolbox/log.h"

#define SINK_BYTES 256

static char sink[SINK_BYTES];
static size_t sink_used;
static uint32_t sink_calls;

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
    cads_log_init(sink_write, &sink_used, CadsLogDebug); /* most verbose by default */
}

void tearDown(void) {
    cads_log_init(NULL, NULL, CadsLogInfo); /* leave no sink dangling for the next test */
}

static void test_format_is_level_bracket_tag_message(void) {
    cads_log(CadsLogInfo, "eth", "link up");
    TEST_ASSERT_EQUAL_STRING("I [eth] link up\r\n", sink);
}

static void test_each_level_has_its_own_letter(void) {
    cads_log(CadsLogError, "a", "x");
    cads_log(CadsLogWarn, "a", "x");
    cads_log(CadsLogInfo, "a", "x");
    cads_log(CadsLogDebug, "a", "x");
    TEST_ASSERT_EQUAL_STRING(
        "E [a] x\r\n"
        "W [a] x\r\n"
        "I [a] x\r\n"
        "D [a] x\r\n",
        sink);
}

static void test_minimum_level_filters_less_severe_messages(void) {
    cads_log_set_level(CadsLogWarn);
    cads_log(CadsLogError, "a", "shown");
    cads_log(CadsLogWarn, "a", "shown");
    cads_log(CadsLogInfo, "a", "dropped");
    cads_log(CadsLogDebug, "a", "dropped");

    TEST_ASSERT_EQUAL_STRING(
        "E [a] shown\r\n"
        "W [a] shown\r\n",
        sink);
    /* letter, " [", tag, "] ", message, "\r\n" - six cads_log_puts() calls
     * per emitted line, times two emitted lines. */
    TEST_ASSERT_EQUAL_UINT32(2u * 6u, sink_calls);
}

static void test_dropped_messages_do_not_touch_the_sink_at_all(void) {
    cads_log_set_level(CadsLogError);
    cads_log(CadsLogDebug, "a", "never reaches sink_write");
    TEST_ASSERT_EQUAL_UINT32(0u, sink_calls);
    TEST_ASSERT_EQUAL_STRING("", sink);
}

static void test_level_getter_reflects_setter(void) {
    cads_log_set_level(CadsLogError);
    TEST_ASSERT_EQUAL(CadsLogError, cads_log_level());
    cads_log_set_level(CadsLogDebug);
    TEST_ASSERT_EQUAL(CadsLogDebug, cads_log_level());
}

static void test_convenience_wrappers_match_the_explicit_call(void) {
    cads_log_error("a", "x");
    TEST_ASSERT_EQUAL_STRING("E [a] x\r\n", sink);

    sink_used = 0u;
    sink[0] = '\0';
    cads_log_warn("a", "x");
    TEST_ASSERT_EQUAL_STRING("W [a] x\r\n", sink);

    sink_used = 0u;
    sink[0] = '\0';
    cads_log_info("a", "x");
    TEST_ASSERT_EQUAL_STRING("I [a] x\r\n", sink);

    sink_used = 0u;
    sink[0] = '\0';
    cads_log_debug("a", "x");
    TEST_ASSERT_EQUAL_STRING("D [a] x\r\n", sink);
}

static void test_no_sink_configured_is_silent_and_safe(void) {
    cads_log_init(NULL, NULL, CadsLogDebug);
    cads_log(CadsLogError, "a", "x"); /* must not crash */
    TEST_ASSERT_TRUE(true);
}

static void test_null_tag_and_message_are_treated_as_empty(void) {
    cads_log(CadsLogInfo, NULL, NULL);
    TEST_ASSERT_EQUAL_STRING("I [] \r\n", sink);
}

static void test_level_letters_are_stable(void) {
    TEST_ASSERT_EQUAL_STRING("E", cads_log_level_letter(CadsLogError));
    TEST_ASSERT_EQUAL_STRING("W", cads_log_level_letter(CadsLogWarn));
    TEST_ASSERT_EQUAL_STRING("I", cads_log_level_letter(CadsLogInfo));
    TEST_ASSERT_EQUAL_STRING("D", cads_log_level_letter(CadsLogDebug));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_format_is_level_bracket_tag_message);
    RUN_TEST(test_each_level_has_its_own_letter);
    RUN_TEST(test_minimum_level_filters_less_severe_messages);
    RUN_TEST(test_dropped_messages_do_not_touch_the_sink_at_all);
    RUN_TEST(test_level_getter_reflects_setter);
    RUN_TEST(test_convenience_wrappers_match_the_explicit_call);
    RUN_TEST(test_no_sink_configured_is_silent_and_safe);
    RUN_TEST(test_null_tag_and_message_are_treated_as_empty);
    RUN_TEST(test_level_letters_are_stable);
    return UNITY_END();
}

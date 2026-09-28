/* rnlab hook dispatcher: two lessons hook the same points at once and both
 * are called, in lesson order; drop decisions are ORed without skipping
 * anyone; the first ip4_input taker wins. The "lessons" are this file's own
 * strong definitions, overriding the dispatcher's weak defaults - exactly
 * how a lesson's lNN_<slug>.c overrides them on the board. */

#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/net/rnlab_hooks.h"
#include "rnlab/rnlab_lesson.h"

static char s_order[16];
static size_t s_order_len;
static size_t s_l01_rx_len, s_l07_rx_len, s_l07_tx_len;
static unsigned s_l01_drop_asks, s_l07_drop_asks;
static bool s_l07_wants_drop;
static int s_l03_ip4_result;
static unsigned s_l09_ip4_calls;

static void note(char c) {
    if(s_order_len < sizeof(s_order) - 1u) s_order[s_order_len++] = c;
    s_order[s_order_len] = '\0';
}

void rnlab_l01_hook_rx_frame(const uint8_t* frame, size_t len) {
    (void)frame;
    s_l01_rx_len = len;
    note('a');
}

void rnlab_l07_hook_rx_frame(const uint8_t* frame, size_t len) {
    (void)frame;
    s_l07_rx_len = len;
    note('g');
}

bool rnlab_l01_hook_rx_drop(const uint8_t* frame, size_t len) {
    (void)frame;
    (void)len;
    s_l01_drop_asks++;
    return false;
}

bool rnlab_l07_hook_rx_drop(const uint8_t* frame, size_t len) {
    (void)frame;
    (void)len;
    s_l07_drop_asks++;
    return s_l07_wants_drop;
}

void rnlab_l07_hook_tx_frame(const uint8_t* frame, size_t len) {
    (void)frame;
    s_l07_tx_len = len;
}

int rnlab_l03_hook_ip4_input(struct pbuf* p, struct netif* inp) {
    (void)p;
    (void)inp;
    return s_l03_ip4_result;
}

int rnlab_l09_hook_ip4_input(struct pbuf* p, struct netif* inp) {
    (void)p;
    (void)inp;
    s_l09_ip4_calls++;
    return 1;
}

static const uint8_t s_frame[60];

void setUp(void) {
    s_order_len = 0u;
    s_order[0] = '\0';
    s_l01_rx_len = s_l07_rx_len = s_l07_tx_len = 0u;
    s_l01_drop_asks = s_l07_drop_asks = 0u;
    s_l07_wants_drop = false;
    s_l03_ip4_result = 0;
    s_l09_ip4_calls = 0u;
}

void tearDown(void) {
}

static void test_both_lessons_see_every_frame_in_order(void) {
    rnlab_hook_rx_frame(s_frame, sizeof(s_frame));
    TEST_ASSERT_EQUAL_STRING("ag", s_order);
    TEST_ASSERT_EQUAL_size_t(60u, s_l01_rx_len);
    TEST_ASSERT_EQUAL_size_t(60u, s_l07_rx_len);
}

static void test_tx_frame_reaches_its_lesson(void) {
    rnlab_hook_tx_frame(s_frame, 42u);
    TEST_ASSERT_EQUAL_size_t(42u, s_l07_tx_len);
}

static void test_drop_is_or_and_asks_everyone(void) {
    TEST_ASSERT_FALSE(rnlab_hook_rx_drop(s_frame, sizeof(s_frame)));
    s_l07_wants_drop = true;
    TEST_ASSERT_TRUE(rnlab_hook_rx_drop(s_frame, sizeof(s_frame)));
    TEST_ASSERT_EQUAL_UINT(2u, s_l01_drop_asks);
    TEST_ASSERT_EQUAL_UINT(2u, s_l07_drop_asks);
}

static void test_unhooked_points_default_to_no_op(void) {
    TEST_ASSERT_FALSE(rnlab_hook_tx_drop(s_frame, sizeof(s_frame)));
}

static void test_first_ip4_taker_wins(void) {
    /* L03 passes, L09 takes it. */
    TEST_ASSERT_EQUAL_INT(1, rnlab_hook_ip4_input(NULL, NULL));
    TEST_ASSERT_EQUAL_UINT(1u, s_l09_ip4_calls);

    /* L03 takes it: L09 must not see a pbuf that is already freed. */
    s_l03_ip4_result = 7;
    TEST_ASSERT_EQUAL_INT(7, rnlab_hook_ip4_input(NULL, NULL));
    TEST_ASSERT_EQUAL_UINT(1u, s_l09_ip4_calls);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_both_lessons_see_every_frame_in_order);
    RUN_TEST(test_tx_frame_reaches_its_lesson);
    RUN_TEST(test_drop_is_or_and_asks_everyone);
    RUN_TEST(test_unhooked_points_default_to_no_op);
    RUN_TEST(test_first_ip4_taker_wins);
    return UNITY_END();
}

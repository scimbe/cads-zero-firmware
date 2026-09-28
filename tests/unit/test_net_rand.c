/* cads_net_rand: lwIP's LWIP_RAND source - hardware words pass straight
 * through; on a hardware error the seeded fallback steps in and is counted.
 * The hardware is a fake here: a scripted sequence that can be told to fail. */

#include <stdint.h>

#include "unity.h"

#include "cads/net/rand.h"

static bool s_hw_ok;
static uint32_t s_hw_next;

static bool fake_hw(uint32_t* word) {
    if(!s_hw_ok) return false;
    *word = s_hw_next++;
    return true;
}

void setUp(void) {
    s_hw_ok = true;
    s_hw_next = 0xA5A50000u;
}

void tearDown(void) {
}

static void test_hardware_words_pass_through_uncounted(void) {
    cads_net_rand_t r;
    cads_net_rand_seed(&r, 1u);
    TEST_ASSERT_EQUAL_HEX32(0xA5A50000u, cads_net_rand_next(&r, fake_hw));
    TEST_ASSERT_EQUAL_HEX32(0xA5A50001u, cads_net_rand_next(&r, fake_hw));
    TEST_ASSERT_EQUAL_UINT32(0u, r.fallbacks);
}

static void test_hardware_error_falls_back_and_counts(void) {
    cads_net_rand_t r;
    cads_net_rand_seed(&r, 42u);
    s_hw_ok = false;
    uint32_t a = cads_net_rand_next(&r, fake_hw);
    uint32_t b = cads_net_rand_next(&r, fake_hw);
    TEST_ASSERT_NOT_EQUAL(0u, a);
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_EQUAL_UINT32(2u, r.fallbacks);

    /* The hardware recovering takes over again immediately. */
    s_hw_ok = true;
    TEST_ASSERT_EQUAL_HEX32(0xA5A50000u, cads_net_rand_next(&r, fake_hw));
    TEST_ASSERT_EQUAL_UINT32(2u, r.fallbacks);
}

/* The old bug in miniature: one fixed seed = one fixed sequence. Seeds that
 * differ in a single bit (consecutive boot ticks, neighbouring UIDs) must
 * give unrelated first words. */
static void test_nearby_seeds_give_unrelated_sequences(void) {
    cads_net_rand_t r1, r2;
    cads_net_rand_seed(&r1, 1000u);
    cads_net_rand_seed(&r2, 1001u);
    uint32_t a = cads_net_rand_next(&r1, NULL);
    uint32_t b = cads_net_rand_next(&r2, NULL);
    TEST_ASSERT_NOT_EQUAL(a, b);
    /* roughly half the bits differ, not just the low ones */
    uint32_t diff = a ^ b;
    unsigned bits = 0u;
    while(diff) {
        bits += diff & 1u;
        diff >>= 1;
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(8u, bits);
}

static void test_zero_seed_never_sticks_at_zero(void) {
    cads_net_rand_t r;
    cads_net_rand_seed(&r, 0u);
    TEST_ASSERT_NOT_EQUAL(0u, r.state);
    for(int i = 0; i < 100; i++) TEST_ASSERT_NOT_EQUAL(0u, cads_net_rand_next(&r, NULL));
}

static void test_isn_depends_on_tuple_and_secret_and_advances_with_time(void) {
    uint32_t a = cads_net_tcp_isn(0x1234u, 0xC0A82163u, 4242u, 0xC0A82101u, 50000u, 1000u);
    TEST_ASSERT_NOT_EQUAL(a, cads_net_tcp_isn(0x1234u, 0xC0A82163u, 4242u, 0xC0A82101u, 50001u, 1000u));
    TEST_ASSERT_NOT_EQUAL(a, cads_net_tcp_isn(0x1235u, 0xC0A82163u, 4242u, 0xC0A82101u, 50000u, 1000u));
    /* Same connection a millisecond later: exactly 250 ticks further on. */
    TEST_ASSERT_EQUAL_UINT32(a + 250u, cads_net_tcp_isn(0x1234u, 0xC0A82163u, 4242u, 0xC0A82101u, 50000u, 1001u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hardware_words_pass_through_uncounted);
    RUN_TEST(test_hardware_error_falls_back_and_counts);
    RUN_TEST(test_nearby_seeds_give_unrelated_sequences);
    RUN_TEST(test_zero_seed_never_sticks_at_zero);
    RUN_TEST(test_isn_depends_on_tuple_and_secret_and_advances_with_time);
    return UNITY_END();
}

/* DMAMFBOCR decoding: counters in the right fields, overflow bits counted as
 * a wrap (never an undercount), neighbouring bits not leaking into the
 * values. The register layout is RM0090 33.8.1. */

#include "unity.h"

#include "hal_eth_missed.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_zero_is_zero(void) {
    cads_eth_missed_t m = cads_eth_missed_decode(0u);
    TEST_ASSERT_EQUAL_UINT32(0u, m.no_descriptor);
    TEST_ASSERT_EQUAL_UINT32(0u, m.fifo_overflow);
}

static void test_fields(void) {
    cads_eth_missed_t m = cads_eth_missed_decode((5u << 17) | 7u);
    TEST_ASSERT_EQUAL_UINT32(7u, m.no_descriptor);
    TEST_ASSERT_EQUAL_UINT32(5u, m.fifo_overflow);
    m = cads_eth_missed_decode((0x7FFu << 17) | 0xFFFFu);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFu, m.no_descriptor);
    TEST_ASSERT_EQUAL_UINT32(0x7FFu, m.fifo_overflow);
}

static void test_overflow_bits_add_a_wrap(void) {
    cads_eth_missed_t m = cads_eth_missed_decode((1u << 16) | 3u);
    TEST_ASSERT_EQUAL_UINT32(0x10003u, m.no_descriptor);
    TEST_ASSERT_EQUAL_UINT32(0u, m.fifo_overflow); /* OMFC is not part of MFA */
    m = cads_eth_missed_decode((1u << 28) | (2u << 17));
    TEST_ASSERT_EQUAL_UINT32(0x802u, m.fifo_overflow);
    TEST_ASSERT_EQUAL_UINT32(0u, m.no_descriptor);
}

static void test_reserved_bits_ignored(void) {
    cads_eth_missed_t m = cads_eth_missed_decode(0xE0000000u | 1u);
    TEST_ASSERT_EQUAL_UINT32(1u, m.no_descriptor);
    TEST_ASSERT_EQUAL_UINT32(0u, m.fifo_overflow);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_is_zero);
    RUN_TEST(test_fields);
    RUN_TEST(test_overflow_bits_add_a_wrap);
    RUN_TEST(test_reserved_bits_ignored);
    return UNITY_END();
}

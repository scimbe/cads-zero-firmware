/* cads_fmt: the integer printing that apps/bringup hand-rolled twice. */

#include <stddef.h>

#include "unity.h"

#include "cads/toolbox/fmt.h"

static char out[CADS_FMT_BUFFER];

void setUp(void) {
    for(size_t i = 0u; i < sizeof(out); i++) {
        out[i] = '?';
    }
}

void tearDown(void) {
}

static void test_unsigned_decimal(void) {
    TEST_ASSERT_EQUAL_size_t(1u, cads_fmt_uint(out, sizeof(out), 0u));
    TEST_ASSERT_EQUAL_STRING("0", out);

    TEST_ASSERT_EQUAL_size_t(1u, cads_fmt_uint(out, sizeof(out), 7u));
    TEST_ASSERT_EQUAL_STRING("7", out);

    TEST_ASSERT_EQUAL_size_t(2u, cads_fmt_uint(out, sizeof(out), 10u));
    TEST_ASSERT_EQUAL_STRING("10", out);

    /* The widest uint32_t. Ten digits is the sizing assumption every scratch
     * buffer in the module is built on. */
    TEST_ASSERT_EQUAL_size_t(10u, cads_fmt_uint(out, sizeof(out), 4294967295u));
    TEST_ASSERT_EQUAL_STRING("4294967295", out);
}

static void test_signed_decimal(void) {
    TEST_ASSERT_EQUAL_size_t(1u, cads_fmt_int(out, sizeof(out), 0));
    TEST_ASSERT_EQUAL_STRING("0", out);

    TEST_ASSERT_EQUAL_size_t(2u, cads_fmt_int(out, sizeof(out), -1));
    TEST_ASSERT_EQUAL_STRING("-1", out);

    TEST_ASSERT_EQUAL_size_t(10u, cads_fmt_int(out, sizeof(out), 2147483647));
    TEST_ASSERT_EQUAL_STRING("2147483647", out);

    /* INT32_MIN has no positive counterpart, so a formatter that negates
     * before printing gets this one wrong - or invokes undefined behaviour. */
    TEST_ASSERT_EQUAL_size_t(11u, cads_fmt_int(out, sizeof(out), (int32_t)-2147483647 - 1));
    TEST_ASSERT_EQUAL_STRING("-2147483648", out);
}

static void test_hex_natural_width(void) {
    TEST_ASSERT_EQUAL_size_t(1u, cads_fmt_hex(out, sizeof(out), 0u, 0u, false));
    TEST_ASSERT_EQUAL_STRING("0", out);

    TEST_ASSERT_EQUAL_size_t(2u, cads_fmt_hex(out, sizeof(out), 0xFFu, 0u, false));
    TEST_ASSERT_EQUAL_STRING("ff", out);

    TEST_ASSERT_EQUAL_size_t(8u, cads_fmt_hex(out, sizeof(out), 0xDEADBEEFu, 0u, false));
    TEST_ASSERT_EQUAL_STRING("deadbeef", out);

    TEST_ASSERT_EQUAL_size_t(8u, cads_fmt_hex(out, sizeof(out), 0xDEADBEEFu, 0u, true));
    TEST_ASSERT_EQUAL_STRING("DEADBEEF", out);

    TEST_ASSERT_EQUAL_size_t(8u, cads_fmt_hex(out, sizeof(out), 4294967295u, 0u, true));
    TEST_ASSERT_EQUAL_STRING("FFFFFFFF", out);
}

static void test_hex_fixed_width(void) {
    /* Four upper-case digits is what the hardware explorer prints for a GPIO
     * IDR, and a fixed width is the only reason those dumps line up. */
    TEST_ASSERT_EQUAL_size_t(4u, cads_fmt_hex(out, sizeof(out), 0x00FFu, 4u, true));
    TEST_ASSERT_EQUAL_STRING("00FF", out);

    TEST_ASSERT_EQUAL_size_t(4u, cads_fmt_hex(out, sizeof(out), 0u, 4u, true));
    TEST_ASSERT_EQUAL_STRING("0000", out);

    /* More digits than a uint32_t has are clamped rather than padded into
     * nonsense. */
    TEST_ASSERT_EQUAL_size_t(8u, cads_fmt_hex(out, sizeof(out), 0x1234u, 12u, false));
    TEST_ASSERT_EQUAL_STRING("00001234", out);

    /* A narrow field keeps the low digits, which is what a register mask
     * reader wants. */
    TEST_ASSERT_EQUAL_size_t(2u, cads_fmt_hex(out, sizeof(out), 0xABCDu, 2u, true));
    TEST_ASSERT_EQUAL_STRING("CD", out);
}

static void test_truncation_terminates_and_reports_the_full_length(void) {
    char small[4];

    TEST_ASSERT_EQUAL_size_t(5u, cads_fmt_uint(small, sizeof(small), 12345u));
    TEST_ASSERT_EQUAL_STRING("123", small);

    TEST_ASSERT_EQUAL_size_t(11u, cads_fmt_int(small, sizeof(small), (int32_t)-2147483647 - 1));
    TEST_ASSERT_EQUAL_STRING("-21", small);

    /* Exactly fitting is not truncation. */
    TEST_ASSERT_EQUAL_size_t(3u, cads_fmt_uint(small, sizeof(small), 123u));
    TEST_ASSERT_EQUAL_STRING("123", small);
}

static void test_zero_size_and_null_write_nothing(void) {
    char guard[2] = {'!', '!'};

    TEST_ASSERT_EQUAL_size_t(3u, cads_fmt_uint(guard, 0u, 123u));
    TEST_ASSERT_EQUAL_CHAR('!', guard[0]);

    /* Measuring without writing is how a caller sizes a buffer. */
    TEST_ASSERT_EQUAL_size_t(10u, cads_fmt_uint(NULL, 0u, 4294967295u));
    TEST_ASSERT_EQUAL_size_t(8u, cads_fmt_hex(NULL, 32u, 0xDEADBEEFu, 0u, false));

    /* A one-byte buffer holds only the terminator. */
    TEST_ASSERT_EQUAL_size_t(3u, cads_fmt_uint(guard, 1u, 123u));
    TEST_ASSERT_EQUAL_CHAR('\0', guard[0]);
    TEST_ASSERT_EQUAL_CHAR('!', guard[1]);
}

static void test_padding(void) {
    TEST_ASSERT_EQUAL_size_t(5u, cads_fmt_uint_pad(out, sizeof(out), 42u, 5u, ' '));
    TEST_ASSERT_EQUAL_STRING("   42", out);

    TEST_ASSERT_EQUAL_size_t(5u, cads_fmt_uint_pad(out, sizeof(out), 42u, 5u, '0'));
    TEST_ASSERT_EQUAL_STRING("00042", out);

    TEST_ASSERT_EQUAL_size_t(3u, cads_fmt_uint_pad(out, sizeof(out), 0u, 3u, '0'));
    TEST_ASSERT_EQUAL_STRING("000", out);

    /* A number wider than its column widens the column: losing a digit to
     * keep an alignment would be the worse failure. */
    TEST_ASSERT_EQUAL_size_t(6u, cads_fmt_uint_pad(out, sizeof(out), 123456u, 3u, '0'));
    TEST_ASSERT_EQUAL_STRING("123456", out);

    /* Width beyond CADS_FMT_MAX is clamped, so a bad argument cannot run the
     * scratch buffer off its end. */
    TEST_ASSERT_EQUAL_size_t(CADS_FMT_MAX, cads_fmt_uint_pad(out, sizeof(out), 1u, 200u, '.'));
}

static void test_signed_padding_places_the_sign_correctly(void) {
    /* Zero fill goes after the sign, space fill before it. A minus stranded
     * among the spaces reads as a dash. */
    TEST_ASSERT_EQUAL_size_t(5u, cads_fmt_int_pad(out, sizeof(out), -7, 5u, '0'));
    TEST_ASSERT_EQUAL_STRING("-0007", out);

    TEST_ASSERT_EQUAL_size_t(5u, cads_fmt_int_pad(out, sizeof(out), -7, 5u, ' '));
    TEST_ASSERT_EQUAL_STRING("   -7", out);

    TEST_ASSERT_EQUAL_size_t(5u, cads_fmt_int_pad(out, sizeof(out), 7, 5u, '0'));
    TEST_ASSERT_EQUAL_STRING("00007", out);

    TEST_ASSERT_EQUAL_size_t(11u, cads_fmt_int_pad(out, sizeof(out), (int32_t)-2147483647 - 1, 4u, '0'));
    TEST_ASSERT_EQUAL_STRING("-2147483648", out);
}

static void test_ipv4(void) {
    TEST_ASSERT_EQUAL_size_t(15u, cads_fmt_ipv4(out, sizeof(out), 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_STRING("255.255.255.255", out);

    TEST_ASSERT_EQUAL_size_t(7u, cads_fmt_ipv4(out, sizeof(out), 0u));
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", out);

    /* 192.168.1.5, most significant octet first. */
    TEST_ASSERT_EQUAL_size_t(11u, cads_fmt_ipv4(out, sizeof(out), 0xC0A80105u));
    TEST_ASSERT_EQUAL_STRING("192.168.1.5", out);

    /* Truncation still terminates and still reports the untruncated length,
     * the same contract every other function in this file keeps. */
    char small[8];
    TEST_ASSERT_EQUAL_size_t(15u, cads_fmt_ipv4(small, sizeof(small), 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_STRING("255.255", small);
}

static void test_mac(void) {
    const uint8_t addr[6] = {0x02, 0xCA, 0xD5, 0x5E, 0x00, 0x01};
    TEST_ASSERT_EQUAL_size_t(17u, cads_fmt_mac(out, sizeof(out), addr));
    TEST_ASSERT_EQUAL_STRING("02:CA:D5:5E:00:01", out);

    const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_size_t(17u, cads_fmt_mac(out, sizeof(out), zero));
    TEST_ASSERT_EQUAL_STRING("00:00:00:00:00:00", out);

    char small[10];
    TEST_ASSERT_EQUAL_size_t(17u, cads_fmt_mac(small, sizeof(small), addr));
    TEST_ASSERT_EQUAL_STRING("02:CA:D5:", small);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_unsigned_decimal);
    RUN_TEST(test_signed_decimal);
    RUN_TEST(test_hex_natural_width);
    RUN_TEST(test_hex_fixed_width);
    RUN_TEST(test_truncation_terminates_and_reports_the_full_length);
    RUN_TEST(test_zero_size_and_null_write_nothing);
    RUN_TEST(test_padding);
    RUN_TEST(test_signed_padding_places_the_sign_correctly);
    RUN_TEST(test_ipv4);
    RUN_TEST(test_mac);
    return UNITY_END();
}

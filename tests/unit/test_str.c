/* cads_str: bounded copies and the strict parsers apps/bringup/explorer.c
 * hand-rolled without them. */

#include <stddef.h>
#include <stdint.h>

#include "unity.h"

#include "cads/toolbox/str.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_length_stops_at_the_bound(void) {
    TEST_ASSERT_EQUAL_size_t(0u, cads_str_len("", 8u));
    TEST_ASSERT_EQUAL_size_t(4u, cads_str_len("CaDS", 8u));
    TEST_ASSERT_EQUAL_size_t(4u, cads_str_len("CaDS", 4u));

    /* An unterminated buffer must not be walked past its end. */
    static const char raw[3] = {'a', 'b', 'c'};
    TEST_ASSERT_EQUAL_size_t(3u, cads_str_len(raw, 3u));
    TEST_ASSERT_EQUAL_size_t(0u, cads_str_len(NULL, 8u));
}

static void test_copy_always_terminates(void) {
    char dst[8];

    TEST_ASSERT_EQUAL_size_t(4u, cads_str_copy(dst, sizeof(dst), "CaDS"));
    TEST_ASSERT_EQUAL_STRING("CaDS", dst);

    /* Exactly filling the buffer, terminator included, is not truncation. */
    TEST_ASSERT_EQUAL_size_t(7u, cads_str_copy(dst, sizeof(dst), "1234567"));
    TEST_ASSERT_EQUAL_STRING("1234567", dst);

    /* One over: the result is the source length, so result >= size detects it,
     * and the destination is still a valid string. */
    TEST_ASSERT_EQUAL_size_t(8u, cads_str_copy(dst, sizeof(dst), "12345678"));
    TEST_ASSERT_EQUAL_STRING("1234567", dst);

    char guard = '!';
    TEST_ASSERT_EQUAL_size_t(4u, cads_str_copy(&guard, 0u, "CaDS"));
    TEST_ASSERT_EQUAL_CHAR('!', guard);
    TEST_ASSERT_EQUAL_size_t(4u, cads_str_copy(NULL, 0u, "CaDS"));
}

static void test_append(void) {
    char dst[8] = "CaDS";

    TEST_ASSERT_EQUAL_size_t(6u, cads_str_append(dst, sizeof(dst), " 0"));
    TEST_ASSERT_EQUAL_STRING("CaDS 0", dst);

    TEST_ASSERT_EQUAL_size_t(10u, cads_str_append(dst, sizeof(dst), "1234"));
    TEST_ASSERT_EQUAL_STRING("CaDS 01", dst);

    char empty[4] = "";
    TEST_ASSERT_EQUAL_size_t(3u, cads_str_append(empty, sizeof(empty), "abc"));
    TEST_ASSERT_EQUAL_STRING("abc", empty);
}

static void test_comparison(void) {
    TEST_ASSERT_EQUAL_INT(0, cads_str_compare("abc", "abc"));
    TEST_ASSERT_TRUE(cads_str_compare("abc", "abd") < 0);
    TEST_ASSERT_TRUE(cads_str_compare("abd", "abc") > 0);
    TEST_ASSERT_TRUE(cads_str_compare("ab", "abc") < 0);

    /* Bytes compare unsigned, so a high-bit character sorts after ASCII
     * rather than before it - the trap in a naive signed char comparison. */
    TEST_ASSERT_TRUE(cads_str_compare("\x80", "a") > 0);

    TEST_ASSERT_EQUAL_INT(0, cads_str_compare_n("abcdef", "abcxyz", 3u));
    TEST_ASSERT_TRUE(cads_str_compare_n("abcdef", "abcxyz", 4u) < 0);
    TEST_ASSERT_EQUAL_INT(0, cads_str_compare_n("ab", "ab", 8u));

    TEST_ASSERT_TRUE(cads_str_equal("CaDS", "CaDS"));
    TEST_ASSERT_FALSE(cads_str_equal("CaDS", "cads"));
    TEST_ASSERT_EQUAL_INT(0, cads_str_compare(NULL, NULL));
    TEST_ASSERT_TRUE(cads_str_compare(NULL, "a") < 0);
}

static void test_prefix_and_spaces(void) {
    TEST_ASSERT_TRUE(cads_str_starts_with("b 90", "b "));
    TEST_ASSERT_TRUE(cads_str_starts_with("b 90", ""));
    TEST_ASSERT_FALSE(cads_str_starts_with("b", "b 90"));
    TEST_ASSERT_FALSE(cads_str_starts_with("o 00FF", "b "));

    TEST_ASSERT_EQUAL_STRING("90", cads_str_skip_spaces("  \t 90"));
    TEST_ASSERT_EQUAL_STRING("", cads_str_skip_spaces("   "));
    TEST_ASSERT_EQUAL_STRING("90", cads_str_skip_spaces("90"));
}

static void test_parse_unsigned(void) {
    uint32_t value = 0xDEADu;
    const char* end = NULL;

    TEST_ASSERT_TRUE(cads_str_to_uint(" 90 rest", &value, &end));
    TEST_ASSERT_EQUAL_UINT32(90u, value);
    TEST_ASSERT_EQUAL_STRING(" rest", end);

    TEST_ASSERT_TRUE(cads_str_to_uint("4294967295", &value, NULL));
    TEST_ASSERT_EQUAL_UINT32(4294967295u, value);

    TEST_ASSERT_TRUE(cads_str_to_uint("0", &value, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, value);

    /* The failures that matter. An empty or non-numeric argument must not
     * parse as a perfectly plausible zero - that is how "b 90" arriving as
     * "b" once set the backlight to 0% and reported success. */
    value = 0xDEADu;
    TEST_ASSERT_FALSE(cads_str_to_uint("", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_uint("   ", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_uint("abc", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_uint("-1", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_uint("4294967296", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_uint("99999999999", &value, NULL));
    TEST_ASSERT_EQUAL_UINT32(0xDEADu, value);
}

static void test_parse_signed(void) {
    int32_t value = 99;
    const char* end = NULL;

    TEST_ASSERT_TRUE(cads_str_to_int("-5,", &value, &end));
    TEST_ASSERT_EQUAL_INT32(-5, value);
    TEST_ASSERT_EQUAL_STRING(",", end);

    TEST_ASSERT_TRUE(cads_str_to_int("+5", &value, NULL));
    TEST_ASSERT_EQUAL_INT32(5, value);

    TEST_ASSERT_TRUE(cads_str_to_int("2147483647", &value, NULL));
    TEST_ASSERT_EQUAL_INT32(2147483647, value);

    /* The asymmetric end of the range: one more negative value exists than
     * positive, and it has to round trip. */
    TEST_ASSERT_TRUE(cads_str_to_int("-2147483648", &value, NULL));
    TEST_ASSERT_EQUAL_INT32((int32_t)-2147483647 - 1, value);

    TEST_ASSERT_TRUE(cads_str_to_int("-0", &value, NULL));
    TEST_ASSERT_EQUAL_INT32(0, value);

    value = 99;
    TEST_ASSERT_FALSE(cads_str_to_int("2147483648", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_int("-2147483649", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_int("-", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_int("", &value, NULL));
    TEST_ASSERT_EQUAL_INT32(99, value);
}

static void test_parse_hex(void) {
    uint32_t value = 0u;
    const char* end = NULL;

    TEST_ASSERT_TRUE(cads_str_to_hex("00FF", &value, &end));
    TEST_ASSERT_EQUAL_UINT32(0x00FFu, value);
    TEST_ASSERT_EQUAL_STRING("", end);

    TEST_ASSERT_TRUE(cads_str_to_hex("aAbB ", &value, &end));
    TEST_ASSERT_EQUAL_UINT32(0xAABBu, value);
    TEST_ASSERT_EQUAL_STRING(" ", end);

    TEST_ASSERT_TRUE(cads_str_to_hex(" 0xdeadbeef", &value, NULL));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, value);

    TEST_ASSERT_TRUE(cads_str_to_hex("FFFFFFFF", &value, NULL));
    TEST_ASSERT_EQUAL_UINT32(4294967295u, value);

    value = 0x1234u;
    TEST_ASSERT_FALSE(cads_str_to_hex("100000000", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_hex("xyz", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_hex("0x", &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_hex("", &value, NULL));
    TEST_ASSERT_EQUAL_UINT32(0x1234u, value);
}

static void test_null_arguments_are_refused(void) {
    uint32_t value = 0u;
    int32_t signed_value = 0;

    TEST_ASSERT_FALSE(cads_str_to_uint(NULL, &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_uint("1", NULL, NULL));
    TEST_ASSERT_FALSE(cads_str_to_hex(NULL, &value, NULL));
    TEST_ASSERT_FALSE(cads_str_to_int(NULL, &signed_value, NULL));
    TEST_ASSERT_FALSE(cads_str_starts_with(NULL, "a"));
    TEST_ASSERT_NULL(cads_str_skip_spaces(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_length_stops_at_the_bound);
    RUN_TEST(test_copy_always_terminates);
    RUN_TEST(test_append);
    RUN_TEST(test_comparison);
    RUN_TEST(test_prefix_and_spaces);
    RUN_TEST(test_parse_unsigned);
    RUN_TEST(test_parse_signed);
    RUN_TEST(test_parse_hex);
    RUN_TEST(test_null_arguments_are_refused);
    return UNITY_END();
}

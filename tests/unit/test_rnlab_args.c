/* rnlab: the `lab` dispatcher's argument parsing - word splitting, dotted-quad
 * addresses for `lab net static`, and lesson numbers for `lab NN ...`. */

#include <stdint.h>

#include "unity.h"

#include "rnlab_args_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_split_words(void) {
    char line[] = "  01  trace\t5 ";
    char* argv[RNLAB_ARGV_MAX];
    int argc = rnlab_split_args(line, argv, (int)RNLAB_ARGV_MAX);
    TEST_ASSERT_EQUAL_INT(3, argc);
    TEST_ASSERT_EQUAL_STRING("01", argv[0]);
    TEST_ASSERT_EQUAL_STRING("trace", argv[1]);
    TEST_ASSERT_EQUAL_STRING("5", argv[2]);
}

static void test_split_empty(void) {
    char line[] = "   ";
    char* argv[RNLAB_ARGV_MAX];
    TEST_ASSERT_EQUAL_INT(0, rnlab_split_args(line, argv, (int)RNLAB_ARGV_MAX));
}

static void test_split_bounded(void) {
    char line[] = "a b c d";
    char* argv[2];
    TEST_ASSERT_EQUAL_INT(2, rnlab_split_args(line, argv, 2));
    TEST_ASSERT_EQUAL_STRING("a", argv[0]);
    TEST_ASSERT_EQUAL_STRING("b c d", argv[1]);
}

static void test_ipv4(void) {
    uint32_t ip = 0u;
    TEST_ASSERT_TRUE(rnlab_parse_ipv4("192.168.33.99", &ip));
    TEST_ASSERT_EQUAL_HEX32(0xC0A82163u, ip);
    TEST_ASSERT_TRUE(rnlab_parse_ipv4("255.255.255.0", &ip));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF00u, ip);

    TEST_ASSERT_FALSE(rnlab_parse_ipv4("192.168.33", &ip));
    TEST_ASSERT_FALSE(rnlab_parse_ipv4("192.168.33.256", &ip));
    TEST_ASSERT_FALSE(rnlab_parse_ipv4("192.168.33.1x", &ip));
    TEST_ASSERT_FALSE(rnlab_parse_ipv4("192..33.1", &ip));
    TEST_ASSERT_FALSE(rnlab_parse_ipv4(" 1.2.3.4", &ip));
    TEST_ASSERT_FALSE(rnlab_parse_ipv4("", &ip));
    TEST_ASSERT_FALSE(rnlab_parse_ipv4(NULL, &ip));
}

static void test_lesson_number(void) {
    uint32_t lesson = 0u;
    TEST_ASSERT_TRUE(rnlab_parse_lesson("01", &lesson));
    TEST_ASSERT_EQUAL_UINT32(1u, lesson);
    TEST_ASSERT_TRUE(rnlab_parse_lesson("11", &lesson));
    TEST_ASSERT_EQUAL_UINT32(11u, lesson);
    TEST_ASSERT_TRUE(rnlab_parse_lesson("7", &lesson));
    TEST_ASSERT_EQUAL_UINT32(7u, lesson);

    TEST_ASSERT_FALSE(rnlab_parse_lesson("00", &lesson));
    TEST_ASSERT_FALSE(rnlab_parse_lesson("12", &lesson));
    TEST_ASSERT_FALSE(rnlab_parse_lesson("001", &lesson));
    TEST_ASSERT_FALSE(rnlab_parse_lesson("info", &lesson));
    TEST_ASSERT_FALSE(rnlab_parse_lesson("1a", &lesson));
}

/* `lab key`: the same codes as scripts/board_key.py, plus S0..S7. */
static void test_key_names_match_board_key_py(void) {
    static const struct { const char* name; uint8_t code; } expected[] = {
        {"up", 0x80}, {"down", 0x81}, {"left", 0x82}, {"right", 0x83},
        {"ok", 0x84}, {"back", 0x85}, {"f1", 0x86}, {"f2", 0x87}, {"quit", 0x88},
    };
    for(size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        uint8_t code = 0u;
        TEST_ASSERT_TRUE_MESSAGE(rnlab_key_lookup(expected[i].name, &code), expected[i].name);
        TEST_ASSERT_EQUAL_HEX8(expected[i].code, code);
    }
}

static void test_key_buttons_and_case(void) {
    uint8_t code = 0u;
    TEST_ASSERT_TRUE(rnlab_key_lookup("s0", &code));
    TEST_ASSERT_EQUAL_HEX8(0x80, code);
    TEST_ASSERT_TRUE(rnlab_key_lookup("S4", &code));
    TEST_ASSERT_EQUAL_HEX8(0x84, code); /* S4 = ok, positional binding */
    TEST_ASSERT_TRUE(rnlab_key_lookup("S7", &code));
    TEST_ASSERT_EQUAL_HEX8(0x87, code);
    TEST_ASSERT_TRUE(rnlab_key_lookup("OK", &code));
    TEST_ASSERT_EQUAL_HEX8(0x84, code);

    TEST_ASSERT_FALSE(rnlab_key_lookup("s8", &code));
    TEST_ASSERT_FALSE(rnlab_key_lookup("s10", &code));
    TEST_ASSERT_FALSE(rnlab_key_lookup("okay", &code));
    TEST_ASSERT_FALSE(rnlab_key_lookup("", &code));
    TEST_ASSERT_FALSE(rnlab_key_lookup(NULL, &code));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_split_words);
    RUN_TEST(test_split_empty);
    RUN_TEST(test_split_bounded);
    RUN_TEST(test_ipv4);
    RUN_TEST(test_lesson_number);
    RUN_TEST(test_key_names_match_board_key_py);
    RUN_TEST(test_key_buttons_and_case);
    return UNITY_END();
}

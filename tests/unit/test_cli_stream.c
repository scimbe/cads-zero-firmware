/* cads_cli byte-stream robustness: the telnet command filter and the TCP
 * output queue (cads/cli/cli_stream.h). */

#include <string.h>

#include "unity.h"

#include "cads/cli/cli_stream.h"

void setUp(void) {
}

void tearDown(void) {
}

static size_t filter_all(const uint8_t* in, size_t n, char* out) {
    cads_cli_telnet_t telnet;
    cads_cli_telnet_init(&telnet);
    size_t kept = 0u;
    for(size_t i = 0; i < n; i++) {
        if(cads_cli_telnet_filter(&telnet, in[i])) out[kept++] = (char)in[i];
    }
    out[kept] = '\0';
    return kept;
}

static void test_plain_text_passes(void) {
    static const uint8_t in[] = "lab info\r\n";
    char out[32];
    filter_all(in, sizeof(in) - 1u, out);
    TEST_ASSERT_EQUAL_STRING("lab info\r\n", out);
}

/* What Windows telnet sends on connect, followed by the first command. */
static void test_option_negotiation_is_stripped(void) {
    static const uint8_t in[] = {
        0xFF, 0xFD, 0x03,             /* IAC DO SUPPRESS-GO-AHEAD */
        0xFF, 0xFB, 0x18,             /* IAC WILL TERMINAL-TYPE */
        0xFF, 0xFB, 0x1F,             /* IAC WILL NAWS */
        0xFF, 0xFA, 0x1F, 0x00, 0x50, 0x00, 0x18, 0xFF, 0xF0, /* SB NAWS 80x24 SE */
        'l', 'a', 'b', '\r', '\n'};
    char out[32];
    filter_all(in, sizeof(in), out);
    TEST_ASSERT_EQUAL_STRING("lab\r\n", out);
}

static void test_two_byte_commands_and_escaped_iac(void) {
    static const uint8_t in[] = {'a', 0xFF, 0xF1, 'b', 0xFF, 0xFF, 'c'}; /* IAC NOP, IAC IAC */
    char out[8];
    filter_all(in, sizeof(in), out);
    TEST_ASSERT_EQUAL_STRING("abc", out);
}

static void test_iac_inside_subnegotiation(void) {
    /* IAC IAC inside SB is data of the subnegotiation, not its end. */
    static const uint8_t in[] = {0xFF, 0xFA, 0x18, 0xFF, 0xFF, 0x01, 0xFF, 0xF0, 'x'};
    char out[8];
    filter_all(in, sizeof(in), out);
    TEST_ASSERT_EQUAL_STRING("x", out);
}

static void test_outq_wraps_in_order(void) {
    char storage[8];
    cads_cli_outq_t q;
    cads_cli_outq_init(&q, storage, sizeof(storage));

    TEST_ASSERT_EQUAL_size_t(6u, cads_cli_outq_push(&q, "abcdef", 6u));
    cads_cli_outq_consume(&q, 4u);                      /* "ef" left at index 4 */
    TEST_ASSERT_EQUAL_size_t(5u, cads_cli_outq_push(&q, "ghijk", 5u)); /* wraps */
    TEST_ASSERT_FALSE(q.truncated);

    char out[16] = {0};
    size_t total = 0u;
    const char* chunk;
    size_t n;
    while((n = cads_cli_outq_peek(&q, &chunk)) > 0u) {
        memcpy(out + total, chunk, n);
        total += n;
        cads_cli_outq_consume(&q, n);
    }
    TEST_ASSERT_EQUAL_STRING("efghijk", out);
}

static void test_outq_overflow_sets_truncated(void) {
    char storage[4];
    cads_cli_outq_t q;
    cads_cli_outq_init(&q, storage, sizeof(storage));
    TEST_ASSERT_EQUAL_size_t(4u, cads_cli_outq_push(&q, "123456", 6u));
    TEST_ASSERT_TRUE(q.truncated);
    TEST_ASSERT_EQUAL_size_t(0u, cads_cli_outq_push(&q, "7", 1u));

    const char* chunk;
    TEST_ASSERT_EQUAL_size_t(4u, cads_cli_outq_peek(&q, &chunk));
    TEST_ASSERT_EQUAL_MEMORY("1234", chunk, 4u);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_plain_text_passes);
    RUN_TEST(test_option_negotiation_is_stripped);
    RUN_TEST(test_two_byte_commands_and_escaped_iac);
    RUN_TEST(test_iac_inside_subnegotiation);
    RUN_TEST(test_outq_wraps_in_order);
    RUN_TEST(test_outq_overflow_sets_truncated);
    return UNITY_END();
}

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

/* --- deferred input: bytes arriving must not execute anything ------------- */

static unsigned s_runs;
static char s_last_args[32];
static cads_cli_outq_t* s_reentry_queue;
static cads_cli_telnet_t* s_reentry_telnet;

static void noop_write(void* context, const char* text, size_t length) {
    (void)context;
    (void)text;
    (void)length;
}

/* Stands in for a lesson handler that pumps the network: while it runs,
 * "the network" delivers another command into the same queue. */
static void probe_handler(cads_cli_session_t* session, const char* args) {
    (void)session;
    s_runs++;
    strncpy(s_last_args, args, sizeof(s_last_args) - 1u);
    if(s_reentry_queue && strcmp(args, "first") == 0) {
        static const uint8_t more[] = "probe second\r\n";
        TEST_ASSERT_TRUE(cads_cli_input_push(s_reentry_queue, s_reentry_telnet, more, sizeof(more) - 1u));
    }
}

static const cads_cli_command_t s_probe = {"probe", probe_handler, "test"};

static void test_push_queues_without_executing(void) {
    TEST_ASSERT_TRUE(cads_cli_register(&s_probe));
    char storage[64];
    cads_cli_outq_t in;
    cads_cli_telnet_t telnet;
    cads_cli_session_t session;
    cads_cli_outq_init(&in, storage, sizeof(storage));
    cads_cli_telnet_init(&telnet);
    cads_cli_session_init(&session, noop_write, NULL);
    s_runs = 0u;
    s_reentry_queue = NULL;

    static const uint8_t line[] = {0xFF, 0xFB, 0x18, 'p', 'r', 'o', 'b', 'e', ' ', 'x', '\r', '\n'};
    TEST_ASSERT_TRUE(cads_cli_input_push(&in, &telnet, line, sizeof(line)));
    TEST_ASSERT_EQUAL_UINT(0u, s_runs); /* the "tcp_recv" side ran nothing */

    TEST_ASSERT_EQUAL_size_t(9u, cads_cli_input_drain(&in, &session)); /* IAC WILL TTYPE filtered */
    TEST_ASSERT_EQUAL_UINT(1u, s_runs);
    TEST_ASSERT_EQUAL_STRING("x", s_last_args);
}

static void test_push_refuses_without_room_and_keeps_state(void) {
    char storage[8];
    cads_cli_outq_t in;
    cads_cli_telnet_t telnet;
    cads_cli_outq_init(&in, storage, sizeof(storage));
    cads_cli_telnet_init(&telnet);

    static const uint8_t iac_start[] = {0xFF};
    static const uint8_t too_long[] = "0123456789";
    TEST_ASSERT_TRUE(cads_cli_input_push(&in, &telnet, iac_start, 1u));
    TEST_ASSERT_FALSE(cads_cli_input_push(&in, &telnet, too_long, sizeof(too_long) - 1u));
    TEST_ASSERT_EQUAL_size_t(0u, in.count);
    /* The refused push did not advance the telnet state: the next byte is
     * still read as the command after IAC. */
    static const uint8_t nop_then_a[] = {0xF1, 'a'};
    TEST_ASSERT_TRUE(cads_cli_input_push(&in, &telnet, nop_then_a, 2u));
    TEST_ASSERT_EQUAL_size_t(1u, in.count);
}

static void test_bytes_arriving_during_a_command_run_after_it(void) {
    char storage[64];
    cads_cli_outq_t in;
    cads_cli_telnet_t telnet;
    cads_cli_session_t session;
    cads_cli_outq_init(&in, storage, sizeof(storage));
    cads_cli_telnet_init(&telnet);
    cads_cli_session_init(&session, noop_write, NULL);
    s_runs = 0u;
    s_reentry_queue = &in;
    s_reentry_telnet = &telnet;

    static const uint8_t first[] = "probe first\r\n";
    TEST_ASSERT_TRUE(cads_cli_input_push(&in, &telnet, first, sizeof(first) - 1u));
    cads_cli_input_drain(&in, &session);

    /* Sequential, not nested: the second ran after the first returned. */
    TEST_ASSERT_EQUAL_UINT(2u, s_runs);
    TEST_ASSERT_EQUAL_STRING("second", s_last_args);
    TEST_ASSERT_EQUAL_size_t(0u, in.count);
    s_reentry_queue = NULL;
}

/* --- progress flush (cads_cli_outq_flush_due) --------------------------------
 *
 * A stand-in for the TCP transport: every write is queued, then sent if the
 * rule says so; a "segment" is one flush that moved bytes. The line end
 * flushes unconditionally (cads_cli_session_feed() -> cads_cli_flush()). */
typedef struct {
    char storage[1024];
    cads_cli_outq_t q;
    char sent[2048];
    size_t sent_len;
    uint32_t last_flush_ms;
    uint32_t segments;
    uint32_t segment_ms[64];
    uint32_t oldest_wait_ms; /* longest a byte waited in the queue */
    uint32_t queued_since_ms;
} fake_tcp_t;

static void fake_init(fake_tcp_t* t, uint32_t now_ms) {
    memset(t, 0, sizeof(*t));
    cads_cli_outq_init(&t->q, t->storage, sizeof(t->storage));
    t->last_flush_ms = now_ms; /* the transport resets it when a command starts */
}

static void fake_flush(fake_tcp_t* t, uint32_t now_ms) {
    if(t->q.count == 0u) return;
    uint32_t waited = now_ms - t->queued_since_ms;
    if(waited > t->oldest_wait_ms) t->oldest_wait_ms = waited;
    const char* chunk;
    size_t n;
    while((n = cads_cli_outq_peek(&t->q, &chunk)) > 0u) {
        memcpy(&t->sent[t->sent_len], chunk, n);
        t->sent_len += n;
        cads_cli_outq_consume(&t->q, n);
    }
    if(t->segments < 64u) t->segment_ms[t->segments] = now_ms;
    t->segments++;
    t->last_flush_ms = now_ms;
}

static void fake_write(fake_tcp_t* t, const char* text, uint32_t now_ms, bool in_command) {
    if(t->q.count == 0u) t->queued_since_ms = now_ms;
    cads_cli_outq_push(&t->q, text, strlen(text));
    if(cads_cli_outq_flush_due(&t->q, in_command, now_ms, t->last_flush_ms)) fake_flush(t, now_ms);
}

/* `lab 04 ping ... 100` shape: a dot every 200 ms for 2 s, then the result
 * line. Dots must leave while the command runs (rnlab.py gives up after 1 s
 * of silence), but never more than one segment per 250 ms. */
static void test_slow_command_progress_leaves_in_time(void) {
    static fake_tcp_t t;
    fake_init(&t, 1000u);
    char expected[64] = "";
    for(uint32_t ms = 1000u; ms < 3000u; ms += 200u) {
        fake_write(&t, ".", ms, true);
        strcat(expected, ".");
    }
    fake_write(&t, "\r\nping: 10 gesendet\r\n", 3000u, true);
    strcat(expected, "\r\nping: 10 gesendet\r\n");
    uint32_t during = t.segments;
    fake_flush(&t, 3000u); /* line end */

    TEST_ASSERT_GREATER_THAN_UINT32(0u, during);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(2000u / CADS_CLI_PROGRESS_FLUSH_MS, during);
    for(uint32_t i = 1u; i < during; i++) {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(CADS_CLI_PROGRESS_FLUSH_MS, t.segment_ms[i] - t.segment_ms[i - 1u]);
    }
    TEST_ASSERT_LESS_THAN_UINT32(1000u, t.oldest_wait_ms); /* well inside rnlab.py's 1 s */
    TEST_ASSERT_EQUAL_size_t(strlen(expected), t.sent_len);
    TEST_ASSERT_EQUAL_MEMORY(expected, t.sent, t.sent_len);
}

/* `lab info` shape: 20 lines (460 B, under half the queue) within 10 ms stay
 * one segment, sent at the line end - the property 6585812 introduced against the RX-ring ACK burst. */
static void test_fast_command_stays_one_segment(void) {
    static fake_tcp_t t;
    fake_init(&t, 5000u);
    for(uint32_t i = 0; i < 20u; i++) fake_write(&t, "ip:     192.168.33.99\r\n", 5000u + i / 2u, true);
    TEST_ASSERT_EQUAL_UINT32(0u, t.segments);
    fake_flush(&t, 5010u);
    TEST_ASSERT_EQUAL_UINT32(1u, t.segments);
    TEST_ASSERT_EQUAL_size_t(20u * 23u, t.sent_len);
}

static void test_flush_due_rules(void) {
    char storage[16];
    cads_cli_outq_t q;
    cads_cli_outq_init(&q, storage, sizeof(storage));
    TEST_ASSERT_FALSE(cads_cli_outq_flush_due(&q, false, 0u, 0u)); /* nothing queued */
    cads_cli_outq_push(&q, "ab", 2u);
    TEST_ASSERT_TRUE(cads_cli_outq_flush_due(&q, false, 0u, 0u));   /* banner, outside a command */
    TEST_ASSERT_FALSE(cads_cli_outq_flush_due(&q, true, 249u, 0u));
    TEST_ASSERT_TRUE(cads_cli_outq_flush_due(&q, true, 250u, 0u));
    TEST_ASSERT_TRUE(cads_cli_outq_flush_due(&q, true, 5u, 0xFFFFFF00u)); /* across the tick wrap */
    cads_cli_outq_push(&q, "cdefgh", 6u); /* 8 of 16: half full */
    TEST_ASSERT_TRUE(cads_cli_outq_flush_due(&q, true, 1u, 0u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_plain_text_passes);
    RUN_TEST(test_option_negotiation_is_stripped);
    RUN_TEST(test_two_byte_commands_and_escaped_iac);
    RUN_TEST(test_iac_inside_subnegotiation);
    RUN_TEST(test_outq_wraps_in_order);
    RUN_TEST(test_outq_overflow_sets_truncated);
    RUN_TEST(test_push_queues_without_executing);
    RUN_TEST(test_push_refuses_without_room_and_keeps_state);
    RUN_TEST(test_bytes_arriving_during_a_command_run_after_it);
    RUN_TEST(test_slow_command_progress_leaves_in_time);
    RUN_TEST(test_fast_command_stays_one_segment);
    RUN_TEST(test_flush_due_rules);
    return UNITY_END();
}

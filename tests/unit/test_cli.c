/* cads_cli: the shared command table's registration hook - an app adds a
 * command once and every transport dispatches it, without the built-ins or
 * each other being shadowed. Output is captured through the session's own
 * write callback, the same seam both real transports use. */

#include <stddef.h>
#include <string.h>

#include "unity.h"

#include "cads/cli/cli.h"

static char s_output[512];
static size_t s_output_length;
static const char* s_last_args;
static char s_last_args_copy[64];
static unsigned s_calls;

static void capture_write(void* context, const char* text, size_t length) {
    (void)context;
    if(s_output_length + length >= sizeof(s_output)) length = sizeof(s_output) - 1u - s_output_length;
    memcpy(s_output + s_output_length, text, length);
    s_output_length += length;
    s_output[s_output_length] = '\0';
}

static void record_handler(cads_cli_session_t* session, const char* args) {
    s_calls++;
    s_last_args = args;
    strncpy(s_last_args_copy, args, sizeof(s_last_args_copy) - 1u);
    cads_cli_write(session, "ran\r\n");
}

static const cads_cli_command_t s_lab = {"lab", record_handler, "lab test command"};

static cads_cli_session_t s_session;

void setUp(void) {
    s_output_length = 0u;
    s_output[0] = '\0';
    s_last_args = NULL;
    s_last_args_copy[0] = '\0';
    s_calls = 0u;
    cads_cli_session_init(&s_session, capture_write, NULL);
}

void tearDown(void) {
}

static void feed(const char* text) {
    while(*text) cads_cli_session_feed(&s_session, (uint8_t)*text++);
}

/* Runs first: nothing registered yet, so `lab` is an unknown command. */
static void test_unregistered_command_is_unknown(void) {
    feed("lab info\r");
    TEST_ASSERT_EQUAL_UINT(0u, s_calls);
    TEST_ASSERT_NOT_NULL(strstr(s_output, "? unknown command: lab"));
}

static void test_registered_command_dispatches_with_args(void) {
    TEST_ASSERT_TRUE(cads_cli_register(&s_lab));
    feed("  lab   01 trace 5\r");
    TEST_ASSERT_EQUAL_UINT(1u, s_calls);
    TEST_ASSERT_EQUAL_STRING("01 trace 5", s_last_args_copy);
    TEST_ASSERT_NOT_NULL(strstr(s_output, "ran\r\n> "));
}

static void test_registered_command_without_args_gets_empty_string(void) {
    feed("lab\r");
    TEST_ASSERT_EQUAL_UINT(1u, s_calls);
    TEST_ASSERT_NOT_NULL(s_last_args);
    TEST_ASSERT_EQUAL_STRING("", s_last_args_copy);
}

static void test_prefix_does_not_match(void) {
    feed("la\r");
    feed("labx\r");
    TEST_ASSERT_EQUAL_UINT(0u, s_calls);
}

static void test_registering_same_pointer_twice_is_idempotent(void) {
    TEST_ASSERT_TRUE(cads_cli_register(&s_lab));
    feed("help\r");
    /* Listed once, after the built-ins. */
    const char* first = strstr(s_output, "lab - lab test command");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NULL(strstr(first + 1, "lab - lab test command"));
    TEST_ASSERT_NOT_NULL(strstr(s_output, "echo - "));
}

static void test_name_clash_is_refused(void) {
    static const cads_cli_command_t other_lab = {"lab", record_handler, "second lab"};
    static const cads_cli_command_t fake_help = {"help", record_handler, "shadow"};
    TEST_ASSERT_FALSE(cads_cli_register(&other_lab));
    TEST_ASSERT_FALSE(cads_cli_register(&fake_help));

    /* The built-in still answers, not the would-be shadow. */
    feed("help\r");
    TEST_ASSERT_EQUAL_UINT(0u, s_calls);
}

static void test_invalid_entries_are_refused(void) {
    static const cads_cli_command_t no_name = {NULL, record_handler, ""};
    static const cads_cli_command_t empty_name = {"", record_handler, ""};
    static const cads_cli_command_t no_handler = {"x", NULL, ""};
    TEST_ASSERT_FALSE(cads_cli_register(NULL));
    TEST_ASSERT_FALSE(cads_cli_register(&no_name));
    TEST_ASSERT_FALSE(cads_cli_register(&empty_name));
    TEST_ASSERT_FALSE(cads_cli_register(&no_handler));
}

static void test_table_is_bounded(void) {
    static const cads_cli_command_t extra[] = {
        {"x1", record_handler, ""}, {"x2", record_handler, ""},
        {"x3", record_handler, ""}, {"x4", record_handler, ""},
    };
    /* `lab` already holds one of the CADS_CLI_REGISTERED_MAX slots. */
    size_t accepted = 0u;
    for(size_t i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) {
        if(cads_cli_register(&extra[i])) accepted++;
    }
    TEST_ASSERT_EQUAL_size_t(CADS_CLI_REGISTERED_MAX - 1u, accepted);
    feed("x3\r");
    TEST_ASSERT_EQUAL_UINT(1u, s_calls);
}

static void test_execute_dispatches_without_prompt(void) {
    cads_cli_execute(&s_session, "lab info");
    TEST_ASSERT_EQUAL_UINT(1u, s_calls);
    TEST_ASSERT_EQUAL_STRING("info", s_last_args_copy);
    TEST_ASSERT_EQUAL_STRING("ran\r\n", s_output);
}

static void test_builtins_still_work(void) {
    feed("echo hallo\r");
    TEST_ASSERT_NOT_NULL(strstr(s_output, "hallo\r\n"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_unregistered_command_is_unknown);
    RUN_TEST(test_registered_command_dispatches_with_args);
    RUN_TEST(test_registered_command_without_args_gets_empty_string);
    RUN_TEST(test_prefix_does_not_match);
    RUN_TEST(test_registering_same_pointer_twice_is_idempotent);
    RUN_TEST(test_name_clash_is_refused);
    RUN_TEST(test_invalid_entries_are_refused);
    RUN_TEST(test_table_is_bounded);
    RUN_TEST(test_execute_dispatches_without_prompt);
    RUN_TEST(test_builtins_still_work);
    return UNITY_END();
}

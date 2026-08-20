#include "cads/cli/cli.h"

#include <string.h>

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"

void cads_cli_write(cads_cli_session_t* session, const char* text) {
    if(!session || !session->write || !text) return;
    session->write(session->write_context, text, cads_str_len(text, CADS_CLI_LINE_MAX * 4u));
}

void cads_cli_write_uint(cads_cli_session_t* session, uint32_t value) {
    char digits[CADS_FMT_BUFFER];
    size_t length = cads_fmt_uint(digits, sizeof(digits), value);
    if(!session || !session->write) return;
    session->write(session->write_context, digits, length);
}

/*
 * The "shared command table" this module's header talks about: one static
 * array, compiled in once, dispatched into by every transport through
 * cads_cli_session_feed(). Not exposed in cads/cli/cli.h - nothing outside
 * this file adds or removes a command, so the type has no reason to be
 * public.
 */
typedef void (*cads_cli_handler_fn)(cads_cli_session_t* session, const char* args);

typedef struct {
    const char* name;
    cads_cli_handler_fn handler;
    const char* help;
} cads_cli_command_t;

/* --- built-in commands ----------------------------------------------------- */

static void cads_cli_cmd_help(cads_cli_session_t* session, const char* args);
static void cads_cli_cmd_version(cads_cli_session_t* session, const char* args);
static void cads_cli_cmd_uptime(cads_cli_session_t* session, const char* args);
static void cads_cli_cmd_net(cads_cli_session_t* session, const char* args);
static void cads_cli_cmd_echo(cads_cli_session_t* session, const char* args);

static const cads_cli_command_t cads_cli_commands[] = {
    {"help", cads_cli_cmd_help, "list commands"},
    {"version", cads_cli_cmd_version, "firmware build identity"},
    {"uptime", cads_cli_cmd_uptime, "milliseconds since boot"},
    {"net", cads_cli_cmd_net, "link state, speed, IP/gateway/DNS, lease"},
    {"echo", cads_cli_cmd_echo, "echo the rest of the line back"},
};
#define CADS_CLI_COMMAND_COUNT (sizeof(cads_cli_commands) / sizeof(cads_cli_commands[0]))

static void cads_cli_cmd_help(cads_cli_session_t* session, const char* args) {
    (void)args;
    for(size_t i = 0; i < CADS_CLI_COMMAND_COUNT; i++) {
        cads_cli_write(session, cads_cli_commands[i].name);
        cads_cli_write(session, " - ");
        cads_cli_write(session, cads_cli_commands[i].help);
        cads_cli_write(session, "\r\n");
    }
}

static void cads_cli_cmd_version(cads_cli_session_t* session, const char* args) {
    (void)args;
    cads_cli_write(session, "CaDS Zero - build " __DATE__ " " __TIME__ "\r\n");
}

static void cads_cli_cmd_uptime(cads_cli_session_t* session, const char* args) {
    (void)args;
    cads_cli_write_uint(session, cads_hal_ticks_ms());
    cads_cli_write(session, " ms\r\n");
}

static void cads_cli_write_ipv4(cads_cli_session_t* session, uint32_t ip) {
    if(ip == 0u) {
        cads_cli_write(session, "none");
        return;
    }
    char text[16];
    cads_fmt_ipv4(text, sizeof(text), ip);
    cads_cli_write(session, text);
}

static void cads_cli_cmd_net(cads_cli_session_t* session, const char* args) {
    (void)args;
    cads_net_status_t status;
    cads_net_status(&status);

    if(!status.link_up) {
        cads_cli_write(session, "link down\r\n");
        return;
    }

    cads_cli_write(session, "link up ");
    cads_cli_write_uint(session, status.speed_mbit);
    cads_cli_write(session, status.full_duplex ? "M full" : "M half");

    cads_cli_write(session, " ip=");
    cads_cli_write_ipv4(session, status.ip_addr);
    cads_cli_write(session, status.ip_addr == 0u ? " (no lease)" : (status.dhcp_bound ? " (dhcp)" : " (static)"));

    cads_cli_write(session, " gw=");
    cads_cli_write_ipv4(session, status.gw_addr);
    cads_cli_write(session, " dns=");
    cads_cli_write_ipv4(session, status.dns_addr);
    cads_cli_write(session, "\r\n");
}

static void cads_cli_cmd_echo(cads_cli_session_t* session, const char* args) {
    cads_cli_write(session, args);
    cads_cli_write(session, "\r\n");
}

/* --- session / dispatch ----------------------------------------------------- */

void cads_cli_session_init(cads_cli_session_t* session, cads_cli_write_fn write, void* write_context) {
    if(!session) return;
    session->write = write;
    session->write_context = write_context;
    session->length = 0u;
}

static void cads_cli_dispatch(cads_cli_session_t* session) {
    session->line[session->length] = '\0';

    const char* name = cads_str_skip_spaces(session->line);
    if(*name == '\0') return; /* blank line: just a fresh prompt, no error */

    const char* args = name;
    while(*args != '\0' && *args != ' ' && *args != '\t') args++;
    size_t name_length = (size_t)(args - name);
    args = cads_str_skip_spaces(args);

    for(size_t i = 0; i < CADS_CLI_COMMAND_COUNT; i++) {
        if(cads_str_compare_n(name, cads_cli_commands[i].name, name_length) == 0 &&
           cads_cli_commands[i].name[name_length] == '\0') {
            cads_cli_commands[i].handler(session, args);
            return;
        }
    }

    cads_cli_write(session, "? unknown command: ");
    cads_cli_write(session, name);
    cads_cli_write(session, " (try 'help')\r\n");
}

void cads_cli_session_feed(cads_cli_session_t* session, uint8_t byte) {
    if(!session) return;

    if(byte == '\r' || byte == '\n') {
        if(session->length > 0u) {
            cads_cli_dispatch(session);
            session->length = 0u;
        }
        cads_cli_write(session, "> ");
        return;
    }

    if(session->length >= CADS_CLI_LINE_MAX - 1u) {
        /* Overslength: drop the line rather than silently truncate it into
         * a different, shorter command - explorer.c's own str.h header
         * comment on why a parser should never turn "too long" into a
         * plausible-looking wrong answer applies here too. */
        session->length = 0u;
        cads_cli_write(session, "? line too long, discarded\r\n> ");
        return;
    }

    session->line[session->length++] = (char)byte;
}

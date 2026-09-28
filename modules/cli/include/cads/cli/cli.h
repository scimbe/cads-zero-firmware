/*
 * CaDS Zero - a small command-line interpreter with one shared command
 * table, driven by whichever byte stream calls cads_cli_session_feed().
 *
 * Transport agnostic on purpose: apps/bringup/explorer_cli_demo.c feeds it
 * from the serial console, cads/cli/cli_tcp.h feeds it from a TCP
 * connection. Neither transport knows what a command does, and this file
 * knows nothing about UARTs or sockets - the split modules/net's
 * cads_net_board.c/cads_net_sim.c already established between "the
 * portable logic" and "what carries the bytes".
 *
 * Not the hardware explorer (apps/bringup/explorer.c). That is a
 * single-letter, ~30-command bring-up diagnostic tool with its own
 * conventions, hardware-verified command by command over this whole
 * project. Folding it into a generic framework is a real rewrite of code
 * that already works and is not what this roadmap bullet asks for; cads_cli
 * is a second, smaller, general-purpose command set, reachable over the
 * network as well as serial, which the explorer specifically is not.
 */

#ifndef CADS_CLI_H
#define CADS_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest line this parses. A command line, not a file - kept small
 *  deliberately, matching apps/bringup/explorer.c's own `char line[32]`
 *  reasoning: anything needing more belongs in its own binary transfer,
 *  not a text command. */
#define CADS_CLI_LINE_MAX 96u

/** Write `length` bytes of output. Implemented once per transport
 *  (console write for serial, tcp_write() for TCP). Never blocks - a
 *  transport that cannot accept the bytes right now drops them rather than
 *  stall the command that produced them. */
typedef void (*cads_cli_write_fn)(void* context, const char* text, size_t length);

/** Push out what a buffering transport has collected (optional, may be
 *  NULL). Called once per completed line and by cads_cli_flush(). */
typedef void (*cads_cli_flush_fn)(void* context);

typedef struct {
    cads_cli_write_fn write;
    void* write_context;
    cads_cli_flush_fn flush; /**< NULL after cads_cli_session_init(); a transport sets it itself */
    char line[CADS_CLI_LINE_MAX];
    size_t length;
    bool last_was_cr; /**< CR just ended a line: a following LF (or telnet's NUL) belongs to it */
} cads_cli_session_t;

/** Reset a session and bind it to a transport. Call once per connection
 *  (once per TCP accept; once for the whole life of a serial demo run). */
void cads_cli_session_init(cads_cli_session_t* session, cads_cli_write_fn write, void* write_context);

/**
 * Feed one byte from the transport.
 *
 * Buffers until '\r' or '\n' - CR LF and telnet's CR NUL count as ONE line
 * end, so PuTTY/Windows telnet/nc all get exactly one prompt per line -
 * then looks the first whitespace-delimited
 * word up in the shared command table and calls its handler with the rest
 * of the line as `args` (never NULL; "" when there were no arguments).
 * An unrecognised command or an overslength line gets a one-line error
 * instead of being silently dropped. Always writes a fresh "> " prompt
 * after handling a line, so both transports get the same interactive feel
 * for free.
 */
void cads_cli_session_feed(cads_cli_session_t* session, uint8_t byte);

/** One command: the first word of a line, and the handler that gets the
 *  rest of it as `args` (never NULL; "" when there were no arguments). */
typedef void (*cads_cli_handler_fn)(cads_cli_session_t* session, const char* args);

typedef struct {
    const char* name;
    cads_cli_handler_fn handler;
    const char* help;
} cads_cli_command_t;

/** How many commands cads_cli_register() accepts on top of the built-in
 *  ones. Small on purpose: every slot is a pointer of static RAM, and the
 *  only caller today (apps/rnlab's `lab`) needs one. */
#define CADS_CLI_REGISTERED_MAX 4u

/**
 * Add `command` to the shared table, for every transport at once.
 *
 * `command` must stay valid for the life of the firmware (a `static const`
 * in the caller) - only the pointer is stored. Returns false, and changes
 * nothing, when `command` or its name/handler is NULL, when the name is
 * already taken (built-in or registered - a second `help` would silently
 * shadow the first), or when all CADS_CLI_REGISTERED_MAX slots are used.
 * Registering the very same pointer twice is a no-op that returns true, so
 * an idempotent init function can call this unconditionally.
 */
bool cads_cli_register(const cads_cli_command_t* command);

/** Dispatch one complete line (no trailing newline needed) exactly as
 *  cads_cli_session_feed() would on '\r', minus the "> " prompt - for a
 *  transport that already assembles lines itself (the hardware explorer's
 *  own serial loop). */
void cads_cli_execute(cads_cli_session_t* session, const char* line);

/**
 * Send what the transport has buffered now, instead of at the end of the
 * command. The TCP transport collects a command's writes and sends them
 * as one segment when the line is done - many tiny segments made the
 * client's ACK burst overrun the 8-frame receive ring (DMAMFBOCR.MFC).
 * A command that is about to make the network briefly deaf (`lab key`
 * switching views: the display blit borrows PA7) calls this first.
 */
void cads_cli_flush(cads_cli_session_t* session);

/** Write a NUL-terminated string of any length through the session's
 *  transport (handed over in pieces of at most CADS_CLI_LINE_MAX * 4 bytes). */
void cads_cli_write(cads_cli_session_t* session, const char* text);

/** Write an unsigned decimal through the session's transport. */
void cads_cli_write_uint(cads_cli_session_t* session, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* CADS_CLI_H */

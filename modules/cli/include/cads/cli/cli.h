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

typedef struct {
    cads_cli_write_fn write;
    void* write_context;
    char line[CADS_CLI_LINE_MAX];
    size_t length;
} cads_cli_session_t;

/** Reset a session and bind it to a transport. Call once per connection
 *  (once per TCP accept; once for the whole life of a serial demo run). */
void cads_cli_session_init(cads_cli_session_t* session, cads_cli_write_fn write, void* write_context);

/**
 * Feed one byte from the transport.
 *
 * Buffers until '\r' or '\n', then looks the first whitespace-delimited
 * word up in the shared command table and calls its handler with the rest
 * of the line as `args` (never NULL; "" when there were no arguments).
 * An unrecognised command or an overslength line gets a one-line error
 * instead of being silently dropped. Always writes a fresh "> " prompt
 * after handling a line, so both transports get the same interactive feel
 * for free.
 */
void cads_cli_session_feed(cads_cli_session_t* session, uint8_t byte);

/** Write a NUL-terminated string through the session's transport. */
void cads_cli_write(cads_cli_session_t* session, const char* text);

/** Write an unsigned decimal through the session's transport. */
void cads_cli_write_uint(cads_cli_session_t* session, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* CADS_CLI_H */

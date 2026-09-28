/*
 * CaDS Zero - the byte-stream helpers the TCP transport (cli_tcp.h) needs,
 * kept portable so they are host tested: a telnet command filter for input
 * and a bounded output queue for when the TCP send buffer is full.
 */

#ifndef CADS_CLI_STREAM_H
#define CADS_CLI_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- telnet command filter --------------------------------------------------
 *
 * A real telnet client (Windows telnet, PuTTY in telnet mode) opens with IAC
 * option negotiation (RFC 854/855: 0xFF followed by WILL/WONT/DO/DONT + an
 * option, or an SB ... IAC SE subnegotiation). Fed to the CLI as text those
 * bytes made the first command "? unknown command". This strips every
 * command sequence and never answers one: the server negotiates nothing, and
 * a client that gets no reply falls back to plain NVT behaviour. */
typedef struct {
    uint8_t state;
} cads_cli_telnet_t;

void cads_cli_telnet_init(cads_cli_telnet_t* telnet);

/** True if `byte` is user data to feed to the CLI, false if it was part of a
 *  telnet command sequence (or an escaped 0xFF, which is never CLI text). */
bool cads_cli_telnet_filter(cads_cli_telnet_t* telnet, uint8_t byte);

/* --- output queue ----------------------------------------------------------
 *
 * A byte ring over caller-owned storage. The TCP transport queues whatever
 * tcp_sndbuf() cannot take right now and sends it from its tcp_sent callback
 * as ACKs free room, instead of dropping it silently. When even the queue is
 * full the rest is dropped and `truncated` is set, so the transport can tell
 * the operator once the queue has drained. */
typedef struct {
    char* data;
    size_t size;
    size_t head;  /**< index of the oldest byte */
    size_t count; /**< bytes queued */
    bool truncated;
} cads_cli_outq_t;

void cads_cli_outq_init(cads_cli_outq_t* q, char* storage, size_t size);

/** Queue as much of `text` as fits; returns the bytes queued. Anything that
 *  did not fit sets `truncated`. */
size_t cads_cli_outq_push(cads_cli_outq_t* q, const char* text, size_t length);

/** The oldest contiguous run of queued bytes (may be shorter than `count`
 *  when the ring wraps); 0 when empty. */
size_t cads_cli_outq_peek(const cads_cli_outq_t* q, const char** chunk);

/** Drop the first `length` queued bytes (after they were sent). */
void cads_cli_outq_consume(cads_cli_outq_t* q, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* CADS_CLI_STREAM_H */

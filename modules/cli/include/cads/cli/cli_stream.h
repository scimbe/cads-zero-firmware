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

#include "cads/cli/cli.h"

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

/* --- deferred input ----------------------------------------------------------
 *
 * Why commands must not run where the bytes arrive: the TCP transport's
 * tcp_recv callback fires inside cads_net_poll() -> tcp_input(). A command
 * that itself pumps the network (`lab 04 ping`, cads_net_arp_probe(), any
 * lesson waiting for a reply) would re-enter tcp_input() and overwrite its
 * file-scope state (inseg, recv_data, tcp_input_pcb) - silent corruption.
 * So the callback only queues the (telnet-filtered) bytes, and the owner of
 * the main loop runs them later, outside every lwIP callback. */

/** Queue `length` received bytes, dropping telnet command sequences on the
 *  way. All or nothing: returns false and consumes nothing (not even telnet
 *  state) when the queue cannot be guaranteed to hold them - the caller then
 *  leaves the data with lwIP (return ERR_MEM from tcp_recv) to retry later,
 *  which is TCP flow control rather than loss. */
bool cads_cli_input_push(cads_cli_outq_t* in, cads_cli_telnet_t* telnet, const uint8_t* data, size_t length);

/** Feed every queued byte to `session` (which dispatches complete lines).
 *  Bytes pushed while a command runs (it may pump the network) are fed in
 *  the same call. Returns the number of bytes fed. */
size_t cads_cli_input_drain(cads_cli_outq_t* in, cads_cli_session_t* session);

#ifdef __cplusplus
}
#endif

#endif /* CADS_CLI_STREAM_H */

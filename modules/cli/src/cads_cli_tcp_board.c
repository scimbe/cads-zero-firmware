/*
 * CaDS Zero - cads_cli over TCP, itsboard implementation.
 *
 * Raw (NO_SYS=1) lwIP API - the same one modules/net/src/cads_net_board.c
 * already uses for the netif itself, so this needs no OS thread either.
 * Every callback here fires synchronously from inside cads_net_poll()'s
 * call chain (cads_netif.input() -> ethernet_input() -> ... -> tcp_input()),
 * the same way the netif's own receive path already works - see that
 * file's header comment on why NO_SYS=1 raw API needs no separate polling
 * for this. Which is exactly why no command runs in them: tcp_recv only
 * queues, and cads_cli_tcp_service() executes from the caller's loop (see
 * cli_tcp.h and cli_stream.h's "deferred input").
 */

#include "cads/cli/cli_tcp.h"

#include "cads/cli/cli.h"
#include "cads/cli/cli_stream.h"

#include "cads/net/net.h"
#include "cads_hal.h"

#include "lwip/tcp.h"

/* Output that tcp_sndbuf() cannot take yet waits here and goes out from the
 * tcp_sent callback as ACKs free room. Waiting in place is not an option:
 * the command writing it runs inside cads_net_poll() (tcp_recv), and the
 * ACKs that would free the buffer are only processed by that same poll. CCM:
 * CPU-only (tcp_write copies it), and the SRAM budget is tight. */
#define CADS_CLI_TCP_OUTQ_SIZE 1024u
__attribute__((section(".ccm"))) static char s_outq_storage[CADS_CLI_TCP_OUTQ_SIZE];

/* Received, telnet-filtered bytes waiting for cads_cli_tcp_service() - see
 * cli_stream.h's "deferred input" for why they are not executed in
 * tcp_recv. Four full CLI lines; more than that stays with lwIP (flow
 * control) until the queue drains. */
#define CADS_CLI_TCP_INQ_SIZE 384u
__attribute__((section(".ccm"))) static char s_inq_storage[CADS_CLI_TCP_INQ_SIZE];

/* How long one write may wait for ACKs to free room before it gives up and
 * truncates - only outside lwIP callbacks (s_servicing), where polling the
 * network is safe. */
#define CADS_CLI_TCP_WRITE_WAIT_MS 1000u

static const char s_truncated_notice[] = "\r\n? Ausgabe gekuerzt\r\n";

/* One session at a time - see cli_tcp.h's own header comment on why. */
typedef struct {
    struct tcp_pcb* pcb;
    cads_cli_session_t session;
    cads_cli_telnet_t telnet;
    cads_cli_outq_t outq;
    cads_cli_outq_t inq;
    bool in_use;
    bool remote_closed; /* FIN seen: run what is queued, then close */
} cads_cli_tcp_conn_t;

static cads_cli_tcp_conn_t s_conn;
static struct tcp_pcb* s_listen_pcb = NULL;
/* True while cads_cli_tcp_service() runs commands - the only context in
 * which a write may pump the network while it waits for room. */
static bool s_servicing = false;
/* When output last went to tcp_write() - the clock for the progress flush
 * (cads_cli_outq_flush_due()). Reset when a command starts, so a fast reply
 * still leaves as one segment at its line end. */
static uint32_t s_last_flush_ms = 0u;

/* Move as much queued output into lwIP as it will take right now. Never
 * blocks (see cli.h's write_fn contract); whatever is left waits for the
 * next tcp_sent. Once the queue is empty, a truncation is reported - one
 * line, so the operator knows output was lost instead of guessing. */
static void cads_cli_tcp_flush(struct tcp_pcb* pcb) {
    bool wrote = false;
    for(;;) {
        const char* chunk;
        size_t length = cads_cli_outq_peek(&s_conn.outq, &chunk);
        if(length == 0u) break;
        u16_t room = tcp_sndbuf(pcb);
        if(room == 0u || tcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN) break;
        if(length > room) length = room;
        if(tcp_write(pcb, chunk, (u16_t)length, TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        cads_cli_outq_consume(&s_conn.outq, length);
        wrote = true;
    }
    if(s_conn.outq.count == 0u && s_conn.outq.truncated &&
       tcp_sndbuf(pcb) >= sizeof(s_truncated_notice) - 1u &&
       tcp_write(pcb, s_truncated_notice, sizeof(s_truncated_notice) - 1u, TCP_WRITE_FLAG_COPY) == ERR_OK) {
        s_conn.outq.truncated = false;
        wrote = true;
    }
    if(wrote) {
        tcp_output(pcb);
        s_last_flush_ms = cads_hal_ticks_ms();
    }
}

static bool cads_cli_tcp_alive(const struct tcp_pcb* pcb) {
    return s_conn.in_use && s_conn.pcb == pcb;
}

static void cads_cli_tcp_write(void* context, const char* text, size_t length) {
    struct tcp_pcb* pcb = (struct tcp_pcb*)context;
    if(!pcb || length == 0u || !cads_cli_tcp_alive(pcb)) return;

    /* Outside lwIP callbacks a command may wait (bounded) for ACKs instead
     * of truncating: cads_net_poll() is safe here and is what processes
     * them. Inside a callback (the accept banner) it must not, and the
     * queue alone has to do. */
    uint32_t start = cads_hal_ticks_ms();
    while(s_servicing && s_conn.outq.size - s_conn.outq.count < length &&
          cads_hal_ticks_ms() - start < CADS_CLI_TCP_WRITE_WAIT_MS) {
        cads_cli_tcp_flush(pcb);
        cads_net_poll();
        if(!cads_cli_tcp_alive(pcb)) return; /* closed or reset while waiting */
        cads_hal_delay_ms(1u);
    }
    (void)cads_cli_outq_push(&s_conn.outq, text, length);
    /* While a command runs, collect: its reply leaves as one segment when
     * the line is done (cads_cli_session_feed() -> cads_cli_flush()), not
     * as dozens of tiny ones. With Nagle off (below), tiny segments each
     * drew an ACK, and that burst overran the 8-frame RX ring while the
     * console task was still busy (rx_ring_overruns, found by lek-03-04).
     * But a slow command's progress dots must not wait for its end either
     * (rnlab.py takes 1 s of silence for "done"): see
     * cads_cli_outq_flush_due() for the whole rule. The bytes only reach
     * the wire if the command keeps polling the network (NO_SYS) - which
     * every waiting loop that prints progress does. */
    if(cads_cli_outq_flush_due(&s_conn.outq, s_servicing, cads_hal_ticks_ms(), s_last_flush_ms)) {
        cads_cli_tcp_flush(pcb);
    }
}

static void cads_cli_tcp_session_flush(void* context) {
    struct tcp_pcb* pcb = (struct tcp_pcb*)context;
    if(pcb && cads_cli_tcp_alive(pcb)) cads_cli_tcp_flush(pcb);
}

static err_t cads_cli_tcp_sent(void* arg, struct tcp_pcb* pcb, u16_t len) {
    (void)arg;
    (void)len;
    /* A closed session's pcb can still see ACKs for its last bytes; the
     * queue may by then belong to the next connection. */
    if(!s_conn.in_use || pcb != s_conn.pcb) return ERR_OK;
    cads_cli_tcp_flush(pcb);
    return ERR_OK;
}

static err_t cads_cli_tcp_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    (void)arg;
    (void)err;

    if(!cads_cli_tcp_alive(pcb)) {
        if(p) pbuf_free(p);
        return ERR_OK;
    }

    if(!p) {
        /* The remote end closed its side. `printf 'lab info\n' | nc -N`
         * sends the command and the FIN together, so the close waits until
         * cads_cli_tcp_service() has run what is still queued. */
        s_conn.remote_closed = true;
        return ERR_OK;
    }

    /* Queue only - never execute here (cli_stream.h, "deferred input").
     * No room yet: leave the data with lwIP, which offers it again later. */
    if(s_conn.inq.size - s_conn.inq.count < p->tot_len) return ERR_MEM;
    for(struct pbuf* q = p; q != NULL; q = q->next) {
        (void)cads_cli_input_push(&s_conn.inq, &s_conn.telnet, (const uint8_t*)q->payload, q->len);
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void cads_cli_tcp_error(void* arg, err_t err) {
    (void)arg;
    (void)err;
    /* lwIP has already freed the pcb by the time this fires - nothing here
     * calls tcp_close() on it, only clears the local "someone is connected"
     * flag so the next accept() is allowed through. */
    s_conn.in_use = false;
}

static err_t cads_cli_tcp_accept(void* arg, struct tcp_pcb* new_pcb, err_t err) {
    (void)arg;
    if(err != ERR_OK || new_pcb == NULL) return ERR_VAL;

    if(s_conn.in_use) {
        /* Refuse a second operator rather than let two sessions dispatch
         * into the same command table unsynchronised - see cli_tcp.h. Say
         * why first: a bare close looks like a crashed board. tcp_close()
         * still delivers what tcp_write() queued. */
        static const char busy[] = "? belegt: andere Sitzung aktiv - bitte spaeter erneut\r\n";
        if(tcp_write(new_pcb, busy, sizeof(busy) - 1u, TCP_WRITE_FLAG_COPY) == ERR_OK) tcp_output(new_pcb);
        tcp_close(new_pcb);
        return ERR_OK;
    }

    s_conn.in_use = true;
    s_conn.pcb = new_pcb;
    cads_cli_session_init(&s_conn.session, cads_cli_tcp_write, new_pcb);
    s_conn.session.flush = cads_cli_tcp_session_flush;
    cads_cli_telnet_init(&s_conn.telnet);
    cads_cli_outq_init(&s_conn.outq, s_outq_storage, sizeof(s_outq_storage));
    cads_cli_outq_init(&s_conn.inq, s_inq_storage, sizeof(s_inq_storage));
    s_conn.remote_closed = false;

    /* No Nagle. After a command that switches views (`lab key`), the panel
     * blit takes the Ethernet datapath away (PA7) for ~0.45 s; with Nagle
     * the trailing "> " prompt - a second small write - then waited for the
     * ACK of the reply line, 0.4..0.7 s late. Safe here only because
     * replies go through the output queue (cads_cli_tcp_flush() waits for
     * TCP_SND_QUEUELEN room): without it, main's CLI lost the tail of `help`
     * with Nagle off (every write its own segment, queue full). */
    tcp_nagle_disable(new_pcb);

    tcp_arg(new_pcb, NULL);
    tcp_recv(new_pcb, cads_cli_tcp_recv);
    tcp_sent(new_pcb, cads_cli_tcp_sent);
    tcp_err(new_pcb, cads_cli_tcp_error);

    cads_cli_write(&s_conn.session, "CaDS Zero CLI - 'help' for commands\r\n> ");
    return ERR_OK;
}

bool cads_cli_tcp_start(uint16_t port) {
    if(s_listen_pcb != NULL) return true;

    struct tcp_pcb* pcb = tcp_new();
    if(!pcb) return false;

    if(tcp_bind(pcb, IP_ADDR_ANY, port) != ERR_OK) {
        tcp_close(pcb);
        return false;
    }

    struct tcp_pcb* listening = tcp_listen(pcb);
    if(!listening) {
        tcp_close(pcb);
        return false;
    }

    s_listen_pcb = listening;
    s_conn.in_use = false;
    tcp_accept(s_listen_pcb, cads_cli_tcp_accept);
    return true;
}

void cads_cli_tcp_service(void) {
    if(s_servicing || !s_conn.in_use) return; /* a command calling back in here runs nothing twice */
    struct tcp_pcb* pcb = s_conn.pcb;

    s_servicing = true;
    s_last_flush_ms = cads_hal_ticks_ms(); /* a command's first 250 ms are collected */
    (void)cads_cli_input_drain(&s_conn.inq, &s_conn.session);
    s_servicing = false;
    if(cads_cli_tcp_alive(pcb)) cads_cli_tcp_flush(pcb); /* a partial line's output, if any */

    if(cads_cli_tcp_alive(pcb) && s_conn.remote_closed && s_conn.inq.count == 0u) {
        tcp_sent(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        /* tcp_close() still delivers whatever tcp_write() already queued. */
        tcp_close(pcb);
        s_conn.in_use = false;
    }
}

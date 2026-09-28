/*
 * CaDS Zero - cads_cli over TCP, itsboard implementation.
 *
 * Raw (NO_SYS=1) lwIP API - the same one modules/net/src/cads_net_board.c
 * already uses for the netif itself, so this needs no OS thread either.
 * Every callback here fires synchronously from inside cads_net_poll()'s
 * call chain (cads_netif.input() -> ethernet_input() -> ... -> tcp_input()),
 * the same way the netif's own receive path already works - see that
 * file's header comment on why NO_SYS=1 raw API needs no separate polling
 * for this.
 */

#include "cads/cli/cli_tcp.h"

#include "cads/cli/cli.h"
#include "cads/cli/cli_stream.h"

#include "lwip/tcp.h"

/* Output that tcp_sndbuf() cannot take yet waits here and goes out from the
 * tcp_sent callback as ACKs free room. Waiting in place is not an option:
 * the command writing it runs inside cads_net_poll() (tcp_recv), and the
 * ACKs that would free the buffer are only processed by that same poll. CCM:
 * CPU-only (tcp_write copies it), and the SRAM budget is tight. */
#define CADS_CLI_TCP_OUTQ_SIZE 1024u
__attribute__((section(".ccm"))) static char s_outq_storage[CADS_CLI_TCP_OUTQ_SIZE];

static const char s_truncated_notice[] = "\r\n? Ausgabe gekuerzt\r\n";

/* One session at a time - see cli_tcp.h's own header comment on why. */
typedef struct {
    struct tcp_pcb* pcb;
    cads_cli_session_t session;
    cads_cli_telnet_t telnet;
    cads_cli_outq_t outq;
    bool in_use;
} cads_cli_tcp_conn_t;

static cads_cli_tcp_conn_t s_conn;
static struct tcp_pcb* s_listen_pcb = NULL;

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
    if(wrote) tcp_output(pcb);
}

static void cads_cli_tcp_write(void* context, const char* text, size_t length) {
    struct tcp_pcb* pcb = (struct tcp_pcb*)context;
    if(!pcb || length == 0u) return;
    (void)cads_cli_outq_push(&s_conn.outq, text, length);
    cads_cli_tcp_flush(pcb);
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

    if(!p) {
        /* The remote end closed its side. */
        tcp_sent(pcb, NULL);
        tcp_close(pcb);
        s_conn.in_use = false;
        return ERR_OK;
    }

    for(struct pbuf* q = p; q != NULL; q = q->next) {
        const uint8_t* data = (const uint8_t*)q->payload;
        for(u16_t i = 0; i < q->len; i++) {
            if(cads_cli_telnet_filter(&s_conn.telnet, data[i])) {
                cads_cli_session_feed(&s_conn.session, data[i]);
            }
        }
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
         * into the same command table unsynchronised - see cli_tcp.h. */
        tcp_close(new_pcb);
        return ERR_OK;
    }

    s_conn.in_use = true;
    s_conn.pcb = new_pcb;
    cads_cli_session_init(&s_conn.session, cads_cli_tcp_write, new_pcb);
    cads_cli_telnet_init(&s_conn.telnet);
    cads_cli_outq_init(&s_conn.outq, s_outq_storage, sizeof(s_outq_storage));

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

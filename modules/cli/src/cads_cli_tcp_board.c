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

#include "lwip/tcp.h"

/* One session at a time - see cli_tcp.h's own header comment on why. */
typedef struct {
    struct tcp_pcb* pcb;
    cads_cli_session_t session;
    bool in_use;
    uint32_t idle_polls;
} cads_cli_tcp_conn_t;

static cads_cli_tcp_conn_t s_conn;
static struct tcp_pcb* s_listen_pcb = NULL;

/* tcp_poll() interval in lwIP coarse ticks (TCP_SLOW_INTERVAL, 500 ms): one
 * poll per second. A session that sends nothing for CADS_CLI_TCP_IDLE_S is
 * dropped - otherwise a peer that vanished without FIN/RST (cable pulled,
 * host suspended) held the one session slot, and every later operator was
 * refused, until reboot. */
#define CADS_CLI_TCP_POLL_TICKS 2u
#define CADS_CLI_TCP_IDLE_S     300u

static void cads_cli_tcp_write(void* context, const char* text, size_t length) {
    struct tcp_pcb* pcb = (struct tcp_pcb*)context;
    if(!pcb || length == 0u) return;

    u16_t available = tcp_sndbuf(pcb);
    if(available == 0u) return; /* send buffer full: drop rather than block - see cli.h's write_fn contract */
    if((size_t)available < length) length = (size_t)available;

    if(tcp_write(pcb, text, (u16_t)length, TCP_WRITE_FLAG_COPY) == ERR_OK) {
        tcp_output(pcb);
    }
}

/* Detach every callback before letting go of the pcb, so a late event on a
 * closing connection (a sent/err while it drains) can never act on the
 * session state of the NEXT connection. Returns ERR_ABRT when the close had
 * to fall back to tcp_abort() - a callback must then return ERR_ABRT too. */
static err_t cads_cli_tcp_release(struct tcp_pcb* pcb) {
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0u);
    if(pcb == s_conn.pcb) {
        s_conn.pcb = NULL;
        s_conn.in_use = false;
    }
    if(tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb); /* lwIP's guidance when close fails (ERR_MEM) */
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t cads_cli_tcp_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    (void)arg;
    (void)err;

    if(!p) {
        /* The remote end closed its side. */
        return cads_cli_tcp_release(pcb);
    }

    s_conn.idle_polls = 0u;
    for(struct pbuf* q = p; q != NULL; q = q->next) {
        const uint8_t* data = (const uint8_t*)q->payload;
        for(u16_t i = 0; i < q->len; i++) {
            cads_cli_session_feed(&s_conn.session, data[i]);
        }
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t cads_cli_tcp_poll(void* arg, struct tcp_pcb* pcb) {
    (void)arg;
    if(++s_conn.idle_polls * CADS_CLI_TCP_POLL_TICKS / 2u < CADS_CLI_TCP_IDLE_S) return ERR_OK;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0u);
    s_conn.pcb = NULL;
    s_conn.in_use = false;
    tcp_abort(pcb);
    return ERR_ABRT;
}

static void cads_cli_tcp_error(void* arg, err_t err) {
    (void)err;
    /* lwIP has already freed the pcb by the time this fires - nothing here
     * calls tcp_close() on it. `arg` is the pcb this callback was installed
     * for: only that session's slot is freed, never a newer one's. */
    if(arg != NULL && arg == s_conn.pcb) {
        s_conn.pcb = NULL;
        s_conn.in_use = false;
    }
}

static err_t cads_cli_tcp_accept(void* arg, struct tcp_pcb* new_pcb, err_t err) {
    (void)arg;
    if(err != ERR_OK || new_pcb == NULL) return ERR_VAL;

    if(s_conn.in_use) {
        /* Refuse a second operator rather than let two sessions dispatch
         * into the same command table unsynchronised - see cli_tcp.h. */
        if(tcp_close(new_pcb) != ERR_OK) {
            tcp_abort(new_pcb);
            return ERR_ABRT;
        }
        return ERR_OK;
    }

    s_conn.in_use = true;
    s_conn.pcb = new_pcb;
    s_conn.idle_polls = 0u;
    cads_cli_session_init(&s_conn.session, cads_cli_tcp_write, new_pcb);

    tcp_arg(new_pcb, new_pcb);
    tcp_recv(new_pcb, cads_cli_tcp_recv);
    tcp_err(new_pcb, cads_cli_tcp_error);
    tcp_poll(new_pcb, cads_cli_tcp_poll, CADS_CLI_TCP_POLL_TICKS);

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
    s_conn.pcb = NULL;
    tcp_accept(s_listen_pcb, cads_cli_tcp_accept);
    return true;
}

void cads_cli_tcp_stop(void) {
    if(s_conn.pcb != NULL) {
        struct tcp_pcb* pcb = s_conn.pcb;
        (void)cads_cli_tcp_release(pcb);
    }
    s_conn.in_use = false;
    if(s_listen_pcb != NULL) {
        tcp_accept(s_listen_pcb, NULL);
        (void)tcp_close(s_listen_pcb); /* a LISTEN pcb closes synchronously */
        s_listen_pcb = NULL;
    }
}

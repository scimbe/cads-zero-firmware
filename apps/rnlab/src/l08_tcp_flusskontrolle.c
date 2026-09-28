/*
 * CaDS Zero - rnlab L08 (TCP-Flusskontrolle): board integration.
 *
 * A TCP sink on port 7008 for tools/rnlab.py tcp-send, written against
 * lwIP's raw API so that the one lever of receiver-side flow control is in
 * plain sight: tcp_recved(). lwIP shrinks the advertised window by every
 * byte it delivers and only reopens it when the application says it has
 * read them. Here the "application" reads at a chosen rate
 * (`lab 08 sink rate <kB/s>`): received data is counted and dropped at
 * once, but tcp_recved() is paced by the limiter in
 * l08_tcp_flusskontrolle_logic.c from a 10 ms lwIP timer. A slow reader
 * therefore drives the window to zero, the sender stalls and probes, and
 * every tcp_recved() that reopens it shows up as a window update.
 *
 * One connection at a time; a second one is refused (RST).
 */

#include "rnlab/rnlab_lesson.h"

#include "cads/net/net.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"

#include "l08_tcp_flusskontrolle_logic.h"

#define L08_TICK_MS 10u

typedef struct {
    struct tcp_pcb* listener;
    struct tcp_pcb* conn;
    rnlab_l08_limiter_t limiter;
    uint32_t rate;          /* bytes/s, 0 = unlimited; survives connections */
    uint32_t pending;       /* received, not yet "read" (window not reopened) */
    uint32_t bytes;
    uint64_t first_us;
    uint64_t last_us;
    bool paused;            /* `sink pause`: the application reads nothing */
    bool fin;               /* peer sent FIN; we close once everything is read */
    bool closed;            /* ... and have closed */
    bool zero;              /* advertised window is 0 right now */
    uint32_t zero_events;
    uint32_t zero_ms;
    uint32_t zero_since_ms;
    uint32_t updates;       /* window reopened from 0 */
    uint16_t min_wnd;
    char peer[16];
    bool ticking;
} l08_state_t;

static l08_state_t l08;

static void l08_note_window(uint32_t now_ms) {
    if(!l08.conn) return;
    tcpwnd_size_t wnd = l08.conn->rcv_ann_wnd;
    if(wnd < l08.min_wnd) l08.min_wnd = (uint16_t)wnd;
    if(wnd == 0u && !l08.zero) {
        l08.zero = true;
        l08.zero_events++;
        l08.zero_since_ms = now_ms;
    } else if(wnd != 0u && l08.zero) {
        l08.zero = false;
        l08.updates++;
        l08.zero_ms += now_ms - l08.zero_since_ms;
    }
}

static void l08_forget_conn(void) {
    if(l08.zero) {
        l08.zero = false;
        l08.zero_ms += cads_hal_ticks_ms() - l08.zero_since_ms;
    }
    l08.conn = NULL;
    l08.pending = 0u;
}

/* Our FIN. Only once the application has read everything: lwIP answers a
 * close with unread data (rcv_wnd != TCP_WND) with RST instead of FIN, and
 * the sender would see an aborted transfer. Returns false if it had to abort. */
static bool l08_close(void) {
    struct tcp_pcb* pcb = l08.conn;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    l08_forget_conn();
    l08.closed = true;
    if(tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
        return false;
    }
    return true;
}

static void l08_read(uint32_t now_ms) {
    if(l08.paused) {
        l08_note_window(now_ms);
        return;
    }
    /* Unlimited reading does not need the limiter at all - so the sink also
     * works as a plain W/RTT receiver before rnlab_l08_limiter_release() is. */
    uint32_t release = l08.pending;
    if(l08.rate != 0u) release = rnlab_l08_limiter_release(&l08.limiter, now_ms, l08.pending);
    if(release > l08.pending) release = l08.pending; /* never reopen more than was taken */
    l08.pending -= release;
    while(release > 0u && l08.conn) {
        u16_t chunk = (release > 0xFFFFu) ? 0xFFFFu : (u16_t)release;
        /* Reopens the window; lwIP sends the update itself once it has grown
         * by TCP_WND_UPDATE_THRESHOLD (silly-window avoidance, RFC 1122). */
        tcp_recved(l08.conn, chunk);
        release -= chunk;
    }
    l08_note_window(now_ms);
    if(l08.fin && l08.pending == 0u && l08.conn) (void)l08_close();
}

static void l08_tick(void* arg) {
    (void)arg;
    if(l08.conn) l08_read(cads_hal_ticks_ms());
    if(l08.listener || l08.conn) {
        sys_timeout(L08_TICK_MS, l08_tick, NULL);
    } else {
        l08.ticking = false;
    }
}

static void l08_start_ticking(void) {
    if(l08.ticking) return;
    l08.ticking = true;
    sys_timeout(L08_TICK_MS, l08_tick, NULL);
}

static void l08_err(void* arg, err_t err) {
    (void)arg;
    (void)err;
    /* lwIP has already freed the pcb (RST or abort) - just forget it. */
    l08_forget_conn();
}

static err_t l08_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    (void)arg;
    (void)err;
    if(!p) {
        /* FIN: the sender is done. Our FIN is what rnlab.py tcp-send waits for
         * before it stops its clock - sent now if everything has been read,
         * otherwise by the tick once the application has caught up. */
        l08.fin = true;
        (void)pcb;
        if(l08.pending == 0u && !l08.paused) return l08_close() ? ERR_OK : ERR_ABRT;
        return ERR_OK;
    }
    uint64_t now = cads_hal_ticks_us();
    if(l08.first_us == 0u) l08.first_us = now;
    l08.last_us = now;
    l08.bytes += p->tot_len;
    l08.pending += p->tot_len;
    /* The payload itself is of no interest - free the buffer right away, so
     * that only the window (not the pbuf pool) is what a slow reader holds. */
    pbuf_free(p);
    if(l08.rate == 0u && !l08.paused) {
        l08_read(cads_hal_ticks_ms());
    } else {
        l08_note_window(cads_hal_ticks_ms());
    }
    return ERR_OK;
}

static err_t l08_accept(void* arg, struct tcp_pcb* pcb, err_t err) {
    (void)arg;
    if(err != ERR_OK || !pcb) return ERR_VAL;
    if(l08.conn) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    l08.conn = pcb;
    l08.pending = 0u;
    l08.bytes = 0u;
    l08.first_us = 0u;
    l08.last_us = 0u;
    l08.fin = false;
    l08.closed = false;
    l08.zero = false;
    l08.zero_events = 0u;
    l08.zero_ms = 0u;
    l08.updates = 0u;
    l08.min_wnd = (uint16_t)pcb->rcv_ann_wnd;
    ipaddr_ntoa_r(&pcb->remote_ip, l08.peer, sizeof(l08.peer));
    rnlab_l08_limiter_init(&l08.limiter, l08.rate, cads_hal_ticks_ms());
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, l08_recv);
    tcp_err(pcb, l08_err);
    l08_start_ticking();
    return ERR_OK;
}

static void l08_cmd_start(cads_cli_session_t* s) {
    if(!l08.listener) {
        struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
        if(!pcb) {
            cads_cli_write(s, "! kein TCP-PCB frei\r\n");
            return;
        }
        if(tcp_bind(pcb, IP_ANY_TYPE, RNLAB_L08_PORT) != ERR_OK) {
            tcp_close(pcb);
            cads_cli_write(s, "! Port 7008 belegt\r\n");
            return;
        }
        struct tcp_pcb* listener = tcp_listen_with_backlog(pcb, 1);
        if(!listener) {
            tcp_close(pcb);
            cads_cli_write(s, "! kein Listen-PCB frei (MEMP_NUM_TCP_PCB_LISTEN)\r\n");
            return;
        }
        tcp_accept(listener, l08_accept);
        l08.listener = listener;
        l08_start_ticking();
    }
    cads_cli_write(s, "TCP-Senke auf Port 7008, Leserate ");
    if(l08.rate) {
        cads_cli_write_uint(s, l08.rate / 1000u);
        cads_cli_write(s, " kB/s\r\n");
    } else {
        cads_cli_write(s, "unbegrenzt\r\n");
    }
}

static void l08_cmd_stop(cads_cli_session_t* s) {
    if(l08.conn) {
        struct tcp_pcb* pcb = l08.conn;
        tcp_err(pcb, NULL);
        l08_forget_conn();
        tcp_abort(pcb);
    }
    if(l08.listener) {
        tcp_close(l08.listener);
        l08.listener = NULL;
    }
    if(l08.ticking) {
        /* Free the timeout slot now, not only when the tick next runs. */
        sys_untimeout(l08_tick, NULL);
        l08.ticking = false;
    }
    cads_cli_write(s, "TCP-Senke gestoppt\r\n");
}

static void l08_cmd_rate(cads_cli_session_t* s, int argc, char* argv[]) {
    uint32_t kbps;
    if(argc < 1 || !cads_str_to_uint(argv[0], &kbps, NULL) || kbps > 100000u) {
        cads_cli_write(s, "? Aufruf: lab 08 sink rate <kB/s>   (0 = unbegrenzt)\r\n");
        return;
    }
    l08.rate = kbps * 1000u;
    /* Applies immediately, also to a running transfer. */
    rnlab_l08_limiter_init(&l08.limiter, l08.rate, cads_hal_ticks_ms());
    cads_cli_write(s, "Leserate ");
    if(kbps) {
        cads_cli_write_uint(s, kbps);
        cads_cli_write(s, " kB/s\r\n");
    } else {
        cads_cli_write(s, "unbegrenzt\r\n");
    }
}

static void l08_cmd_pause(cads_cli_session_t* s, bool pause) {
    l08.paused = pause;
    /* No banked credit from the pause: resuming continues at the set rate. */
    rnlab_l08_limiter_init(&l08.limiter, l08.rate, cads_hal_ticks_ms());
    cads_cli_write(s, pause ? "Anwendung liest nicht mehr (pause)\r\n" : "Anwendung liest wieder\r\n");
}

static void l08_write_kbit(cads_cli_session_t* s, uint64_t bps) {
    cads_cli_write_uint(s, (uint32_t)(bps / 1000u));
    cads_cli_write(s, " kbit/s");
}

static void l08_cmd_stats(cads_cli_session_t* s) {
    uint32_t now = cads_hal_ticks_ms();
    cads_cli_write(s, "senke:      ");
    cads_cli_write(s, l08.listener ? "aktiv, Port 7008" : "aus");
    cads_cli_write(s, ", Leserate ");
    if(l08.rate) {
        cads_cli_write_uint(s, l08.rate / 1000u);
        cads_cli_write(s, " kB/s");
    } else {
        cads_cli_write(s, "unbegrenzt");
    }
    if(l08.paused) cads_cli_write(s, ", pausiert");
    cads_cli_write(s, "\r\nverbindung: ");
    if(l08.conn) {
        cads_cli_write(s, l08.peer);
        cads_cli_write(s, ", offen");
    } else if(l08.peer[0]) {
        cads_cli_write(s, l08.peer);
        cads_cli_write(s, l08.closed ? ", beendet (FIN)" : ", abgebrochen (RST)");
    } else {
        cads_cli_write(s, "keine");
    }
    cads_cli_write(s, "\r\nempfangen:  ");
    cads_cli_write_uint(s, l08.bytes);
    cads_cli_write(s, " Byte");
    uint64_t span = l08.last_us - l08.first_us;
    if(span > 0u) {
        cads_cli_write(s, " in ");
        cads_cli_write_uint(s, (uint32_t)(span / 1000u));
        cads_cli_write(s, " ms = ");
        l08_write_kbit(s, ((uint64_t)l08.bytes * 8u * 1000000u) / span);
    }
    cads_cli_write(s, "\r\nfenster:    jetzt ");
    cads_cli_write_uint(s, l08.conn ? l08.conn->rcv_ann_wnd : 0u);
    cads_cli_write(s, " B, min ");
    cads_cli_write_uint(s, l08.min_wnd);
    cads_cli_write(s, " B, TCP_WND ");
    cads_cli_write_uint(s, TCP_WND);
    cads_cli_write(s, " B, ungelesen ");
    cads_cli_write_uint(s, l08.pending);
    cads_cli_write(s, " B\r\nnullfenster: ");
    cads_cli_write_uint(s, l08.zero_events);
    cads_cli_write(s, " mal, ");
    cads_cli_write_uint(s, l08.zero_ms + (l08.zero ? now - l08.zero_since_ms : 0u));
    cads_cli_write(s, " ms gesamt, ");
    cads_cli_write_uint(s, l08.updates);
    cads_cli_write(s, " Fenster-Updates\r\n");
}

static void l08_cmd_info(cads_cli_session_t* s) {
    cads_cli_write(s, "TCP_MSS ");
    cads_cli_write_uint(s, TCP_MSS);
    cads_cli_write(s, " B, TCP_WND ");
    cads_cli_write_uint(s, TCP_WND);
    cads_cli_write(s, " B (");
    cads_cli_write_uint(s, TCP_WND / TCP_MSS);
    cads_cli_write(s, " MSS), TCP_SND_BUF ");
    cads_cli_write_uint(s, TCP_SND_BUF);
    cads_cli_write(s, " B, PBUF_POOL_SIZE ");
    cads_cli_write_uint(s, PBUF_POOL_SIZE);
    cads_cli_write(s, "\r\n");
}

static void l08_cmd_calc(cads_cli_session_t* s, int argc, char* argv[]) {
    uint32_t rtt_us;
    uint32_t app_kbps = 0u;
    if(argc < 1 || !cads_str_to_uint(argv[0], &rtt_us, NULL) || rtt_us == 0u ||
       (argc >= 2 && !cads_str_to_uint(argv[1], &app_kbps, NULL))) {
        cads_cli_write(s, "? Aufruf: lab 08 calc <rtt_us> [Leserate kB/s]\r\n");
        return;
    }
    cads_net_status_t status;
    cads_net_status(&status);
    uint32_t link_bps = (uint32_t)(status.speed_mbit ? status.speed_mbit : 100u) * 1000000u;

    cads_cli_write(s, "W/RTT:      ");
    l08_write_kbit(s, rnlab_l08_window_limit_bps(TCP_WND, rtt_us));
    cads_cli_write(s, "  (W = ");
    cads_cli_write_uint(s, TCP_WND);
    cads_cli_write(s, " B, RTT = ");
    cads_cli_write_uint(s, rtt_us);
    cads_cli_write(s, " us)\r\nlink:       ");
    l08_write_kbit(s, rnlab_l08_link_limit_bps(link_bps, TCP_MSS));
    cads_cli_write(s, "  (");
    cads_cli_write_uint(s, link_bps / 1000000u);
    cads_cli_write(s, " Mbit/s, MSS ");
    cads_cli_write_uint(s, TCP_MSS);
    cads_cli_write(s, " B)\r\nvorhersage: ");
    l08_write_kbit(s, rnlab_l08_predict_bps(TCP_WND, rtt_us, link_bps, TCP_MSS, app_kbps * 1000u));
    cads_cli_write(s, "\r\n");
}

static void l08_help(cads_cli_session_t* s) {
    cads_cli_write(s,
        "lab 08 sink start           TCP-Senke Port 7008 (eine Verbindung)\r\n"
        "lab 08 sink rate <kB/s>     Leserate der Anwendung, 0 = unbegrenzt\r\n"
        "lab 08 sink pause|resume    Anwendung liest gar nicht / wieder\r\n"
        "lab 08 sink stats           Bytes, Goodput, Fenster, Nullfenster\r\n"
        "lab 08 sink stop            Senke schliessen\r\n"
        "lab 08 info                 TCP_MSS/TCP_WND/TCP_SND_BUF dieses Builds\r\n"
        "lab 08 calc <rtt_us> [kB/s] Goodput-Vorhersage fuer diesen Build\r\n");
}

void rnlab_l08_command(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc >= 2 && cads_str_equal(argv[0], "sink")) {
        const char* sub = argv[1];
        if(cads_str_equal(sub, "start")) {
            l08_cmd_start(session);
        } else if(cads_str_equal(sub, "stop")) {
            l08_cmd_stop(session);
        } else if(cads_str_equal(sub, "pause") || cads_str_equal(sub, "resume")) {
            l08_cmd_pause(session, cads_str_equal(sub, "pause"));
        } else if(cads_str_equal(sub, "rate")) {
            l08_cmd_rate(session, argc - 2, argv + 2);
        } else if(cads_str_equal(sub, "stats")) {
            l08_cmd_stats(session);
        } else {
            l08_help(session);
        }
        return;
    }
    if(argc >= 1 && cads_str_equal(argv[0], "info")) {
        l08_cmd_info(session);
        return;
    }
    if(argc >= 1 && cads_str_equal(argv[0], "calc")) {
        l08_cmd_calc(session, argc - 1, argv + 1);
        return;
    }
    l08_help(session);
}

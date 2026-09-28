/*
 * CaDS Zero - rnlab L09 (Congestion Control): board integration.
 *
 * The board is the TCP sender here: `lab 09 cc start <ip> <port> [bytes]`
 * connects to tools/rnlab.py tcp-recv on the computer and pushes zeros as
 * fast as lwIP's congestion control lets it. Two driver hooks watch the
 * connection from the outside:
 *
 *  - rx_frame sees every ACK before lwIP does. Each one opens a trace
 *    entry; the entry is completed with lwIP's cwnd/ssthresh/snd_wnd at the
 *    next point where lwIP has finished processing it (the transmission it
 *    triggers, the sent callback, or the next frame) - so every line of
 *    `lab 09 trace` shows the state *after* that ACK. The same ACK/dupACK
 *    events drive the Reno model from l09_congestion_control_logic.c,
 *    whose cwnd is traced alongside for comparison.
 *  - tx_frame sees every data segment: new data starts an RTT sample
 *    (one at a time, Karn: never across a retransmission), old data is a
 *    retransmission - fast (lwIP is in fast recovery) or after a timeout.
 *  - tx_drop discards data segments according to `lab 09 loss`, before the
 *    MAC: lwIP believes they were sent, the receiver never sees them.
 *
 * An ACK that triggers nothing (the first dupACKs, the last ACK before a
 * timeout) would otherwise only be completed at the next event, possibly
 * a whole RTO later and with the wrong state - a 1 ms lwIP timer, which
 * runs at the end of the same poll that processed the ACK, closes it.
 *
 * The trace ring lives in CCM (CPU-only, never DMA) and stops when full;
 * `lab 09 trace [from]` prints it as CSV in pages that fit one TCP send
 * buffer of the telnet session.
 */

#include "rnlab/rnlab_lesson.h"

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "rnlab_args_logic.h"

#include "l09_congestion_control_logic.h"

#define L09_TRACE_LEN 320u
#define L09_TRACE_PAGE 25u
#define L09_DEFAULT_BYTES 300000u
#define L09_BLOCK 512u
#define L09_TICK_MS 1u

typedef enum { L09_IDLE = 0, L09_CONNECTING, L09_SENDING, L09_DONE, L09_FAILED } l09_run_t;

typedef struct {
    struct tcp_pcb* pcb;
    l09_run_t run;
    uint32_t total;
    uint32_t written;
    uint32_t acked;
    uint32_t iss;          /* snd_una when the connection was established */
    uint32_t snd_max;      /* highest sequence number sent so far (end of segment) */
    uint32_t last_ack;
    uint16_t local_port;
    uint16_t peer_port;
    uint64_t t0_us;
    uint64_t t_end_us;
    uint8_t seen_nrtx;     /* pcb->nrtx already accounted as a timeout */
    /* RTT sample in flight (Karn's algorithm: one at a time, never across a retransmission) */
    bool timing;
    uint32_t timed_end;
    uint64_t timed_t0;
    uint32_t rtt_min, rtt_max, rtt_n;
    uint64_t rtt_sum;
    /* counters */
    uint32_t acks, dupacks, fast, rto;
    /* entry waiting for lwIP to finish processing its ACK */
    bool pending;
    rnlab_l09_trace_entry_t pend;
    rnlab_reno_t model;
    rnlab_l09_dropper_t dropper;
    uint32_t trace_len;
    uint32_t trace_lost;
} l09_state_t;

static l09_state_t l09;
static bool l09_ticking; /* outside l09: survives the reset in cc start */
RNLAB_CCM static rnlab_l09_trace_entry_t l09_trace[L09_TRACE_LEN];
static const uint8_t l09_block[L09_BLOCK]; /* the payload: zeros, from flash */

static uint16_t l09_sat16(uint32_t v) {
    return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v;
}

/* Complete an entry with lwIP's current state and append it. */
static void l09_commit(rnlab_l09_trace_entry_t* e) {
    const struct tcp_pcb* pcb = l09.pcb;
    if(pcb) {
        e->cwnd = pcb->cwnd;
        e->ssthresh = pcb->ssthresh;
        e->snd_wnd = pcb->snd_wnd;
        e->flight = l09_sat16(pcb->snd_nxt - pcb->lastack);
        e->phase = (uint8_t)rnlab_l09_phase(pcb->cwnd, pcb->ssthresh, (pcb->flags & TF_INFR) != 0u);
    }
    if(l09.trace_len < L09_TRACE_LEN) {
        l09_trace[l09.trace_len++] = *e;
    } else {
        l09.trace_lost++;
    }
}

static void l09_flush_pending(void) {
    if(!l09.pending) return;
    l09.pending = false;
    l09_commit(&l09.pend);
}

static rnlab_l09_trace_entry_t l09_new_entry(uint8_t event, uint32_t ack) {
    rnlab_l09_trace_entry_t e = {0};
    e.t_ms = (uint32_t)((cads_hal_ticks_us() - l09.t0_us) / 1000u);
    e.acked = ack - l09.iss;
    e.event = event;
    e.model_cwnd = l09_sat16(l09.model.cwnd);
    return e;
}

static bool l09_is_ours(const rnlab_l09_seg_t* s, bool outgoing) {
    if(!l09.pcb || l09.run != L09_SENDING) return false;
    return outgoing ? (s->src_port == l09.local_port && s->dst_port == l09.peer_port)
                    : (s->src_port == l09.peer_port && s->dst_port == l09.local_port);
}

/* --- driver hooks ---------------------------------------------------------- */

void rnlab_l09_hook_rx_frame(const uint8_t* frame, size_t len) {
    rnlab_l09_seg_t s;
    if(!rnlab_l09_parse_tcp(frame, len, &s) || !l09_is_ours(&s, false)) return;
    if(!(s.flags & RNLAB_L09_TCP_ACK)) return;

    /* lwIP is done with the previous ACK by now. */
    l09_flush_pending();

    uint64_t now = cads_hal_ticks_us();
    uint32_t flight = l09.snd_max - l09.last_ack;
    int32_t advance = (int32_t)(s.ack - l09.last_ack);

    if(advance > 0) {
        l09.acks++;
        l09.last_ack = s.ack;
        l09.seen_nrtx = 0u; /* a new ACK resets lwIP's retransmission count */
        rnlab_reno_on_ack(&l09.model, (uint32_t)advance);
        rnlab_l09_trace_entry_t e = l09_new_entry(RNLAB_L09_EV_ACK, s.ack);
        if(l09.timing && (int32_t)(s.ack - l09.timed_end) >= 0) {
            uint32_t rtt = (uint32_t)(now - l09.timed_t0);
            l09.timing = false;
            if(l09.rtt_n == 0u || rtt < l09.rtt_min) l09.rtt_min = rtt;
            if(rtt > l09.rtt_max) l09.rtt_max = rtt;
            l09.rtt_sum += rtt;
            l09.rtt_n++;
            e.rtt_us = l09_sat16(rtt);
        }
        l09.pend = e;
        l09.pending = true;
    } else if(advance == 0 && s.payload == 0u && flight > 0u &&
              !(s.flags & (RNLAB_L09_TCP_SYN | RNLAB_L09_TCP_FIN))) {
        /* Same ACK again, no data, something outstanding: a duplicate ACK
         * (lwIP additionally requires an unchanged window - close enough). */
        l09.dupacks++;
        rnlab_reno_on_dupack(&l09.model, flight);
        l09.pend = l09_new_entry(RNLAB_L09_EV_DUPACK, s.ack);
        l09.pending = true;
    }
}

void rnlab_l09_hook_tx_frame(const uint8_t* frame, size_t len) {
    rnlab_l09_seg_t s;
    if(!rnlab_l09_parse_tcp(frame, len, &s) || !l09_is_ours(&s, true) || s.payload == 0u) return;

    /* Sent from tcp_output() at the end of lwIP's ACK processing: the state
     * the pending ACK produced is final now. */
    l09_flush_pending();

    uint32_t end = s.seq + s.payload;
    if((int32_t)(end - l09.snd_max) > 0) {
        l09.snd_max = end;
        if(!l09.timing) {
            l09.timing = true;
            l09.timed_end = end;
            l09.timed_t0 = cads_hal_ticks_us();
        }
        return;
    }

    /* Old data again: a retransmission. Its ACK would be ambiguous (Karn). */
    l09.timing = false;
    const struct tcp_pcb* pcb = l09.pcb;
    if(pcb->flags & TF_INFR) {
        /* tcp_rexmit_fast() set TF_INFR before this tcp_output() ran. */
        l09.fast++;
        rnlab_l09_trace_entry_t e = l09_new_entry(RNLAB_L09_EV_FAST, l09.last_ack);
        l09_commit(&e);
    } else if(pcb->nrtx > l09.seen_nrtx) {
        /* First resend after the retransmission timer fired (lwIP counts it
         * in nrtx); the go-back-N resends that follow are not new events. */
        l09.seen_nrtx = pcb->nrtx;
        l09.rto++;
        rnlab_reno_on_timeout(&l09.model, l09.snd_max - l09.last_ack);
        rnlab_l09_trace_entry_t e = l09_new_entry(RNLAB_L09_EV_RTO, l09.last_ack);
        l09_commit(&e);
    }
}

bool rnlab_l09_hook_tx_drop(const uint8_t* frame, size_t len) {
    rnlab_l09_seg_t s;
    if(!rnlab_l09_parse_tcp(frame, len, &s) || !l09_is_ours(&s, true) || s.payload == 0u) return false;
    if(!rnlab_l09_dropper_decide(&l09.dropper)) return false;
    rnlab_l09_trace_entry_t e = l09_new_entry(RNLAB_L09_EV_DROP, s.seq);
    l09_commit(&e);
    return true;
}

static void l09_tick(void* arg) {
    (void)arg;
    l09_flush_pending();
    if(l09.run == L09_SENDING) {
        sys_timeout(L09_TICK_MS, l09_tick, NULL);
    } else {
        l09_ticking = false;
    }
}

/* --- the sender ------------------------------------------------------------ */

static void l09_detach(void) {
    if(!l09.pcb) return;
    tcp_arg(l09.pcb, NULL);
    tcp_sent(l09.pcb, NULL);
    tcp_recv(l09.pcb, NULL);
    tcp_err(l09.pcb, NULL);
    l09.pcb = NULL;
}

static void l09_fill(struct tcp_pcb* pcb) {
    while(l09.written < l09.total) {
        uint32_t n = tcp_sndbuf(pcb);
        if(n == 0u) break;
        if(n > l09.total - l09.written) n = l09.total - l09.written;
        if(n > L09_BLOCK) n = L09_BLOCK;
        /* COPY: lwIP keeps its own copy until ACKed. Without it every block
         * would need a ROM pbuf from MEMP_NUM_PBUF (12) and that pool, not
         * cwnd, would limit how much is in flight. */
        if(tcp_write(pcb, l09_block, (u16_t)n, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE) != ERR_OK) break;
        l09.written += n;
    }
    tcp_output(pcb);
}

static err_t l09_sent(void* arg, struct tcp_pcb* pcb, u16_t len) {
    (void)arg;
    l09_flush_pending();
    l09.acked += len;
    if(l09.acked >= l09.total) {
        l09.t_end_us = cads_hal_ticks_us();
        l09.run = L09_DONE;
        l09_detach();
        /* Everything is ACKed, so the FIN goes out on its own; rnlab.py
         * tcp-recv stops its clock there. */
        if(tcp_close(pcb) != ERR_OK) {
            tcp_abort(pcb);
            return ERR_ABRT;
        }
        return ERR_OK;
    }
    l09_fill(pcb);
    return ERR_OK;
}

static err_t l09_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    (void)arg;
    (void)err;
    if(p) {
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }
    /* The receiver closed before everything was ACKed. */
    l09.run = L09_FAILED;
    l09.t_end_us = cads_hal_ticks_us();
    l09_detach();
    if(tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static void l09_err(void* arg, err_t err) {
    (void)arg;
    (void)err;
    l09.pcb = NULL; /* already freed by lwIP */
    l09.run = L09_FAILED;
    l09.t_end_us = cads_hal_ticks_us();
}

static err_t l09_connected(void* arg, struct tcp_pcb* pcb, err_t err) {
    (void)arg;
    if(err != ERR_OK) return err;
    l09.run = L09_SENDING;
    l09.t0_us = cads_hal_ticks_us();
    l09.iss = pcb->lastack;
    l09.last_ack = pcb->lastack;
    l09.snd_max = pcb->snd_nxt;
    l09.local_port = pcb->local_port;
    /* The model starts where lwIP starts: initial window (RFC 3390) and
     * ssthresh = TCP_SND_BUF (lwIP's choice, see tcp_alloc()). */
    rnlab_reno_init(&l09.model, pcb->mss, pcb->cwnd, pcb->ssthresh);
    if(!l09_ticking) {
        l09_ticking = true;
        sys_timeout(L09_TICK_MS, l09_tick, NULL);
    }
    l09_fill(pcb);
    return ERR_OK;
}

/* --- commands -------------------------------------------------------------- */

static void l09_cmd_start(cads_cli_session_t* s, int argc, char* argv[]) {
    uint32_t ip, port, bytes = L09_DEFAULT_BYTES;
    if(argc < 2 || !rnlab_parse_ipv4(argv[0], &ip) || !cads_str_to_uint(argv[1], &port, NULL) ||
       port == 0u || port > 65535u || (argc >= 3 && (!cads_str_to_uint(argv[2], &bytes, NULL) || bytes == 0u))) {
        cads_cli_write(s, "? Aufruf: lab 09 cc start <ip> <port> [bytes]\r\n");
        return;
    }
    if(l09.run == L09_CONNECTING || l09.run == L09_SENDING) {
        cads_cli_write(s, "! laeuft schon ('lab 09 cc stop')\r\n");
        return;
    }
    struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if(!pcb) {
        cads_cli_write(s, "! kein TCP-PCB frei\r\n");
        return;
    }
    /* Everything except the loss setting starts over. */
    rnlab_l09_dropper_t keep = l09.dropper;
    l09 = (l09_state_t){0};
    l09.dropper = keep;
    l09.dropper.seen = 0u;
    l09.dropper.dropped = 0u;
    l09.total = bytes;
    l09.peer_port = (uint16_t)port;
    l09.pcb = pcb;
    l09.run = L09_CONNECTING;

    ip_addr_t addr;
    ip_addr_set_ip4_u32(&addr, lwip_htonl(ip));
    tcp_arg(pcb, NULL);
    tcp_sent(pcb, l09_sent);
    tcp_recv(pcb, l09_recv);
    tcp_err(pcb, l09_err);
    if(tcp_connect(pcb, &addr, (u16_t)port, l09_connected) != ERR_OK) {
        l09_detach();
        tcp_abort(pcb);
        l09.run = L09_FAILED;
        cads_cli_write(s, "! tcp_connect fehlgeschlagen\r\n");
        return;
    }
    cads_cli_write(s, "sende ");
    cads_cli_write_uint(s, bytes);
    cads_cli_write(s, " Byte an ");
    cads_cli_write(s, argv[0]);
    cads_cli_write(s, ":");
    cads_cli_write_uint(s, port);
    cads_cli_write(s, " - Fortschritt: lab 09 status\r\n");
}

static void l09_cmd_stop(cads_cli_session_t* s) {
    if(l09.pcb) {
        struct tcp_pcb* pcb = l09.pcb;
        l09_detach();
        tcp_abort(pcb);
        l09.run = L09_FAILED;
        l09.t_end_us = cads_hal_ticks_us();
    }
    cads_cli_write(s, "gestoppt\r\n");
}

static void l09_write_pct(cads_cli_session_t* s, uint32_t ppm) {
    /* ppm -> "x.yyy %" */
    cads_cli_write_uint(s, ppm / 10000u);
    cads_cli_write(s, ".");
    char frac[CADS_FMT_BUFFER];
    cads_fmt_uint_pad(frac, sizeof(frac), (ppm % 10000u) / 10u, 3u, '0');
    cads_cli_write(s, frac);
    cads_cli_write(s, " %");
}

static uint32_t l09_loss_ppm(void) {
    if(l09.dropper.every_n) return 1000000u / l09.dropper.every_n;
    return l09.dropper.p_ppm;
}

static void l09_cmd_loss(cads_cli_session_t* s, int argc, char* argv[]) {
    if(l09.dropper.rng == 0u) {
        /* First use: seed from the hardware RNG, so runs with p differ. */
        uint32_t seed = 0u;
        (void)cads_hal_rng_bytes((uint8_t*)&seed, sizeof(seed));
        rnlab_l09_dropper_init(&l09.dropper, seed);
    }
    uint32_t v;
    if(argc >= 1 && cads_str_equal(argv[0], "off")) {
        l09.dropper.every_n = 0u;
        l09.dropper.p_ppm = 0u;
    } else if(argc >= 2 && cads_str_equal(argv[0], "p") && rnlab_l09_parse_percent(argv[1], &v)) {
        l09.dropper.every_n = 0u;
        l09.dropper.p_ppm = v;
    } else if(argc >= 1 && cads_str_to_uint(argv[0], &v, NULL) && v >= 1u) {
        l09.dropper.every_n = v;
        l09.dropper.p_ppm = 0u;
    } else {
        cads_cli_write(s, "? Aufruf: lab 09 loss <n> | p <prozent> | off\r\n");
        return;
    }
    cads_cli_write(s, "verlust: ");
    if(l09.dropper.every_n) {
        cads_cli_write(s, "jedes ");
        cads_cli_write_uint(s, l09.dropper.every_n);
        cads_cli_write(s, ". Datensegment (p = ");
        l09_write_pct(s, l09_loss_ppm());
        cads_cli_write(s, ")\r\n");
    } else if(l09.dropper.p_ppm) {
        cads_cli_write(s, "zufaellig, p = ");
        l09_write_pct(s, l09.dropper.p_ppm);
        cads_cli_write(s, "\r\n");
    } else {
        cads_cli_write(s, "aus\r\n");
    }
}

static void l09_cmd_status(cads_cli_session_t* s) {
    static const char* const names[] = {"bereit", "verbinde", "sendet", "fertig", "abgebrochen"};
    cads_cli_write(s, "zustand:   ");
    cads_cli_write(s, names[l09.run]);
    cads_cli_write(s, ", ");
    cads_cli_write_uint(s, l09.acked);
    cads_cli_write(s, " von ");
    cads_cli_write_uint(s, l09.total);
    cads_cli_write(s, " Byte bestaetigt\r\n");
    uint64_t end = (l09.run == L09_SENDING) ? cads_hal_ticks_us() : l09.t_end_us;
    if(l09.t0_us && end > l09.t0_us) {
        uint64_t span = end - l09.t0_us;
        cads_cli_write(s, "goodput:   ");
        cads_cli_write_uint(s, (uint32_t)(((uint64_t)l09.acked * 8000u) / span));
        cads_cli_write(s, " kbit/s in ");
        cads_cli_write_uint(s, (uint32_t)(span / 1000u));
        cads_cli_write(s, " ms\r\n");
    }
    cads_cli_write(s, "rtt:       ");
    if(l09.rtt_n) {
        cads_cli_write_uint(s, l09.rtt_min);
        cads_cli_write(s, " / ");
        cads_cli_write_uint(s, (uint32_t)(l09.rtt_sum / l09.rtt_n));
        cads_cli_write(s, " / ");
        cads_cli_write_uint(s, l09.rtt_max);
        cads_cli_write(s, " us min/avg/max (");
        cads_cli_write_uint(s, l09.rtt_n);
        cads_cli_write(s, " Proben)\r\n");
    } else {
        cads_cli_write(s, "keine Probe\r\n");
    }
    cads_cli_write(s, "acks:      ");
    cads_cli_write_uint(s, l09.acks);
    cads_cli_write(s, " neu, ");
    cads_cli_write_uint(s, l09.dupacks);
    cads_cli_write(s, " doppelt\r\nwiederholt: ");
    cads_cli_write_uint(s, l09.fast);
    cads_cli_write(s, " Fast Retransmit, ");
    cads_cli_write_uint(s, l09.rto);
    cads_cli_write(s, " Timeout (RTO)\r\nverworfen: ");
    cads_cli_write_uint(s, l09.dropper.dropped);
    cads_cli_write(s, " von ");
    cads_cli_write_uint(s, l09.dropper.seen);
    cads_cli_write(s, " Datensegmenten, Einstellung ");
    l09_write_pct(s, l09_loss_ppm());
    cads_cli_write(s, "\r\ntrace:     ");
    cads_cli_write_uint(s, l09.trace_len);
    cads_cli_write(s, " Eintraege");
    if(l09.trace_lost) {
        cads_cli_write(s, ", ");
        cads_cli_write_uint(s, l09.trace_lost);
        cads_cli_write(s, " nicht mehr aufgezeichnet (Ring voll)");
    }
    cads_cli_write(s, "\r\n");
}

static void l09_cmd_trace(cads_cli_session_t* s, int argc, char* argv[]) {
    uint32_t from = 0u;
    if(argc >= 1 && !cads_str_to_uint(argv[0], &from, NULL)) {
        cads_cli_write(s, "? Aufruf: lab 09 trace [ab]\r\n");
        return;
    }
    if(l09.pcb) l09_flush_pending();
    if(from == 0u) cads_cli_write(s, RNLAB_L09_TRACE_HEADER "\r\n");
    uint32_t to = from + L09_TRACE_PAGE;
    if(to > l09.trace_len) to = l09.trace_len;
    char line[96];
    for(uint32_t i = from; i < to; i++) {
        if(rnlab_l09_trace_format(&l09_trace[i], line, sizeof(line)) == 0u) continue;
        cads_cli_write(s, line);
        cads_cli_write(s, "\r\n");
    }
    if(to < l09.trace_len) {
        cads_cli_write(s, "# weiter: lab 09 trace ");
        cads_cli_write_uint(s, to);
    } else {
        cads_cli_write(s, "# ende, ");
        cads_cli_write_uint(s, l09.trace_len);
        cads_cli_write(s, " Eintraege");
    }
    cads_cli_write(s, "\r\n");
}

static void l09_cmd_calc(cads_cli_session_t* s, int argc, char* argv[]) {
    uint32_t rtt = l09.rtt_n ? (uint32_t)(l09.rtt_sum / l09.rtt_n) : 0u;
    if(argc >= 1 && (!cads_str_to_uint(argv[0], &rtt, NULL) || rtt == 0u)) {
        cads_cli_write(s, "? Aufruf: lab 09 calc [rtt_us]\r\n");
        return;
    }
    uint32_t ppm = l09_loss_ppm();
    if(rtt == 0u || ppm == 0u) {
        cads_cli_write(s, "? braucht RTT (Messung oder Argument) und 'lab 09 loss'\r\n");
        return;
    }
    uint32_t mss = TCP_MSS;
    cads_cli_write(s, "mathis:    ");
    cads_cli_write_uint(s, (uint32_t)(rnlab_l09_mathis_bps(mss, rtt, ppm) / 1000u));
    cads_cli_write(s, " kbit/s  (MSS ");
    cads_cli_write_uint(s, mss);
    cads_cli_write(s, " B, RTT ");
    cads_cli_write_uint(s, rtt);
    cads_cli_write(s, " us, p = ");
    l09_write_pct(s, ppm);
    cads_cli_write(s, ")\r\n");
}

static void l09_help(cads_cli_session_t* s) {
    cads_cli_write(s,
        "lab 09 cc start <ip> <port> [bytes]  an rnlab.py tcp-recv senden (300000)\r\n"
        "lab 09 cc stop                       Verbindung abbrechen\r\n"
        "lab 09 loss <n> | p <prozent> | off  jedes n-te Datensegment / zufaellig\r\n"
        "lab 09 status                        Fortschritt, RTT, Wiederholungen\r\n"
        "lab 09 trace [ab]                    cwnd/ssthresh je ACK als CSV (Seiten)\r\n"
        "lab 09 calc [rtt_us]                 Mathis-Schaetzung fuer diese Einstellung\r\n");
}

void rnlab_l09_command(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc >= 2 && cads_str_equal(argv[0], "cc")) {
        if(cads_str_equal(argv[1], "start")) {
            l09_cmd_start(session, argc - 2, argv + 2);
        } else if(cads_str_equal(argv[1], "stop")) {
            l09_cmd_stop(session);
        } else {
            l09_help(session);
        }
    } else if(argc >= 1 && cads_str_equal(argv[0], "loss")) {
        l09_cmd_loss(session, argc - 1, argv + 1);
    } else if(argc >= 1 && cads_str_equal(argv[0], "status")) {
        l09_cmd_status(session);
    } else if(argc >= 1 && cads_str_equal(argv[0], "trace")) {
        l09_cmd_trace(session, argc - 1, argv + 1);
    } else if(argc >= 1 && cads_str_equal(argv[0], "calc")) {
        l09_cmd_calc(session, argc - 1, argv + 1);
    } else {
        l09_help(session);
    }
}

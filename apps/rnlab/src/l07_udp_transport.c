/*
 * CaDS Zero - rnlab L07 (UDP-Transport): board integration.
 *
 * A UDP sink on port 7007 for tools/rnlab.py udp-send: every datagram's
 * sequence number goes through the tracker (l07_udp_transport_logic.c), and
 * `lab 07 udp stats` sets the transport-level view (gaps in the numbering)
 * next to what the layers below counted themselves - pbuf allocation
 * failures in the driver and frames the Ethernet DMA had no descriptor for.
 * Where the two disagree, the loss happened somewhere no counter looks,
 * e.g. while the display owns PA7 (`lab 07 blit`).
 *
 * Optional echo: every datagram goes back unchanged to the sender's address
 * on a chosen port, so `rnlab.py udp-recv <port>` on the same computer sees
 * the round trip - with one clock on both ends its "one_way_delay_ms" is
 * then the RTT.
 */

#include "rnlab/rnlab_lesson.h"

#include "cads/net/net.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "hal_eth_mac.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "l07_udp_transport_logic.h"

/* Pixels for `lab 07 blit`: one small rectangle, re-sent over and over. The
 * display DMA reads it, so it must stay in SRAM (never RNLAB_CCM). */
#define L07_BLIT_W 32u
#define L07_BLIT_H 8u
#define L07_BLIT_MAX_MS 10000u

typedef struct {
    struct udp_pcb* pcb;
    rnlab_seq_tracker_t tracker;
    uint32_t malformed;
    uint32_t bytes;
    uint64_t first_us;
    uint64_t last_us;
    uint16_t echo_port; /* 0 = no echo */
    uint32_t echo_errors;
    /* Driver/MAC counters at `start`/`reset`, to report only this run's share.
     * DMAMFBOCR clears on read, so the MAC's are accumulated, not snapshotted. */
    uint32_t rx_dropped_base;
    uint32_t mac_no_desc;
    uint32_t mac_fifo;
} l07_state_t;

static l07_state_t l07;
static uint16_t l07_blit_pixels[L07_BLIT_W * L07_BLIT_H];

static void l07_mac_accumulate(void) {
    uint32_t no_desc = 0u, fifo = 0u;
    cads_hal_eth_mac_missed_frames(&no_desc, &fifo);
    l07.mac_no_desc += no_desc;
    l07.mac_fifo += fifo;
}

static void l07_reset_counters(void) {
    cads_net_status_t status;
    cads_net_status(&status);
    rnlab_seq_reset(&l07.tracker);
    l07.malformed = 0u;
    l07.bytes = 0u;
    l07.first_us = 0u;
    l07.last_us = 0u;
    l07.echo_errors = 0u;
    l07.rx_dropped_base = status.rx_dropped;
    cads_hal_eth_mac_missed_frames(NULL, NULL); /* discard what happened before */
    l07.mac_no_desc = 0u;
    l07.mac_fifo = 0u;
}

static void l07_recv(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port) {
    (void)arg;
    (void)port;
    uint64_t now = cads_hal_ticks_us();
    if(l07.first_us == 0u) l07.first_us = now;
    l07.last_us = now;
    l07.bytes += p->tot_len;

    /* The header may straddle two pool buffers of a chained pbuf; copy it out
     * rather than reading p->payload directly. */
    uint8_t header[RNLAB_L07_HEADER_LEN];
    uint16_t got = pbuf_copy_partial(p, header, sizeof(header), 0u);
    uint32_t seq;
    if(rnlab_l07_parse_header(header, got, &seq, NULL)) {
        (void)rnlab_seq_update(&l07.tracker, seq);
    } else {
        l07.malformed++;
    }

    if(l07.echo_port != 0u) {
        /* udp_sendto() prepends the headers in the space the receive path
         * already stripped from this very pbuf - no copy. */
        if(udp_sendto(pcb, p, addr, l07.echo_port) != ERR_OK) l07.echo_errors++;
    }
    pbuf_free(p);
}

static void l07_write_bp(cads_cli_session_t* s, uint32_t bp) {
    /* basis points -> "12.34" */
    cads_cli_write_uint(s, bp / 100u);
    cads_cli_write(s, ".");
    if(bp % 100u < 10u) cads_cli_write(s, "0");
    cads_cli_write_uint(s, bp % 100u);
}

static void l07_cmd_start(cads_cli_session_t* s, int argc, char* argv[]) {
    uint16_t echo = 0u;
    if(argc >= 2 && cads_str_equal(argv[0], "echo")) {
        uint32_t port;
        if(!cads_str_to_uint(argv[1], &port, NULL) || port == 0u || port > 65535u) {
            cads_cli_write(s, "? Aufruf: lab 07 udp start [echo <port>]\r\n");
            return;
        }
        echo = (uint16_t)port;
    }
    if(!l07.pcb) {
        struct udp_pcb* pcb = udp_new();
        if(!pcb) {
            cads_cli_write(s, "! kein UDP-PCB frei (MEMP_NUM_UDP_PCB)\r\n");
            return;
        }
        if(udp_bind(pcb, IP_ANY_TYPE, RNLAB_L07_PORT) != ERR_OK) {
            udp_remove(pcb);
            cads_cli_write(s, "! Port 7007 belegt\r\n");
            return;
        }
        udp_recv(pcb, l07_recv, NULL);
        l07.pcb = pcb;
    }
    l07.echo_port = echo;
    l07_reset_counters();
    cads_cli_write(s, "UDP-Senke auf Port 7007");
    if(echo) {
        cads_cli_write(s, ", Echo an Absender Port ");
        cads_cli_write_uint(s, echo);
    }
    cads_cli_write(s, "\r\n");
}

static void l07_cmd_stop(cads_cli_session_t* s) {
    if(l07.pcb) {
        udp_remove(l07.pcb);
        l07.pcb = NULL;
    }
    cads_cli_write(s, "UDP-Senke gestoppt (Zaehler bleiben bis start/reset)\r\n");
}

static void l07_cmd_stats(cads_cli_session_t* s) {
    const rnlab_seq_tracker_t* t = &l07.tracker;
    cads_net_status_t status;
    cads_net_status(&status);
    l07_mac_accumulate();

    cads_cli_write(s, l07.pcb ? "senke:      aktiv, Port 7007\r\n" : "senke:      aus\r\n");
    cads_cli_write(s, "empfangen:  ");
    cads_cli_write_uint(s, t->received);
    cads_cli_write(s, " Datagramme, ");
    cads_cli_write_uint(s, l07.bytes);
    cads_cli_write(s, " Byte\r\nerwartet:   ");
    cads_cli_write_uint(s, rnlab_seq_expected(t));
    if(t->started) {
        cads_cli_write(s, " (Seq ");
        cads_cli_write_uint(s, t->first);
        cads_cli_write(s, "..");
        cads_cli_write_uint(s, t->next - 1u);
        cads_cli_write(s, ")");
    }
    cads_cli_write(s, "\r\nverloren:   ");
    cads_cli_write_uint(s, t->lost);
    cads_cli_write(s, " (");
    l07_write_bp(s, rnlab_seq_loss_bp(t));
    cads_cli_write(s, " %)\r\numsortiert: ");
    cads_cli_write_uint(s, t->reordered);
    cads_cli_write(s, "\r\nduplikate:  ");
    cads_cli_write_uint(s, t->duplicates);
    cads_cli_write(s, "\r\nveraltet:   ");
    cads_cli_write_uint(s, t->stale);
    cads_cli_write(s, "\r\nfehlerhaft: ");
    cads_cli_write_uint(s, l07.malformed);

    uint64_t span_us = l07.last_us - l07.first_us;
    cads_cli_write(s, "\r\ndauer:      ");
    cads_cli_write_uint(s, (uint32_t)(span_us / 1000u));
    cads_cli_write(s, " ms");
    if(span_us > 0u && t->received > 1u) {
        cads_cli_write(s, ", ");
        cads_cli_write_uint(s, (uint32_t)(((uint64_t)(t->received - 1u) * 1000000u) / span_us));
        cads_cli_write(s, " pps, ");
        cads_cli_write_uint(s, (uint32_t)(((uint64_t)l07.bytes * 8000u) / span_us));
        cads_cli_write(s, " kbit/s Nutzdaten");
    }
    if(l07.echo_port) {
        cads_cli_write(s, "\r\necho:       Port ");
        cads_cli_write_uint(s, l07.echo_port);
        cads_cli_write(s, ", Sendefehler ");
        cads_cli_write_uint(s, l07.echo_errors);
    }
    cads_cli_write(s, "\r\ntreiber:    ");
    cads_cli_write_uint(s, status.rx_dropped - l07.rx_dropped_base);
    cads_cli_write(s, " verworfen (kein pbuf/Hook)\r\nmac:        ");
    cads_cli_write_uint(s, l07.mac_no_desc);
    cads_cli_write(s, " ohne RX-Deskriptor, ");
    cads_cli_write_uint(s, l07.mac_fifo);
    cads_cli_write(s, " FIFO-Ueberlauf\r\n");
}

static void l07_cmd_blit(cads_cli_session_t* s, int argc, char* argv[]) {
    uint32_t ms = 2000u;
    if(argc >= 1 && (!cads_str_to_uint(argv[0], &ms, NULL) || ms == 0u || ms > L07_BLIT_MAX_MS)) {
        cads_cli_write(s, "? Aufruf: lab 07 blit [ms]   (1..10000, Standard 2000)\r\n");
        return;
    }
    for(uint32_t i = 0u; i < L07_BLIT_W * L07_BLIT_H; i++) {
        l07_blit_pixels[i] = (i & 8u) ? 0xFD20u : 0x0000u; /* orange/black stripes */
    }
    /* Every blit claims PA7 for the display: the MAC's receiver is stopped
     * for the transfer and frames arriving meanwhile never reach it. Between
     * blits the network is polled, so the RX ring itself does not overflow -
     * whatever is missing afterwards went missing on the wire side of PA7. */
    uint32_t start = cads_hal_ticks_ms();
    uint32_t last_dot = start;
    uint32_t blits = 0u;
    while(cads_hal_ticks_ms() - start < ms) {
        cads_hal_display_blit(CADS_DISPLAY_WIDTH - L07_BLIT_W, 0u, L07_BLIT_W, L07_BLIT_H, l07_blit_pixels);
        cads_hal_display_wait();
        blits++;
        cads_net_poll();
        /* A sign of life every 300 ms: rnlab.py lab takes 0.5 s of silence
         * as the end of the answer. */
        if(cads_hal_ticks_ms() - last_dot >= 300u) {
            last_dot = cads_hal_ticks_ms();
            cads_cli_write(s, ".");
        }
    }
    cads_cli_write(s, "\r\nblit: ");
    cads_cli_write_uint(s, blits);
    cads_cli_write(s, " Rechtecke in ");
    cads_cli_write_uint(s, cads_hal_ticks_ms() - start);
    cads_cli_write(s, " ms (je 32x8 Pixel, oben rechts)\r\n");
}

static void l07_help(cads_cli_session_t* s) {
    cads_cli_write(s,
        "lab 07 udp start [echo <port>]  UDP-Senke Port 7007 (Zaehler auf 0)\r\n"
        "lab 07 udp stats                Verlust/Reihenfolge/Duplikate + Treiber\r\n"
        "lab 07 udp reset                Zaehler auf 0 (vor jedem udp-send-Lauf)\r\n"
        "lab 07 udp stop                 Senke schliessen\r\n"
        "lab 07 blit [ms]                Display-Last: PA7 wiederholt belegen\r\n");
}

void rnlab_l07_command(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc >= 2 && cads_str_equal(argv[0], "udp")) {
        const char* sub = argv[1];
        if(cads_str_equal(sub, "start")) {
            l07_cmd_start(session, argc - 2, argv + 2);
        } else if(cads_str_equal(sub, "stats")) {
            l07_cmd_stats(session);
        } else if(cads_str_equal(sub, "reset")) {
            l07_reset_counters();
            cads_cli_write(session, "Zaehler auf 0\r\n");
        } else if(cads_str_equal(sub, "stop")) {
            l07_cmd_stop(session);
        } else {
            l07_help(session);
        }
        return;
    }
    if(argc >= 1 && cads_str_equal(argv[0], "blit")) {
        l07_cmd_blit(session, argc - 1, argv + 1);
        return;
    }
    l07_help(session);
}

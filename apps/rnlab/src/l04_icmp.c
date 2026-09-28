/*
 * CaDS Zero - rnlab L04 (ICMP): board integration.
 *
 * `lab 04 <cmd> [args]` lands in rnlab_l04_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l04_icmp_logic.c.
 *
 * HOW THE OWN ECHO RESPONDER TAKES OVER FROM lwIP
 * ------------------------------------------------
 * ip4_input() hands every packet to raw_input() BEFORE it dispatches by
 * protocol. A raw pcb for IP_PROTO_ICMP therefore sees each ICMP packet
 * first, with p->payload still at the IP header. Its recv callback decides:
 *   return 1 - "eaten": the callback owns p (must pbuf_free it) and lwIP
 *              stops here, so icmp_input() - lwIP's built-in responder -
 *              never runs for this packet;
 *   return 0 - not ours: p must be left untouched, lwIP carries on and
 *              icmp_input() answers as if we were not there.
 * `lab 04 echo on` creates that pcb, `off` removes it, and lwIP answers
 * again. The replies are told apart on the wire by their TTL: ours 64, lwIP's
 * ICMP_TTL 255 - `ping` prints it for every reply.
 *
 * The same pcb also catches the echo REPLIES to `lab 04 ping`, because this
 * firmware has exactly one raw pcb (MEMP_NUM_RAW_PCB 1, lwipopts.h): while
 * the responder is on, cads_net_ping() (explorer, `lab 03 probe`) finds no
 * free pcb, so `lab 04 ping` reuses ours instead of asking for a second.
 */

#include "rnlab/rnlab_lesson.h"

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "l04_icmp_logic.h"
#include "rnlab_args_logic.h"
/* opt.h before the rest: def.h/arch.h would otherwise define a fallback
 * LWIP_DECLARE_MEMORY_ALIGNED that lwipopts.h then redefines. */
#include "lwip/opt.h"
#include "lwip/ip.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/timeouts.h"

#define L04_TTL             64u
#define L04_DELAY_SLOTS     4u      /* delayed replies in flight at once */
#define L04_DELAY_MAX_MS    2000u
#define L04_PING_MAX_COUNT  200u
#define L04_PING_MAX_DATA   1472u   /* 1500 - 20 IP - 8 ICMP: one frame, no fragments */
#define L04_PING_TIMEOUT_US 1000000u
#define L04_PING_GAP_US     20000u  /* request spacing, like `ping -i 0.02` */
#define L04_GAP_MAX_SAMPLES 500u

static struct raw_pcb* l04_pcb;     /* our responder; NULL = lwIP answers */
static uint32_t l04_delay_ms;
static uint32_t l04_loss_pct;
static uint32_t l04_rand_state = 0x4C303421u;

/* Responder counters. Everything here runs on the console task, inside
 * cads_net_poll() or between two calls of it - never concurrently. */
static uint32_t l04_requests;
static uint32_t l04_replied;
static uint32_t l04_lost;      /* dropped on purpose (`loss`) */
static uint32_t l04_bad;       /* too short, wrong code, bad checksum */
static uint32_t l04_overflow;  /* no delay slot or no heap for the reply */
static rnlab_l04_stats_t l04_proc; /* recv callback entry -> reply handed to the MAC */

typedef struct {
    struct pbuf* reply; /* ready ICMP reply (no IP header), NULL = slot free */
    ip_addr_t dst;
} l04_pending_t;
static l04_pending_t l04_pending[L04_DELAY_SLOTS];

/* The one board-initiated ping in progress (`lab 04 ping`). */
static struct {
    bool active;
    uint16_t id;
    uint16_t seq;
    bool got_reply;
    uint64_t sent_us;
    uint32_t rtt_us;
} l04_ping;
static rnlab_l04_stats_t l04_rtt;
static uint32_t l04_rtt_target;
static uint32_t l04_rtt_size;

/* `lab 04 pollgap`: time between two runs of lwIP's timer check, i.e. between
 * two cads_net_poll() calls of whatever loop owns the console task. */
static struct {
    bool running;
    uint32_t remaining;
    uint64_t last_us;
    rnlab_l04_stats_t stats;
} l04_gap;

/* --- responder ----------------------------------------------------------- */

static void l04_send_reply(struct pbuf* reply, const ip_addr_t* dst) {
    if(l04_pcb && raw_sendto(l04_pcb, reply, dst) == ERR_OK) l04_replied++;
    pbuf_free(reply);
}

static void l04_delayed_send(void* arg) {
    l04_pending_t* slot = (l04_pending_t*)arg;
    if(!slot->reply) return;
    struct pbuf* reply = slot->reply;
    slot->reply = NULL;
    l04_send_reply(reply, &slot->dst);
}

static void l04_cancel_pending(void) {
    for(uint32_t i = 0; i < L04_DELAY_SLOTS; i++) {
        sys_untimeout(l04_delayed_send, &l04_pending[i]);
        if(l04_pending[i].reply) pbuf_free(l04_pending[i].reply);
        l04_pending[i].reply = NULL;
    }
}

/* Echo request for our unicast address: build the reply from a copy, then
 * send, delay or drop it. Returns 1 in every case - once we have looked at
 * a request to us, lwIP must not answer it a second time. */
static u8_t l04_answer(struct pbuf* p, u16_t iphdr_len, const ip_addr_t* addr, uint64_t t_in) {
    l04_requests++;

    if(rnlab_l04_should_drop(&l04_rand_state, l04_loss_pct)) {
        l04_lost++;
        pbuf_free(p);
        return 1u;
    }

    u16_t icmp_len = (u16_t)(p->tot_len - iphdr_len);
    /* PBUF_IP reserves room for the IP and Ethernet headers in front, so
     * raw_sendto() can prepend them without a second allocation. PBUF_RAM =
     * one contiguous buffer, which is what the byte-level logic wants. */
    struct pbuf* reply = pbuf_alloc(PBUF_IP, icmp_len, PBUF_RAM);
    if(!reply) {
        l04_overflow++;
        pbuf_free(p);
        return 1u;
    }
    pbuf_copy_partial(p, reply->payload, icmp_len, iphdr_len);
    pbuf_free(p);

    if(!rnlab_l04_echo_to_reply((uint8_t*)reply->payload, icmp_len)) {
        l04_bad++;
        pbuf_free(reply);
        return 1u;
    }

    if(l04_delay_ms == 0u) {
        l04_send_reply(reply, addr);
        rnlab_l04_stats_add(&l04_proc, (uint32_t)(cads_hal_ticks_us() - t_in));
        return 1u;
    }

    /* Never block in here (we are deep inside cads_net_poll()): park the
     * reply and let lwIP's timer list send it. Its resolution is the poll
     * interval, which is itself part of what this lesson measures. */
    for(uint32_t i = 0; i < L04_DELAY_SLOTS; i++) {
        if(!l04_pending[i].reply) {
            l04_pending[i].reply = reply;
            ip_addr_copy(l04_pending[i].dst, *addr);
            sys_timeout(l04_delay_ms, l04_delayed_send, &l04_pending[i]);
            return 1u;
        }
    }
    l04_overflow++;
    pbuf_free(reply);
    return 1u;
}

static u8_t l04_recv(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
    (void)arg;
    (void)pcb;
    uint64_t t_in = cads_hal_ticks_us();

    u16_t iphdr_len = ip_current_header_tot_len();
    uint8_t hdr[RNLAB_L04_ICMP_HEADER_LEN];
    if(p->tot_len < iphdr_len + sizeof(hdr)) return 0u; /* lwIP counts and drops it */
    pbuf_copy_partial(p, hdr, sizeof(hdr), iphdr_len);

    if(hdr[0] == RNLAB_L04_ICMP_ECHO_REPLY && l04_ping.active) {
        uint16_t id = (uint16_t)((hdr[4] << 8) | hdr[5]);
        uint16_t seq = (uint16_t)((hdr[6] << 8) | hdr[7]);
        if(id == l04_ping.id && seq == l04_ping.seq) {
            l04_ping.rtt_us = (uint32_t)(t_in - l04_ping.sent_us);
            l04_ping.got_reply = true;
            pbuf_free(p);
            return 1u;
        }
        return 0u;
    }

    /* Only requests to our own unicast address. Broadcast/multicast pings
     * go back to lwIP, which ignores them (LWIP_BROADCAST_PING 0) - the same
     * policy, so switching the responder on changes nothing there. The pcb
     * of a `lab 04 ping` with the responder off also lands here: then lwIP
     * keeps answering. */
    if(hdr[0] != RNLAB_L04_ICMP_ECHO_REQUEST || !l04_pcb) return 0u;
    if(ip4_addr_isbroadcast(ip4_current_dest_addr(), ip_current_netif()) ||
       ip4_addr_ismulticast(ip4_current_dest_addr())) {
        return 0u;
    }
    return l04_answer(p, iphdr_len, addr, t_in);
}

/* --- output helpers ----------------------------------------------------- */

/* Microseconds as "m.uuu" milliseconds, the unit `ping` prints. */
static void l04_write_ms(cads_cli_session_t* session, uint32_t us) {
    char frac[4];
    cads_cli_write_uint(session, us / 1000u);
    cads_cli_write(session, ".");
    cads_fmt_uint_pad(frac, sizeof(frac), us % 1000u, 3u, '0');
    cads_cli_write(session, frac);
}

static void l04_write_summary(cads_cli_session_t* session, const rnlab_l04_stats_t* s) {
    if(s->received == 0u) {
        cads_cli_write(session, "keine Messwerte\r\n");
        return;
    }
    cads_cli_write(session, "min/avg/max/stddev = ");
    l04_write_ms(session, s->min_us);
    cads_cli_write(session, "/");
    l04_write_ms(session, rnlab_l04_stats_avg_us(s));
    cads_cli_write(session, "/");
    l04_write_ms(session, s->max_us);
    cads_cli_write(session, "/");
    l04_write_ms(session, rnlab_l04_stats_stddev_us(s));
    cads_cli_write(session, " ms\r\n");
}

static void l04_write_histogram(cads_cli_session_t* session, const rnlab_l04_stats_t* s) {
    static const char* const labels[RNLAB_L04_HIST_BINS] = {
        "     <0.25", " 0.25-0.5 ", "  0.5-1   ", "    1-2   ", "    2-4   ",
        "    4-8   ", "    8-16  ", "   16-32  ", "   32-64  ", "   >=64   ",
    };
    uint32_t peak = 0u;
    for(uint32_t i = 0; i < RNLAB_L04_HIST_BINS; i++) {
        if(s->hist[i] > peak) peak = s->hist[i];
    }
    if(peak == 0u) return;
    cads_cli_write(session, "ms         Anzahl\r\n");
    for(uint32_t i = 0; i < RNLAB_L04_HIST_BINS; i++) {
        char bar[41];
        uint32_t width = (s->hist[i] * 40u + peak - 1u) / peak; /* round up: 1 sample shows */
        for(uint32_t k = 0; k < width; k++) bar[k] = '#';
        bar[width] = '\0';
        cads_cli_write(session, labels[i]);
        cads_cli_write(session, "|");
        cads_cli_write(session, bar);
        cads_cli_write(session, " ");
        cads_cli_write_uint(session, s->hist[i]);
        cads_cli_write(session, "\r\n");
    }
}

static void l04_write_ip(cads_cli_session_t* session, uint32_t ip) {
    char text[16];
    cads_fmt_ipv4(text, sizeof(text), ip);
    cads_cli_write(session, text);
}

/* --- commands ------------------------------------------------------------ */

static void l04_cmd_echo(cads_cli_session_t* session, bool on) {
    if(on && !l04_pcb) {
        struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
        if(!pcb) {
            cads_cli_write(session, "? kein RAW-PCB frei (MEMP_NUM_RAW_PCB)\r\n");
            return;
        }
        pcb->ttl = L04_TTL;
        raw_bind(pcb, IP_ADDR_ANY);
        raw_recv(pcb, l04_recv, NULL);
        l04_pcb = pcb;
    } else if(!on && l04_pcb) {
        l04_cancel_pending();
        raw_remove(l04_pcb);
        l04_pcb = NULL;
    }
    cads_cli_write(session, l04_pcb ? "echo: eigener Responder (TTL 64)\r\n"
                                    : "echo: lwIP-Responder (TTL 255)\r\n");
}

static void l04_cmd_ping(cads_cli_session_t* session, uint32_t ip, uint32_t count, uint32_t size) {
    struct raw_pcb* pcb = l04_pcb;
    bool own_pcb = false;
    if(!pcb) {
        pcb = raw_new(IP_PROTO_ICMP);
        if(!pcb) {
            cads_cli_write(session, "? kein RAW-PCB frei (MEMP_NUM_RAW_PCB)\r\n");
            return;
        }
        raw_bind(pcb, IP_ADDR_ANY);
        raw_recv(pcb, l04_recv, NULL);
        own_pcb = true;
    }

    static uint16_t next_id = 0x4C04u;
    l04_ping.id = ++next_id;
    rnlab_l04_stats_init(&l04_rtt);
    l04_rtt_target = ip;
    l04_rtt_size = size;

    ip_addr_t dst;
    ip_addr_set_ip4_u32(&dst, lwip_htonl(ip));

    for(uint32_t seq = 1u; seq <= count; seq++) {
        struct pbuf* p = pbuf_alloc(PBUF_IP, (u16_t)(RNLAB_L04_ICMP_HEADER_LEN + size), PBUF_RAM);
        if(!p) break;
        rnlab_l04_build_echo_request((uint8_t*)p->payload, l04_ping.id, (uint16_t)seq, size);

        l04_ping.seq = (uint16_t)seq;
        l04_ping.got_reply = false;
        l04_ping.active = true;
        l04_ping.sent_us = cads_hal_ticks_us();
        err_t sent = raw_sendto(pcb, p, &dst);
        pbuf_free(p);
        l04_rtt.sent++;

        /* Busy-poll the stack without any sleep: the reply is timestamped the
         * moment the MAC has it in its ring, so this RTT is the wire +
         * the Mac's answer + our own receive path - NOT the menu loop's
         * polling interval. Compare with `ping` from the Mac. */
        while(sent == ERR_OK && !l04_ping.got_reply &&
              cads_hal_ticks_us() - l04_ping.sent_us < L04_PING_TIMEOUT_US) {
            cads_net_poll();
        }
        l04_ping.active = false;
        if(l04_ping.got_reply) rnlab_l04_stats_add(&l04_rtt, l04_ping.rtt_us);

        while(seq < count && cads_hal_ticks_us() - l04_ping.sent_us < L04_PING_GAP_US) {
            cads_net_poll();
        }
    }

    if(own_pcb) raw_remove(pcb);

    cads_cli_write(session, "ping ");
    l04_write_ip(session, ip);
    cads_cli_write(session, ": ");
    cads_cli_write_uint(session, l04_rtt.sent);
    cads_cli_write(session, " gesendet, ");
    cads_cli_write_uint(session, l04_rtt.received);
    cads_cli_write(session, " empfangen, ");
    cads_cli_write_uint(session, rnlab_l04_stats_loss_pct(&l04_rtt));
    cads_cli_write(session, " % Verlust\r\n");
    l04_write_summary(session, &l04_rtt);
    l04_write_histogram(session, &l04_rtt);
}

static void l04_gap_tick(void* arg) {
    (void)arg;
    uint64_t now = cads_hal_ticks_us();
    if(l04_gap.last_us != 0u) rnlab_l04_stats_add(&l04_gap.stats, (uint32_t)(now - l04_gap.last_us));
    l04_gap.last_us = now;
    if(--l04_gap.remaining > 0u) {
        /* 1 ms, not 0: a 0 ms timeout re-added from inside sys_check_timeouts()
         * would be due again in the same pass and never let it return. So each
         * sample is "the first poll at least 1 ms after the previous one". */
        sys_timeout(1u, l04_gap_tick, NULL);
    } else {
        l04_gap.running = false;
    }
}

static void l04_cmd_pollgap(cads_cli_session_t* session, int argc, char* argv[]) {
    if(l04_gap.running) {
        cads_cli_write(session, "pollgap laeuft noch, ");
        cads_cli_write_uint(session, l04_gap.stats.received);
        cads_cli_write(session, " Werte\r\n");
        return;
    }
    uint32_t n = 0u;
    if(argc == 2) {
        const char* end = NULL;
        if(!cads_str_to_uint(argv[1], &n, &end) || *end != '\0' || n < 2u || n > L04_GAP_MAX_SAMPLES) {
            cads_cli_write(session, "? Aufruf: lab 04 pollgap [2..500]\r\n");
            return;
        }
    }
    if(n == 0u) {
        /* Without a count: show the last result. */
        cads_cli_write(session, "Abstand zweier Poll-Durchlaeufe (>= 1 ms):\r\n");
        l04_write_summary(session, &l04_gap.stats);
        l04_write_histogram(session, &l04_gap.stats);
        return;
    }
    rnlab_l04_stats_init(&l04_gap.stats);
    l04_gap.remaining = n + 1u; /* first tick only sets the reference */
    l04_gap.last_us = 0u;
    l04_gap.running = true;
    sys_timeout(1u, l04_gap_tick, NULL);
    cads_cli_write(session, "pollgap gestartet - Ergebnis mit 'lab 04 pollgap'\r\n");
}

static void l04_cmd_stats(cads_cli_session_t* session) {
    cads_cli_write(session, l04_pcb ? "echo:      eigener Responder, TTL 64\r\n"
                                    : "echo:      lwIP (icmp.c), TTL 255\r\n");
    cads_cli_write(session, "delay:     ");
    cads_cli_write_uint(session, l04_delay_ms);
    cads_cli_write(session, " ms, loss: ");
    cads_cli_write_uint(session, l04_loss_pct);
    cads_cli_write(session, " %\r\nanfragen:  ");
    cads_cli_write_uint(session, l04_requests);
    cads_cli_write(session, ", beantwortet ");
    cads_cli_write_uint(session, l04_replied);
    cads_cli_write(session, ", verworfen ");
    cads_cli_write_uint(session, l04_lost);
    cads_cli_write(session, ", fehlerhaft ");
    cads_cli_write_uint(session, l04_bad);
    cads_cli_write(session, ", ueberlauf ");
    cads_cli_write_uint(session, l04_overflow);
    cads_cli_write(session, "\r\nbearbeitung ");
    l04_write_summary(session, &l04_proc);
    if(l04_rtt.sent) {
        cads_cli_write(session, "letzter ping ");
        l04_write_ip(session, l04_rtt_target);
        cads_cli_write(session, ", ");
        cads_cli_write_uint(session, l04_rtt_size);
        cads_cli_write(session, " B: ");
        l04_write_summary(session, &l04_rtt);
    }
}

static void l04_reset(void) {
    l04_requests = l04_replied = l04_lost = l04_bad = l04_overflow = 0u;
    rnlab_l04_stats_init(&l04_proc);
    rnlab_l04_stats_init(&l04_rtt);
}

static void l04_help(cads_cli_session_t* session) {
    cads_cli_write(session,
        "lab 04 echo on|off          eigener Echo-Responder (RAW-PCB) an/aus\r\n"
        "lab 04 delay <ms>           Antwort verzoegern (0..2000)\r\n"
        "lab 04 loss <p>             Anfragen mit p % verwerfen (0..100)\r\n"
        "lab 04 ping <ip> [n] [size] n Pings (1..200), size B Daten (0..1472)\r\n"
        "lab 04 pollgap [n]          Poll-Abstand messen / anzeigen\r\n"
        "lab 04 stats                Zaehler und letzte Messungen\r\n"
        "lab 04 reset                Zaehler auf 0\r\n");
}

static bool l04_parse_bounded(const char* text, uint32_t max, uint32_t* value) {
    const char* end = NULL;
    return cads_str_to_uint(text, value, &end) && *end == '\0' && *value <= max;
}

void rnlab_l04_command(cads_cli_session_t* session, int argc, char* argv[]) {
    static bool initialised = false;
    if(!initialised) {
        initialised = true;
        l04_reset();
        rnlab_l04_stats_init(&l04_gap.stats);
    }

    if(argc == 0 || cads_str_equal(argv[0], "stats")) {
        l04_cmd_stats(session);
        return;
    }
    if(cads_str_equal(argv[0], "help")) {
        l04_help(session);
        return;
    }
    if(cads_str_equal(argv[0], "reset")) {
        l04_reset();
        cads_cli_write(session, "Zaehler geloescht\r\n");
        return;
    }
    if(argc == 2 && cads_str_equal(argv[0], "echo") &&
       (cads_str_equal(argv[1], "on") || cads_str_equal(argv[1], "off"))) {
        l04_cmd_echo(session, cads_str_equal(argv[1], "on"));
        return;
    }
    uint32_t value;
    if(argc == 2 && cads_str_equal(argv[0], "delay") && l04_parse_bounded(argv[1], L04_DELAY_MAX_MS, &value)) {
        l04_delay_ms = value;
        cads_cli_write(session, "delay ");
        cads_cli_write_uint(session, value);
        cads_cli_write(session, l04_pcb ? " ms\r\n" : " ms (wirkt erst mit 'lab 04 echo on')\r\n");
        return;
    }
    if(argc == 2 && cads_str_equal(argv[0], "loss") && rnlab_l04_parse_percent(argv[1], &value)) {
        l04_loss_pct = value;
        cads_cli_write(session, "loss ");
        cads_cli_write_uint(session, value);
        cads_cli_write(session, l04_pcb ? " %\r\n" : " % (wirkt erst mit 'lab 04 echo on')\r\n");
        return;
    }
    if(cads_str_equal(argv[0], "pollgap") && argc <= 2) {
        l04_cmd_pollgap(session, argc, argv);
        return;
    }
    if(cads_str_equal(argv[0], "ping") && argc >= 2 && argc <= 4) {
        uint32_t ip, count = 10u, size = 56u;
        if(rnlab_parse_ipv4(argv[1], &ip) &&
           (argc < 3 || (l04_parse_bounded(argv[2], L04_PING_MAX_COUNT, &count) && count > 0u)) &&
           (argc < 4 || l04_parse_bounded(argv[3], L04_PING_MAX_DATA, &size))) {
            l04_cmd_ping(session, ip, count, size);
            return;
        }
        cads_cli_write(session, "? Aufruf: lab 04 ping <ip> [1..200] [0..1472]\r\n");
        return;
    }
    cads_cli_write(session, "? unbekannt ('lab 04 help')\r\n");
}

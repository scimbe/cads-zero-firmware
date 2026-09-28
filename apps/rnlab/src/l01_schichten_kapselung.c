/*
 * CaDS Zero - rnlab L01 (Schichten und Kapselung): board integration.
 *
 * `lab 01 <cmd> [args]` lands in rnlab_l01_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l01_schichten_kapselung_logic.c.
 *
 *   lab 01 trace [n]   the last n frames (RX and TX), layer by layer, with
 *                      each layer's header bytes
 *   lab 01 eff [n]     protocol efficiency of the recorded frames, or of a
 *                      ping with n bytes of data (the prediction)
 *   lab 01 clear       forget the recorded frames
 *
 * The RX/TX hooks decode every frame with rnlab_decode_frame() and keep the
 * last RNLAB_L01_RING of them. Frames of the lab CLI itself (TCP 4242) are
 * not recorded - otherwise `lab 01 trace` over Telnet would mostly show its
 * own output.
 */

#include "rnlab/rnlab.h"
#include "rnlab/rnlab_lesson.h"

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "l01_schichten_kapselung_logic.h"

#define RNLAB_L01_RING       16u /* frames kept for trace/eff */
#define RNLAB_L01_SNAP       64u /* header bytes kept per frame: Eth + IPv4 + TCP w/ options */
#define RNLAB_L01_TRACE_DEF  4u
#define RNLAB_L01_MAX_PING   1472u /* 1500 B MTU - 20 IPv4 - 8 ICMP; more fragments */
#define RNLAB_L01_HEX_LINE   16u

typedef struct {
    uint32_t number; /* 1, 2, ... since boot or `lab 01 clear` */
    uint32_t t_ms;
    bool tx;
    uint8_t snap_len;
    rnlab_frame_info_t info;
    uint8_t bytes[RNLAB_L01_SNAP];
} rnlab_l01_entry_t;

/* ~2.3 KB of ring: CPU-only, so it lives in CCM instead of the tight SRAM
 * budget. CCM is not zeroed at boot - harmless, because only entries below
 * rnlab_l01_count (.bss, zeroed) are ever read. */
RNLAB_CCM static rnlab_l01_entry_t rnlab_l01_ring[RNLAB_L01_RING];
static uint32_t rnlab_l01_count;
static uint32_t rnlab_l01_cli_skipped;
static bool rnlab_l01_paused;

/* A raw look at the TCP ports, deliberately not via rnlab_decode_frame():
 * the filter must already work on the student stub, where the decoder is
 * still a TODO. Deliberately rough (no version or fragment check): at worst
 * it hides a stray frame that merely looks like CLI traffic. */
static bool rnlab_l01_is_cli_frame(const uint8_t* f, size_t len) {
    if(len < 14u + 20u + 4u || f[12] != 0x08u || f[13] != 0x00u || f[23] != 6u) return false;
    size_t ihl = (size_t)(f[14] & 0x0Fu) * 4u;
    if(ihl < 20u || len < 14u + ihl + 4u) return false;
    const uint8_t* tcp = &f[14u + ihl];
    uint16_t src = (uint16_t)((tcp[0] << 8) | tcp[1]);
    uint16_t dst = (uint16_t)((tcp[2] << 8) | tcp[3]);
    return src == RNLAB_CLI_TCP_PORT || dst == RNLAB_CLI_TCP_PORT;
}

static void rnlab_l01_record(const uint8_t* frame, size_t len, bool tx) {
    /* Printing over Telnet sends frames from inside the command handler;
     * `paused` keeps a trace from overwriting the entries it is printing. */
    if(rnlab_l01_paused) return;
    if(rnlab_l01_is_cli_frame(frame, len)) {
        rnlab_l01_cli_skipped++;
        return;
    }
    rnlab_l01_entry_t* e = &rnlab_l01_ring[rnlab_l01_count % RNLAB_L01_RING];
    e->number = rnlab_l01_count + 1u;
    e->t_ms = cads_hal_ticks_ms();
    e->tx = tx;
    e->snap_len = (uint8_t)(len < RNLAB_L01_SNAP ? len : RNLAB_L01_SNAP);
    for(size_t i = 0; i < e->snap_len; i++) e->bytes[i] = frame[i];
    (void)rnlab_decode_frame(frame, len, &e->info);
    rnlab_l01_count++;
}

void rnlab_l01_hook_rx_frame(const uint8_t* frame, size_t len) {
    rnlab_l01_record(frame, len, false);
}

void rnlab_l01_hook_tx_frame(const uint8_t* frame, size_t len) {
    rnlab_l01_record(frame, len, true);
}

/* --- output helpers --------------------------------------------------------- */

static void rnlab_l01_write_mac(cads_cli_session_t* session, const uint8_t mac[6]) {
    char text[18];
    cads_fmt_mac(text, sizeof(text), mac);
    cads_cli_write(session, text);
}

static void rnlab_l01_write_ip(cads_cli_session_t* session, const uint8_t ip[4]) {
    char text[16];
    uint32_t v = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
    cads_fmt_ipv4(text, sizeof(text), v);
    cads_cli_write(session, text);
}

static void rnlab_l01_write_hex(cads_cli_session_t* session, uint32_t value, uint8_t digits) {
    char text[9];
    cads_fmt_hex(text, sizeof(text), value, digits, false);
    cads_cli_write(session, text);
}

static void rnlab_l01_write_percent(cads_cli_session_t* session, uint32_t basis_points) {
    char text[16];
    rnlab_l01_format_percent(text, sizeof(text), basis_points);
    cads_cli_write(session, text);
}

/* "123,04 us": the time a frame of `bytes` occupies a 100 Mbit/s wire
 * (80 ns per byte). */
static void rnlab_l01_write_wire_time(cads_cli_session_t* session, uint32_t bytes) {
    uint32_t centi_us = bytes * 8u;
    char frac[3];
    cads_fmt_uint_pad(frac, sizeof(frac), centi_us % 100u, 2u, '0');
    cads_cli_write_uint(session, centi_us / 100u);
    cads_cli_write(session, ",");
    cads_cli_write(session, frac);
    cads_cli_write(session, " us");
}

/* Header bytes [off, off+len) of one layer, 16 per line, as far as the
 * snapshot reaches. */
static void rnlab_l01_write_bytes(cads_cli_session_t* session, const rnlab_l01_entry_t* e,
                                  uint32_t off, uint32_t len) {
    for(uint32_t i = 0; i < len; i++) {
        if(i % RNLAB_L01_HEX_LINE == 0u) cads_cli_write(session, i == 0u ? "          " : "\r\n          ");
        if(off + i >= e->snap_len) {
            cads_cli_write(session, "..");
            break;
        }
        rnlab_l01_write_hex(session, e->bytes[off + i], 2u);
        cads_cli_write(session, " ");
    }
    cads_cli_write(session, "\r\n");
}

static void rnlab_l01_write_tcp_flags(cads_cli_session_t* session, uint8_t flags) {
    static const char* const names[] = {"FIN", "SYN", "RST", "PSH", "ACK", "URG"};
    cads_cli_write(session, "  Flags");
    for(uint32_t bit = 0; bit < 6u; bit++) {
        if((flags & (1u << bit)) == 0u) continue;
        cads_cli_write(session, " ");
        cads_cli_write(session, names[bit]);
    }
}

static void rnlab_l01_write_len(cads_cli_session_t* session, uint32_t len) {
    cads_cli_write(session, "  ");
    cads_cli_write_uint(session, len);
    cads_cli_write(session, " B\r\n");
}

/* --- trace ------------------------------------------------------------------ */

static void rnlab_l01_trace_upper(cads_cli_session_t* session, const rnlab_l01_entry_t* e) {
    const rnlab_frame_info_t* in = &e->info;
    switch(in->upper) {
    case RNLAB_L01_UPPER_ICMP:
        cads_cli_write(session, "  OSI 3   ICMP ");
        cads_cli_write(session, rnlab_l01_icmp_type_name(in->icmp_type));
        cads_cli_write(session, " (Typ ");
        cads_cli_write_uint(session, in->icmp_type);
        cads_cli_write(session, ", Code ");
        cads_cli_write_uint(session, in->icmp_code);
        cads_cli_write(session, ")");
        break;
    case RNLAB_L01_UPPER_UDP:
    case RNLAB_L01_UPPER_TCP:
        cads_cli_write(session, "  OSI 4   ");
        cads_cli_write(session, rnlab_l01_upper_name(in->upper));
        cads_cli_write(session, " Port ");
        cads_cli_write_uint(session, in->src_port);
        cads_cli_write(session, " > ");
        cads_cli_write_uint(session, in->dst_port);
        if(in->upper == RNLAB_L01_UPPER_TCP) rnlab_l01_write_tcp_flags(session, in->tcp_flags);
        break;
    case RNLAB_L01_UPPER_OTHER:
        cads_cli_write(session, "  OSI 4?  IP-Protokoll ");
        cads_cli_write_uint(session, in->ip_proto);
        cads_cli_write(session, " (nicht dekodiert)\r\n");
        return;
    default:
        return;
    }
    if(in->upper_hdr_len == 0u) {
        cads_cli_write(session, "\r\n"); /* header cut off: the status line says so */
        return;
    }
    rnlab_l01_write_len(session, in->upper_hdr_len);
    rnlab_l01_write_bytes(session, e, in->upper_off, in->upper_hdr_len);
}

static void rnlab_l01_trace_entry(cads_cli_session_t* session, const rnlab_l01_entry_t* e) {
    const rnlab_frame_info_t* in = &e->info;

    cads_cli_write(session, "#");
    cads_cli_write_uint(session, e->number);
    cads_cli_write(session, e->tx ? " TX t=" : " RX t=");
    cads_cli_write_uint(session, e->t_ms);
    cads_cli_write(session, " ms  ");
    cads_cli_write_uint(session, in->frame_len);
    cads_cli_write(session, " B  [");
    cads_cli_write(session, rnlab_l01_status_text(in->status));
    cads_cli_write(session, "]\r\n");

    if(in->eth_hdr_len == 0u) {
        /* Nothing decoded (short frame, or the decoder is still the stub):
         * show the raw start of the frame instead. */
        cads_cli_write(session, "  roh\r\n");
        rnlab_l01_write_bytes(session, e, 0u, e->snap_len < 32u ? e->snap_len : 32u);
        return;
    }

    cads_cli_write(session, "  OSI 2   Ethernet II ");
    rnlab_l01_write_mac(session, in->eth_src);
    cads_cli_write(session, " > ");
    rnlab_l01_write_mac(session, in->eth_dst);
    cads_cli_write(session, "  Typ 0x");
    rnlab_l01_write_hex(session, in->ethertype, 4u);
    rnlab_l01_write_len(session, in->eth_hdr_len);
    rnlab_l01_write_bytes(session, e, 0u, in->eth_hdr_len);

    switch(in->net) {
    case RNLAB_L01_NET_IPV4:
        cads_cli_write(session, "  OSI 3   IPv4 ");
        rnlab_l01_write_ip(session, in->ip_src);
        cads_cli_write(session, " > ");
        rnlab_l01_write_ip(session, in->ip_dst);
        cads_cli_write(session, "  TTL ");
        cads_cli_write_uint(session, in->ip_ttl);
        cads_cli_write(session, "  Proto ");
        cads_cli_write_uint(session, in->ip_proto);
        cads_cli_write(session, "  Laenge ");
        cads_cli_write_uint(session, in->ip_total_len);
        if(in->ip_fragment) cads_cli_write(session, "  Fragment");
        if(in->net_hdr_len == 0u) {
            cads_cli_write(session, "\r\n");
            break;
        }
        rnlab_l01_write_len(session, in->net_hdr_len);
        rnlab_l01_write_bytes(session, e, in->net_off, in->net_hdr_len);
        rnlab_l01_trace_upper(session, e);
        break;
    case RNLAB_L01_NET_ARP:
        cads_cli_write(session, "  OSI 2/3 ARP ");
        cads_cli_write(session, in->arp_oper == 1u ? "Request" : in->arp_oper == 2u ? "Reply" : "?");
        if(in->net_hdr_len == 0u) {
            cads_cli_write(session, "\r\n");
            break;
        }
        rnlab_l01_write_len(session, in->net_hdr_len);
        rnlab_l01_write_bytes(session, e, in->net_off, in->net_hdr_len);
        break;
    default:
        cads_cli_write(session, "  OSI 3   EtherType 0x");
        rnlab_l01_write_hex(session, in->ethertype, 4u);
        cads_cli_write(session, " nicht dekodiert\r\n");
        break;
    }

    if(in->payload_len > 0u) {
        /* ICMP's data is still layer 3; only above UDP/TCP is it application data. */
        bool app = in->upper == RNLAB_L01_UPPER_UDP || in->upper == RNLAB_L01_UPPER_TCP;
        cads_cli_write(session, app ? "  OSI 5-7 Nutzdaten " : "          Daten ");
        cads_cli_write_uint(session, in->payload_len);
        cads_cli_write(session, " B\r\n");
    }
    if(in->pad_len > 0u) {
        cads_cli_write(session, "          Padding ");
        cads_cli_write_uint(session, in->pad_len);
        cads_cli_write(session, " B\r\n");
    }
}

static bool rnlab_l01_parse_count(const char* text, uint32_t max, uint32_t* out) {
    const char* end = NULL;
    uint32_t value;
    if(!cads_str_to_uint(text, &value, &end) || *end != '\0' || value == 0u || value > max) return false;
    *out = value;
    return true;
}

static void rnlab_l01_trace(cads_cli_session_t* session, int argc, char* argv[]) {
    uint32_t n = RNLAB_L01_TRACE_DEF;
    if(argc > 1 || (argc == 1 && !rnlab_l01_parse_count(argv[0], RNLAB_L01_RING, &n))) {
        cads_cli_write(session, "? Aufruf: lab 01 trace [n], n = 1..16\r\n");
        return;
    }
    if(rnlab_l01_count == 0u) {
        cads_cli_write(session, "noch keine Frames - z.B. ping 192.168.33.99 senden\r\n");
        return;
    }
    if(n > rnlab_l01_count) n = rnlab_l01_count;

    rnlab_l01_paused = true;
    for(uint32_t k = rnlab_l01_count - n; k < rnlab_l01_count; k++) {
        rnlab_l01_trace_entry(session, &rnlab_l01_ring[k % RNLAB_L01_RING]);
    }
    rnlab_l01_paused = false;
    cads_cli_write(session, "(CLI-Frames auf TCP 4242 nicht aufgezeichnet: ");
    cads_cli_write_uint(session, rnlab_l01_cli_skipped);
    cads_cli_write(session, ")\r\n");
}

/* --- eff -------------------------------------------------------------------- */

static void rnlab_l01_eff_prediction(cads_cli_session_t* session, uint32_t payload) {
    rnlab_l01_overhead_t o;
    rnlab_l01_ping_overhead(payload, &o);
    cads_cli_write(session, "Ping mit ");
    cads_cli_write_uint(session, payload);
    cads_cli_write(session, " B Daten (ICMP Echo) ueber Ethernet II:\r\n  Header   ");
    cads_cli_write_uint(session, o.headers);
    cads_cli_write(session, " B (Ethernet 14 + IPv4 20 + ICMP 8)\r\n  Padding  ");
    cads_cli_write_uint(session, o.padding);
    cads_cli_write(session, " B\r\n  Rahmen   ");
    cads_cli_write_uint(session, o.frame);
    cads_cli_write(session, " B (inkl. 4 B FCS, mind. 64 B)\r\n  Draht    ");
    cads_cli_write_uint(session, o.wire);
    cads_cli_write(session, " B (+ 8 B Praeambel/SFD + 12 B Interframe Gap) = ");
    rnlab_l01_write_wire_time(session, o.wire);
    cads_cli_write(session, " bei 100 Mbit/s\r\n  Effizienz Rahmen ");
    rnlab_l01_write_percent(session, o.eff_frame_bp);
    cads_cli_write(session, ", Draht ");
    rnlab_l01_write_percent(session, o.eff_wire_bp);
    cads_cli_write(session, "\r\n");
}

static void rnlab_l01_eff_recorded(cads_cli_session_t* session) {
    if(rnlab_l01_count == 0u) {
        cads_cli_write(session, "noch keine Frames - z.B. ping 192.168.33.99 senden\r\n");
        return;
    }
    uint32_t n = rnlab_l01_count < RNLAB_L01_RING ? rnlab_l01_count : RNLAB_L01_RING;
    uint32_t sum_payload = 0u, sum_frame = 0u, sum_wire = 0u;

    cads_cli_write(session, "Nr     Dir Protokoll  Daten  Rahmen  Draht  eta Rahmen  eta Draht\r\n");
    rnlab_l01_paused = true;
    for(uint32_t k = rnlab_l01_count - n; k < rnlab_l01_count; k++) {
        const rnlab_l01_entry_t* e = &rnlab_l01_ring[k % RNLAB_L01_RING];
        rnlab_l01_overhead_t o;
        if(!rnlab_l01_overhead(&e->info, &o)) continue; /* nothing decoded */
        char col[12];
        cads_fmt_uint_pad(col, sizeof(col), e->number, 6u, ' ');
        cads_cli_write(session, col);
        cads_cli_write(session, e->tx ? " TX " : " RX ");
        const rnlab_frame_info_t* in = &e->info;
        const char* proto = in->upper != RNLAB_L01_UPPER_NONE ? rnlab_l01_upper_name(in->upper)
                                                               : rnlab_l01_net_name(in->net);
        cads_str_copy(col, sizeof(col), proto);
        cads_str_append(col, sizeof(col), "        ");
        col[9] = '\0'; /* fixed 9-character column */
        cads_cli_write(session, col);
        cads_fmt_uint_pad(col, sizeof(col), o.payload, 7u, ' ');
        cads_cli_write(session, col);
        cads_fmt_uint_pad(col, sizeof(col), o.frame, 8u, ' ');
        cads_cli_write(session, col);
        cads_fmt_uint_pad(col, sizeof(col), o.wire, 7u, ' ');
        cads_cli_write(session, col);
        cads_cli_write(session, "     ");
        rnlab_l01_write_percent(session, o.eff_frame_bp);
        cads_cli_write(session, "     ");
        rnlab_l01_write_percent(session, o.eff_wire_bp);
        cads_cli_write(session, "\r\n");
        sum_payload += o.payload;
        sum_frame += o.frame;
        sum_wire += o.wire;
    }
    rnlab_l01_paused = false;

    /* The efficiency of the whole exchange weights every frame by its size -
     * that is not the mean of the per-frame percentages. */
    cads_cli_write(session, "Summe: Daten ");
    cads_cli_write_uint(session, sum_payload);
    cads_cli_write(session, " B, Rahmen ");
    cads_cli_write_uint(session, sum_frame);
    cads_cli_write(session, " B, Draht ");
    cads_cli_write_uint(session, sum_wire);
    cads_cli_write(session, " B -> ");
    rnlab_l01_write_percent(session,
        sum_frame ? (uint32_t)(((uint64_t)sum_payload * 10000u + sum_frame / 2u) / sum_frame) : 0u);
    cads_cli_write(session, " / ");
    rnlab_l01_write_percent(session,
        sum_wire ? (uint32_t)(((uint64_t)sum_payload * 10000u + sum_wire / 2u) / sum_wire) : 0u);
    cads_cli_write(session, "\r\n");
}

static void rnlab_l01_eff(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc == 0) {
        rnlab_l01_eff_recorded(session);
        return;
    }
    const char* end = NULL;
    uint32_t payload;
    if(argc != 1 || !cads_str_to_uint(argv[0], &payload, &end) || *end != '\0' ||
       payload > RNLAB_L01_MAX_PING) {
        cads_cli_write(session, "? Aufruf: lab 01 eff [n], n = 0..1472 B Ping-Daten "
                                "(mehr wird in IPv4 fragmentiert)\r\n");
        return;
    }
    rnlab_l01_eff_prediction(session, payload);
}

static void rnlab_l01_help(cads_cli_session_t* session) {
    cads_cli_write(session,
        "lab 01 trace [n]   letzte n Frames (Standard 4, max 16) Schicht fuer Schicht\r\n"
        "lab 01 eff         Protokolleffizienz der aufgezeichneten Frames\r\n"
        "lab 01 eff <n>     Vorhersage fuer einen Ping mit n B Daten (0..1472)\r\n"
        "lab 01 clear       Aufzeichnung leeren\r\n");
}

void rnlab_l01_command(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc == 0 || cads_str_equal(argv[0], "help")) {
        rnlab_l01_help(session);
        return;
    }
    if(cads_str_equal(argv[0], "trace")) {
        rnlab_l01_trace(session, argc - 1, argv + 1);
        return;
    }
    if(cads_str_equal(argv[0], "eff")) {
        rnlab_l01_eff(session, argc - 1, argv + 1);
        return;
    }
    if(cads_str_equal(argv[0], "clear")) {
        rnlab_l01_count = 0u;
        rnlab_l01_cli_skipped = 0u;
        cads_cli_write(session, "Aufzeichnung geleert\r\n");
        return;
    }
    cads_cli_write(session, "? unbekannt: lab 01 ");
    cads_cli_write(session, argv[0]);
    cads_cli_write(session, " ('lab 01 help')\r\n");
}

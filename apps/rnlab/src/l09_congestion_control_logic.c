/*
 * CaDS Zero - rnlab L09 (Congestion Control): Reno model, Mathis estimate,
 * loss injector, TCP decoder and trace formatting. See
 * l09_congestion_control_logic.h.
 */

#include "l09_congestion_control_logic.h"

#include "cads/toolbox/fmt.h"

/* --- Reno reference model -------------------------------------------------- */

void rnlab_reno_init(rnlab_reno_t* reno, uint32_t mss, uint32_t cwnd, uint32_t ssthresh) {
    if(!reno) return;
    reno->mss = mss;
    reno->cwnd = cwnd;
    reno->ssthresh = ssthresh;
    reno->dupacks = 0u;
    reno->in_fr = false;
}

void rnlab_reno_on_ack(rnlab_reno_t* reno, uint32_t acked) {
    /* TODO(L09): neues ACK fuer 'acked' Byte (Skript 5.31, RFC 5681).
     *  - reno NULL: nichts tun
     *  - dupacks = 0
     *  - in Fast Recovery: verlassen, cwnd = ssthresh ("deflate"), fertig
     *  - Slow Start (cwnd < ssthresh): cwnd += min(acked, mss)
     *  - Congestion Avoidance: cwnd += mss*mss/cwnd, mindestens 1 Byte */
    (void)reno;
    (void)acked;
}

void rnlab_reno_on_dupack(rnlab_reno_t* reno, uint32_t flight) {
    /* TODO(L09): doppeltes ACK, 'flight' Byte unterwegs.
     *  - dupacks++
     *  - schon in Fast Recovery: cwnd += mss (ein weiteres Segment hat das Netz verlassen)
     *  - beim dritten: ssthresh = max(flight/2, 2*mss), cwnd = ssthresh + 3*mss,
     *    in_fr = true (Fast Retransmit)
     *  - erstes und zweites: keine Aenderung
     * Tipp: max(flight/2, 2*mss) brauchst du auch beim Timeout - eine
     * kleine static-Hilfsfunktion lohnt sich. */
    (void)reno;
    (void)flight;
}

void rnlab_reno_on_timeout(rnlab_reno_t* reno, uint32_t flight) {
    /* TODO(L09): Timeout (RTO): ssthresh = max(flight/2, 2*mss), cwnd = mss,
     *  dupacks = 0, in_fr = false. */
    (void)reno;
    (void)flight;
}

rnlab_l09_phase_t rnlab_l09_phase(uint32_t cwnd, uint32_t ssthresh, bool in_fr) {
    /* TODO(L09): in_fr -> FR; cwnd < ssthresh -> SS; sonst CA. */
    (void)cwnd;
    (void)ssthresh;
    (void)in_fr;
    return RNLAB_L09_PHASE_UNKNOWN;
}

char rnlab_l09_phase_letter(rnlab_l09_phase_t phase) {
    switch(phase) {
    case RNLAB_L09_PHASE_SS: return 'S';
    case RNLAB_L09_PHASE_CA: return 'C';
    case RNLAB_L09_PHASE_FR: return 'F';
    default: return '?';
    }
}

/* --- Mathis ---------------------------------------------------------------- */

/* Integer square root (Newton), so that the logic needs no libm - on the
 * host the tests would otherwise have to link it explicitly. */
static uint64_t rnlab_isqrt64(uint64_t v) {
    if(v < 2u) return v;
    uint64_t x = v;
    uint64_t y = (x + 1u) / 2u;
    while(y < x) {
        x = y;
        y = (x + v / x) / 2u;
    }
    return x;
}

uint64_t rnlab_l09_mathis_bps(uint32_t mss, uint32_t rtt_us, uint32_t p_ppm) {
    /* TODO(L09): Mathis-Abschaetzung in bit/s:
     *    rate = MSS/RTT * C/sqrt(p),  C = sqrt(3/2) ~ 1.2247,  p = p_ppm / 10^6
     *  rtt_us == 0 oder p_ppm == 0 -> 0.
     *  Ohne libm: rnlab_isqrt64() oben liefert die ganzzahlige Wurzel. Rechne
     *  mit s = isqrt(p_ppm * 10^6) = sqrt(p) * 10^6 - dann bleibt alles
     *  ganzzahlig (Ueberlauf pruefen: MSS bis 65535!). */
    (void)mss;
    (void)rtt_us;
    (void)p_ppm;
    (void)rnlab_isqrt64;
    return 0u;
}

/* --- loss injector --------------------------------------------------------- */

void rnlab_l09_dropper_init(rnlab_l09_dropper_t* dropper, uint32_t seed) {
    if(!dropper) return;
    dropper->every_n = 0u;
    dropper->p_ppm = 0u;
    dropper->rng = (seed != 0u) ? seed : 0x2545F491u;
    dropper->seen = 0u;
    dropper->dropped = 0u;
}

uint32_t rnlab_l09_xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

bool rnlab_l09_dropper_decide(rnlab_l09_dropper_t* dropper) {
    /* TODO(L09): Entscheidung fuer ein Datensegment.
     *  - dropper NULL -> false
     *  - seen++
     *  - every_n > 0: verwerfen, wenn seen durch every_n teilbar ist
     *  - sonst p_ppm > 0: verwerfen, wenn rnlab_l09_xorshift32(&rng) % 1000000 < p_ppm
     *  - bei Verwerfen dropped++ und true zurueck */
    (void)dropper;
    return false;
}

bool rnlab_l09_parse_percent(const char* text, uint32_t* ppm) {
    if(!text || !ppm || *text == '\0') return false;
    uint32_t whole = 0u;
    const char* c = text;
    for(; *c >= '0' && *c <= '9'; c++) {
        whole = whole * 10u + (uint32_t)(*c - '0');
        if(whole > 100u) return false;
    }
    if(c == text) return false;
    uint32_t frac = 0u; /* thousandths of a percent */
    if(*c == '.') {
        c++;
        uint32_t digits = 0u;
        for(; *c >= '0' && *c <= '9'; c++) {
            if(++digits > 3u) return false;
            frac = frac * 10u + (uint32_t)(*c - '0');
        }
        if(digits == 0u) return false;
        for(; digits < 3u; digits++) frac *= 10u;
    }
    if(*c != '\0') return false;
    uint32_t value = whole * 10000u + frac * 10u;
    if(value > 1000000u) return false;
    *ppm = value;
    return true;
}

/* --- TCP segment decoder --------------------------------------------------- */

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

bool rnlab_l09_parse_tcp(const uint8_t* frame, size_t len, rnlab_l09_seg_t* seg) {
    if(!frame || !seg || len < 14u + 20u + 20u) return false;
    if(rd16(frame + 12) != 0x0800u) return false; /* IPv4 only, no VLAN */
    const uint8_t* ip = frame + 14;
    if((ip[0] >> 4) != 4u) return false;
    size_t ihl = (size_t)(ip[0] & 0x0Fu) * 4u;
    uint16_t total = rd16(ip + 2);
    if(ihl < 20u || ip[9] != 6u) return false; /* TCP */
    if(14u + (size_t)total > len || total < ihl + 20u) return false;
    const uint8_t* tcp = ip + ihl;
    size_t thl = (size_t)(tcp[12] >> 4) * 4u;
    if(thl < 20u || ihl + thl > total) return false;

    seg->src_ip = rd32(ip + 12);
    seg->dst_ip = rd32(ip + 16);
    seg->src_port = rd16(tcp);
    seg->dst_port = rd16(tcp + 2);
    seg->seq = rd32(tcp + 4);
    seg->ack = rd32(tcp + 8);
    seg->flags = tcp[13];
    seg->window = rd16(tcp + 14);
    /* From the IP total length, not the frame length: short frames are
     * padded to 60 bytes on the wire. */
    seg->payload = (uint16_t)(total - ihl - thl);
    return true;
}

/* --- trace ----------------------------------------------------------------- */

size_t rnlab_l09_trace_format(const rnlab_l09_trace_entry_t* e, char* out, size_t size) {
    if(!out || size == 0u) return 0u;
    out[0] = '\0';
    if(!e) return 0u;
    const uint32_t v[8] = {e->t_ms, e->acked, e->cwnd, e->ssthresh,
                           e->snd_wnd, e->flight, e->rtt_us, e->model_cwnd};
    size_t n = 0u;
    char num[CADS_FMT_BUFFER];
    for(size_t i = 0u; i < 8u; i++) {
        size_t w = cads_fmt_uint(num, sizeof(num), v[i]);
        if(n + w + 1u >= size) goto full;
        for(size_t k = 0u; k < w; k++) out[n++] = num[k];
        out[n++] = ',';
    }
    /* event, comma, phase, NUL */
    if(n + 4u > size) goto full;
    out[n++] = (char)e->event;
    out[n++] = ',';
    out[n++] = rnlab_l09_phase_letter((rnlab_l09_phase_t)e->phase);
    out[n] = '\0';
    return n;
full:
    out[0] = '\0';
    return 0u;
}

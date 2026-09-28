/*
 * CaDS Zero - rnlab L09 (Congestion Control): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l09.c links this file
 * directly on the host. Board integration lives in l09_congestion_control.c.
 *
 * Contents:
 *  - a Reno reference model (RFC 5681: slow start, congestion avoidance,
 *    fast retransmit/fast recovery with cwnd = ssthresh + 3*MSS, timeout).
 *    The board feeds it the same ACK/dupACK/timeout events lwIP sees, so
 *    the trace shows the model's cwnd next to lwIP's own;
 *  - the phase of a (cwnd, ssthresh, in fast recovery) triple;
 *  - the Mathis et al. estimate  rate ~ MSS/RTT * 1.22/sqrt(p);
 *  - the loss injector's decision (every n-th data segment, or with
 *    probability p);
 *  - a decoder for the TCP/IPv4/Ethernet headers the hooks see;
 *  - the trace entry and its CSV line.
 */

#ifndef RNLAB_L09_CONGESTION_CONTROL_LOGIC_H
#define RNLAB_L09_CONGESTION_CONTROL_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Reno reference model -------------------------------------------------- */

typedef enum {
    RNLAB_L09_PHASE_UNKNOWN = 0, /**< not classified (stub) */
    RNLAB_L09_PHASE_SS,          /**< slow start: cwnd < ssthresh */
    RNLAB_L09_PHASE_CA,          /**< congestion avoidance: cwnd >= ssthresh */
    RNLAB_L09_PHASE_FR,          /**< fast recovery (after the third dupACK) */
} rnlab_l09_phase_t;

typedef struct {
    uint32_t mss;
    uint32_t cwnd;      /**< bytes */
    uint32_t ssthresh;  /**< bytes */
    uint32_t dupacks;   /**< consecutive duplicate ACKs */
    bool in_fr;         /**< in fast recovery */
} rnlab_reno_t;

/** Start state: cwnd and ssthresh in bytes, no dupACKs, not in fast recovery. */
void rnlab_reno_init(rnlab_reno_t* reno, uint32_t mss, uint32_t cwnd, uint32_t ssthresh);

/**
 * An ACK for `acked` new bytes. Ends fast recovery (cwnd = ssthresh, "deflate")
 * and resets the dupACK count. Otherwise slow start grows cwnd by
 * min(acked, MSS) per ACK, congestion avoidance by MSS*MSS/cwnd (at least 1 byte),
 * i.e. about one MSS per round trip.
 */
void rnlab_reno_on_ack(rnlab_reno_t* reno, uint32_t acked);

/**
 * A duplicate ACK while `flight` bytes are outstanding. The third one is
 * fast retransmit: ssthresh = max(flight/2, 2*MSS), cwnd = ssthresh + 3*MSS
 * (the three segments that left the network), enter fast recovery. Every
 * further one inflates cwnd by one MSS. The first two change nothing.
 */
void rnlab_reno_on_dupack(rnlab_reno_t* reno, uint32_t flight);

/** Retransmission timeout: ssthresh = max(flight/2, 2*MSS), cwnd = 1 MSS, back to slow start. */
void rnlab_reno_on_timeout(rnlab_reno_t* reno, uint32_t flight);

/** Phase of a (cwnd, ssthresh, in fast recovery) triple - for the model and for lwIP's pcb alike. */
rnlab_l09_phase_t rnlab_l09_phase(uint32_t cwnd, uint32_t ssthresh, bool in_fr);

/** One letter for the CSV: S, C, F, or ? for UNKNOWN. */
char rnlab_l09_phase_letter(rnlab_l09_phase_t phase);

/* --- Mathis ---------------------------------------------------------------- */

/**
 * Mathis/Semke/Mahdavi/Ott (1997): steady-state TCP Reno throughput under a
 * random loss rate p, rate = MSS/RTT * C/sqrt(p) with C = sqrt(3/2) ~ 1.22,
 * in bit/s. p_ppm is p in parts per million (1 % = 10000).
 * 0 when rtt_us or p_ppm is 0 (no loss: the formula has no finite answer).
 */
uint64_t rnlab_l09_mathis_bps(uint32_t mss, uint32_t rtt_us, uint32_t p_ppm);

/* --- loss injector --------------------------------------------------------- */

typedef struct {
    uint32_t every_n;  /**< drop every n-th data segment (0 = off) */
    uint32_t p_ppm;    /**< or drop with this probability (used when every_n is 0) */
    uint32_t rng;      /**< xorshift32 state, never 0 */
    uint32_t seen;     /**< data segments decided on */
    uint32_t dropped;
} rnlab_l09_dropper_t;

/** Off, counters zero, random state from `seed` (0 is replaced by a fixed non-zero value). */
void rnlab_l09_dropper_init(rnlab_l09_dropper_t* dropper, uint32_t seed);

/** Next xorshift32 value (Marsaglia 2003) - the dropper's random source. */
uint32_t rnlab_l09_xorshift32(uint32_t* state);

/**
 * Decide for one data segment: counts it in `seen`, returns true to drop it
 * (and counts `dropped`). every_n > 0: the n-th, 2n-th, ... segment since the
 * counters were last reset. Otherwise p_ppm > 0: drop when the next random
 * value r satisfies r % 1000000 < p_ppm. Neither set: never drop.
 */
bool rnlab_l09_dropper_decide(rnlab_l09_dropper_t* dropper);

/**
 * "2", "2.5", "0.125", "100" (percent, at most three decimals) -> parts per
 * million (20000, 25000, 1250, 1000000). False for anything else or above 100 %.
 */
bool rnlab_l09_parse_percent(const char* text, uint32_t* ppm);

/* --- TCP segment decoder --------------------------------------------------- */

#define RNLAB_L09_TCP_FIN 0x01u
#define RNLAB_L09_TCP_SYN 0x02u
#define RNLAB_L09_TCP_RST 0x04u
#define RNLAB_L09_TCP_ACK 0x10u

typedef struct {
    uint32_t src_ip;   /**< host byte order */
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint16_t window;   /**< raw, unscaled */
    uint8_t flags;
    uint16_t payload;  /**< TCP payload bytes */
} rnlab_l09_seg_t;

/**
 * Decode an Ethernet II frame carrying IPv4/TCP (from the destination MAC,
 * no FCS). False for anything else or when truncated.
 */
bool rnlab_l09_parse_tcp(const uint8_t* frame, size_t len, rnlab_l09_seg_t* seg);

/* --- trace ----------------------------------------------------------------- */

typedef enum {
    RNLAB_L09_EV_ACK = 'A',    /**< ACK for new data */
    RNLAB_L09_EV_DUPACK = 'D', /**< duplicate ACK */
    RNLAB_L09_EV_FAST = 'F',   /**< retransmission in fast recovery */
    RNLAB_L09_EV_RTO = 'R',    /**< retransmission after a timeout */
    RNLAB_L09_EV_DROP = 'X',   /**< data segment dropped by `lab 09 loss` */
} rnlab_l09_event_t;

typedef struct {
    uint32_t t_ms;       /**< since the connection was established */
    uint32_t acked;      /**< bytes cumulatively ACKed so far */
    uint16_t cwnd;       /**< lwIP, after the event */
    uint16_t ssthresh;
    uint16_t snd_wnd;    /**< receiver's advertised window */
    uint16_t flight;     /**< snd_nxt - snd_una */
    uint16_t rtt_us;     /**< RTT sample completed by this ACK, 0 = none (saturates at 65535) */
    uint16_t model_cwnd; /**< the Reno model's cwnd after the same event */
    uint8_t event;       /**< rnlab_l09_event_t */
    uint8_t phase;       /**< rnlab_l09_phase_t of lwIP's values */
} rnlab_l09_trace_entry_t;

/** CSV column names, matching rnlab_l09_trace_format(). */
#define RNLAB_L09_TRACE_HEADER "t_ms,acked,cwnd,ssthresh,snd_wnd,flight,rtt_us,model_cwnd,event,phase"

/**
 * One CSV line (no line end) into `out`, e.g. "12,14600,5840,8760,65535,4380,812,5840,A,S".
 * Returns the length, or 0 if `out` is too small (then out is "").
 */
size_t rnlab_l09_trace_format(const rnlab_l09_trace_entry_t* entry, char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L09_CONGESTION_CONTROL_LOGIC_H */

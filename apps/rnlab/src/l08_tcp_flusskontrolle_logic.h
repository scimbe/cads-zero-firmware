/*
 * CaDS Zero - rnlab L08 (TCP-Flusskontrolle): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l08.c links this file
 * directly on the host. Board integration lives in l08_tcp_flusskontrolle.c.
 *
 * Two things live here:
 *  - the throughput model a sender runs into: at most one receive window W
 *    per round trip (W/RTT), at most what the link carries after the
 *    per-segment header overhead, at most what the receiving application
 *    reads - whichever is smallest;
 *  - the rate limiter that makes the board's sink an application-limited
 *    receiver: it decides how many received bytes the "application" has
 *    read by now, i.e. how much window tcp_recved() may reopen.
 */

#ifndef RNLAB_L08_TCP_FLUSSKONTROLLE_LOGIC_H
#define RNLAB_L08_TCP_FLUSSKONTROLLE_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** TCP port of the board's sink (`lab 08 sink start`). */
#define RNLAB_L08_PORT 7008u

/**
 * Bytes on the wire per TCP segment besides the payload, with no IP or TCP
 * options: preamble+SFD 8, Ethernet header 14, FCS 4, inter-frame gap 12,
 * IPv4 20, TCP 20. (lwIP sends no TCP timestamps; the Mac's ACKs do not count
 * here - they travel the other direction of the full-duplex link.)
 */
#define RNLAB_L08_WIRE_OVERHEAD 78u

/** How much unused read allowance the limiter may bank, in milliseconds of its rate. */
#define RNLAB_L08_BUCKET_MS 100u

/**
 * Window limit: one window per round trip, W / RTT, in bit/s.
 * 0 when rtt_us is 0 (no meaningful limit can be stated).
 */
uint64_t rnlab_l08_window_limit_bps(uint32_t window_bytes, uint32_t rtt_us);

/**
 * Goodput the link allows for full segments: link_bps * mss / (mss + overhead),
 * overhead = RNLAB_L08_WIRE_OVERHEAD. 0 when mss is 0.
 */
uint64_t rnlab_l08_link_limit_bps(uint32_t link_bps, uint32_t mss);

/**
 * Predicted goodput: the smallest of window limit, link limit and the
 * application's read rate (app_bytes_per_s, 0 = reads as fast as data comes).
 * 0 when rtt_us or mss is 0.
 */
uint64_t rnlab_l08_predict_bps(
    uint32_t window_bytes, uint32_t rtt_us, uint32_t link_bps, uint32_t mss, uint32_t app_bytes_per_s);

/** Token bucket for the application's read rate. */
typedef struct {
    uint32_t rate;       /**< bytes per second the application reads, 0 = unlimited */
    uint32_t last_ms;    /**< time of the previous release call */
    uint64_t credit_mb;  /**< banked allowance in milli-bytes (byte * 1/1000) */
} rnlab_l08_limiter_t;

/** Start the bucket empty at `now_ms` with `rate` bytes/s (0 = unlimited). */
void rnlab_l08_limiter_init(rnlab_l08_limiter_t* limiter, uint32_t rate, uint32_t now_ms);

/**
 * How many of the `pending` received-but-unread bytes the application has
 * read by `now_ms`. Accrues rate * elapsed time as credit (capped at
 * RNLAB_L08_BUCKET_MS worth of the rate, at least one byte, so a pause does
 * not turn into a burst), releases min(credit, pending) and keeps the rest
 * of the credit.
 * rate 0: everything pending. Survives the 32-bit millisecond wrap.
 */
uint32_t rnlab_l08_limiter_release(rnlab_l08_limiter_t* limiter, uint32_t now_ms, uint32_t pending);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L08_TCP_FLUSSKONTROLLE_LOGIC_H */

/*
 * CaDS Zero - rnlab L04 (ICMP): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l04.c links this file
 * directly on the host. Board integration lives in l04_icmp.c.
 *
 * Byte buffers are packets exactly as they travel on the wire (network byte
 * order); RTTs are microseconds.
 */

#ifndef RNLAB_L04_ICMP_LOGIC_H
#define RNLAB_L04_ICMP_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNLAB_L04_ICMP_ECHO_REPLY   0u
#define RNLAB_L04_ICMP_ECHO_REQUEST 8u
#define RNLAB_L04_ICMP_HEADER_LEN   8u

/* Histogram: bin 0 = RTT < 250 us, bin k (1..8) = [250 us * 2^(k-1), 250 us * 2^k),
 * bin 9 = 64 ms and more. Doubling bins keep a 0.2 ms wire RTT and a 10 ms
 * poll-loop RTT apart on one screen. */
#define RNLAB_L04_HIST_BINS     10u
#define RNLAB_L04_HIST_FIRST_US 250u

/** Running RTT statistics - no samples kept, so the struct stays small. */
typedef struct {
    uint32_t sent;     /**< requests sent (set by the caller) */
    uint32_t received; /**< samples added with rnlab_l04_stats_add() */
    uint32_t min_us;
    uint32_t max_us;
    uint64_t sum_us;
    uint64_t sum_sq_us; /**< sum of squares, us^2 */
    uint32_t hist[RNLAB_L04_HIST_BINS];
} rnlab_l04_stats_t;

/* --- TODO(L04): the functions students implement ---------------------- */

/** Internet checksum (RFC 1071) over `len` bytes: one's-complement of the
 *  one's-complement sum of the big-endian 16-bit words, an odd last byte
 *  padded with a zero byte. Store the result big-endian into the checksum
 *  field. Over a packet whose checksum field is already correct the result
 *  is 0. */
uint16_t rnlab_inet_checksum(const uint8_t* data, size_t len);

/** Turn the ICMP echo request at `icmp` (`len` bytes, ICMP header + data) into
 *  its echo reply in place: type 8 -> 0, identifier/sequence/data unchanged,
 *  checksum recomputed. False (buffer untouched) when it is shorter than the
 *  header, not an echo request with code 0, or its checksum is wrong. */
bool rnlab_l04_echo_to_reply(uint8_t* icmp, size_t len);

/** Add one RTT sample: received, min, max, sums and the histogram. */
void rnlab_l04_stats_add(rnlab_l04_stats_t* stats, uint32_t rtt_us);

/** Histogram bin for one RTT (see RNLAB_L04_HIST_BINS). */
unsigned rnlab_l04_hist_bin(uint32_t rtt_us);

/** Mean RTT in us, 0 without samples. */
uint32_t rnlab_l04_stats_avg_us(const rnlab_l04_stats_t* stats);

/** Standard deviation in us, population form sqrt(E[x^2] - E[x]^2) - the one
 *  `ping` prints as "stddev"/"mdev". 0 without samples. */
uint32_t rnlab_l04_stats_stddev_us(const rnlab_l04_stats_t* stats);

/* --- provided --------------------------------------------------------- */

/** Empty statistics (min starts at UINT32_MAX so the first sample wins). */
void rnlab_l04_stats_init(rnlab_l04_stats_t* stats);

/** Loss in percent of `sent`, rounded down; 0 when nothing was sent. */
uint32_t rnlab_l04_stats_loss_pct(const rnlab_l04_stats_t* stats);

/** Integer square root, floor(sqrt(value)). */
uint32_t rnlab_l04_isqrt64(uint64_t value);

/** Pseudo-random loss decision: true with probability `percent`/100.
 *  `state` is an xorshift32 state, never 0 (0 is treated as 1). */
bool rnlab_l04_should_drop(uint32_t* state, uint32_t percent);

/** Write an ICMP echo request (header + `data_len` bytes of pattern data,
 *  checksum filled) to `out`, which must hold 8 + data_len bytes. */
void rnlab_l04_build_echo_request(uint8_t* out, uint16_t id, uint16_t seq, size_t data_len);

/** Parse a percentage "0".."100", optional trailing '%'. */
bool rnlab_l04_parse_percent(const char* text, uint32_t* percent);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L04_ICMP_LOGIC_H */

/*
 * CaDS Zero - rnlab L07 (UDP-Transport): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l07.c links this file
 * directly on the host. Board integration lives in l07_udp_transport.c.
 *
 * Wire format (identical to tools/rnlab.py udp-send/udp-recv):
 *   byte 0..3   sequence number, uint32 big-endian, starts at 0
 *   byte 4..11  send time in microseconds since the Unix epoch, uint64 big-endian
 *   rest        padding up to the chosen datagram size
 *
 * The sequence tracker is what a UDP receiver has to do itself, because UDP
 * (unlike TCP) neither numbers nor orders nor repeats anything: it counts
 * gaps as losses, late arrivals that fill a gap as reordered, and repeats
 * as duplicates. Sequence numbers are compared in serial-number arithmetic
 * (RFC 1982), so the counter may wrap from 0xFFFFFFFF to 0 mid-stream.
 */

#ifndef RNLAB_L07_UDP_TRANSPORT_LOGIC_H
#define RNLAB_L07_UDP_TRANSPORT_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** UDP port of the board's sink (`lab 07 udp start`). */
#define RNLAB_L07_PORT 7007u

/** Bytes of the rnlab header at the start of every datagram. */
#define RNLAB_L07_HEADER_LEN 12u

/** How many sequence numbers below the highest one the tracker remembers. */
#define RNLAB_SEQ_WINDOW 32u

/** What rnlab_seq_update() made of one sequence number. */
typedef enum {
    RNLAB_SEQ_ERROR = 0,  /**< not handled (tracker NULL or not implemented) */
    RNLAB_SEQ_FIRST,      /**< first datagram since the last reset */
    RNLAB_SEQ_IN_ORDER,   /**< exactly the next expected number */
    RNLAB_SEQ_GAP,        /**< newer than expected: the numbers in between count as lost */
    RNLAB_SEQ_LATE,       /**< older, not seen before, inside the window: fills a gap */
    RNLAB_SEQ_DUPLICATE,  /**< older and already seen inside the window */
    RNLAB_SEQ_STALE,      /**< older than the window: cannot tell late from duplicate */
} rnlab_seq_event_t;

typedef struct {
    bool started;
    uint32_t first;      /**< sequence number of the first datagram */
    uint32_t next;       /**< highest sequence number seen + 1 */
    /** Bit i set = sequence number (next - 1 - i) was received, i < RNLAB_SEQ_WINDOW. */
    uint32_t window;
    uint32_t received;   /**< every call, duplicates and stale ones included */
    uint32_t lost;       /**< gaps not (yet) filled by a late arrival */
    uint32_t reordered;  /**< late arrivals (RNLAB_SEQ_LATE) */
    uint32_t duplicates;
    uint32_t stale;
} rnlab_seq_tracker_t;

/**
 * Decode the rnlab header. Returns false (outputs untouched) when `len` is
 * shorter than RNLAB_L07_HEADER_LEN.
 */
bool rnlab_l07_parse_header(const uint8_t* data, size_t len, uint32_t* seq, uint64_t* sent_us);

/** Start over: everything zero, the next datagram is RNLAB_SEQ_FIRST. */
void rnlab_seq_reset(rnlab_seq_tracker_t* tracker);

/** Account one received sequence number and classify it. */
rnlab_seq_event_t rnlab_seq_update(rnlab_seq_tracker_t* tracker, uint32_t seq);

/** Sequence numbers the sender must have used so far: highest - first + 1 (0 before the first). */
uint32_t rnlab_seq_expected(const rnlab_seq_tracker_t* tracker);

/** Loss rate in basis points (1/100 %), lost / expected; 0 when nothing expected. */
uint32_t rnlab_seq_loss_bp(const rnlab_seq_tracker_t* tracker);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L07_UDP_TRANSPORT_LOGIC_H */

/*
 * CaDS Zero - Marauder live-capture relay: PCAP-over-serial demux + minimal
 * TZSP encapsulation. Deliberately has zero HAL/lwIP dependencies (like
 * cads_marauder_reader.h) so the whole byte-stream state machine is
 * unit-testable with synthetic byte arrays - see
 * tests/unit/test_marauder_pcap.c. cads_marauder.c owns the one instance and
 * the UDP send glue; this header only knows how to turn bytes into frames.
 *
 * THE WIRE FORMAT (Marauder's own, not ours to redesign)
 * --------------------------------------------------------------------------
 * `sniffraw -serial` makes Marauder's Buffer::saveSerial() (pinned commit's
 * Buffer.cpp) periodically write one burst onto the SAME Serial line its CLI
 * text already uses: the literal ASCII marker "[BUF/BEGIN]", then whatever
 * pcap-format bytes it has queued, then "[BUF/CLOSE]". Ordinary CLI text
 * (command echoes, status lines) can appear before/after/between bursts -
 * this module must therefore demux, not just extract.
 *
 * Inside a burst: the classic 24 B pcap global header (magic
 * 0xa1b2c3d4 little-endian first) appears exactly once, at the very start of
 * a capture session's FIRST burst only (Buffer::open() writes it once, when
 * the capture starts) - every later burst in that same session goes straight
 * to records. Rather than tracking "is this the first burst" as external
 * session state (one more thing a caller could forget to reset), this
 * parser recognises the global header by its own magic number: right after
 * every "[BUF/BEGIN]", it races the next bytes against BOTH the pcap magic
 * and "[BUF/CLOSE]" at once (cads_marauder_pcap.c's BOUNDARY stage) and
 * whichever candidate the bytes actually match decides the outcome. A
 * record's ts_sec (recent Unix time, nowhere near the magic's numeric value)
 * essentially never collides with the magic, so this self-describes
 * correctly on every burst without any "first capture" bookkeeping.
 *
 * A record is 16 B (ts_sec, ts_usec, incl_len, orig_len, all LE u32) then
 * incl_len raw bytes. Marauder's own Buffer::add()/write() never let a
 * save() cut a record in half (a record that would not fit in the active
 * half-buffer is dropped whole, not split - see Buffer.cpp), so a boundary
 * after a completed record is always either a fresh 16 B header or the
 * close marker - never a partial record. What CAN legitimately split across
 * this parser's own feed() calls is any of these atoms individually (a UART
 * read can return however many bytes happen to be queued), which is why
 * every stage below is resumable one byte at a time, the same discipline
 * cads_marauder_reader.c already uses for CLI lines.
 *
 * RAM, NOT FIDELITY, SETS THE FRAME CAP
 * --------------------------------------------------------------------------
 * This firmware has no heap and (see CLAUDE.md's RAM budget note) a margin
 * measured in the hundreds of bytes, not kilobytes - so this module holds
 * at most one frame at a time, and that frame is capped at
 * CADS_MARAUDER_PCAP_FRAME_MAX bytes, well under Marauder's own SNAP_LEN
 * (2324 B on GENERIC_ESP32). A frame longer than the cap is not dropped: the
 * excess bytes are read and discarded (payload_remaining still counts them
 * so the stream stays in sync) and frame_len < orig_len in the callback -
 * exactly PCAP's own "snapshot length" truncation semantics, which
 * Wireshark already renders as "[Frame is marked as truncated]" rather than
 * an error. Most of what sniffraw actually captures (deauth/disassoc ~26 B,
 * most probe/beacon management frames under a couple hundred bytes) fits
 * whole; only unusually large data frames get cut.
 */
#ifndef CADS_MARAUDER_PCAP_H
#define CADS_MARAUDER_PCAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* See this header's own "RAM, NOT FIDELITY" note above for why this is small
 * on purpose. Costs CADS_MARAUDER_PCAP_FRAME_MAX bytes of .bss (one static
 * instance, owned by cads_marauder.c). */
#define CADS_MARAUDER_PCAP_FRAME_MAX 128u

/* The UDP port Wireshark's own udpdump extcap defaults to for TZSP payload
 * (its "Port" field, decode type "tzsp") - matching it means no manual port
 * entry on the Wireshark side, only picking the payload type once. Not this
 * project's choice; it is udpdump's own default. */
#define CADS_MARAUDER_PCAP_UDP_PORT 37008u

/** Called once per decoded record. `frame`/`frame_len` are only valid for
 *  the duration of the call (the same static buffer is reused for the next
 *  record immediately after). `frame_len` is what this parser actually
 *  captured (<= CADS_MARAUDER_PCAP_FRAME_MAX); `orig_len` is the length
 *  Marauder itself reported capturing - the two differ exactly when this
 *  parser truncated. `frame_len` may be 0 for a genuine zero-length record
 *  (rare, but the pcap format allows it) - `frame` is still a valid,
 *  non-NULL pointer in that case, just to zero readable bytes. */
typedef void (*cads_marauder_pcap_frame_cb_t)(
    void* ctx, const uint8_t* frame, uint16_t frame_len, uint32_t orig_len);

/** Called for every byte that is NOT part of a "[BUF/BEGIN]"..."[BUF/CLOSE]"
 *  burst - i.e. ordinary CLI text sharing the same wire. `data`/`len` are
 *  only valid for the duration of the call; `len` is always small (at most
 *  12 - the marker length plus one) since bytes are flushed as soon as they
 *  are known not to be the start of a marker, not batched. A caller that
 *  wants this fed into cads_marauder_reader_feed() (the CLI line reader)
 *  can do so directly from inside this callback. */
typedef void (*cads_marauder_pcap_passthrough_cb_t)(void* ctx, const uint8_t* data, uint8_t len);

typedef enum {
    CADS_MARAUDER_PCAP_STAGE_SCAN_BEGIN = 0, /* outside a burst, scanning for "[BUF/BEGIN]" */
    CADS_MARAUDER_PCAP_STAGE_BOUNDARY,       /* inside a burst, at a record boundary: could be
                                               * the pcap global header, a new record, or
                                               * "[BUF/CLOSE]" - see this header's own note */
    CADS_MARAUDER_PCAP_STAGE_GLOBAL_HDR,     /* inside a burst, discarding the one-time 24 B
                                               * pcap global header (4 already matched as the
                                               * magic; stage_remaining counts the other 20) */
    CADS_MARAUDER_PCAP_STAGE_PAYLOAD,        /* inside a burst, consuming a record's payload */
} cads_marauder_pcap_stage_t;

typedef struct {
    cads_marauder_pcap_stage_t stage;

    uint8_t look[16]; /* SCAN_BEGIN: up to 11 marker bytes. BOUNDARY: up to 16
                        * record-header bytes (magic/close-marker candidates
                        * race over the first few, see cads_marauder_pcap.c). */
    uint8_t look_len;
    bool magic_alive; /* BOUNDARY only: still a possible pcap global header */
    bool close_alive; /* BOUNDARY only: still a possible "[BUF/CLOSE]"      */
    bool committed;   /* BOUNDARY only: both candidates died - look[] is
                        * definitely (the start of) a record header now    */

    uint32_t stage_remaining; /* GLOBAL_HDR: bytes left to discard.
                                * PAYLOAD: bytes of this record's payload left
                                * to consume (captured or truncated-away). */
    uint32_t cur_orig_len;    /* PAYLOAD: the record's own orig_len field */

    uint8_t frame[CADS_MARAUDER_PCAP_FRAME_MAX];
    uint16_t frame_len; /* PAYLOAD: bytes captured into frame[] so far */

    cads_marauder_pcap_frame_cb_t frame_cb;
    void* frame_cb_ctx;
    cads_marauder_pcap_passthrough_cb_t pass_cb;
    void* pass_cb_ctx;
} cads_marauder_pcap_t;

/** Zero every field, including the callbacks - call once at startup, the
 *  same "init" vs "reset" split cads_marauder_reader.h documents on its own
 *  reset (there, resetting mid-session drops the line_cb; here there is no
 *  such per-session state to lose - see this header's "self-describing"
 *  note - so a mid-stream reset is always safe and just resyncs to the next
 *  "[BUF/BEGIN]", but it also silently drops the callbacks, so prefer
 *  cads_marauder_pcap_resync() for anything past first init). */
void cads_marauder_pcap_init(cads_marauder_pcap_t* p);

/** Reset the parse state only (stage/look/frame buffers) without touching
 *  the callbacks - safe to call at any time (e.g. when the owning tool view
 *  is re-entered) to force a resync to the next "[BUF/BEGIN]" rather than
 *  trusting mid-burst state left over from before. */
void cads_marauder_pcap_resync(cads_marauder_pcap_t* p);

void cads_marauder_pcap_set_frame_cb(
    cads_marauder_pcap_t* p, cads_marauder_pcap_frame_cb_t cb, void* ctx);
void cads_marauder_pcap_set_passthrough_cb(
    cads_marauder_pcap_t* p, cads_marauder_pcap_passthrough_cb_t cb, void* ctx);

/** Feed raw bytes as read off the WiFi UART. Drives frame_cb/pass_cb
 *  synchronously, zero or more times each, before returning. */
void cads_marauder_pcap_feed(cads_marauder_pcap_t* p, const uint8_t* data, size_t len);

/* --- TZSP encapsulation ----------------------------------------------------
 *
 * The minimum valid TZSP framing for a received-packet report: a 4 B header
 * (version=1, type=0 "received", encapsulation=18 "IEEE 802.11", the last
 * two big-endian) then a single TAG_END (0x01, no length/value bytes - see
 * Wireshark's own packet-tzsp.c for the tag values this matches) and then
 * the raw frame. No RSSI/channel/timestamp tags - those are optional per
 * the format and this parser does not carry Marauder's own capture
 * timestamp forward (see cads_marauder_pcap_frame_cb_t's own comment: only
 * frame bytes cross that boundary), so there is nothing to put in them yet.
 * Pure function, caller-owned buffer - the same convention
 * modules/netx/cads_netx_frame.h already uses for exactly this "no heap for
 * a staging buffer" reason.
 */
#define CADS_MARAUDER_TZSP_HDR_LEN         5u
#define CADS_MARAUDER_TZSP_ENCAP_IEEE80211 18u

/** Write a TZSP header + `frame` into `out` (capacity `cap`). Returns the
 *  total bytes written (CADS_MARAUDER_TZSP_HDR_LEN + frame_len), or 0 if
 *  `cap` cannot hold it (caller should just skip sending that datagram -
 *  the whole call is otherwise a no-op). */
size_t cads_marauder_tzsp_build(uint8_t* out, size_t cap, const uint8_t* frame, size_t frame_len);

#ifdef __cplusplus
}
#endif

#endif /* CADS_MARAUDER_PCAP_H */

#include "cads_marauder_pcap.h"

#include <string.h>

/* Byte arrays, not C strings - no NUL terminator wanted (would be an
 * unmatchable 12th "byte" no real record header ever contains). */
static const uint8_t CADS_MARAUDER_PCAP_BEGIN[11] = {
    '[', 'B', 'U', 'F', '/', 'B', 'E', 'G', 'I', 'N', ']'};
static const uint8_t CADS_MARAUDER_PCAP_CLOSE[11] = {
    '[', 'B', 'U', 'F', '/', 'C', 'L', 'O', 'S', 'E', ']'};
/* pcap global header magic, on the wire little-endian: D4 C3 B2 A1. */
static const uint8_t CADS_MARAUDER_PCAP_MAGIC[4] = {0xD4u, 0xC3u, 0xB2u, 0xA1u};

static uint32_t cads_marauder_pcap_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void cads_marauder_pcap_enter_boundary(cads_marauder_pcap_t* p) {
    p->stage = CADS_MARAUDER_PCAP_STAGE_BOUNDARY;
    p->look_len = 0u;
    p->magic_alive = true;
    p->close_alive = true;
    p->committed = false;
}

void cads_marauder_pcap_init(cads_marauder_pcap_t* p) {
    memset(p, 0, sizeof(*p));
    p->stage = CADS_MARAUDER_PCAP_STAGE_SCAN_BEGIN;
}

void cads_marauder_pcap_resync(cads_marauder_pcap_t* p) {
    p->stage = CADS_MARAUDER_PCAP_STAGE_SCAN_BEGIN;
    p->look_len = 0u;
    p->magic_alive = false;
    p->close_alive = false;
    p->committed = false;
    p->stage_remaining = 0u;
    p->cur_orig_len = 0u;
    p->frame_len = 0u;
}

void cads_marauder_pcap_set_frame_cb(
    cads_marauder_pcap_t* p, cads_marauder_pcap_frame_cb_t cb, void* ctx) {
    p->frame_cb = cb;
    p->frame_cb_ctx = ctx;
}

void cads_marauder_pcap_set_passthrough_cb(
    cads_marauder_pcap_t* p, cads_marauder_pcap_passthrough_cb_t cb, void* ctx) {
    p->pass_cb = cb;
    p->pass_cb_ctx = ctx;
}

/* Finished collecting 16 header bytes at a BOUNDARY (committed, not the
 * magic/close marker): decode incl_len/orig_len and either emit a
 * zero-length record immediately or move on to PAYLOAD. */
static void cads_marauder_pcap_header_ready(cads_marauder_pcap_t* p) {
    uint32_t incl_len = cads_marauder_pcap_le32(&p->look[8]);
    uint32_t orig_len = cads_marauder_pcap_le32(&p->look[12]);
    p->look_len = 0u;

    if(incl_len == 0u) {
        if(p->frame_cb) p->frame_cb(p->frame_cb_ctx, p->frame, 0u, orig_len);
        cads_marauder_pcap_enter_boundary(p);
        return;
    }

    p->cur_orig_len = orig_len;
    p->frame_len = 0u;
    p->stage_remaining = incl_len;
    p->stage = CADS_MARAUDER_PCAP_STAGE_PAYLOAD;
}

static void cads_marauder_pcap_feed_byte(cads_marauder_pcap_t* p, uint8_t b) {
    switch(p->stage) {
        case CADS_MARAUDER_PCAP_STAGE_SCAN_BEGIN: {
            bool matches = (p->look_len < 11u) && (b == CADS_MARAUDER_PCAP_BEGIN[p->look_len]);
            p->look[p->look_len++] = b;
            if(!matches) {
                if(p->pass_cb) p->pass_cb(p->pass_cb_ctx, p->look, p->look_len);
                p->look_len = 0u;
                return;
            }
            if(p->look_len == 11u) cads_marauder_pcap_enter_boundary(p);
            return;
        }

        case CADS_MARAUDER_PCAP_STAGE_BOUNDARY: {
            if(!p->committed) {
                if(p->magic_alive) {
                    if(p->look_len < 4u && b == CADS_MARAUDER_PCAP_MAGIC[p->look_len]) {
                        /* still alive */
                    } else {
                        p->magic_alive = false;
                    }
                }
                if(p->close_alive) {
                    if(p->look_len < 11u && b == CADS_MARAUDER_PCAP_CLOSE[p->look_len]) {
                        /* still alive */
                    } else {
                        p->close_alive = false;
                    }
                }
                p->look[p->look_len++] = b;

                if(p->magic_alive && p->look_len == 4u) {
                    /* confirmed: the one-time 24 B global pcap header - 4 bytes
                     * already matched as the magic, 20 more to discard. */
                    p->look_len = 0u;
                    p->stage_remaining = 20u;
                    p->stage = CADS_MARAUDER_PCAP_STAGE_GLOBAL_HDR;
                    return;
                }
                if(p->close_alive && p->look_len == 11u) {
                    /* confirmed: end of burst. */
                    p->look_len = 0u;
                    p->stage = CADS_MARAUDER_PCAP_STAGE_SCAN_BEGIN;
                    return;
                }
                if(!p->magic_alive && !p->close_alive) p->committed = true;
            } else {
                p->look[p->look_len++] = b;
            }

            if(p->committed && p->look_len == 16u) cads_marauder_pcap_header_ready(p);
            return;
        }

        case CADS_MARAUDER_PCAP_STAGE_GLOBAL_HDR: {
            p->stage_remaining--;
            if(p->stage_remaining == 0u) cads_marauder_pcap_enter_boundary(p);
            return;
        }

        case CADS_MARAUDER_PCAP_STAGE_PAYLOAD: {
            if(p->frame_len < CADS_MARAUDER_PCAP_FRAME_MAX) p->frame[p->frame_len++] = b;
            p->stage_remaining--;
            if(p->stage_remaining == 0u) {
                if(p->frame_cb) p->frame_cb(p->frame_cb_ctx, p->frame, p->frame_len, p->cur_orig_len);
                cads_marauder_pcap_enter_boundary(p);
            }
            return;
        }
    }
}

void cads_marauder_pcap_feed(cads_marauder_pcap_t* p, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) cads_marauder_pcap_feed_byte(p, data[i]);
}

size_t cads_marauder_tzsp_build(uint8_t* out, size_t cap, const uint8_t* frame, size_t frame_len) {
    if(cap < CADS_MARAUDER_TZSP_HDR_LEN + frame_len) return 0u;
    out[0] = 1u; /* version */
    out[1] = 0u; /* type: TZSP_RX_PACKET (received) */
    out[2] = (uint8_t)(CADS_MARAUDER_TZSP_ENCAP_IEEE80211 >> 8);
    out[3] = (uint8_t)(CADS_MARAUDER_TZSP_ENCAP_IEEE80211 & 0xFFu);
    out[4] = 1u; /* TZSP_HDR_END - no further tags */
    if(frame_len > 0u) memcpy(out + CADS_MARAUDER_TZSP_HDR_LEN, frame, frame_len);
    return CADS_MARAUDER_TZSP_HDR_LEN + frame_len;
}

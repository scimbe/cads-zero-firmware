/* CaDS Zero - Marauder CLI line reader implementation. See the header for
 * why this is split out from cads_marauder.c's GUI/HAL-heavy code. */
#include "cads_marauder_reader.h"

#include <stdbool.h>
#include <string.h>

void cads_marauder_reader_reset(cads_marauder_reader_t* r) {
    memset(r, 0, sizeof(*r));
}

void cads_marauder_reader_set_line_cb(cads_marauder_reader_t* r, cads_marauder_line_cb_t cb, void* ctx) {
    r->line_cb = cb;
    r->line_cb_ctx = ctx;
}

void cads_marauder_reader_set_split_marker(cads_marauder_reader_t* r, const char* marker) {
    r->split_marker = marker;
}

/* True if partial[0..partial_len) ends with marker - marker is always
 * short (CADS_MARAUDER_LINE_LEN-bounded, e.g. "Device: "), so a plain
 * suffix compare is cheap enough to run on every byte fed in. */
static bool cads_marauder_reader_partial_ends_with(
    const char* partial, uint8_t partial_len, const char* marker) {
    size_t marker_len = strlen(marker);
    if(marker_len == 0u || marker_len > partial_len) return false;
    return memcmp(partial + (partial_len - marker_len), marker, marker_len) == 0;
}

static void cads_marauder_reader_push_line(cads_marauder_reader_t* r, const char* text, uint8_t len) {
    uint8_t slot = (uint8_t)((r->head + r->count) % CADS_MARAUDER_OUT_LINES);
    if(r->count == CADS_MARAUDER_OUT_LINES) {
        slot = r->head;
        r->head = (uint8_t)((r->head + 1u) % CADS_MARAUDER_OUT_LINES);
    } else {
        r->count++;
    }
    uint8_t n = len < CADS_MARAUDER_LINE_LEN - 1u ? len : CADS_MARAUDER_LINE_LEN - 1u;
    memcpy(r->lines[slot], text, n);
    r->lines[slot][n] = '\0';
    r->lines_total++;

    if(r->line_cb != NULL) r->line_cb(r->line_cb_ctx, r->lines[slot]);
}

void cads_marauder_reader_feed(cads_marauder_reader_t* r, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        if(byte == '\n') {
            uint8_t n = r->partial_len;
            if(n > 0u && r->partial[n - 1u] == '\r') n--;
            cads_marauder_reader_push_line(r, r->partial, n);
            r->partial_len = 0u;
            continue;
        }
        if(r->partial_len + 1u < CADS_MARAUDER_LINE_LEN) {
            r->partial[r->partial_len++] = (char)byte;
        } else {
            cads_marauder_reader_push_line(r, r->partial, r->partial_len);
            r->partial_len = 0u;
            continue;
        }

        if(r->split_marker != NULL &&
           cads_marauder_reader_partial_ends_with(r->partial, r->partial_len, r->split_marker)) {
            size_t marker_len = strlen(r->split_marker);
            uint8_t before_len = (uint8_t)(r->partial_len - marker_len);
            if(before_len > 0u) {
                cads_marauder_reader_push_line(r, r->partial, before_len);
                memmove(r->partial, r->partial + before_len, marker_len);
                r->partial_len = (uint8_t)marker_len;
            }
            /* before_len == 0: partial IS just the marker so far (the very
             * first device in a burst) - nothing to flush yet, keep
             * accumulating past it. */
        }
    }
}

const char* cads_marauder_reader_line(const cads_marauder_reader_t* r, uint8_t display_index) {
    uint8_t slot = (uint8_t)((r->head + display_index) % CADS_MARAUDER_OUT_LINES);
    return r->lines[slot];
}

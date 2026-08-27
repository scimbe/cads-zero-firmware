/* CaDS Zero - Marauder CLI line reader implementation. See the header for
 * why this is split out from cads_marauder.c's GUI/HAL-heavy code. */
#include "cads_marauder_reader.h"

#include <string.h>

void cads_marauder_reader_reset(cads_marauder_reader_t* r) {
    memset(r, 0, sizeof(*r));
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
        }
    }
}

const char* cads_marauder_reader_line(const cads_marauder_reader_t* r, uint8_t display_index) {
    uint8_t slot = (uint8_t)((r->head + display_index) % CADS_MARAUDER_OUT_LINES);
    return r->lines[slot];
}

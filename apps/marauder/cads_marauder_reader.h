/*
 * CaDS Zero - Marauder CLI line reader. Deliberately has zero HAL/GUI
 * dependencies (no cads_hal.h, no canvas.h) so it can be unit tested with
 * plain synthetic byte arrays - see tests/unit/test_marauder_reader.c.
 * cads_marauder.c owns the only cads_marauder_reader_t instance and is the
 * only caller; this header exists so that instance's type and functions
 * have somewhere to live outside the GUI/HAL-heavy .c file.
 */
#ifndef CADS_MARAUDER_READER_H
#define CADS_MARAUDER_READER_H

#include <stddef.h>
#include <stdint.h>

#define CADS_MARAUDER_OUT_LINES 5u
/* 48, not the panel-width-driven 30 an earlier version of this file used:
 * a real AP scan line ("-76 Ch: 2 fc:34:97:30:ad:21 ESSID: persepolis-XI 11
 * 14", 57 chars incl. the two trailing beacon-interval hex bytes Marauder's
 * WiFiScan.cpp always appends) does not fit in 30 chars, and
 * cads_marauder_join's SSID matcher (cads_marauder_join.c) needs to see a
 * whole "ESSID: <name>" segment unsplit to match reliably - a line that
 * gets overflow-split mid-ESSID is invisible to the matcher. 48 covers
 * every SSID up to ~20 chars unsplit (the large majority of real networks);
 * the 802.11 max of 32 bytes can still split on an unusually long name,
 * which degrades to "not found" rather than a wrong match or a crash. Costs
 * (48-30)*(OUT_LINES+1 partial) = 108 B over the previous 30-char buffer -
 * paid because both the join feature's correctness and the plain display's
 * readability depend on it, not merely cosmetic. Display truncation for the
 * panel width (480 px, area.width-8 usable at cads_font12) still happens at
 * draw time in cads_marauder.c, independent of this parse-time bound. */
#define CADS_MARAUDER_LINE_LEN  48u

/* Called once per completed line, in addition to (not instead of) the ring
 * buffer - a way for something other than the display (e.g. the join
 * scan-and-match state machine, cads_marauder_join.h) to observe every line
 * as it arrives, since the ring only keeps the last OUT_LINES and the UART
 * can only have one reader draining it. `line` is NUL-terminated and valid
 * only for the duration of the call. */
typedef void (*cads_marauder_line_cb_t)(void* ctx, const char* line);

typedef struct {
    char lines[CADS_MARAUDER_OUT_LINES][CADS_MARAUDER_LINE_LEN];
    uint8_t count;             /* valid lines, saturates at OUT_LINES        */
    uint8_t head;              /* index of the OLDEST line (ring)            */
    char partial[CADS_MARAUDER_LINE_LEN];
    uint8_t partial_len;
    uint32_t lines_total;      /* lifetime count, for a future "N lines" status */
    cads_marauder_line_cb_t line_cb;
    void* line_cb_ctx;
    const char* split_marker;  /* borrowed, static string; NULL = off - see setter */
} cads_marauder_reader_t;

void cads_marauder_reader_reset(cads_marauder_reader_t* r);

/** Set (or clear, with cb=NULL) the per-line observer. Reset by
 *  cads_marauder_reader_reset() like everything else in the struct. */
void cads_marauder_reader_set_line_cb(cads_marauder_reader_t* r, cads_marauder_line_cb_t cb, void* ctx);

/**
 * Set (or clear, with marker=NULL) an extra line-break trigger beyond '\n'.
 * Some Marauder CLI output is not one-record-per-line by design - BLE scan
 * results in particular print every discovered device on Marauder's own
 * Serial.print() (not println()) calls, so a whole burst arrives as one
 * unbroken run of "<rssi> Device: <name/addr>" segments with no real
 * newline between them; without this, CADS_MARAUDER_LINE_LEN's hard wrap
 * cuts mid-MAC-address wherever the buffer happens to fill (confirmed live
 * on the panel, 2026-08-28 - see docs/reference/marauder-coprocessor.md).
 * When set, every time the accumulating partial line's tail matches
 * `marker`, whatever came before that match is flushed as its own line and
 * the marker itself starts the next one - so "Device: " turns Marauder's
 * single run-on burst into one device per row, the same as WiFi's scanall
 * output already gets for free from its own real newlines. `marker` is
 * borrowed and must be a string literal or otherwise outlive the reader
 * (same ownership convention as everything else here); cleared along with
 * everything else by cads_marauder_reader_reset(), so re-set it after every
 * reset for a tool that needs it, the same way line_cb already has to be. */
void cads_marauder_reader_set_split_marker(cads_marauder_reader_t* r, const char* marker);

/*
 * Feed raw bytes (as read off the UART) into the reader. Splits on '\n',
 * drops a bare '\r' immediately before it (CRLF line endings), and silently
 * truncates a line longer than the display width - this is a status view,
 * not a faithful terminal, so a long line's tail is acceptable loss. A line
 * that never terminates before the buffer fills is flushed as a line rather
 * than growing unboundedly (bounded buffers, no dynamic allocation, matching
 * modules/wifi's own overflow-resync choice for the same reason).
 */
void cads_marauder_reader_feed(cads_marauder_reader_t* r, const uint8_t* data, size_t len);

/** Oldest-to-newest line at display index 0..count-1. Never NULL; an
 *  out-of-range index returns the slot at that index modulo the ring size,
 *  which for any index the caller should ever pass (0..count-1) is always a
 *  real, populated line. */
const char* cads_marauder_reader_line(const cads_marauder_reader_t* r, uint8_t display_index);

#endif /* CADS_MARAUDER_READER_H */

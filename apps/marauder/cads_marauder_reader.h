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
#define CADS_MARAUDER_LINE_LEN  30u /* fits the panel width at cads_font12 */

typedef struct {
    char lines[CADS_MARAUDER_OUT_LINES][CADS_MARAUDER_LINE_LEN];
    uint8_t count;             /* valid lines, saturates at OUT_LINES        */
    uint8_t head;              /* index of the OLDEST line (ring)            */
    char partial[CADS_MARAUDER_LINE_LEN];
    uint8_t partial_len;
    uint32_t lines_total;      /* lifetime count, for a future "N lines" status */
} cads_marauder_reader_t;

void cads_marauder_reader_reset(cads_marauder_reader_t* r);

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

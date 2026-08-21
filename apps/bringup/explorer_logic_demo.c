/*
 * CaDS Zero - logic analyzer (board side).
 *
 * CAPTURE-THEN-RENDER, NOT LIVE-SCROLLING
 * -------------------------------------------
 * apps/bringup/tasks.c's own rule: "THE DISPLAY HAS EXACTLY ONE
 * FLUSHER" - only the ui task ever calls cads_canvas_flush(), and
 * anything else that draws waits for it via cads_tasks_redraw_sync().
 * A live, continuously-scrolling waveform would mean this command's
 * own sampling loop yielding to that wait after every redraw, and
 * every sample interval that elapsed while blocked there would be
 * lost - measurable, sure (the same "count what you cannot capture"
 * policy explorer_sniff_demo.c and hal_freqcounter.c already use), but
 * a materially bigger, riskier change for this bullet's own "M" sizing
 * than sampling for a fixed window first and rendering the whole
 * result once the ui task is the only thing left with a reason to
 * touch the panel. docs/ROADMAP.md's own "sample rate bounded by how
 * fast the canvas can be redrawn" still holds here - it just applies
 * to how densely `seconds` worth of samples can usefully be drawn
 * across the panel's width, not to a per-sample redraw.
 *
 * THE CAPTURE BUFFER IS THE RENDER SOURCE, NOTHING ELSE
 * -----------------------------------------------------------
 * cads_ring_t (cads/toolbox/ring.h) is the only buffer here: filled
 * during capture, drained sample-by-sample during render, no second
 * array holding the same data twice. Its own "drop when full, keep the
 * oldest" behaviour (see ring.h's own header) means asking for more
 * seconds x sample_rate_hz than CADS_LOGIC_BUFFER_BYTES holds captures
 * the first slice of the window and reports the rest as dropped,
 * honestly, rather than silently truncating the request.
 *
 * SAMPLE FORMAT
 * -------------
 * One uint16_t per tick: bits 0..7 are cads_hal_adapter_inputs()'s own
 * IN0..7 byte, bits 8..13 are cads_hal_adapter_interrupts()'s own
 * INT0..5 byte (already masked to six bits by that function itself).
 */

#include "explorer_logic_demo.h"

#include "cads/toolbox/ring.h"
#include "cads_hal.h"
#include "canvas.h"
#include "hal_sample_timer.h"
#include "input_probe.h"
#include "tasks.h" /* cads_tasks_redraw_sync() */

/* 128 samples, 2 bytes each - a power of two, cads_ring_t's own
 * requirement. Was 256 (512 B): that left targets/itsboard/linker/
 * cads_itsboard.ld's `ASSERT(__cads_heap_size >= 48K, ...)` guard at
 * *exactly* 48K, zero bytes of margin - this session's own recurring
 * lesson is to never leave that at a razor's edge when the choice
 * consuming it is this driver's own, not a shared resource everything
 * else also needs. 128 samples across the ~438 px the waveform area
 * actually has (CADS_CANVAS_WIDTH minus the label margin) is still
 * ~3 px per column - a clearer trace to read than 256 near-1px columns
 * would have been, not just a smaller one. */
#define CADS_LOGIC_BUFFER_BYTES 256u

#define CADS_LOGIC_IN_COUNT  8u
#define CADS_LOGIC_INT_COUNT 6u
#define CADS_LOGIC_CHANNELS  (CADS_LOGIC_IN_COUNT + CADS_LOGIC_INT_COUNT)

#define CADS_LOGIC_ROW_HEIGHT     18
#define CADS_LOGIC_TOP_MARGIN     24
#define CADS_LOGIC_LEFT_MARGIN    42
#define CADS_LOGIC_HIGH_Y_OFFSET  3
#define CADS_LOGIC_LOW_Y_OFFSET   13

static const char* const cads_logic_channel_names[CADS_LOGIC_CHANNELS] = {
    "IN0", "IN1", "IN2", "IN3", "IN4", "IN5", "IN6", "IN7",
    "INT0", "INT1", "INT2", "INT3", "INT4", "INT5",
};

static bool cads_logic_channel_level(uint16_t sample, uint32_t channel) {
    if(channel < CADS_LOGIC_IN_COUNT) return (sample & (1u << channel)) != 0u;
    return (sample & (1u << (8u + (channel - CADS_LOGIC_IN_COUNT)))) != 0u;
}

static void cads_logic_render(cads_ring_t* ring, uint32_t sample_count) {
    cads_canvas_clear(CadsColorBlack);

    cads_rect_t header = {0, 0, CADS_CANVAS_WIDTH, CADS_LOGIC_TOP_MARGIN};
    cads_canvas_draw_text_aligned(
        header, CadsAlignCenter, &cads_font12, "logic analyzer - IN0..7 / INT0..5", CadsColorGrayLight);

    int16_t area_x = CADS_LOGIC_LEFT_MARGIN;
    int16_t area_width = CADS_CANVAS_WIDTH - CADS_LOGIC_LEFT_MARGIN;
    int16_t column_width = sample_count > 0u ? (int16_t)(area_width / (int16_t)sample_count) : 0;
    if(column_width < 1) column_width = 1;

    for(uint32_t channel = 0u; channel < CADS_LOGIC_CHANNELS; channel++) {
        int16_t row_y = (int16_t)(CADS_LOGIC_TOP_MARGIN + channel * CADS_LOGIC_ROW_HEIGHT);
        cads_canvas_draw_text(2, row_y, &cads_font12, cads_logic_channel_names[channel], CadsColorGray);
        cads_canvas_draw_hline(
            area_x, (int16_t)(row_y + CADS_LOGIC_LOW_Y_OFFSET + 1), area_width, CadsColorGrayDark);
    }

    /* One pass over the ring per channel - cads_ring_read() drains it,
     * so re-reading for channel N+1 would find nothing left. Each
     * sample is unpacked into every channel's row before the next
     * sample is read, trading "one read pass" for "one small local
     * variable" rather than the other way round. */
    for(uint32_t i = 0u; i < sample_count; i++) {
        uint16_t sample = 0u;
        if(cads_ring_read(ring, &sample, sizeof(sample)) != sizeof(sample)) break;

        int16_t x = (int16_t)(area_x + (int32_t)(i * (uint32_t)area_width) / (int32_t)sample_count);

        for(uint32_t channel = 0u; channel < CADS_LOGIC_CHANNELS; channel++) {
            int16_t row_y = (int16_t)(CADS_LOGIC_TOP_MARGIN + channel * CADS_LOGIC_ROW_HEIGHT);
            int16_t y = (int16_t)(
                row_y + (cads_logic_channel_level(sample, channel) ? CADS_LOGIC_HIGH_Y_OFFSET
                                                                    : CADS_LOGIC_LOW_Y_OFFSET));
            cads_canvas_draw_hline(x, y, column_width, CadsColorAccent);
        }
    }
}

void cads_explorer_logic_demo(uint32_t sample_rate_hz, uint32_t seconds) {
    /* 25 Hz x 5 s = 125 samples, just inside the 128-sample buffer - a
     * plain `L` with no arguments captures cleanly rather than
     * routinely reporting drops on its own default. */
    if(sample_rate_hz == 0u) sample_rate_hz = 25u;
    if(seconds == 0u) seconds = 5u;

    uint32_t period_us = 1000000u / sample_rate_hz;
    if(period_us == 0u) period_us = 1u;

    static uint8_t storage[CADS_LOGIC_BUFFER_BYTES];
    cads_ring_t ring;
    cads_ring_init(&ring, storage, sizeof(storage));

    cads_probe_puts("# logic: sampling IN0-7/INT0-5 at ");
    cads_probe_put_uint(sample_rate_hz);
    cads_probe_puts("Hz for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    cads_hal_sample_timer_start(period_us);

    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        if(!cads_hal_sample_timer_elapsed()) continue;

        uint16_t sample = (uint16_t)cads_hal_adapter_inputs() |
                           (uint16_t)((uint16_t)(cads_hal_adapter_interrupts() & 0x3Fu) << 8);
        cads_ring_write(&ring, &sample, sizeof(sample));
    }

    cads_hal_sample_timer_stop();

    uint32_t captured = cads_ring_count(&ring) / (uint32_t)sizeof(uint16_t);
    uint32_t dropped_bytes = cads_ring_dropped(&ring);

    cads_probe_puts("# logic: captured ");
    cads_probe_put_uint(captured);
    cads_probe_puts(" sample(s), ");
    cads_probe_put_uint(dropped_bytes / (uint32_t)sizeof(uint16_t));
    cads_probe_puts(" dropped (buffer full)\r\n");

    if(captured == 0u) return;

    cads_logic_render(&ring, captured);

    if(!cads_tasks_redraw_sync(3000u)) {
        cads_probe_puts("# warning: redraw did not complete within 3 s\r\n");
    }
}

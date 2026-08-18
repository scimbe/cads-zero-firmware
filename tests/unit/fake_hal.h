/*
 * A HAL that records instead of doing.
 *
 * Everything above core/cads_hal.h is portable C, which is only useful if
 * something on the host actually satisfies that header. This is the smallest
 * thing that does: the clock is a variable the test advances, the buttons are a
 * byte the test writes, and a blit is appended to a log and composed into an
 * in-memory panel rather than shifted out over SPI.
 *
 * Recording the rectangle matters more than recording the pixels. The canvas'
 * whole reason for existing is that it flushes only what changed, so "which
 * rectangle was pushed" is the assertion that catches a dirty-rectangle
 * regression - and it is exactly the thing a real panel cannot be asked.
 */

#ifndef CADS_FAKE_HAL_H
#define CADS_FAKE_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads_hal.h"

#define CADS_FAKE_MAX_BLITS 256u
#define CADS_FAKE_CONSOLE_BYTES 4096u

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} cads_fake_blit_t;

/** Forget everything: clock, inputs, blit log, console, panel. Call from
 *  setUp() so no test can be influenced by the one before it. */
void cads_fake_reset(void);

/* --- time ----------------------------------------------------------------- */

void cads_fake_set_ms(uint32_t ms);
void cads_fake_advance_ms(uint32_t ms);

/* --- adapter I/O ----------------------------------------------------------- */

/** Bit n set means Sn is pressed: the real HAL has already undone the
 *  active-low wiring by the time cads_hal_adapter_inputs() returns. */
void cads_fake_set_inputs(uint8_t bits);
void cads_fake_set_interrupts(uint8_t bits);
uint16_t cads_fake_outputs(void);

/* --- touch ----------------------------------------------------------------- */

void cads_fake_set_touch(bool pressed, uint16_t x, uint16_t y);

/* --- display --------------------------------------------------------------- */

uint32_t cads_fake_blit_count(void);

/** The `index`th blit since the last reset, or NULL if there was no such blit. */
const cads_fake_blit_t* cads_fake_blit_at(uint32_t index);

/** Total pixels across every blit, which is what cads_canvas_flush() claims to
 *  have transferred. */
uint32_t cads_fake_blit_pixels(void);

/** Bounding box of every blit since the reset. False when nothing was blitted. */
bool cads_fake_blit_bounds(cads_fake_blit_t* bounds);

/** A pixel of the composed panel, exactly as the bytes were handed over - the
 *  canvas stores its palette byte-swapped, so this is big-endian RGB565. */
uint16_t cads_fake_panel_pixel(uint16_t x, uint16_t y);

void cads_fake_panel_fill(uint16_t value);

uint8_t cads_fake_backlight(void);
bool cads_fake_fast_clock(void);

/* --- console --------------------------------------------------------------- */

/** Everything written to the console since the reset, NUL terminated. */
const char* cads_fake_console_text(void);
size_t cads_fake_console_length(void);

/** Bytes the test hands to cads_hal_console_read(). */
void cads_fake_console_feed(const char* text);

#endif /* CADS_FAKE_HAL_H */

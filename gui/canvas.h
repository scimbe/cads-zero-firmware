/*
 * CaDS Zero - drawing surface.
 *
 * WHY 4 BITS PER PIXEL
 * --------------------
 * A full 480x320 RGB565 framebuffer is 300 KB. The STM32F429 has 192 KB of
 * DMA-capable SRAM, so a retained truecolour buffer is simply not on the table.
 * The alternatives were band rendering (no retained state, every widget must
 * redraw itself on demand, painful) or an indexed buffer.
 *
 * 4 bpp indexed costs 75 KB and leaves room for lwIP, which is the whole point
 * of this board. Sixteen colours is not a limitation for this kind of UI - the
 * device we are taking cues from is monochrome - and an indexed buffer has a
 * second advantage: the F429's DMA2D can expand L4 through a CLUT into RGB565
 * in hardware, so the conversion on the way to the panel is close to free.
 *
 * DIRTY RECTANGLES
 * ----------------
 * The panel is fed over a 5.6 MHz SPI link through a shift register chain that
 * costs 16 clocks per pixel. A full screen is ~440 ms. Redrawing everything
 * every frame is therefore not merely wasteful, it is unusable. The canvas
 * tracks one bounding box of everything that changed and flushes only that.
 */

#ifndef CADS_CANVAS_H
#define CADS_CANVAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads_hal.h"

#define CADS_CANVAS_WIDTH  CADS_DISPLAY_WIDTH
#define CADS_CANVAS_HEIGHT CADS_DISPLAY_HEIGHT
#define CADS_CANVAS_STRIDE (CADS_CANVAS_WIDTH / 2) /* two pixels per byte */
#define CADS_PALETTE_SIZE  16

/** Palette slots. Names, not raw indices, so a theme change is one table. */
typedef enum {
    CadsColorBlack = 0,
    CadsColorWhite = 1,
    CadsColorBrand = 2,      /**< CaDS blue   #204C86 */
    CadsColorBrandLight = 3, /**< lion blue   #B5C4D8 */
    CadsColorAccent = 4,     /**< CaDS green  #9CB33B */
    CadsColorGrayDark = 5,
    CadsColorGray = 6,
    CadsColorGrayLight = 7,
    CadsColorRed = 8,
    CadsColorAmber = 9,
    CadsColorTeal = 10,
    CadsColorBrandDark = 11,
    CadsColorSurface = 12,
    CadsColorMagenta = 13,
    CadsColorLionMid = 14,
    CadsColorBackground = 15,
} cads_color_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
} cads_rect_t;

typedef enum {
    CadsAlignLeft,
    CadsAlignCenter,
    CadsAlignRight,
} cads_align_t;

/* --- lifecycle ------------------------------------------------------------ */

void cads_canvas_init(void);

/** Replace a palette entry. Takes effect on the next flush. */
void cads_canvas_set_palette(cads_color_t slot, uint8_t r, uint8_t g, uint8_t b);

/* --- drawing -------------------------------------------------------------- */

void cads_canvas_clear(cads_color_t color);
void cads_canvas_set_pixel(int16_t x, int16_t y, cads_color_t color);
cads_color_t cads_canvas_get_pixel(int16_t x, int16_t y);

void cads_canvas_fill_rect(int16_t x, int16_t y, int16_t width, int16_t height, cads_color_t color);
void cads_canvas_draw_rect(int16_t x, int16_t y, int16_t width, int16_t height, cads_color_t color);
void cads_canvas_draw_hline(int16_t x, int16_t y, int16_t length, cads_color_t color);
void cads_canvas_draw_vline(int16_t x, int16_t y, int16_t length, cads_color_t color);
void cads_canvas_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, cads_color_t color);

/**
 * Draw a 4 bpp indexed image. `data` is packed two pixels per byte, high
 * nibble first, rows padded to whole bytes. Index `transparent` is skipped;
 * pass a value >= 16 to draw every pixel.
 */
void cads_canvas_draw_bitmap4(
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    const uint8_t* data,
    uint8_t transparent);

/* --- clipping ------------------------------------------------------------- */

/** Restrict subsequent drawing to a rectangle. Nested via save/restore. */
void cads_canvas_push_clip(cads_rect_t rect);
void cads_canvas_pop_clip(void);

/* --- damage tracking and output ------------------------------------------- */

/** Mark a rectangle as needing to reach the panel. Drawing calls do this
 *  automatically; widgets that write the buffer directly must call it. */
void cads_canvas_damage(int16_t x, int16_t y, int16_t width, int16_t height);

/** True when anything is waiting to be flushed. */
bool cads_canvas_is_dirty(void);

/**
 * Push the damaged region to the panel and clear the damage.
 * Returns the number of pixels actually transferred, which the performance
 * overlay and the on-target tests both assert against.
 */
uint32_t cads_canvas_flush(void);

/** Direct access for tests and for the simulator's screenshot path. */
const uint8_t* cads_canvas_buffer(void);
const uint16_t* cads_canvas_palette_rgb565(void);

#endif /* CADS_CANVAS_H */

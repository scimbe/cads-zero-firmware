/*
 * CaDS Zero - 4 bpp indexed canvas with dirty-rectangle output.
 *
 * See canvas.h for why the buffer is indexed rather than truecolour.
 *
 * Memory layout note: the framebuffer itself is never handed to DMA - only the
 * staging buffers are - but both live in the .dmaram section anyway. Putting
 * them in CCM would be a silent trap for anyone who later points DMA2D at the
 * framebuffer, and CCM has no DMA access at all.
 */

#include "canvas.h"

#include <string.h>

#define CADS_CLIP_STACK_DEPTH 8

/* One band of RGB565 pixels on its way to the panel. Two of them, so the next
 * band can be converted while the current one is still going out over SPI. */
#define CADS_STAGE_ROWS 16
#define CADS_STAGE_PIXELS (CADS_CANVAS_WIDTH * CADS_STAGE_ROWS)

CADS_DMA_SECTION __attribute__((aligned(4)))
static uint8_t cads_framebuffer[CADS_CANVAS_STRIDE * CADS_CANVAS_HEIGHT];

CADS_DMA_SECTION __attribute__((aligned(4)))
static uint16_t cads_stage[2][CADS_STAGE_PIXELS];

/*
 * Palette in native RGB565.
 *
 * The panel wants the high byte first, but pixels reach it through the SPI's
 * 16-bit frame format, which already transmits most significant byte first.
 * So no swap is needed anywhere - and, more importantly, this is the layout
 * DMA2D produces, which is what lets the hardware accelerator drop straight
 * into the flush path.
 */
static uint16_t cads_palette[CADS_PALETTE_SIZE];

static cads_rect_t cads_clip_stack[CADS_CLIP_STACK_DEPTH];
static uint32_t cads_clip_depth;

static struct {
    int16_t x0, y0, x1, y1; /* half open: x1/y1 are exclusive */
    bool valid;
} cads_damage_box;

/* --- palette --------------------------------------------------------------- */

static uint16_t cads_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t value = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
    return (uint16_t)((value >> 8) | (value << 8));
}

void cads_canvas_set_palette(cads_color_t slot, uint8_t r, uint8_t g, uint8_t b) {
    if((uint32_t)slot >= CADS_PALETTE_SIZE) return;
    cads_palette[slot] = cads_rgb565(r, g, b);
}

static void cads_palette_defaults(void) {
    /* Derived from the CaDS mark: #204C86 blue, #B5C4D8 lion, #9CB33B green. */
    cads_canvas_set_palette(CadsColorBlack, 0x00, 0x00, 0x00);
    cads_canvas_set_palette(CadsColorWhite, 0xFF, 0xFF, 0xFF);
    cads_canvas_set_palette(CadsColorBrand, 0x20, 0x4C, 0x86);
    cads_canvas_set_palette(CadsColorBrandLight, 0xB5, 0xC4, 0xD8);
    cads_canvas_set_palette(CadsColorAccent, 0x9C, 0xB3, 0x3B);
    cads_canvas_set_palette(CadsColorGrayDark, 0x30, 0x35, 0x40);
    cads_canvas_set_palette(CadsColorGray, 0x6B, 0x74, 0x80);
    cads_canvas_set_palette(CadsColorGrayLight, 0xC8, 0xCE, 0xD6);
    cads_canvas_set_palette(CadsColorRed, 0xC0, 0x39, 0x2B);
    cads_canvas_set_palette(CadsColorAmber, 0xE0, 0xA0, 0x00);
    cads_canvas_set_palette(CadsColorTeal, 0x1F, 0x8A, 0x80);
    cads_canvas_set_palette(CadsColorBrandDark, 0x12, 0x30, 0x5A);
    cads_canvas_set_palette(CadsColorSurface, 0xF2, 0xF5, 0xF9);
    cads_canvas_set_palette(CadsColorMagenta, 0xA0, 0x30, 0x70);
    cads_canvas_set_palette(CadsColorLionMid, 0x8F, 0xA6, 0xC4);
    cads_canvas_set_palette(CadsColorBackground, 0x10, 0x14, 0x18);
}

const uint16_t* cads_canvas_palette_rgb565(void) {
    return cads_palette;
}

const uint8_t* cads_canvas_buffer(void) {
    return cads_framebuffer;
}

/* --- clipping -------------------------------------------------------------- */

static cads_rect_t cads_current_clip(void) {
    if(cads_clip_depth == 0u) {
        cads_rect_t full = {0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT};
        return full;
    }
    return cads_clip_stack[cads_clip_depth - 1u];
}

void cads_canvas_push_clip(cads_rect_t rect) {
    if(cads_clip_depth >= CADS_CLIP_STACK_DEPTH) return;

    cads_rect_t parent = cads_current_clip();
    int16_t x0 = rect.x > parent.x ? rect.x : parent.x;
    int16_t y0 = rect.y > parent.y ? rect.y : parent.y;
    int16_t x1 = (rect.x + rect.width) < (parent.x + parent.width) ? (int16_t)(rect.x + rect.width) :
                                                                    (int16_t)(parent.x + parent.width);
    int16_t y1 = (rect.y + rect.height) < (parent.y + parent.height) ?
                     (int16_t)(rect.y + rect.height) :
                     (int16_t)(parent.y + parent.height);

    cads_rect_t clipped = {x0, y0, (int16_t)(x1 > x0 ? x1 - x0 : 0), (int16_t)(y1 > y0 ? y1 - y0 : 0)};
    cads_clip_stack[cads_clip_depth++] = clipped;
}

void cads_canvas_pop_clip(void) {
    if(cads_clip_depth > 0u) cads_clip_depth--;
}

/* --- damage ---------------------------------------------------------------- */

void cads_canvas_damage(int16_t x, int16_t y, int16_t width, int16_t height) {
    if(width <= 0 || height <= 0) return;

    int16_t x1 = (int16_t)(x + width);
    int16_t y1 = (int16_t)(y + height);
    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(x1 > CADS_CANVAS_WIDTH) x1 = CADS_CANVAS_WIDTH;
    if(y1 > CADS_CANVAS_HEIGHT) y1 = CADS_CANVAS_HEIGHT;
    if(x >= x1 || y >= y1) return;

    if(!cads_damage_box.valid) {
        cads_damage_box.x0 = x;
        cads_damage_box.y0 = y;
        cads_damage_box.x1 = x1;
        cads_damage_box.y1 = y1;
        cads_damage_box.valid = true;
        return;
    }
    if(x < cads_damage_box.x0) cads_damage_box.x0 = x;
    if(y < cads_damage_box.y0) cads_damage_box.y0 = y;
    if(x1 > cads_damage_box.x1) cads_damage_box.x1 = x1;
    if(y1 > cads_damage_box.y1) cads_damage_box.y1 = y1;
}

bool cads_canvas_is_dirty(void) {
    return cads_damage_box.valid;
}

/* --- pixel access ---------------------------------------------------------- */

void cads_canvas_set_pixel(int16_t x, int16_t y, cads_color_t color) {
    cads_rect_t clip = cads_current_clip();
    if(x < clip.x || y < clip.y || x >= clip.x + clip.width || y >= clip.y + clip.height) return;

    uint32_t offset = (uint32_t)y * CADS_CANVAS_STRIDE + ((uint32_t)x >> 1);
    uint8_t value = (uint8_t)(color & 0x0Fu);
    if(x & 1) {
        cads_framebuffer[offset] = (uint8_t)((cads_framebuffer[offset] & 0xF0u) | value);
    } else {
        cads_framebuffer[offset] = (uint8_t)((cads_framebuffer[offset] & 0x0Fu) | (value << 4));
    }
    cads_canvas_damage(x, y, 1, 1);
}

cads_color_t cads_canvas_get_pixel(int16_t x, int16_t y) {
    if(x < 0 || y < 0 || x >= CADS_CANVAS_WIDTH || y >= CADS_CANVAS_HEIGHT) return CadsColorBlack;
    uint32_t offset = (uint32_t)y * CADS_CANVAS_STRIDE + ((uint32_t)x >> 1);
    uint8_t byte = cads_framebuffer[offset];
    return (cads_color_t)((x & 1) ? (byte & 0x0Fu) : (byte >> 4));
}

/* --- shapes ---------------------------------------------------------------- */

void cads_canvas_fill_rect(int16_t x, int16_t y, int16_t width, int16_t height, cads_color_t color) {
    cads_rect_t clip = cads_current_clip();

    int16_t x0 = x > clip.x ? x : clip.x;
    int16_t y0 = y > clip.y ? y : clip.y;
    int16_t x1 = (int16_t)(x + width);
    int16_t y1 = (int16_t)(y + height);
    if(x1 > clip.x + clip.width) x1 = (int16_t)(clip.x + clip.width);
    if(y1 > clip.y + clip.height) y1 = (int16_t)(clip.y + clip.height);
    if(x0 >= x1 || y0 >= y1) return;

    uint8_t index = (uint8_t)(color & 0x0Fu);
    uint8_t both = (uint8_t)((index << 4) | index);

    for(int16_t row = y0; row < y1; row++) {
        uint8_t* line = &cads_framebuffer[(uint32_t)row * CADS_CANVAS_STRIDE];
        int16_t column = x0;

        /* Ragged left edge: one nibble. */
        if(column & 1) {
            uint32_t offset = (uint32_t)column >> 1;
            line[offset] = (uint8_t)((line[offset] & 0xF0u) | index);
            column++;
        }
        /* Aligned middle: whole bytes, two pixels at a time. */
        int16_t aligned_end = (int16_t)(x1 & ~1);
        if(aligned_end > column) {
            memset(&line[(uint32_t)column >> 1], both, (size_t)((aligned_end - column) >> 1));
            column = aligned_end;
        }
        /* Ragged right edge. */
        if(column < x1) {
            uint32_t offset = (uint32_t)column >> 1;
            line[offset] = (uint8_t)((line[offset] & 0x0Fu) | (index << 4));
        }
    }

    cads_canvas_damage(x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0));
}

void cads_canvas_clear(cads_color_t color) {
    uint8_t index = (uint8_t)(color & 0x0Fu);
    memset(cads_framebuffer, (uint8_t)((index << 4) | index), sizeof(cads_framebuffer));
    cads_canvas_damage(0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT);
}

void cads_canvas_draw_hline(int16_t x, int16_t y, int16_t length, cads_color_t color) {
    cads_canvas_fill_rect(x, y, length, 1, color);
}

void cads_canvas_draw_vline(int16_t x, int16_t y, int16_t length, cads_color_t color) {
    cads_canvas_fill_rect(x, y, 1, length, color);
}

void cads_canvas_draw_rect(int16_t x, int16_t y, int16_t width, int16_t height, cads_color_t color) {
    if(width <= 0 || height <= 0) return;
    cads_canvas_draw_hline(x, y, width, color);
    cads_canvas_draw_hline(x, (int16_t)(y + height - 1), width, color);
    cads_canvas_draw_vline(x, y, height, color);
    cads_canvas_draw_vline((int16_t)(x + width - 1), y, height, color);
}

void cads_canvas_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, cads_color_t color) {
    /* Bresenham. */
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx - dy;

    for(;;) {
        cads_canvas_set_pixel(x0, y0, color);
        if(x0 == x1 && y0 == y1) break;
        int32_t doubled = error * 2;
        if(doubled > -dy) {
            error -= dy;
            x0 = (int16_t)(x0 + sx);
        }
        if(doubled < dx) {
            error += dx;
            y0 = (int16_t)(y0 + sy);
        }
    }
}

void cads_canvas_draw_bitmap4(
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    const uint8_t* data,
    uint8_t transparent) {
    uint32_t row_bytes = ((uint32_t)width + 1u) / 2u;

    for(int16_t row = 0; row < height; row++) {
        const uint8_t* source = &data[(uint32_t)row * row_bytes];
        for(int16_t column = 0; column < width; column++) {
            uint8_t byte = source[column >> 1];
            uint8_t index = (column & 1) ? (byte & 0x0Fu) : (byte >> 4);
            if(index == transparent) continue;
            cads_canvas_set_pixel((int16_t)(x + column), (int16_t)(y + row), (cads_color_t)index);
        }
    }
}

void cads_canvas_draw_image(int16_t x, int16_t y, const cads_image_t* image) {
    cads_canvas_draw_bitmap4(
        x, y, (int16_t)image->width, (int16_t)image->height, image->data, image->transparent);
}

/* --- text ------------------------------------------------------------------ */

static const cads_glyph_t* cads_glyph_for(const cads_font_t* font, char character) {
    uint8_t code = (uint8_t)character;
    if(code < font->first || code > font->last) {
        /* Anything outside the baked range renders as a space rather than as
         * a garbage glyph or a crash. */
        code = (uint8_t)' ';
        if(code < font->first || code > font->last) return NULL;
    }
    return &font->glyphs[code - font->first];
}

int16_t cads_canvas_text_width(const cads_font_t* font, const char* text) {
    int16_t width = 0;
    for(const char* p = text; *p; p++) {
        const cads_glyph_t* glyph = cads_glyph_for(font, *p);
        if(glyph) width = (int16_t)(width + glyph->advance);
    }
    return width;
}

int16_t cads_canvas_draw_text(
    int16_t x,
    int16_t y,
    const cads_font_t* font,
    const char* text,
    cads_color_t color) {
    cads_rect_t clip = cads_current_clip();
    int16_t pen = x;
    int16_t min_y = CADS_CANVAS_HEIGHT;
    int16_t max_y = 0;

    for(const char* p = text; *p; p++) {
        const cads_glyph_t* glyph = cads_glyph_for(font, *p);
        if(!glyph) continue;

        if(glyph->width && glyph->height) {
            int16_t gx = (int16_t)(pen + glyph->left);
            int16_t gy = (int16_t)(y + glyph->top);
            uint32_t row_bytes = ((uint32_t)glyph->width + 7u) / 8u;

            /* Whole glyph outside the clip: skip the inner loops entirely.
             * Text is drawn often enough that this is worth the branch. */
            if(gx < clip.x + clip.width && gx + glyph->width > clip.x &&
               gy < clip.y + clip.height && gy + glyph->height > clip.y) {
                for(uint32_t row = 0; row < glyph->height; row++) {
                    const uint8_t* line = &font->bitmap[glyph->offset + row * row_bytes];
                    for(uint32_t column = 0; column < glyph->width; column++) {
                        if(line[column >> 3] & (0x80u >> (column & 7u))) {
                            /* The baked glyphs are 1bpp with no antialiasing
                             * (gui/fonts/cads_fonts.c's own header explains
                             * why), which on the physical panel reads as too
                             * thin to resolve at a glance - confirmed against
                             * the real hardware, not just a rendering
                             * preference. Smearing every lit pixel one column
                             * right is the standard cheap emboldening trick
                             * for a fixed bitmap font: it thickens vertical
                             * and diagonal strokes without needing a second
                             * baked weight, at the cost of the glyph reading
                             * very slightly wider than its measured advance -
                             * acceptable on a monospace face with normal
                             * side bearing. */
                            cads_canvas_set_pixel(
                                (int16_t)(gx + column), (int16_t)(gy + row), color);
                            cads_canvas_set_pixel(
                                (int16_t)(gx + column + 1), (int16_t)(gy + row), color);
                        }
                    }
                }
                if(gy < min_y) min_y = gy;
                if(gy + glyph->height > max_y) max_y = (int16_t)(gy + glyph->height);
            }
        }
        pen = (int16_t)(pen + glyph->advance);
    }

    /* set_pixel already damaged each lit pixel, but a run of text produces
     * thousands of one-pixel damages; one rectangle for the whole run keeps
     * the bounding box tight and costs nothing. */
    if(max_y > min_y) {
        cads_canvas_damage(x, min_y, (int16_t)(pen - x), (int16_t)(max_y - min_y));
    }
    return pen;
}

void cads_canvas_draw_text_aligned(
    cads_rect_t box,
    cads_align_t align,
    const cads_font_t* font,
    const char* text,
    cads_color_t color) {
    int16_t width = cads_canvas_text_width(font, text);
    int16_t x = box.x;

    if(align == CadsAlignCenter) {
        x = (int16_t)(box.x + (box.width - width) / 2);
    } else if(align == CadsAlignRight) {
        x = (int16_t)(box.x + box.width - width);
    }

    int16_t y = (int16_t)(box.y + (box.height - font->line_height) / 2);

    cads_canvas_push_clip(box);
    cads_canvas_draw_text(x, y, font, text, color);
    cads_canvas_pop_clip();
}

/* --- flush ----------------------------------------------------------------- */

/**
 * Expand one band of the 4 bpp framebuffer into RGB565.
 *
 * This is the hot loop of the whole display path. It reads a byte, which is two
 * pixels, and writes two halfwords - so the source is touched half as often as
 * the destination, which is exactly the point of the indexed format.
 *
 * M1 replaces this with DMA2D in L4-to-RGB565 mode; the software path stays as
 * the reference the on-target test compares against.
 */
static void cads_expand_band(
    uint16_t* destination,
    int16_t x0,
    int16_t y0,
    int16_t width,
    int16_t rows) {
    for(int16_t row = 0; row < rows; row++) {
        const uint8_t* line = &cads_framebuffer[(uint32_t)(y0 + row) * CADS_CANVAS_STRIDE];
        uint16_t* out = &destination[(uint32_t)row * width];
        int16_t column = x0;
        int16_t remaining = width;

        if(column & 1) {
            *out++ = cads_palette[line[column >> 1] & 0x0Fu];
            column++;
            remaining--;
        }
        while(remaining >= 2) {
            uint8_t byte = line[column >> 1];
            *out++ = cads_palette[byte >> 4];
            *out++ = cads_palette[byte & 0x0Fu];
            column = (int16_t)(column + 2);
            remaining = (int16_t)(remaining - 2);
        }
        if(remaining) {
            *out = cads_palette[line[column >> 1] >> 4];
        }
    }
}

uint32_t cads_canvas_flush(void) {
    if(!cads_damage_box.valid) return 0u;

    int16_t x0 = cads_damage_box.x0;
    int16_t y0 = cads_damage_box.y0;
    int16_t width = (int16_t)(cads_damage_box.x1 - x0);
    int16_t height = (int16_t)(cads_damage_box.y1 - y0);
    cads_damage_box.valid = false;

    if(width <= 0 || height <= 0) return 0u;

    int16_t rows_per_band = (int16_t)(CADS_STAGE_PIXELS / width);
    if(rows_per_band < 1) rows_per_band = 1;
    if(rows_per_band > CADS_STAGE_ROWS * CADS_CANVAS_WIDTH / width) {
        rows_per_band = (int16_t)(CADS_STAGE_ROWS * CADS_CANVAS_WIDTH / width);
    }

    uint32_t pixels = 0u;
    uint32_t bank = 0u;

    for(int16_t row = 0; row < height; row += rows_per_band) {
        int16_t rows = (int16_t)((height - row) < rows_per_band ? (height - row) : rows_per_band);

        /* Convert into the bank the panel is not currently reading from. */
        cads_expand_band(cads_stage[bank], x0, (int16_t)(y0 + row), width, rows);

        cads_hal_display_blit(
            (uint16_t)x0, (uint16_t)(y0 + row), (uint16_t)width, (uint16_t)rows, cads_stage[bank]);

        pixels += (uint32_t)width * rows;
        bank ^= 1u;
    }

    return pixels;
}

void cads_canvas_init(void) {
    cads_palette_defaults();
    cads_clip_depth = 0u;
    cads_damage_box.valid = false;
    memset(cads_framebuffer, 0, sizeof(cads_framebuffer));
    cads_canvas_damage(0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT);
}

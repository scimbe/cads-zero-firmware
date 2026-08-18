/*
 * CaDS Zero - bitmap font format.
 *
 * Glyphs are 1 bpp so they can be drawn in any of the canvas's sixteen palette
 * colours; an antialiased glyph would need intermediate shades that an indexed
 * buffer cannot supply without burning palette slots on grey ramps. See
 * scripts/gen_font.py.
 */

#ifndef CADS_FONT_H
#define CADS_FONT_H

#include <stdint.h>

typedef struct {
    uint8_t width;   /**< bitmap width in pixels                            */
    uint8_t height;  /**< bitmap height in pixels                           */
    int16_t left;    /**< horizontal bearing from the pen position          */
    int16_t top;     /**< rows from the line top down to the bitmap top     */
    uint8_t advance; /**< how far the pen moves after this glyph            */
    uint16_t offset; /**< start of this glyph's rows in the bitmap blob     */
} cads_glyph_t;

typedef struct {
    const char* name;
    uint8_t line_height;
    uint8_t ascent;
    uint8_t first; /**< first code point present, inclusive                 */
    uint8_t last;  /**< last code point present, inclusive                  */
    const cads_glyph_t* glyphs;
    const uint8_t* bitmap;
} cads_font_t;

extern const cads_font_t cads_font12;
extern const cads_font_t cads_font16;
extern const cads_font_t cads_font24;

#endif /* CADS_FONT_H */

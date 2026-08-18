/*
 * CaDS Zero - read-only scrolling text, and the word wrapper behind it.
 *
 * WHY THE CALLER SUPPLIES THE LINE INDEX
 * --------------------------------------
 * Scrolling needs random access to line starts, and there is no allocator on a
 * 192 KB device. Wrapping is therefore done once, up front, into an array the
 * caller owns and sizes: a help screen that is 40 lines long declares
 * cads_text_line_t[40] and is done, and the cost is visible in the app's own
 * .bss rather than hidden in a heap that does not exist.
 *
 * Rewrapping happens on set_text() and set_area() only. Drawing never wraps,
 * so scrolling a line costs one row of glyphs.
 */

#ifndef CADS_TEXTBOX_H
#define CADS_TEXTBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canvas.h"
#include "input/cads_input.h"

/** Longest single line the drawing path will render. Lines longer than this
 *  are hard-broken by the wrapper, so the stack buffer can never overflow. */
#define CADS_TEXT_LINE_MAX 128

/** One wrapped line: a byte range into the original text, no copy. */
typedef struct {
    uint16_t offset;
    uint16_t length;
} cads_text_line_t;

/**
 * Break `text` into lines no wider than `width` pixels, at spaces where
 * possible and mid-word where not. Honours embedded '\n'. Writes at most `max`
 * lines and returns how many were produced; a return equal to `max` means the
 * text was truncated.
 */
size_t cads_text_wrap(
    const cads_font_t* font,
    const char* text,
    int16_t width,
    cads_text_line_t* lines,
    size_t max);

/** Draw one wrapped line with its top-left at (x, y). */
void cads_text_draw_line(
    int16_t x,
    int16_t y,
    const cads_font_t* font,
    const char* text,
    cads_text_line_t line,
    cads_color_t color);

typedef struct {
    /* --- private --- */
    cads_rect_t area;
    const cads_font_t* font;
    const char* text;
    cads_text_line_t* lines;
    size_t line_capacity;
    size_t line_count;
    size_t top;
    size_t visible;
    int16_t row_height;
    bool dirty;

    bool dragging;
    int16_t drag_origin_y;
    size_t drag_origin_top;
} cads_textbox_t;

/**
 * Set up a text box over caller-owned line storage.
 *
 * `text` and `lines` are borrowed and must outlive the box. The text is not
 * wrapped until the area is known, so call cads_textbox_set_area() next.
 */
void cads_textbox_init(
    cads_textbox_t* box,
    const char* text,
    const cads_font_t* font,
    cads_text_line_t* lines,
    size_t line_capacity);

/** Set the rectangle and rewrap to its width. */
void cads_textbox_set_area(cads_textbox_t* box, cads_rect_t area);

/** Replace the text and rewrap, resetting the scroll to the top. */
void cads_textbox_set_text(cads_textbox_t* box, const char* text);

/**
 * Up and Down scroll one line, Left and Right a page, and a drag scrolls
 * directly. Returns true when the event was consumed. OK and Back are left
 * alone: a text box is not a chooser, and Back is the view dispatcher's.
 */
bool cads_textbox_input(cads_textbox_t* box, const cads_input_event_t* event);

/** Scroll so that wrapped line `line` is at the top. */
void cads_textbox_scroll_to(cads_textbox_t* box, size_t line);

size_t cads_textbox_line_count(const cads_textbox_t* box);

bool cads_textbox_is_dirty(const cads_textbox_t* box);

/** Whole area: scrolling changes every line, so there is nothing finer to
 *  report. Kept for symmetry with the other widgets. */
cads_rect_t cads_textbox_damage(const cads_textbox_t* box);

void cads_textbox_invalidate(cads_textbox_t* box);
void cads_textbox_draw(cads_textbox_t* box);

#endif /* CADS_TEXTBOX_H */

/*
 * CaDS Zero - scrolling selection list.
 *
 * The generic machinery under cads_menu: a window onto N rows with one
 * selection, driven by Up/Down/OK from the key rail and by tap and drag from
 * the touch rail. It knows nothing about what a row contains - the caller
 * supplies a row renderer - so a file browser, a register dump and a settings
 * page share one implementation and one set of scrolling bugs.
 *
 * WHY THE DAMAGE BOOKKEEPING LOOKS LIKE THIS
 * ------------------------------------------
 * Repainting the whole list to move a selection down one row would cost the
 * best part of a full-screen transfer, and a full screen is 448 ms
 * (docs/explanation/dirty-rectangles.md). So the list records which *rows*
 * changed, not that "something changed": moving the selection inside the
 * visible window damages exactly two rows, about 9 ms at this panel's
 * throughput. Only a scroll - where every row shows different content - falls
 * back to repainting the window.
 *
 * STORAGE
 * -------
 * None is allocated. The caller owns the item data; the list holds a count and
 * a callback and never touches the items itself.
 */

#ifndef CADS_LIST_H
#define CADS_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canvas.h"
#include "input/cads_input.h"

/** Extra pixels above and below the glyphs of a row. */
#define CADS_LIST_ROW_PADDING 6

/** Width of the scroll indicator down the right edge. */
#define CADS_LIST_SCROLLBAR_WIDTH 4

/**
 * Paint one row. `row` is the rectangle to fill - the list has already clipped
 * to it and expects the callback to cover it completely, background included,
 * because nothing else erases the previous contents.
 */
typedef void (*cads_list_row_draw_t)(size_t index, cads_rect_t row, bool selected, void* context);

/** Called when the selection is activated by OK or by a tap. */
typedef void (*cads_list_activate_t)(size_t index, void* context);

typedef struct {
    /* --- private; use the functions --- */
    cads_rect_t area;
    const cads_font_t* font;
    size_t count;
    size_t selected;
    size_t top;
    int16_t row_height;
    size_t visible;
    bool wrap;
    /* Ground for the strip below the last row - the only pixels the list
     * paints itself. Defaults to CadsColorBackground so existing dark screens
     * (apps/gpio) are unchanged; a light menu sets it to CadsColorSurface via
     * cads_list_set_background() so its tail matches its own light rows. */
    cads_color_t background;

    cads_list_row_draw_t row_draw;
    cads_list_activate_t activate;
    void* context;

    bool dirty_all;
    bool dirty_rows;
    size_t dirty_first;
    size_t dirty_last;

    bool dragging;
    bool drag_moved;
    int16_t drag_origin_y;
    size_t drag_origin_top;
} cads_list_t;

/**
 * Set up a list over `count` rows drawn by `row_draw`.
 *
 * `font` decides the row height and must outlive the list; pass &cads_font16
 * unless there is a reason not to. The area defaults to empty - call
 * cads_list_set_area() before drawing.
 */
void cads_list_init(
    cads_list_t* list,
    size_t count,
    const cads_font_t* font,
    cads_list_row_draw_t row_draw,
    void* context);

/** Install the activation handler. Optional; without it OK does nothing. */
void cads_list_set_activate(cads_list_t* list, cads_list_activate_t activate);

/** Ground for the tail strip below the last row. Defaults to
 *  CadsColorBackground; a light list sets CadsColorSurface to match its rows. */
void cads_list_set_background(cads_list_t* list, cads_color_t color);

/** Set the rectangle the list occupies. Recomputes how many rows fit. */
void cads_list_set_area(cads_list_t* list, cads_rect_t area);

/** Change the row count, clamping the selection and the scroll position. */
void cads_list_set_count(cads_list_t* list, size_t count);

/** Wrap the selection from the last row to the first. Off by default: a menu
 *  that wraps makes "hold Down to reach the bottom" never terminate. */
void cads_list_set_wrap(cads_list_t* list, bool wrap);

size_t cads_list_selected(const cads_list_t* list);
size_t cads_list_count(const cads_list_t* list);

/** Move the selection, scrolling it into view if needed. */
void cads_list_set_selected(cads_list_t* list, size_t index);

/**
 * Offer an event to the list. Returns true when it was consumed.
 *
 * Handles Up/Down (Press, Repeat and Long), OK, and the touch rail: a tap
 * selects and activates the row under the finger, a drag scrolls and suppresses
 * the tap. Back is deliberately not handled - navigation belongs to the view
 * dispatcher, and a list that swallowed Back would trap the user.
 */
bool cads_list_input(cads_list_t* list, const cads_input_event_t* event);

bool cads_list_is_dirty(const cads_list_t* list);

/** Bounding box of the rows awaiting redraw. Undefined when not dirty. */
cads_rect_t cads_list_damage(const cads_list_t* list);

void cads_list_invalidate(cads_list_t* list);

/** Repaint only the rows that changed, then clear the dirty state. */
void cads_list_draw(cads_list_t* list);

/** Rectangle of one row, valid only while the row is on screen. */
cads_rect_t cads_list_row_rect(const cads_list_t* list, size_t index);

#endif /* CADS_LIST_H */

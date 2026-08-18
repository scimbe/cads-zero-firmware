/*
 * CaDS Zero - the compositor.
 *
 * Owns the screen layout and the frame loop: an optional status bar at the top,
 * the current view's content in the middle, the soft-key strip along the
 * bottom. It composites those three layers and issues exactly one
 * cads_canvas_flush() per frame, and only when something is dirty.
 *
 * ONE FLUSH, AND ONLY WHEN DIRTY
 * ------------------------------
 * The canvas keeps a single damage bounding box, so two flushes in a frame cost
 * two panel transfers, and a flush with nothing to say still costs the window
 * commands. A full screen is 448 ms on this hardware
 * (docs/explanation/dirty-rectangles.md), so the frame loop is built around
 * doing nothing at all in the common case: cads_gui_tick() returns 0 without
 * touching the bus when no layer has changed.
 *
 * THE TWO INPUT RAILS MEET HERE
 * -----------------------------
 * A touch on the soft-key strip is turned into the key event the physical
 * button would have produced, and then routed down exactly the same path. Below
 * this file nothing can tell the difference, which is what makes "every action
 * reachable by touch is reachable by button" a property of the framework rather
 * than a promise each app has to keep.
 *
 * LAYOUT
 * ------
 * Derived from CADS_CANVAS_WIDTH/HEIGHT and from the widgets' own constants.
 * Hiding the status bar gives its band to the content; the same goes for the
 * strip, though an app that hides the strip has just made its keys unlabelled
 * and should have a good reason.
 */

#ifndef CADS_GUI_H
#define CADS_GUI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canvas.h"
#include "cads_softkeys.h"
#include "cads_statusbar.h"
#include "cads_view_dispatcher.h"
#include "input/cads_input.h"

typedef struct {
    /* --- private --- */
    cads_view_dispatcher_t* dispatcher;
    cads_statusbar_t* statusbar;
    cads_softkeys_t* softkeys;

    cads_rect_t content;
    cads_color_t background;

    uint32_t seen_generation;
    bool recompose;
    bool started;
} cads_gui_t;

/**
 * Wire the compositor to a dispatcher and its chrome.
 *
 * `statusbar` and `softkeys` may be NULL, in which case that band is not
 * reserved. All three are borrowed and must outlive the GUI. The layout is
 * computed here, so views registered afterwards still receive the right area -
 * the compositor assigns it on entry, not on registration.
 */
void cads_gui_init(
    cads_gui_t* gui,
    cads_view_dispatcher_t* dispatcher,
    cads_statusbar_t* statusbar,
    cads_softkeys_t* softkeys);

/** Background colour of the content band. Applied on the next full repaint. */
void cads_gui_set_background(cads_gui_t* gui, cads_color_t color);

/** The rectangle granted to the current view. */
cads_rect_t cads_gui_content(const cads_gui_t* gui);

/**
 * Feed one input event in.
 *
 * Touches on the strip become key events; everything else goes to the current
 * view through the dispatcher. Safe to call from the input service's callback -
 * it draws nothing, it only updates state.
 */
void cads_gui_input(cads_gui_t* gui, const cads_input_event_t* event);

/**
 * Composite and flush if anything changed.
 *
 * Returns the number of pixels transferred, or 0 when the frame was free. Call
 * once per iteration of the main loop with the current millisecond clock; the
 * clock drives repeat events for a soft key held by a finger.
 */
uint32_t cads_gui_tick(cads_gui_t* gui, uint32_t now_ms);

/** Force a full repaint of every layer on the next tick. Costs a full-screen
 *  transfer, so reserve it for a palette change or a resume from blanking. */
void cads_gui_invalidate(cads_gui_t* gui);

/**
 * Install this GUI as the input service's handler.
 *
 * There is one input callback in the system and one compositor, so this stores
 * a file-scope pointer. Calling it for a second GUI replaces the first.
 */
void cads_gui_attach_input(cads_gui_t* gui);

/** Remove the input handler. */
void cads_gui_detach_input(void);

#endif /* CADS_GUI_H */

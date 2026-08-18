/*
 * CaDS Zero - one screen's worth of state.
 *
 * A view is four callbacks and a context pointer. It owns no storage of its
 * own beyond the damage bookkeeping, so an app declares its views statically
 * alongside the widgets they contain.
 *
 * DRAWING IS PULL, NOT PUSH
 * -------------------------
 * A view never draws in response to input. It records *what* changed with
 * cads_view_dirty_rect() and returns; the compositor decides when to call
 * draw(), sets a clip to the accumulated damage first, and flushes once. This
 * is the whole reason the framework can guarantee that a menu selection moving
 * one row costs 9 ms instead of the 448 ms a full screen costs
 * (docs/explanation/dirty-rectangles.md).
 *
 * The clip means correctness is free and efficiency is opt-in: a draw callback
 * that repaints everything is *correct* whatever damage was reported, because
 * the clip discards the rest and the damage box never grows past what was
 * declared. A callback that consults its widgets' own dirty state is *fast*.
 *
 * CONTRACT
 * --------
 *   draw(area, context)   area is the content rectangle the compositor granted,
 *                         already clipped to the damage. Called only when the
 *                         view is dirty.
 *   input(event, context) return true if the event was consumed. An unconsumed
 *                         Back is what pops the navigation stack, so a view
 *                         that consumes everything can never be left.
 *   enter(context)        the view became current. Reload data here; the
 *                         compositor marks the view fully dirty around it, so
 *                         there is no need to invalidate by hand.
 *   exit(context)         the view stopped being current. Release nothing that
 *                         the next enter() cannot rebuild.
 */

#ifndef CADS_VIEW_H
#define CADS_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canvas.h"
#include "cads_softkeys.h"
#include "input/cads_input.h"

typedef void (*cads_view_draw_t)(cads_rect_t area, void* context);
typedef bool (*cads_view_input_t)(const cads_input_event_t* event, void* context);
typedef void (*cads_view_enter_t)(void* context);
typedef void (*cads_view_exit_t)(void* context);

typedef struct {
    /* --- private; set through the functions below --- */
    cads_view_draw_t draw;
    cads_view_input_t input;
    cads_view_enter_t enter;
    cads_view_exit_t exit;
    void* context;

    const char* title;
    const cads_softkey_t* keys;
    size_t key_count;

    cads_rect_t area;
    cads_rect_t damage;
    bool damage_valid;
} cads_view_t;

/** Minimum viable view: a draw callback, an input callback and a context.
 *  Either callback may be NULL - a static splash needs no input handler. */
void cads_view_init(
    cads_view_t* view, cads_view_draw_t draw, cads_view_input_t input, void* context);

/** Optional lifecycle callbacks, for views that load data on entry. */
void cads_view_set_lifecycle(cads_view_t* view, cads_view_enter_t enter, cads_view_exit_t exit);

/**
 * Declare the soft-key labels this view wants.
 *
 * The array is borrowed and must outlive the view. The compositor applies it
 * whenever the view becomes current, which is what makes the strip show the
 * truth after every navigation without each view remembering to repaint it.
 */
void cads_view_set_softkeys(cads_view_t* view, const cads_softkey_t* keys, size_t count);

/** Text for the status bar's left slot while this view is current. */
void cads_view_set_title(cads_view_t* view, const char* title);

const char* cads_view_title(const cads_view_t* view);

/** The content rectangle the compositor granted. Valid from the first enter(). */
cads_rect_t cads_view_area(const cads_view_t* view);

/** Mark the whole content area for redraw. */
void cads_view_dirty(cads_view_t* view);

/** Add a rectangle to the pending damage. Cheaper than cads_view_dirty() by
 *  exactly the ratio of the rectangle to the screen - which on this panel is
 *  the difference between a responsive UI and a sluggish one. */
void cads_view_dirty_rect(cads_view_t* view, cads_rect_t rect);

bool cads_view_is_dirty(const cads_view_t* view);

/* --- called by the compositor --------------------------------------------- */

/** Assign the content rectangle and mark the view fully dirty. */
void cads_view_set_area(cads_view_t* view, cads_rect_t area);

/** Hand over the accumulated damage and clear it. False when nothing is
 *  pending, in which case `out` is untouched. */
bool cads_view_take_damage(cads_view_t* view, cads_rect_t* out);

void cads_view_enter(cads_view_t* view);
void cads_view_exit(cads_view_t* view);

/** Invoke the draw callback over `area`. The compositor has already set the
 *  clip to the damage, so the callback may paint as much of `area` as it
 *  likes. */
void cads_view_render(cads_view_t* view, cads_rect_t area);

/** Route an event. Returns what the view's input callback returned, or false
 *  when it has none. */
bool cads_view_handle_input(cads_view_t* view, const cads_input_event_t* event);

const cads_softkey_t* cads_view_softkeys(const cads_view_t* view, size_t* count);

#endif /* CADS_VIEW_H */

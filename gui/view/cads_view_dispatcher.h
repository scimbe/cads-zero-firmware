/*
 * CaDS Zero - view registry, navigation stack and input router.
 *
 * Holds a set of registered views, keeps a stack of the ones the user walked
 * into, routes events to whichever is on top, and pops it when Back goes
 * unconsumed.
 *
 * WHY A STACK RATHER THAN EACH VIEW KNOWING ITS PARENT
 * ----------------------------------------------------
 * A view that hard-codes where Back leads can only ever be reached from one
 * place. A settings page opened from the desktop and from an app's menu is the
 * same view; only the path differs, so the path is what gets recorded.
 *
 * WHY IT DOES NOT DRAW
 * --------------------
 * The dispatcher tracks *what* is current and *what* is dirty; the compositor
 * (cads_gui) decides when pixels move. Keeping those apart is what stops a
 * "just redraw it" call appearing in an input handler, which on this panel
 * costs 448 ms (docs/explanation/dirty-rectangles.md).
 *
 * STORAGE
 * -------
 * Caller-supplied, as everywhere else: an array of entries and an array of
 * stack slots, both sized by the app.
 */

#ifndef CADS_VIEW_DISPATCHER_H
#define CADS_VIEW_DISPATCHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads_view.h"

/** Reserved id meaning "no view". Apps number their views from 0. */
#define CADS_VIEW_ID_NONE 0xFFFFFFFFu

typedef struct {
    uint32_t id;
    cads_view_t* view;
} cads_view_entry_t;

/** Called when Back is pressed with nothing left to pop - the app is being
 *  asked to close. Optional; without it Back at the root does nothing. */
typedef void (*cads_view_exhausted_t)(void* context);

typedef struct {
    /* --- private --- */
    cads_view_entry_t* entries;
    size_t capacity;
    size_t count;

    uint32_t* stack;
    size_t stack_capacity;
    size_t depth;

    uint32_t generation; /**< bumped whenever the current view changes */

    cads_view_exhausted_t on_exhausted;
    void* context;
} cads_view_dispatcher_t;

/**
 * Set up a dispatcher over caller-owned arrays.
 *
 * `entries` holds up to `capacity` registered views; `stack` holds up to
 * `stack_depth` levels of navigation. Both must outlive the dispatcher. A depth
 * of 4 covers the roadmap's deepest path with room to spare.
 */
void cads_view_dispatcher_init(
    cads_view_dispatcher_t* dispatcher,
    cads_view_entry_t* entries,
    size_t capacity,
    uint32_t* stack,
    size_t stack_depth);

void cads_view_dispatcher_set_exhausted(
    cads_view_dispatcher_t* dispatcher, cads_view_exhausted_t callback, void* context);

/** Register a view under an id. False when the table is full or the id is
 *  already taken - ids are the app's own enum, so a clash is a bug. */
bool cads_view_dispatcher_add(cads_view_dispatcher_t* dispatcher, uint32_t id, cads_view_t* view);

cads_view_t* cads_view_dispatcher_find(const cads_view_dispatcher_t* dispatcher, uint32_t id);

/** Replace the top of the stack. Use for peers - tabs, or a wizard's steps -
 *  where Back should not walk through everything already seen. */
bool cads_view_dispatcher_switch_to(cads_view_dispatcher_t* dispatcher, uint32_t id);

/** Push a view. Back returns to the one below it. */
bool cads_view_dispatcher_push(cads_view_dispatcher_t* dispatcher, uint32_t id);

/** Pop to the previous view. False when already at the root. */
bool cads_view_dispatcher_pop(cads_view_dispatcher_t* dispatcher);

/** Drop everything above the root, for the USER button's "back to desktop". */
void cads_view_dispatcher_pop_to_root(cads_view_dispatcher_t* dispatcher);

cads_view_t* cads_view_dispatcher_current(const cads_view_dispatcher_t* dispatcher);
uint32_t cads_view_dispatcher_current_id(const cads_view_dispatcher_t* dispatcher);
size_t cads_view_dispatcher_depth(const cads_view_dispatcher_t* dispatcher);

/**
 * Route an event to the current view.
 *
 * A Release of CadsKeyBack that the view did not consume pops the stack, so
 * Back works everywhere by default and a view only writes code for it when it
 * wants something else - an editor asking to discard changes, say.
 *
 * Returns true when the event was consumed by the view or by navigation.
 */
bool cads_view_dispatcher_input(
    cads_view_dispatcher_t* dispatcher, const cads_input_event_t* event);

/** Changes whenever the current view changes. The compositor watches this to
 *  know when to reapply the title, the soft keys and a full repaint. */
uint32_t cads_view_dispatcher_generation(const cads_view_dispatcher_t* dispatcher);

#endif /* CADS_VIEW_DISPATCHER_H */

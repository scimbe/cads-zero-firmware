# `gui/view` — views, navigation and the compositor

## What is it?

Three pieces. `cads_view` is one screen's worth of state: four callbacks, a
context pointer, and a record of what it has damaged since the last frame.
`cads_view_dispatcher` owns the registered views, keeps the navigation stack the
user walked into, and routes events to whichever view is on top. `cads_gui` is
the compositor: it owns the screen layout — status bar, content, soft-key strip —
composites those layers and issues exactly one `cads_canvas_flush()` per frame,
and only when something changed.

A whole application's frame loop is:

```c
while(running) {
    cads_input_tick();
    cads_gui_tick(&gui, cads_hal_ticks_ms());
}
```

## Why is it shaped this way?

**Drawing is pull, never push.** A view does not draw in response to input. Its
input callback records *what changed* with `cads_view_dirty_rect()` and returns;
the compositor decides when to paint, sets a clip to the accumulated damage
first, calls `draw()`, and flushes once. A full screen costs 448 ms on this panel
([why dirty rectangles are mandatory](../../docs/explanation/dirty-rectangles.md)),
so a `cads_canvas_flush()` inside an input handler is the difference between a
responsive device and an unusable one. Keeping the two apart is what makes that
mistake impossible to write by accident.

The clip has a second effect worth stating plainly: **correctness is free,
efficiency is opt-in**. A draw callback that repaints its whole content area is
*correct* whatever damage was declared, because the clip discards the rest and
the canvas's damage box never grows past the declared rectangle. A callback that
consults its widgets' own dirty state is *fast*. Nothing silently breaks if an
author writes the simple version first.

**The compositor schedules layers instead of painting them all.** This is the
least obvious decision here and the one that matters most. The canvas keeps a
single damage bounding box, not a list — a deliberate simplification, documented
as such. But the soft-key strip is at the bottom of the screen and the content is
above it, and *every keypress dirties both*: the key highlights its cell, and the
action changes the content. One box around both is very nearly the whole screen.
Measured on the menu case:

| | pixels | at 342 kpixel/s |
|---|---|---|
| One box around strip and content | 139 944 | 409 ms |
| Content this frame, strip the next | 26 656 + 2 400 | 85 ms |

So a frame admits a layer only when adding it does not cost more than painting it
alone would have — union area no greater than the sum of the parts. A layer that
fails the test keeps its dirty state and gets a later frame. Content goes first,
because it is what the user is looking at; the chrome catches up the moment the
content stops changing, which for an event-driven UI is the very next frame. The
threshold is arithmetic rather than a tuned constant, so there is nothing to
re-tune for a different panel.

**Back is a stack, not a field on each view.** A view that hard-codes where Back
leads can only ever be reached from one place, and a settings page opened from
the desktop and from an app's menu is the same view with a different path. So the
path is what gets recorded. A `Release` of `CadsKeyBack` that the current view did
not consume pops the stack, which means Back works everywhere by default and a
view writes code for it only when it wants something else — an editor asking to
discard changes, say.

**The two input rails meet in the compositor.** A touch on the soft-key strip is
converted to the key event the physical button would have produced and then
routed down exactly the same path. Below `cads_gui.c` nothing can tell which rail
an action arrived on. That is what makes
*[every action reachable by touch is reachable by button](../../docs/explanation/input-scheme.md)*
a property of the framework rather than a per-app promise.

**A view learns its area before `enter()` runs.** `enter()` is where widgets size
themselves, so the dispatcher assigns the content rectangle first. Getting this
backwards is not a crash — it is a widget that quietly measures itself against a
zero rectangle and displays nothing, which is exactly the kind of bug that costs
an afternoon.

**No dynamic allocation, here as everywhere.** The dispatcher's view table and
navigation stack are caller-supplied arrays. The whole framework compiles to
about 8 KB of flash and no `.bss` at all.

## How do I use it?

A complete two-view application. This compiles.

```c
#include "cads_gui.h"
#include "cads_menu.h"

enum { VIEW_MENU = 0, VIEW_ABOUT = 1 };

static const cads_menu_item_t items[] = {
    {"Applications", NULL,   0},
    {"Network",      "up",   1},
    {"About",        "v0.1", 2},
};

static const cads_softkey_t menu_keys[] = {
    {CadsKeyUp,   "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk,   "Open"},
    {CadsKeyBack, "Exit"},
};

static cads_menu_t menu;
static cads_view_t menu_view;
static cads_view_t about_view;

static cads_view_dispatcher_t dispatcher;
static cads_view_entry_t entries[2];
static uint32_t stack[4];

static cads_statusbar_t statusbar;
static cads_softkeys_t softkeys;
static cads_gui_t gui;

/* --- the menu view --- */

static void menu_enter(void* context) {
    (void)context;
    /* The area is already assigned when enter() runs. */
    cads_menu_set_area(&menu, cads_view_area(&menu_view));
}

static void menu_draw(cads_rect_t area, void* context) {
    (void)area;
    (void)context;
    cads_menu_draw(&menu);
}

static bool menu_input(const cads_input_event_t* event, void* context) {
    (void)context;
    if(!cads_menu_input(&menu, event)) return false;
    /* Two lines is the whole damage contract: what moved, and that it moved. */
    cads_view_dirty_rect(&menu_view, cads_menu_damage(&menu));
    return true;
}

static void menu_activate(const cads_menu_item_t* item, size_t index, void* context) {
    (void)index;
    (void)context;
    if(item->id == 2u) cads_view_dispatcher_push(&dispatcher, VIEW_ABOUT);
}

/* --- the about view --- */

static void about_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_canvas_draw_text_aligned(area, CadsAlignCenter, &cads_font24, "CaDS Zero", CadsColorWhite);
}

/* --- wiring --- */

void app_init(void) {
    cads_canvas_init();

    cads_menu_init(&menu, items, sizeof(items) / sizeof(items[0]), &cads_font16);
    cads_menu_set_activate(&menu, menu_activate, NULL);

    cads_view_init(&menu_view, menu_draw, menu_input, NULL);
    cads_view_set_lifecycle(&menu_view, menu_enter, NULL);
    cads_view_set_title(&menu_view, "CaDS Zero");
    cads_view_set_softkeys(&menu_view, menu_keys, sizeof(menu_keys) / sizeof(menu_keys[0]));

    /* No input callback: Back is unconsumed, so the dispatcher pops for us. */
    cads_view_init(&about_view, about_draw, NULL, NULL);
    cads_view_set_title(&about_view, "About");

    cads_view_dispatcher_init(&dispatcher, entries, 2, stack, 4);
    cads_view_dispatcher_add(&dispatcher, VIEW_MENU, &menu_view);
    cads_view_dispatcher_add(&dispatcher, VIEW_ABOUT, &about_view);

    cads_statusbar_init(&statusbar);
    cads_softkeys_init(&softkeys);
    cads_gui_init(&gui, &dispatcher, &statusbar, &softkeys);
    cads_gui_attach_input(&gui);

    /* Initialise the GUI before the first navigation: that is what gives the
     * view its area in time for enter(). */
    cads_view_dispatcher_switch_to(&dispatcher, VIEW_MENU);
}

void app_run(void) {
    for(;;) {
        cads_input_tick();
        cads_gui_tick(&gui, cads_hal_ticks_ms());
    }
}
```

Order matters in exactly one place, and it is called out in the comment above:
`cads_gui_init()` before the first `switch_to`, because that is what hands the
content rectangle to the dispatcher.

Layout is derived from `CADS_CANVAS_WIDTH` and `CADS_CANVAS_HEIGHT`, not written
down. At 480×320 the status bar is 26 px, the soft-key strip is 40 px with 60 px
cells, and the content band is 480×254. Passing `NULL` for the status bar or the
strip gives that band to the content.

## What are the limits?

- **One view is current at a time.** There is no overlay layer and no
  half-transparent modal. `cads_dialog` draws inside the current view's content
  area and the view routes to it; that covers the modal case without a
  compositing pass the bus cannot afford anyway.
- **A view that dirties itself every frame starves the chrome.** The layer
  scheduler always gives content the frame. Nothing in the roadmap does this, and
  the fix if something ever does is a damage *list* in the canvas rather than more
  policy in the compositor — measure first.
- **No frame clock and no animation.** `cads_gui_tick()` paints when something is
  dirty and returns 0 otherwise. There is no timer service for a view to animate
  against, on purpose: at 342 kpixel/s animation is not a thing this panel does.
- **The navigation stack has no arguments.** Pushing a view does not pass it a
  parameter; a view that needs context reads it from the app state its `enter()`
  can see. Adding a payload would mean either allocation or a union of every
  possible argument type.
- **One compositor per system.** `cads_gui_attach_input()` installs the input
  service's single callback. Attaching a second GUI replaces the first.
- **The dispatcher does not own the USER button.** Short press to the desktop and
  long press to terminate are system-level escapes that live above this layer;
  `cads_view_dispatcher_pop_to_root()` is what the desktop path calls into.
- **No re-entrancy.** Calling `cads_view_dispatcher_push()` from inside a view's
  `draw()` is undefined. Navigate from input callbacks, which is where it belongs.

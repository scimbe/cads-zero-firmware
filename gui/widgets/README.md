# `gui/widgets` — the on-screen furniture

## What is it?

Six widgets that draw and take input, and nothing else. `cads_softkeys` is the
labelled strip along the bottom edge that names what each of the eight physical
buttons currently does. `cads_statusbar` is the top strip: a title on the left,
fixed indicator slots on the right. `cads_list` is a scrolling window over N rows
with one selection, and `cads_menu` is the common case of it — label, optional
detail, an id handed back on activation. `cads_dialog` is a modal box with a
title, a wrapped message and up to three answers bound to keys. `cads_textbox` is
read-only scrolling text with word wrap.

None of them knows about views, the navigation stack or the frame loop. They
depend on `gui/canvas.h` for drawing and on `services/input/cads_input.h` for the
event types, which is what lets them be exercised on the host without a
compositor — the numbers quoted below come from doing exactly that.

## Why is it shaped this way?

**A full-screen redraw costs 448 ms.** Measured on the board, 2026-08-17
([why dirty rectangles are mandatory](../../docs/explanation/dirty-rectangles.md)).
That single fact is the reason every widget here carries dirty-state bookkeeping
that a desktop widget would not bother with. A menu that repainted itself to move
a selection one row would take about a third of a second to respond to a button
press. So `cads_list` records which *rows* changed rather than that "something
changed": a selection moving inside the visible window damages exactly two rows —
26 656 pixels, 78 ms — and only a scroll, where every row genuinely shows
different content, repaints the window.

The same reasoning shapes `cads_statusbar`. Its indicator slots are fixed width,
which wastes a few pixels, because the alternative — packing indicators
right-to-left at their natural widths — means the clock ticking from `9:59` to
`10:00` shifts everything to its left and damages the whole bar once a minute.
Fixed cells buy the guarantee that redrawing a slot damages that slot.

**The eight buttons are a row, not a D-pad.** They sit in a line directly under
the display, so the display labels them
([the input scheme](../../docs/explanation/input-scheme.md)). This is the ATM and
oscilloscope idiom, and it works for the reason it works there: the meaning is
written above the button, so nothing has to be memorised and an app with unusual
actions does not have to bend them into a cross.

**Touch is a peer input, not a fallback.** A touch landing on a soft-key cell
synthesises exactly the key event the physical button would have produced,
repeats included and with the same timings. Everything downstream sees one event
stream. That is what makes *every action reachable by touch is reachable by
button, and vice versa* a property of the code rather than a promise each app has
to keep — and `cads_dialog` extends it by exporting its answers as soft-key
labels, so the same three choices are reachable both ways without the caller
writing them out twice.

**No dynamic allocation.** This is a 192 KB device with no allocator. Every
widget takes caller-supplied storage: an app declares its items as a static array
and hands the widget a pointer and a count. Strings are borrowed, never copied.
`cads_textbox` goes further and makes the caller supply the line index array,
because scrolling needs random access to line starts and the cost should be
visible in the app's own `.bss` rather than hidden in a heap that does not exist.

## How do I use it?

Each widget follows the same four-call contract: feed it input, ask whether it is
dirty, ask what it damaged, draw. Drawing repaints only what changed and clears
the dirty state.

```c
#include "cads_menu.h"

static const cads_menu_item_t items[] = {
    {"Applications", NULL,   0},
    {"GPIO",         "16",   1},
    {"Network",      "up",   2},
    {"Settings",     NULL,   3},
};

static cads_menu_t menu;

static void on_activate(const cads_menu_item_t* item, size_t index, void* context) {
    (void)index;
    (void)context;
    open_thing(item->id);
}

void screen_init(cads_rect_t area) {
    cads_menu_init(&menu, items, sizeof(items) / sizeof(items[0]), &cads_font16);
    cads_menu_set_activate(&menu, on_activate, NULL);
    cads_menu_set_area(&menu, area);
}

/* Return true when the event was consumed, and tell the caller what moved. */
bool screen_input(const cads_input_event_t* event, cads_rect_t* damage) {
    if(!cads_menu_input(&menu, event)) return false;
    *damage = cads_menu_damage(&menu);
    return true;
}

void screen_draw(void) {
    cads_menu_draw(&menu);
}
```

Under `gui/view` this is three lines shorter still, because the compositor calls
`screen_draw` for you and clips it to the damage you reported.

The soft-key strip is declared once per screen and applied by the compositor:

```c
static const cads_softkey_t keys[] = {
    {CadsKeyUp,   "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk,   "Open"},   /* relabelled, still the OK slot */
    {CadsKeyBack, "Exit"},
    {CadsKeyF1,   "Sort"},
};
```

Keys not named go blank and become inert. An app that calls `cads_input_bind()`
to move a key to a different button calls `cads_softkeys_bind()` with the same
pair, so the label follows; the input service owns the real binding and exposes
no getter, and a strip that silently disagreed with the hardware would be worse
than no strip at all.

## What are the limits?

- **No text entry.** No keyboard, no editable field, no cursor. Nothing in the
  roadmap needs one, and a soft keyboard on a 480×320 resistive panel is a
  project of its own.
- **No animation, no transitions.** At 342 kpixel/s a slide transition between
  two screens is roughly a second of bus time. The framework has no frame clock
  for a widget to animate against, deliberately.
- **`cads_list` scrolls by whole rows.** Pixel-smooth scrolling would damage the
  whole window on every touch move sample; snapping to rows damages it once per
  row crossed.
- **Single touch only, and only tap and drag.** The XPT2046 on a resistive panel
  is single-point and noisy. Pinch and multi-finger gestures would be inventing
  precision the hardware does not have.
- **`cads_dialog` does not seize input.** There is no event loop inside a widget
  to seize it with. Modality is three lines in the view that owns the dialog:
  offer events to it first and return while its result is still pending.
- **`cads_textbox` wraps once, on `set_area()` and `set_text()`.** Text that
  changes every frame will rewrap every frame. It is meant for help screens and
  logs, not for a live console.
- **Wrapped lines are capped at `CADS_TEXT_LINE_MAX` (128) bytes** and hard-broken
  beyond it, so the drawing path can use a stack buffer that cannot overflow.
- **ASCII only**, because the fonts are: code points 0x20 to 0x7E.

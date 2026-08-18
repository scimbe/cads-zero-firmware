/*
 * CaDS Zero - soft-key strip.
 *
 * The eight buttons S0..S7 sit in a physical row directly under the display, so
 * the display labels them: a strip along the bottom edge, eight cells wide,
 * each cell naming what the button below it does right now. This is the ATM and
 * oscilloscope idiom, and it is chosen for the property that makes it work
 * there - the meaning is written above the button, so nothing is memorised.
 * See docs/explanation/input-scheme.md.
 *
 * TOUCH IS A PEER, NOT A FALLBACK
 * -------------------------------
 * A touch landing on a cell synthesises exactly the key event the physical
 * button would have produced, including repeats while the finger is held. The
 * rest of the framework therefore sees one event stream and never has to ask
 * which rail an action arrived on. That is what makes "every action reachable
 * by touch is reachable by button, and vice versa" true by construction rather
 * than by each app remembering to implement it twice.
 *
 * OWNERSHIP
 * ---------
 * Label strings are borrowed, never copied. They must outlive the strip - which
 * is free for the usual case of string literals in a static table.
 */

#ifndef CADS_SOFTKEYS_H
#define CADS_SOFTKEYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canvas.h"
#include "input/cads_input.h"

/** One cell per physical button; the row is the hardware, not a choice. */
#define CADS_SOFTKEY_COUNT CADS_BUTTON_COUNT

/*
 * Geometry is derived from the canvas, not written down, so a different panel
 * needs no edits here. At 480x320 this is the 40 px band the input scheme
 * costs, with 60 px cells.
 */
#define CADS_SOFTKEYS_HEIGHT    ((int16_t)(CADS_CANVAS_HEIGHT / 8))
#define CADS_SOFTKEY_CELL_WIDTH ((int16_t)(CADS_CANVAS_WIDTH / CADS_SOFTKEY_COUNT))

/** A label bound to a logical key. `label` NULL or "" leaves the cell blank
 *  and the key inert, which is how an app says "this key does nothing here". */
typedef struct {
    cads_key_t key;
    const char* label;
} cads_softkey_t;

typedef struct {
    /* --- public, set through the functions below --- */
    cads_rect_t area;

    /* --- private --- */
    const char* labels[CADS_SOFTKEY_COUNT];   /**< resolved per cell   */
    cads_key_t cell_key[CADS_SOFTKEY_COUNT];  /**< key each cell fires */
    uint8_t bind[CADS_SOFTKEY_COUNT];         /**< key -> cell index   */
    uint8_t dirty;                            /**< one bit per cell    */
    int8_t held;                              /**< highlighted cell, -1 none */
    bool held_by_touch;                       /**< we own its repeats  */
    uint32_t next_repeat_ms;
    uint32_t long_due_ms;
    bool long_sent;
} cads_softkeys_t;

/** Reset to the bottom strip, identity key-to-cell binding, all cells blank. */
void cads_softkeys_init(cads_softkeys_t* keys);

/** Move the strip. The compositor calls this; apps normally do not. */
void cads_softkeys_set_area(cads_softkeys_t* keys, cads_rect_t area);

/**
 * Mirror a `cads_input_bind()` call so the label follows the key.
 *
 * The input service owns the real binding and exposes no getter, so an app that
 * rebinds a key tells the strip too. Kept explicit rather than inferred,
 * because a strip that silently disagreed with the hardware would be worse than
 * no strip at all.
 */
void cads_softkeys_bind(cads_softkeys_t* keys, cads_key_t key, uint8_t cell);

/** Replace every label. Cells not named in `set` go blank. */
void cads_softkeys_set(cads_softkeys_t* keys, const cads_softkey_t* set, size_t count);

/** Relabel one key without disturbing the others. */
void cads_softkeys_set_label(cads_softkeys_t* keys, cads_key_t key, const char* label);

/** Show a key as pressed. The compositor drives this from physical events so
 *  both rails give the same visual feedback. */
void cads_softkeys_highlight(cads_softkeys_t* keys, cads_key_t key, bool down);

/**
 * Offer a touch event to the strip.
 *
 * Returns true when the touch belonged to the strip and must not be forwarded
 * to the view. `out_key` is always written; `out_key->key == CadsKeyNone` means
 * the touch was consumed without producing a key event (a move within a cell,
 * say). A finger that slides off the cell it pressed cancels: no Release is
 * emitted, which is the standard touch escape and the reason click semantics
 * belong on Release rather than Press.
 */
bool cads_softkeys_touch(
    cads_softkeys_t* keys,
    const cads_input_event_t* touch,
    cads_input_event_t* out_key);

/**
 * Emit Repeat and Long events for a cell held by touch.
 *
 * Returns true when `out_key` was filled. Call once per frame with the current
 * millisecond clock; timings match the physical rail exactly (see
 * CADS_INPUT_REPEAT_* in cads_input.h), because a list must scroll the same
 * whichever rail is holding it down.
 */
bool cads_softkeys_tick(cads_softkeys_t* keys, uint32_t now_ms, cads_input_event_t* out_key);

/* --- redraw --------------------------------------------------------------- */

bool cads_softkeys_is_dirty(const cads_softkeys_t* keys);

/** Bounding box of the cells awaiting redraw. Undefined when not dirty. */
cads_rect_t cads_softkeys_damage(const cads_softkeys_t* keys);

/** Force the whole strip to repaint on the next draw. */
void cads_softkeys_invalidate(cads_softkeys_t* keys);

/** Repaint only the cells whose content changed, then clear the dirty set. */
void cads_softkeys_draw(cads_softkeys_t* keys);

#endif /* CADS_SOFTKEYS_H */

/*
 * CaDS Zero - status bar.
 *
 * A thin strip along the top edge: a title on the left, a fixed number of
 * indicator slots on the right for things like link state, storage and the
 * clock.
 *
 * WHY THE SLOTS ARE FIXED WIDTH
 * -----------------------------
 * A full-screen transfer costs 448 ms on this panel, so damage has to stay
 * small (docs/explanation/dirty-rectangles.md). If indicators were packed
 * right-to-left at their natural widths, the clock ticking from "9:59" to
 * "10:00" would shift every indicator to its left and damage the whole bar
 * once a minute. Fixed cells cost a little space and buy the guarantee that a
 * slot's redraw damages that slot and nothing else - which is the entire point
 * of having slots rather than one string.
 *
 * OWNERSHIP
 * ---------
 * Strings are borrowed, never copied. A caller that formats a clock into a
 * buffer keeps that buffer alive and calls the setter again when it changes.
 */

#ifndef CADS_STATUSBAR_H
#define CADS_STATUSBAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canvas.h"

/** Four is what the roadmap needs: network, storage, battery-or-load, clock. */
#define CADS_STATUSBAR_SLOTS 4

/* Derived from the canvas so a different panel needs no edits. At 480x320 this
 * is a 26 px bar - font12 is 17 px tall - with 60 px indicator cells. */
#define CADS_STATUSBAR_HEIGHT     ((int16_t)(CADS_CANVAS_HEIGHT / 12))
#define CADS_STATUSBAR_SLOT_WIDTH ((int16_t)(CADS_CANVAS_WIDTH / 8))

typedef struct {
    /* --- public --- */
    cads_rect_t area;

    /* --- private --- */
    const char* title;
    const char* slots[CADS_STATUSBAR_SLOTS];
    uint8_t dirty; /**< bit 0 = title, bit 1+n = slot n */
} cads_statusbar_t;

/** Reset to the top strip with no title and no indicators. */
void cads_statusbar_init(cads_statusbar_t* bar);

/** Move the bar. The compositor calls this; apps normally do not. */
void cads_statusbar_set_area(cads_statusbar_t* bar, cads_rect_t area);

/** Left-hand text, usually the current view's title. NULL clears it. */
void cads_statusbar_set_title(cads_statusbar_t* bar, const char* title);

/**
 * Set indicator `slot`, counted from the right: slot 0 is the rightmost cell.
 * NULL or "" blanks the cell. Setting a slot to the pointer it already holds
 * is free and marks nothing dirty, so a caller may call this every frame.
 */
void cads_statusbar_set_indicator(cads_statusbar_t* bar, size_t slot, const char* text);

bool cads_statusbar_is_dirty(const cads_statusbar_t* bar);

/** Bounding box of the parts awaiting redraw. Undefined when not dirty. */
cads_rect_t cads_statusbar_damage(const cads_statusbar_t* bar);

void cads_statusbar_invalidate(cads_statusbar_t* bar);

/** Repaint only the changed title or slots, then clear the dirty set. */
void cads_statusbar_draw(cads_statusbar_t* bar);

#endif /* CADS_STATUSBAR_H */

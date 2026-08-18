/*
 * CaDS Zero - modal dialog.
 *
 * A centred box with a title, a wrapped message and up to three answers, each
 * bound to a key. The answers are drawn as buttons inside the box *and*
 * exported as soft-key labels, so the same three choices are reachable by
 * finger and by button - the equivalence the input scheme requires, made
 * concrete rather than left to each caller.
 *
 * MODALITY IS THE CALLER'S JOB
 * ----------------------------
 * The dialog does not seize the input stream, because nothing here can: there
 * is no event loop inside a widget. A view that wants a modal dialog offers
 * events to it first and returns without consulting its other widgets while
 * cads_dialog_result() is still pending. That is three lines in the view and
 * keeps the widget free of hidden control flow.
 */

#ifndef CADS_DIALOG_H
#define CADS_DIALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads_softkeys.h"
#include "cads_textbox.h"
#include "canvas.h"
#include "input/cads_input.h"

/** Three is the useful maximum: yes / no / cancel, or save / discard / back. */
#define CADS_DIALOG_ANSWERS   3
#define CADS_DIALOG_MAX_LINES 6

/** No answer chosen yet. */
#define CADS_DIALOG_PENDING (-1)

typedef struct {
    cads_key_t key;
    const char* label;
} cads_dialog_answer_t;

typedef struct {
    /* --- private --- */
    cads_rect_t area;
    const char* title;
    const char* message;
    cads_dialog_answer_t answers[CADS_DIALOG_ANSWERS];
    size_t answer_count;
    cads_text_line_t lines[CADS_DIALOG_MAX_LINES];
    size_t line_count;
    int8_t result;
    bool dirty;
} cads_dialog_t;

/**
 * Fill in a dialog. Title, message and answer labels are borrowed and must
 * outlive it. At most CADS_DIALOG_ANSWERS answers are kept; the rest are
 * ignored rather than silently rearranged.
 */
void cads_dialog_init(
    cads_dialog_t* dialog,
    const char* title,
    const char* message,
    const cads_dialog_answer_t* answers,
    size_t answer_count);

/** Centre the box inside `within`, sizing it to the message. */
void cads_dialog_layout(cads_dialog_t* dialog, cads_rect_t within);

/** Place the box explicitly, for a caller that wants its own geometry. */
void cads_dialog_set_area(cads_dialog_t* dialog, cads_rect_t area);

/**
 * Offer an event. Returns true when the dialog consumed it, which - while the
 * result is pending - is every key event and every touch, so a modal view can
 * simply return this value.
 */
bool cads_dialog_input(cads_dialog_t* dialog, const cads_input_event_t* event);

/** Index of the chosen answer, or CADS_DIALOG_PENDING. */
int cads_dialog_result(const cads_dialog_t* dialog);

/** Clear the answer so the dialog can be shown again. */
void cads_dialog_reset(cads_dialog_t* dialog);

/**
 * Write the answers as soft-key labels, so the strip shows the same choices
 * the box does. Returns how many entries were written.
 */
size_t cads_dialog_softkeys(const cads_dialog_t* dialog, cads_softkey_t* out, size_t max);

bool cads_dialog_is_dirty(const cads_dialog_t* dialog);
cads_rect_t cads_dialog_damage(const cads_dialog_t* dialog);
void cads_dialog_invalidate(cads_dialog_t* dialog);
void cads_dialog_draw(cads_dialog_t* dialog);

#endif /* CADS_DIALOG_H */

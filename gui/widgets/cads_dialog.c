#include "cads_dialog.h"

#define CADS_DIALOG_PAD           10
#define CADS_DIALOG_BUTTON_HEIGHT 26

static const cads_font_t* cads_dialog_title_font(void) {
    return &cads_font16;
}

static const cads_font_t* cads_dialog_body_font(void) {
    return &cads_font12;
}

static int16_t cads_dialog_title_height(void) {
    return (int16_t)(cads_dialog_title_font()->line_height + CADS_DIALOG_PAD);
}

static cads_rect_t cads_dialog_button_rect(const cads_dialog_t* dialog, size_t index) {
    cads_rect_t rect = {0, 0, 0, 0};
    if(dialog->answer_count == 0u) return rect;

    int16_t inner = (int16_t)(dialog->area.width - 2 * CADS_DIALOG_PAD);
    int16_t width = (int16_t)(inner / (int16_t)dialog->answer_count);
    rect.x = (int16_t)(dialog->area.x + CADS_DIALOG_PAD + width * (int16_t)index);
    rect.y = (int16_t)(
        dialog->area.y + dialog->area.height - CADS_DIALOG_PAD - CADS_DIALOG_BUTTON_HEIGHT);
    rect.width = (int16_t)(width - 4);
    rect.height = CADS_DIALOG_BUTTON_HEIGHT;
    return rect;
}

void cads_dialog_init(
    cads_dialog_t* dialog,
    const char* title,
    const char* message,
    const cads_dialog_answer_t* answers,
    size_t answer_count) {
    if(dialog == NULL) return;

    dialog->area.x = 0;
    dialog->area.y = 0;
    dialog->area.width = 0;
    dialog->area.height = 0;
    dialog->title = title;
    dialog->message = message;
    dialog->line_count = 0u;
    dialog->result = CADS_DIALOG_PENDING;
    dialog->dirty = true;

    if(answer_count > CADS_DIALOG_ANSWERS) answer_count = CADS_DIALOG_ANSWERS;
    dialog->answer_count = (answers != NULL) ? answer_count : 0u;
    for(size_t i = 0u; i < dialog->answer_count; i++) dialog->answers[i] = answers[i];
    for(size_t i = dialog->answer_count; i < CADS_DIALOG_ANSWERS; i++) {
        dialog->answers[i].key = CadsKeyNone;
        dialog->answers[i].label = NULL;
    }
}

static void cads_dialog_wrap(cads_dialog_t* dialog) {
    int16_t width = (int16_t)(dialog->area.width - 2 * CADS_DIALOG_PAD);
    dialog->line_count = cads_text_wrap(
        cads_dialog_body_font(), dialog->message, width, dialog->lines, CADS_DIALOG_MAX_LINES);
    dialog->dirty = true;
}

void cads_dialog_set_area(cads_dialog_t* dialog, cads_rect_t area) {
    if(dialog == NULL) return;
    dialog->area = area;
    cads_dialog_wrap(dialog);
}

void cads_dialog_layout(cads_dialog_t* dialog, cads_rect_t within) {
    if(dialog == NULL) return;

    /* Three quarters of the available width leaves the dialog visibly floating
     * over its view, which is what tells the user the view is not listening. */
    cads_rect_t area;
    area.width = (int16_t)(within.width * 3 / 4);
    area.x = (int16_t)(within.x + (within.width - area.width) / 2);

    dialog->area.width = area.width;
    cads_dialog_wrap(dialog);

    int16_t body = (int16_t)((int16_t)dialog->line_count * cads_dialog_body_font()->line_height);
    int16_t height = (int16_t)(cads_dialog_title_height() + body + CADS_DIALOG_PAD * 3 +
                               CADS_DIALOG_BUTTON_HEIGHT);
    if(height > within.height) height = within.height;

    area.height = height;
    area.y = (int16_t)(within.y + (within.height - height) / 2);

    dialog->area = area;
    dialog->dirty = true;
}

bool cads_dialog_input(cads_dialog_t* dialog, const cads_input_event_t* event) {
    if(dialog == NULL || event == NULL) return false;
    if(dialog->result != CADS_DIALOG_PENDING) return false;

    if(event->type == CadsInputRelease) {
        for(size_t i = 0u; i < dialog->answer_count; i++) {
            if(dialog->answers[i].key != event->key) continue;
            dialog->result = (int8_t)i;
            dialog->dirty = true;
            return true;
        }
        return true; /* modal: an unmatched key is swallowed, not passed on */
    }

    if(event->type == CadsInputTouchUp) {
        for(size_t i = 0u; i < dialog->answer_count; i++) {
            cads_rect_t rect = cads_dialog_button_rect(dialog, i);
            if((int16_t)event->x < rect.x || (int16_t)event->x >= rect.x + rect.width) continue;
            if((int16_t)event->y < rect.y || (int16_t)event->y >= rect.y + rect.height) continue;
            dialog->result = (int8_t)i;
            dialog->dirty = true;
            return true;
        }
        return true;
    }

    /* Press, Repeat, Long and the remaining touch phases are consumed so the
     * view underneath never sees half a gesture. */
    return true;
}

int cads_dialog_result(const cads_dialog_t* dialog) {
    return (dialog != NULL) ? (int)dialog->result : CADS_DIALOG_PENDING;
}

void cads_dialog_reset(cads_dialog_t* dialog) {
    if(dialog == NULL) return;
    dialog->result = CADS_DIALOG_PENDING;
    dialog->dirty = true;
}

size_t cads_dialog_softkeys(const cads_dialog_t* dialog, cads_softkey_t* out, size_t max) {
    if(dialog == NULL || out == NULL) return 0u;

    size_t count = 0u;
    for(size_t i = 0u; i < dialog->answer_count && count < max; i++) {
        if(dialog->answers[i].key == CadsKeyNone) continue;
        out[count].key = dialog->answers[i].key;
        out[count].label = dialog->answers[i].label;
        count++;
    }
    return count;
}

bool cads_dialog_is_dirty(const cads_dialog_t* dialog) {
    return dialog != NULL && dialog->dirty;
}

cads_rect_t cads_dialog_damage(const cads_dialog_t* dialog) {
    if(dialog != NULL) return dialog->area;
    cads_rect_t empty = {0, 0, 0, 0};
    return empty;
}

void cads_dialog_invalidate(cads_dialog_t* dialog) {
    if(dialog != NULL) dialog->dirty = true;
}

void cads_dialog_draw(cads_dialog_t* dialog) {
    if(dialog == NULL || !dialog->dirty) return;
    if(dialog->area.width <= 0 || dialog->area.height <= 0) return;

    cads_rect_t area = dialog->area;
    cads_canvas_push_clip(area);

    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorSurface);
    cads_canvas_draw_rect(area.x, area.y, area.width, area.height, CadsColorBrandLight);

    int16_t title_height = cads_dialog_title_height();
    cads_canvas_fill_rect(
        (int16_t)(area.x + 1), (int16_t)(area.y + 1), (int16_t)(area.width - 2),
        (int16_t)(title_height - 1), CadsColorBrand);
    if(dialog->title != NULL) {
        cads_rect_t box = {
            (int16_t)(area.x + CADS_DIALOG_PAD), area.y,
            (int16_t)(area.width - 2 * CADS_DIALOG_PAD), title_height};
        cads_canvas_draw_text_aligned(
            box, CadsAlignLeft, cads_dialog_title_font(), dialog->title, CadsColorWhite);
    }

    int16_t y = (int16_t)(area.y + title_height + CADS_DIALOG_PAD);
    for(size_t i = 0u; i < dialog->line_count; i++) {
        cads_text_draw_line(
            (int16_t)(area.x + CADS_DIALOG_PAD), y, cads_dialog_body_font(), dialog->message,
            dialog->lines[i], CadsColorWhite);
        y = (int16_t)(y + cads_dialog_body_font()->line_height);
    }

    for(size_t i = 0u; i < dialog->answer_count; i++) {
        cads_rect_t rect = cads_dialog_button_rect(dialog, i);
        bool chosen = (dialog->result == (int8_t)i);
        cads_canvas_fill_rect(
            rect.x, rect.y, rect.width, rect.height, chosen ? CadsColorAccent : CadsColorBrandDark);
        cads_canvas_draw_rect(rect.x, rect.y, rect.width, rect.height, CadsColorBrandLight);
        if(dialog->answers[i].label != NULL) {
            cads_canvas_draw_text_aligned(
                rect, CadsAlignCenter, cads_dialog_body_font(), dialog->answers[i].label,
                CadsColorWhite);
        }
    }

    cads_canvas_pop_clip();
    dialog->dirty = false;
}

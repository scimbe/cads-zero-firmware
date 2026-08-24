#include "cads_softkeys.h"

#define CADS_SOFTKEY_NO_CELL (-1)

static bool cads_softkey_cell_valid(int cell) {
    return cell >= 0 && cell < (int)CADS_SOFTKEY_COUNT;
}

static cads_rect_t cads_softkey_cell_rect(const cads_softkeys_t* keys, int cell) {
    /* The last cell absorbs the remainder so the strip always ends flush with
     * the panel edge; 480/8 divides evenly but 800/8 or 320/8 might not. */
    int16_t width = (int16_t)(keys->area.width / CADS_SOFTKEY_COUNT);
    int16_t x = (int16_t)(keys->area.x + width * cell);
    int16_t w = (cell == CADS_SOFTKEY_COUNT - 1) ? (int16_t)(keys->area.x + keys->area.width - x) :
                                                   width;
    cads_rect_t rect = {x, keys->area.y, w, keys->area.height};
    return rect;
}

static int cads_softkey_cell_at(const cads_softkeys_t* keys, int16_t x, int16_t y) {
    if(x < keys->area.x || y < keys->area.y) return CADS_SOFTKEY_NO_CELL;
    if(x >= keys->area.x + keys->area.width) return CADS_SOFTKEY_NO_CELL;
    if(y >= keys->area.y + keys->area.height) return CADS_SOFTKEY_NO_CELL;

    int16_t width = (int16_t)(keys->area.width / CADS_SOFTKEY_COUNT);
    if(width <= 0) return CADS_SOFTKEY_NO_CELL;
    int cell = (x - keys->area.x) / width;
    if(cell >= (int)CADS_SOFTKEY_COUNT) cell = CADS_SOFTKEY_COUNT - 1;
    return cell;
}

static bool cads_softkey_cell_live(const cads_softkeys_t* keys, int cell) {
    const char* label = keys->labels[cell];
    return label != NULL && label[0] != '\0';
}

static void cads_softkey_mark(cads_softkeys_t* keys, int cell) {
    if(cads_softkey_cell_valid(cell)) keys->dirty |= (uint8_t)(1u << cell);
}

static int cads_softkey_cell_of_key(const cads_softkeys_t* keys, cads_key_t key) {
    if(key >= CADS_SOFTKEY_COUNT) return CADS_SOFTKEY_NO_CELL;
    return (int)keys->bind[key];
}

void cads_softkeys_init(cads_softkeys_t* keys) {
    if(keys == NULL) return;

    for(int i = 0; i < (int)CADS_SOFTKEY_COUNT; i++) {
        keys->labels[i] = NULL;
        keys->cell_key[i] = (cads_key_t)i;
        keys->bind[i] = (uint8_t)i;
    }
    keys->area.x = 0;
    keys->area.y = (int16_t)(CADS_CANVAS_HEIGHT - CADS_SOFTKEYS_HEIGHT);
    keys->area.width = CADS_CANVAS_WIDTH;
    keys->area.height = CADS_SOFTKEYS_HEIGHT;
    keys->dirty = 0xFFu;
    keys->held = CADS_SOFTKEY_NO_CELL;
    keys->held_by_touch = false;
    keys->next_repeat_ms = 0u;
    keys->long_due_ms = 0u;
    keys->long_sent = false;
}

void cads_softkeys_set_area(cads_softkeys_t* keys, cads_rect_t area) {
    if(keys == NULL) return;
    keys->area = area;
    keys->dirty = 0xFFu;
}

void cads_softkeys_bind(cads_softkeys_t* keys, cads_key_t key, uint8_t cell) {
    if(keys == NULL || key >= CADS_SOFTKEY_COUNT || cell >= CADS_SOFTKEY_COUNT) return;

    int previous = (int)keys->bind[key];
    const char* label = keys->labels[previous];

    keys->labels[previous] = NULL;
    keys->cell_key[previous] = (cads_key_t)previous;
    keys->bind[key] = cell;
    keys->labels[cell] = label;
    keys->cell_key[cell] = key;

    cads_softkey_mark(keys, previous);
    cads_softkey_mark(keys, (int)cell);
}

void cads_softkeys_set(cads_softkeys_t* keys, const cads_softkey_t* set, size_t count) {
    if(keys == NULL) return;

    for(int i = 0; i < (int)CADS_SOFTKEY_COUNT; i++) {
        if(keys->labels[i] != NULL) cads_softkey_mark(keys, i);
        keys->labels[i] = NULL;
    }
    if(set == NULL) return;

    for(size_t i = 0; i < count; i++) {
        cads_softkeys_set_label(keys, set[i].key, set[i].label);
    }
}

void cads_softkeys_set_label(cads_softkeys_t* keys, cads_key_t key, const char* label) {
    if(keys == NULL) return;
    int cell = cads_softkey_cell_of_key(keys, key);
    if(!cads_softkey_cell_valid(cell)) return;

    if(keys->labels[cell] == label) return; /* same pointer, nothing on screen changes */
    keys->labels[cell] = label;
    keys->cell_key[cell] = key;
    cads_softkey_mark(keys, cell);
}

void cads_softkeys_highlight(cads_softkeys_t* keys, cads_key_t key, bool down) {
    if(keys == NULL) return;
    int cell = cads_softkey_cell_of_key(keys, key);
    if(!cads_softkey_cell_valid(cell)) return;

    if(down) {
        if(keys->held == cell) return;
        if(cads_softkey_cell_valid(keys->held)) cads_softkey_mark(keys, keys->held);
        keys->held = (int8_t)cell;
        keys->held_by_touch = false;
        cads_softkey_mark(keys, cell);
    } else if(keys->held == cell) {
        keys->held = CADS_SOFTKEY_NO_CELL;
        keys->held_by_touch = false;
        cads_softkey_mark(keys, cell);
    }
}

/* --- touch ---------------------------------------------------------------- */

static void cads_softkey_emit(
    const cads_softkeys_t* keys,
    const cads_input_event_t* source,
    int cell,
    cads_input_type_t type,
    cads_input_event_t* out_key) {
    out_key->type = type;
    out_key->key = keys->cell_key[cell];
    out_key->x = source->x;
    out_key->y = source->y;
    out_key->timestamp = source->timestamp;
    out_key->hold_ms = source->hold_ms;
}

static void cads_softkey_release_touch(cads_softkeys_t* keys) {
    if(cads_softkey_cell_valid(keys->held) && keys->held_by_touch) {
        cads_softkey_mark(keys, keys->held);
    }
    keys->held = CADS_SOFTKEY_NO_CELL;
    keys->held_by_touch = false;
    keys->long_sent = false;
}

bool cads_softkeys_touch(
    cads_softkeys_t* keys,
    const cads_input_event_t* touch,
    cads_input_event_t* out_key) {
    if(keys == NULL || touch == NULL || out_key == NULL) return false;

    out_key->type = CadsInputPress;
    out_key->key = CadsKeyNone;

    int cell = cads_softkey_cell_at(keys, (int16_t)touch->x, (int16_t)touch->y);
    bool tracking = keys->held_by_touch && cads_softkey_cell_valid(keys->held);

    switch(touch->type) {
    case CadsInputTouchDown:
        if(!cads_softkey_cell_valid(cell) || !cads_softkey_cell_live(keys, cell)) return false;
        keys->held = (int8_t)cell;
        keys->held_by_touch = true;
        keys->long_sent = false;
        keys->next_repeat_ms = touch->timestamp + CADS_INPUT_REPEAT_DELAY_MS;
        keys->long_due_ms = touch->timestamp + CADS_INPUT_LONG_MS;
        cads_softkey_mark(keys, cell);
        cads_softkey_emit(keys, touch, cell, CadsInputPress, out_key);
        return true;

    case CadsInputTouchMove:
        if(!tracking) return cads_softkey_cell_valid(cell);
        /* Sliding off the pressed cell cancels it, the way every touch surface
         * behaves: the user is backing out, not choosing a neighbour. */
        if(cell != keys->held) cads_softkey_release_touch(keys);
        return true;

    case CadsInputTouchUp:
        if(!tracking) return cads_softkey_cell_valid(cell);
        if(cell == keys->held) {
            cads_softkey_emit(keys, touch, keys->held, CadsInputRelease, out_key);
            cads_softkey_release_touch(keys);
            return true;
        }
        cads_softkey_release_touch(keys);
        return true;

    default:
        return false;
    }
}

bool cads_softkeys_tick(cads_softkeys_t* keys, uint32_t now_ms, cads_input_event_t* out_key) {
    if(keys == NULL || out_key == NULL) return false;
    if(!keys->held_by_touch || !cads_softkey_cell_valid(keys->held)) return false;

    cads_input_event_t source = {CadsInputTouchMove, CadsKeyNone, 0, 0, now_ms, 0};

    if(!keys->long_sent && (int32_t)(now_ms - keys->long_due_ms) >= 0) {
        keys->long_sent = true;
        source.hold_ms = CADS_INPUT_LONG_MS;
        cads_softkey_emit(keys, &source, keys->held, CadsInputLong, out_key);
        return true;
    }
    if((int32_t)(now_ms - keys->next_repeat_ms) >= 0) {
        keys->next_repeat_ms = now_ms + CADS_INPUT_REPEAT_PERIOD_MS;
        source.hold_ms = CADS_INPUT_REPEAT_DELAY_MS;
        cads_softkey_emit(keys, &source, keys->held, CadsInputRepeat, out_key);
        return true;
    }
    return false;
}

/* --- redraw --------------------------------------------------------------- */

bool cads_softkeys_is_dirty(const cads_softkeys_t* keys) {
    return keys != NULL && keys->dirty != 0u;
}

cads_rect_t cads_softkeys_damage(const cads_softkeys_t* keys) {
    cads_rect_t box = {0, 0, 0, 0};
    if(keys == NULL || keys->dirty == 0u) return box;

    int first = -1;
    int last = -1;
    for(int i = 0; i < (int)CADS_SOFTKEY_COUNT; i++) {
        if(!(keys->dirty & (1u << i))) continue;
        if(first < 0) first = i;
        last = i;
    }

    cads_rect_t a = cads_softkey_cell_rect(keys, first);
    cads_rect_t b = cads_softkey_cell_rect(keys, last);
    box.x = a.x;
    box.y = a.y;
    box.width = (int16_t)(b.x + b.width - a.x);
    box.height = a.height;
    return box;
}

void cads_softkeys_invalidate(cads_softkeys_t* keys) {
    if(keys != NULL) keys->dirty = 0xFFu;
}

void cads_softkeys_draw(cads_softkeys_t* keys) {
    if(keys == NULL || keys->dirty == 0u) return;

    const cads_font_t* font = &cads_font12;

    for(int cell = 0; cell < (int)CADS_SOFTKEY_COUNT; cell++) {
        if(!(keys->dirty & (1u << cell))) continue;

        cads_rect_t rect = cads_softkey_cell_rect(keys, cell);
        bool live = cads_softkey_cell_live(keys, cell);
        bool down = (keys->held == cell) && live;

        /* The whole strip reads as one dark instrument bar - "this is the row
         * of physical buttons, and here is what each one does right now" -
         * rather than switching an active cell to a light background. A live
         * cell used to fill CadsColorSurface with CadsColorBrandLight text:
         * pale blue-grey on near-white, genuinely low contrast, and reported
         * as such from the physical panel ("aktuelle Farbgebung... schwer zu
         * lesen"). CadsColorBrandDark everywhere plus white/BrandLight text
         * matches the approved interface-language mockup's soft-key strip. */
        cads_color_t fill = down ? CadsColorBrand : CadsColorBrandDark;
        cads_canvas_fill_rect(rect.x, rect.y, rect.width, rect.height, fill);

        /* A one pixel gutter on the right of every cell but the last keeps the
         * eight targets visually distinct without a full grid. */
        if(cell != CADS_SOFTKEY_COUNT - 1) {
            cads_canvas_draw_vline(
                (int16_t)(rect.x + rect.width - 1), rect.y, rect.height, CadsColorBrandDark);
        }

        if(live) {
            cads_rect_t text = {
                (int16_t)(rect.x + 2), (int16_t)(rect.y + 1), (int16_t)(rect.width - 4),
                (int16_t)(rect.height - 1)};
            cads_canvas_draw_text_aligned(
                text, CadsAlignCenter, font, keys->labels[cell],
                down ? CadsColorWhite : CadsColorBrandLight);
        }
    }
    keys->dirty = 0u;
}

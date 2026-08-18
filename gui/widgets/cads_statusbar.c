#include "cads_statusbar.h"

#define CADS_STATUSBAR_TITLE_BIT 0x01u
#define CADS_STATUSBAR_PAD       4

static int16_t cads_statusbar_slot_width(const cads_statusbar_t* bar) {
    int16_t width = CADS_STATUSBAR_SLOT_WIDTH;
    int16_t total = (int16_t)(width * CADS_STATUSBAR_SLOTS);
    /* Never let the indicators crowd the title off a narrow panel. */
    if(total > bar->area.width / 2) width = (int16_t)(bar->area.width / (2 * CADS_STATUSBAR_SLOTS));
    return width;
}

static cads_rect_t cads_statusbar_slot_rect(const cads_statusbar_t* bar, size_t slot) {
    int16_t width = cads_statusbar_slot_width(bar);
    int16_t right = (int16_t)(bar->area.x + bar->area.width);
    cads_rect_t rect = {
        (int16_t)(right - (int16_t)((slot + 1u) * (size_t)width)), bar->area.y, width,
        bar->area.height};
    return rect;
}

static cads_rect_t cads_statusbar_title_rect(const cads_statusbar_t* bar) {
    int16_t reserved = (int16_t)(cads_statusbar_slot_width(bar) * CADS_STATUSBAR_SLOTS);
    cads_rect_t rect = {
        bar->area.x, bar->area.y, (int16_t)(bar->area.width - reserved), bar->area.height};
    return rect;
}

void cads_statusbar_init(cads_statusbar_t* bar) {
    if(bar == NULL) return;

    bar->area.x = 0;
    bar->area.y = 0;
    bar->area.width = CADS_CANVAS_WIDTH;
    bar->area.height = CADS_STATUSBAR_HEIGHT;
    bar->title = NULL;
    for(size_t i = 0; i < CADS_STATUSBAR_SLOTS; i++) bar->slots[i] = NULL;
    bar->dirty = 0xFFu;
}

void cads_statusbar_set_area(cads_statusbar_t* bar, cads_rect_t area) {
    if(bar == NULL) return;
    bar->area = area;
    bar->dirty = 0xFFu;
}

void cads_statusbar_set_title(cads_statusbar_t* bar, const char* title) {
    if(bar == NULL || bar->title == title) return;
    bar->title = title;
    bar->dirty |= CADS_STATUSBAR_TITLE_BIT;
}

void cads_statusbar_set_indicator(cads_statusbar_t* bar, size_t slot, const char* text) {
    if(bar == NULL || slot >= CADS_STATUSBAR_SLOTS) return;
    if(bar->slots[slot] == text) return;
    bar->slots[slot] = text;
    bar->dirty |= (uint8_t)(1u << (slot + 1u));
}

bool cads_statusbar_is_dirty(const cads_statusbar_t* bar) {
    return bar != NULL && bar->dirty != 0u;
}

static void cads_statusbar_extend(cads_rect_t* box, cads_rect_t add, bool* valid) {
    if(!*valid) {
        *box = add;
        *valid = true;
        return;
    }
    int16_t x0 = box->x < add.x ? box->x : add.x;
    int16_t y0 = box->y < add.y ? box->y : add.y;
    int16_t x1 = (box->x + box->width) > (add.x + add.width) ? (int16_t)(box->x + box->width) :
                                                              (int16_t)(add.x + add.width);
    int16_t y1 = (box->y + box->height) > (add.y + add.height) ? (int16_t)(box->y + box->height) :
                                                                 (int16_t)(add.y + add.height);
    box->x = x0;
    box->y = y0;
    box->width = (int16_t)(x1 - x0);
    box->height = (int16_t)(y1 - y0);
}

cads_rect_t cads_statusbar_damage(const cads_statusbar_t* bar) {
    cads_rect_t box = {0, 0, 0, 0};
    bool valid = false;
    if(bar == NULL || bar->dirty == 0u) return box;

    if(bar->dirty & CADS_STATUSBAR_TITLE_BIT) {
        cads_statusbar_extend(&box, cads_statusbar_title_rect(bar), &valid);
    }
    for(size_t i = 0; i < CADS_STATUSBAR_SLOTS; i++) {
        if(bar->dirty & (1u << (i + 1u))) {
            cads_statusbar_extend(&box, cads_statusbar_slot_rect(bar, i), &valid);
        }
    }
    return box;
}

void cads_statusbar_invalidate(cads_statusbar_t* bar) {
    if(bar != NULL) bar->dirty = 0xFFu;
}

static void cads_statusbar_cell(cads_rect_t rect, cads_align_t align, const char* text) {
    cads_canvas_fill_rect(rect.x, rect.y, rect.width, rect.height, CadsColorBrandDark);
    if(text == NULL || text[0] == '\0') return;

    cads_rect_t inner = {
        (int16_t)(rect.x + CADS_STATUSBAR_PAD), rect.y,
        (int16_t)(rect.width - 2 * CADS_STATUSBAR_PAD), rect.height};
    cads_canvas_draw_text_aligned(inner, align, &cads_font12, text, CadsColorWhite);
}

void cads_statusbar_draw(cads_statusbar_t* bar) {
    if(bar == NULL || bar->dirty == 0u) return;

    if(bar->dirty & CADS_STATUSBAR_TITLE_BIT) {
        cads_statusbar_cell(cads_statusbar_title_rect(bar), CadsAlignLeft, bar->title);
    }
    for(size_t i = 0; i < CADS_STATUSBAR_SLOTS; i++) {
        if(!(bar->dirty & (1u << (i + 1u)))) continue;
        cads_statusbar_cell(cads_statusbar_slot_rect(bar, i), CadsAlignRight, bar->slots[i]);
    }
    bar->dirty = 0u;
}

#include "cads_menu.h"

#define CADS_MENU_PAD 8
/* The one focal cue: a solid brand-green rail down the left edge of the
 * selected row. A flat fill is the cheapest strong-selection mark this
 * display bus can draw, and moving the selection only repaints two rows
 * (the one leaving focus and the one entering it) via the list's own
 * dirty-row tracking - see docs/reference/explorer-console.md's sibling
 * design notes. The label already starts CADS_MENU_PAD (8px) in, clear of
 * this 5px rail, so nothing has to shift to make room for it. */
#define CADS_MENU_RAIL 5

static void cads_menu_draw_row(size_t index, cads_rect_t row, bool selected, void* context) {
    cads_menu_t* menu = (cads_menu_t*)context;
    if(menu == NULL || menu->items == NULL) return;

    const cads_menu_item_t* item = &menu->items[index];
    /* Light content surface with dark text - the approved redesign. A light
     * ground is not just the look: on this glossy panel a near-black ground
     * mirrors the room and washes out under glare, while a light one stays
     * legible (and, as it turns out, photographable). The selected row keeps
     * the strong brand fill + white text + green rail from phase 1. */
    cads_color_t background = selected ? CadsColorBrand : CadsColorSurface;
    cads_color_t label = selected ? CadsColorWhite : CadsColorGrayDark;
    cads_color_t detail = selected ? CadsColorBrandLight : CadsColorGray;

    cads_canvas_fill_rect(row.x, row.y, row.width, row.height, background);

    if(selected) {
        cads_canvas_fill_rect(row.x, row.y, CADS_MENU_RAIL, row.height, CadsColorAccent);
    } else {
        /* A hairline under each unselected row gives the list definition
         * without a full separator's weight; the selected row's brand fill
         * covers its own, so it is only drawn when not selected. GrayLight
         * reads as a subtle divider on the Surface ground. */
        cads_canvas_draw_hline(
            row.x, (int16_t)(row.y + row.height - 1), row.width, CadsColorGrayLight);
    }

    /* The detail is drawn first and the label clipped short of it, so a long
     * label truncates instead of colliding with the value on the right. */
    int16_t reserved = 0;
    if(item->detail != NULL && item->detail[0] != '\0') {
        reserved = (int16_t)(cads_canvas_text_width(menu->list.font, item->detail) + CADS_MENU_PAD);
        cads_rect_t box = {
            (int16_t)(row.x + row.width - reserved - CADS_MENU_PAD), row.y, reserved, row.height};
        cads_canvas_draw_text_aligned(box, CadsAlignRight, menu->list.font, item->detail, detail);
    }

    cads_rect_t box = {
        (int16_t)(row.x + CADS_MENU_PAD), row.y,
        (int16_t)(row.width - 2 * CADS_MENU_PAD - reserved), row.height};
    if(box.width > 0 && item->label != NULL) {
        cads_canvas_draw_text_aligned(box, CadsAlignLeft, menu->list.font, item->label, label);
    }
}

static void cads_menu_on_activate(size_t index, void* context) {
    cads_menu_t* menu = (cads_menu_t*)context;
    if(menu == NULL || menu->on_activate == NULL || menu->items == NULL) return;
    if(index >= cads_list_count(&menu->list)) return;
    menu->on_activate(&menu->items[index], index, menu->context);
}

void cads_menu_init(
    cads_menu_t* menu, const cads_menu_item_t* items, size_t count, const cads_font_t* font) {
    if(menu == NULL) return;

    menu->items = items;
    menu->on_activate = NULL;
    menu->context = NULL;
    cads_list_init(&menu->list, count, font, cads_menu_draw_row, menu);
    cads_list_set_activate(&menu->list, cads_menu_on_activate);
    /* Match the tail below the last row to the light rows above it. */
    cads_list_set_background(&menu->list, CadsColorSurface);
}

void cads_menu_set_activate(cads_menu_t* menu, cads_menu_activate_t activate, void* context) {
    if(menu == NULL) return;
    menu->on_activate = activate;
    menu->context = context;
}

void cads_menu_set_items(cads_menu_t* menu, const cads_menu_item_t* items, size_t count) {
    if(menu == NULL) return;
    menu->items = items;
    cads_list_set_count(&menu->list, count);
}

void cads_menu_set_area(cads_menu_t* menu, cads_rect_t area) {
    if(menu != NULL) cads_list_set_area(&menu->list, area);
}

size_t cads_menu_selected(const cads_menu_t* menu) {
    return (menu != NULL) ? cads_list_selected(&menu->list) : 0u;
}

void cads_menu_set_selected(cads_menu_t* menu, size_t index) {
    if(menu != NULL) cads_list_set_selected(&menu->list, index);
}

const cads_menu_item_t* cads_menu_selected_item(const cads_menu_t* menu) {
    if(menu == NULL || menu->items == NULL || cads_list_count(&menu->list) == 0u) return NULL;
    return &menu->items[cads_list_selected(&menu->list)];
}

bool cads_menu_input(cads_menu_t* menu, const cads_input_event_t* event) {
    return (menu != NULL) ? cads_list_input(&menu->list, event) : false;
}

bool cads_menu_is_dirty(const cads_menu_t* menu) {
    return (menu != NULL) ? cads_list_is_dirty(&menu->list) : false;
}

cads_rect_t cads_menu_damage(const cads_menu_t* menu) {
    if(menu != NULL) return cads_list_damage(&menu->list);
    cads_rect_t empty = {0, 0, 0, 0};
    return empty;
}

void cads_menu_invalidate(cads_menu_t* menu) {
    if(menu != NULL) cads_list_invalidate(&menu->list);
}

void cads_menu_draw(cads_menu_t* menu) {
    if(menu != NULL) cads_list_draw(&menu->list);
}

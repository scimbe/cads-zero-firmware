#include "cads_menu.h"

#define CADS_MENU_PAD 8

static void cads_menu_draw_row(size_t index, cads_rect_t row, bool selected, void* context) {
    cads_menu_t* menu = (cads_menu_t*)context;
    if(menu == NULL || menu->items == NULL) return;

    const cads_menu_item_t* item = &menu->items[index];
    cads_color_t background = selected ? CadsColorBrand : CadsColorBackground;
    cads_color_t label = selected ? CadsColorWhite : CadsColorBrandLight;
    cads_color_t detail = selected ? CadsColorBrandLight : CadsColorGray;

    cads_canvas_fill_rect(row.x, row.y, row.width, row.height, background);

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

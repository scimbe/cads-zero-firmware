/*
 * CaDS Zero - menu.
 *
 * The common case of cads_list: rows are a label, an optional right-hand
 * detail, and an id the app gets back on activation. Everything about
 * scrolling, selection and the two input rails lives in cads_list; this file is
 * only the row renderer and the item table, which is why there is no second
 * copy of the scrolling logic to keep in step.
 *
 * OWNERSHIP
 * ---------
 * The item array is borrowed and must outlive the menu. Declare it static:
 *
 *     static const cads_menu_item_t items[] = {
 *         {"Applications", NULL, MENU_APPS},
 *         {"Settings",     NULL, MENU_SETTINGS},
 *         {"About",        "v0.1", MENU_ABOUT},
 *     };
 */

#ifndef CADS_MENU_H
#define CADS_MENU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads_list.h"

typedef struct {
    const char* label;  /**< left-aligned, required                        */
    const char* detail; /**< right-aligned hint, may be NULL               */
    uint32_t id;        /**< handed back on activation; the app's meaning  */
} cads_menu_item_t;

typedef void (*cads_menu_activate_t)(const cads_menu_item_t* item, size_t index, void* context);

typedef struct {
    cads_list_t list; /**< public only so the struct can be declared statically */

    /* --- private --- */
    const cads_menu_item_t* items;
    cads_menu_activate_t on_activate;
    void* context;
} cads_menu_t;

/** Bind a menu to a static item table. `font` may be NULL for the default. */
void cads_menu_init(
    cads_menu_t* menu, const cads_menu_item_t* items, size_t count, const cads_font_t* font);

/** Install the activation handler, called on OK or on a tap. */
void cads_menu_set_activate(cads_menu_t* menu, cads_menu_activate_t activate, void* context);

/** Swap the item table, for a menu whose contents are built at run time. */
void cads_menu_set_items(cads_menu_t* menu, const cads_menu_item_t* items, size_t count);

void cads_menu_set_area(cads_menu_t* menu, cads_rect_t area);
size_t cads_menu_selected(const cads_menu_t* menu);
void cads_menu_set_selected(cads_menu_t* menu, size_t index);

/** The item under the selection, or NULL when the menu is empty. */
const cads_menu_item_t* cads_menu_selected_item(const cads_menu_t* menu);

bool cads_menu_input(cads_menu_t* menu, const cads_input_event_t* event);
bool cads_menu_is_dirty(const cads_menu_t* menu);
cads_rect_t cads_menu_damage(const cads_menu_t* menu);
void cads_menu_invalidate(cads_menu_t* menu);
void cads_menu_draw(cads_menu_t* menu);

#endif /* CADS_MENU_H */

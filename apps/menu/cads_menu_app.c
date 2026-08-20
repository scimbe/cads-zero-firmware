#include "cads_menu_app.h"

#include <stddef.h>

#include "cads_menu.h"
#include "cads_view.h"

#include "../about/cads_about.h"
#include "../desktop/cads_desktop.h"
#include "../filebrowser/cads_filebrowser.h"
#include "../gpio/cads_gpio.h"
#include "../netinfo/cads_netinfo.h"
#include "../settings/cads_settings.h"

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_menu_t menu;
} cads_menu_app_t;

static cads_menu_app_t s_menu_app;

/* ids double as the row's own view id, so activating a row is exactly
 * "push what it names" - the menu never has to translate one into the other. */
static const cads_menu_item_t cads_menu_app_items[] = {
    {"Settings", NULL, CADS_VIEW_ID_SETTINGS},
    {"About", NULL, CADS_VIEW_ID_ABOUT},
    {"GPIO", "16/8", CADS_VIEW_ID_GPIO},
    {"Network Info", "Ethernet", CADS_VIEW_ID_NETINFO},
    {"Files", NULL, CADS_VIEW_ID_FILEBROWSER},
};

static const cads_softkey_t cads_menu_app_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk, "Open"},
    {CadsKeyBack, "Back"},
};

static void cads_menu_app_activate(const cads_menu_item_t* item, size_t index, void* context) {
    (void)index;
    cads_menu_app_t* app = (cads_menu_app_t*)context;
    if(cads_view_dispatcher_push(app->dispatcher, item->id)) {
        cads_desktop_notify_app_opened();
    }
}

static void cads_menu_app_draw(cads_rect_t area, void* context) {
    (void)area;
    cads_menu_app_t* app = (cads_menu_app_t*)context;
    if(cads_menu_is_dirty(&app->menu)) cads_menu_draw(&app->menu);
}

static bool cads_menu_app_input(const cads_input_event_t* event, void* context) {
    cads_menu_app_t* app = (cads_menu_app_t*)context;
    bool consumed = cads_menu_input(&app->menu, event);
    if(cads_menu_is_dirty(&app->menu)) {
        cads_view_dirty_rect(&app->view, cads_menu_damage(&app->menu));
    }
    return consumed;
}

static void cads_menu_app_enter(void* context) {
    cads_menu_app_t* app = (cads_menu_app_t*)context;
    cads_menu_set_area(&app->menu, cads_view_area(&app->view));
}

void cads_menu_app_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    cads_settings_init(dispatcher);
    cads_about_init(dispatcher);
    cads_gpio_init(dispatcher);
    cads_netinfo_init(dispatcher);
    cads_filebrowser_init(dispatcher);

    s_menu_app.dispatcher = dispatcher;
    cads_menu_init(
        &s_menu_app.menu, cads_menu_app_items,
        sizeof(cads_menu_app_items) / sizeof(cads_menu_app_items[0]), &cads_font16);
    cads_menu_set_activate(&s_menu_app.menu, cads_menu_app_activate, &s_menu_app);

    cads_view_init(&s_menu_app.view, cads_menu_app_draw, cads_menu_app_input, &s_menu_app);
    cads_view_set_lifecycle(&s_menu_app.view, cads_menu_app_enter, NULL);
    cads_view_set_title(&s_menu_app.view, "Applications");
    cads_view_set_softkeys(
        &s_menu_app.view, cads_menu_app_keys,
        sizeof(cads_menu_app_keys) / sizeof(cads_menu_app_keys[0]));

    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_MENU, &s_menu_app.view);
}

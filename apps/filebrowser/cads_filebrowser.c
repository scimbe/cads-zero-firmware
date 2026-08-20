/*
 * CaDS Zero - file browser: read-only navigation of the littlefs volume in
 * cads/storage/storage.h.
 *
 * READ-ONLY, DELIBERATELY. gui/widgets has no text entry (see its README's
 * "what are the limits" section) and this milestone's job was the storage
 * layer and a way to see what is on it, not a full file manager. Rename,
 * delete and a content viewer are all reachable through cads/storage/
 * storage.h already; nothing here stops a later app from adding them.
 *
 * ONE MENU, A MUTABLE PATH, NO PER-DEPTH VIEWS. A view per directory depth
 * would need dynamic view ids for an unbounded tree; instead there is one
 * view whose current path is a fixed CADS_STORAGE_PATH_MAX buffer, reloaded
 * from cads/storage/storage.h every time the user goes up or down. This is
 * the same shape apps/settings uses for its nested confirm dialog - state
 * in the app, not more views than the dispatcher needs to know about.
 *
 * ENTRIES ARE NOT SORTED. littlefs returns directory entries in whatever
 * order its own metadata blocks hold them, not alphabetical. A browser this
 * small does not need to impose one; a future version that does can sort
 * cads_filebrowser_t.entries after cads_filebrowser_refresh() fills it.
 */

#include "cads_filebrowser.h"

#include <stdbool.h>
#include <stdint.h>

#include "cads/storage/storage.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_dialog.h"
#include "cads_menu.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

#define CADS_FILEBROWSER_MAX_ENTRIES 32u
#define CADS_FILEBROWSER_DETAIL_MAX  15u /* "4294967295 B" plus room, never truncates a real size */

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_menu_t menu;
    cads_menu_item_t items[CADS_FILEBROWSER_MAX_ENTRIES];
    char detail_text[CADS_FILEBROWSER_MAX_ENTRIES][CADS_FILEBROWSER_DETAIL_MAX + 1u];
    cads_storage_info_t entries[CADS_FILEBROWSER_MAX_ENTRIES];
    uint32_t entry_count;
    char path[CADS_STORAGE_PATH_MAX + 1u];
    bool mounted;
} cads_filebrowser_t;

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_dialog_t dialog;
    cads_softkey_t keys[CADS_DIALOG_ANSWERS];
    char title[CADS_STORAGE_NAME_MAX + 1u];
    char message[48];
} cads_filebrowser_info_t;

static cads_filebrowser_t s_browser;
static cads_filebrowser_info_t s_info;

static const cads_softkey_t cads_filebrowser_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk, "Open"},
    {CadsKeyBack, "Back"},
};

/* --- path manipulation ------------------------------------------------------- */

static void cads_filebrowser_go_up(cads_filebrowser_t* app) {
    size_t len = cads_str_len(app->path, sizeof(app->path));
    if(len <= 1u) return; /* already at "/" */

    size_t last_slash = 0u;
    for(size_t i = 0; i < len; i++) {
        if(app->path[i] == '/') last_slash = i;
    }
    if(last_slash == 0u) {
        app->path[1] = '\0'; /* keep the leading '/', drop everything after */
    } else {
        app->path[last_slash] = '\0';
    }
}

/** Appends `name` as a child of the current path. Refuses (returns false,
 *  changes nothing) rather than silently truncate a path that does not fit. */
static bool cads_filebrowser_go_down(cads_filebrowser_t* app, const char* name) {
    char next[CADS_STORAGE_PATH_MAX + 1u];
    cads_str_copy(next, sizeof(next), app->path);
    if(!cads_str_equal(app->path, "/")) {
        cads_str_append(next, sizeof(next), "/");
    }
    size_t result = cads_str_append(next, sizeof(next), name);
    if(result >= sizeof(next)) return false;

    cads_str_copy(app->path, sizeof(app->path), next);
    return true;
}

/* --- directory listing -------------------------------------------------------- */

static void cads_filebrowser_fill_detail(char* out, const cads_storage_info_t* info) {
    if(info->type == CADS_STORAGE_TYPE_DIR) {
        cads_str_copy(out, CADS_FILEBROWSER_DETAIL_MAX + 1u, "dir");
        return;
    }
    size_t pos = cads_fmt_uint(out, CADS_FILEBROWSER_DETAIL_MAX + 1u, info->size);
    if(pos < CADS_FILEBROWSER_DETAIL_MAX + 1u) {
        cads_str_append(out, CADS_FILEBROWSER_DETAIL_MAX + 1u, " B");
    }
}

static void cads_filebrowser_refresh(cads_filebrowser_t* app) {
    app->entry_count = 0u;
    cads_view_set_title(&app->view, app->path);

    if(!app->mounted) {
        cads_menu_set_items(&app->menu, app->items, 0u);
        return;
    }

    cads_storage_dir_t* dir = NULL;
    if(cads_storage_dir_open(&dir, app->path) != CADS_STORAGE_OK) {
        cads_menu_set_items(&app->menu, app->items, 0u);
        return;
    }

    cads_storage_info_t info;
    while(app->entry_count < CADS_FILEBROWSER_MAX_ENTRIES && cads_storage_dir_read(dir, &info) == 1) {
        app->entries[app->entry_count] = info;
        cads_filebrowser_fill_detail(app->detail_text[app->entry_count], &info);

        cads_menu_item_t* item = &app->items[app->entry_count];
        item->label = app->entries[app->entry_count].name;
        item->detail = app->detail_text[app->entry_count];
        item->id = app->entry_count;
        app->entry_count++;
    }
    cads_storage_dir_close(dir);

    cads_menu_set_items(&app->menu, app->items, app->entry_count);
}

/* --- the info dialog: one file's size, its own view -------------------------- */

static void cads_filebrowser_info_finish(void) {
    cads_view_dispatcher_pop(s_info.dispatcher);
}

static void cads_filebrowser_info_draw(cads_rect_t area, void* context) {
    (void)area;
    (void)context;
    if(cads_dialog_is_dirty(&s_info.dialog)) cads_dialog_draw(&s_info.dialog);
}

static bool cads_filebrowser_info_input(const cads_input_event_t* event, void* context) {
    (void)context;
    (void)cads_dialog_input(&s_info.dialog, event);
    if(cads_dialog_is_dirty(&s_info.dialog)) {
        cads_view_dirty_rect(&s_info.view, cads_dialog_damage(&s_info.dialog));
    }
    if(cads_dialog_result(&s_info.dialog) != CADS_DIALOG_PENDING) cads_filebrowser_info_finish();
    return true; /* modal */
}

static void cads_filebrowser_info_enter(void* context) {
    (void)context;
    cads_dialog_layout(&s_info.dialog, cads_view_area(&s_info.view));
}

static void cads_filebrowser_show_info(cads_filebrowser_t* app, const cads_storage_info_t* info) {
    cads_str_copy(s_info.title, sizeof(s_info.title), info->name);

    size_t pos = cads_fmt_uint(s_info.message, sizeof(s_info.message), info->size);
    if(pos < sizeof(s_info.message)) cads_str_append(s_info.message, sizeof(s_info.message), " bytes");

    static const cads_dialog_answer_t answers[] = {{CadsKeyOk, "OK"}};
    cads_dialog_init(&s_info.dialog, s_info.title, s_info.message, answers, 1u);

    size_t n = cads_dialog_softkeys(&s_info.dialog, s_info.keys, CADS_DIALOG_ANSWERS);
    cads_view_set_softkeys(&s_info.view, s_info.keys, n);
    cads_view_set_title(&s_info.view, s_info.title);

    (void)cads_view_dispatcher_push(app->dispatcher, CADS_VIEW_ID_FILEBROWSER_INFO);
}

/* --- the browser itself -------------------------------------------------------- */

static void cads_filebrowser_activate(const cads_menu_item_t* item, size_t index, void* context) {
    (void)item;
    cads_filebrowser_t* app = (cads_filebrowser_t*)context;
    if(index >= app->entry_count) return;

    const cads_storage_info_t* info = &app->entries[index];
    if(info->type == CADS_STORAGE_TYPE_DIR) {
        if(cads_filebrowser_go_down(app, info->name)) {
            cads_filebrowser_refresh(app);
            cads_view_dirty(&app->view);
        }
    } else {
        cads_filebrowser_show_info(app, info);
    }
}

static void cads_filebrowser_draw(cads_rect_t area, void* context) {
    (void)area;
    cads_filebrowser_t* app = (cads_filebrowser_t*)context;
    if(cads_menu_is_dirty(&app->menu)) cads_menu_draw(&app->menu);
}

static bool cads_filebrowser_input(const cads_input_event_t* event, void* context) {
    cads_filebrowser_t* app = (cads_filebrowser_t*)context;

    if(event->type == CadsInputRelease && event->key == CadsKeyBack) {
        if(cads_str_equal(app->path, "/")) return false; /* let the dispatcher pop this view */
        cads_filebrowser_go_up(app);
        cads_filebrowser_refresh(app);
        cads_view_dirty(&app->view);
        return true;
    }

    bool consumed = cads_menu_input(&app->menu, event);
    if(cads_menu_is_dirty(&app->menu)) {
        cads_view_dirty_rect(&app->view, cads_menu_damage(&app->menu));
    }
    return consumed;
}

static void cads_filebrowser_enter(void* context) {
    cads_filebrowser_t* app = (cads_filebrowser_t*)context;
    cads_menu_set_area(&app->menu, cads_view_area(&app->view));

    /* Mounting here, not at cads_filebrowser_init(), so a volume formatted
     * after boot (the settings screen's "factory reset", or the explorer's
     * storage test) is picked up the next time this app is opened rather
     * than needing a reboot. Never formats on its own - a browser that could
     * wipe the volume it is looking at would be a trap, not a convenience;
     * that stays apps/settings' and the explorer's job. */
    if(!app->mounted) app->mounted = (cads_storage_mount() == CADS_STORAGE_OK);

    cads_filebrowser_refresh(app);
}

/* --- public -------------------------------------------------------------------- */

void cads_filebrowser_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    s_browser.dispatcher = dispatcher;
    cads_str_copy(s_browser.path, sizeof(s_browser.path), "/");

    cads_menu_init(&s_browser.menu, s_browser.items, 0u, &cads_font16);
    cads_menu_set_activate(&s_browser.menu, cads_filebrowser_activate, &s_browser);

    cads_view_init(&s_browser.view, cads_filebrowser_draw, cads_filebrowser_input, &s_browser);
    cads_view_set_lifecycle(&s_browser.view, cads_filebrowser_enter, NULL);
    cads_view_set_title(&s_browser.view, "/");
    cads_view_set_softkeys(
        &s_browser.view, cads_filebrowser_keys,
        sizeof(cads_filebrowser_keys) / sizeof(cads_filebrowser_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_FILEBROWSER, &s_browser.view);

    s_info.dispatcher = dispatcher;
    cads_view_init(&s_info.view, cads_filebrowser_info_draw, cads_filebrowser_info_input, NULL);
    cads_view_set_lifecycle(&s_info.view, cads_filebrowser_info_enter, NULL);
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_FILEBROWSER_INFO, &s_info.view);
}

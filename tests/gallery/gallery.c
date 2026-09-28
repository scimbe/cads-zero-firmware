/*
 * Host screen gallery: render every application view to a PPM so all of the
 * firmware's surfaces can be reviewed for correct display without walking the
 * real board through them by hand. Uses the real gui/canvas against the
 * recording fake HAL (tests/unit/fake_hal.c) - the same panel the unit tests
 * read back - so what this dumps is exactly what the compositor would push to
 * the ILI9486.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "cads_view_dispatcher.h"
#include "cads_gui.h"
#include "cads_statusbar.h"
#include "cads_softkeys.h"
#include "cads_dialog.h"

#include "cads_desktop.h"
#include "cads_menu_app.h"
#include "cads_settings.h"
#include "cads_about.h"
#include "cads_gpio.h"
#include "cads_netinfo.h"
#include "cads_filebrowser.h"
#include "cads_game.h"
#include "cads_active.h"

#include "cads/net/net.h"
#include "cads/storage/storage.h"

/* The 16 palette slots, exactly as gui/canvas.c seeds them. */
static const uint8_t PALETTE[16][3] = {
    {0x00, 0x00, 0x00}, {0xFF, 0xFF, 0xFF}, {0x20, 0x4C, 0x86}, {0xB5, 0xC4, 0xD8},
    {0x9C, 0xB3, 0x3B}, {0x30, 0x35, 0x40}, {0x6B, 0x74, 0x80}, {0xC8, 0xCE, 0xD6},
    {0xC0, 0x39, 0x2B}, {0xE0, 0xA0, 0x00}, {0x1F, 0x8A, 0x80}, {0x12, 0x30, 0x5A},
    {0xF2, 0xF5, 0xF9}, {0xA0, 0x30, 0x70}, {0x8F, 0xA6, 0xC4}, {0x10, 0x14, 0x18},
};

static void dump_ppm(const char* dir, const char* name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE* f = fopen(path, "wb");
    if(!f) {
        fprintf(stderr, "gallery: cannot open %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT);
    for(int16_t y = 0; y < CADS_CANVAS_HEIGHT; y++) {
        for(int16_t x = 0; x < CADS_CANVAS_WIDTH; x++) {
            uint8_t idx = (uint8_t)(cads_canvas_get_pixel(x, y) & 0x0Fu);
            fwrite(PALETTE[idx], 1u, 3u, f);
        }
    }
    fclose(f);
    printf("wrote %s\n", path);
}

static cads_view_dispatcher_t s_dispatcher;
static cads_view_entry_t s_entries[24];
static uint32_t s_stack[8];
static cads_gui_t s_gui;
static cads_statusbar_t s_statusbar;
static cads_softkeys_t s_softkeys;
static uint32_t s_now = 1000u;

static void shoot(const char* out_dir, uint32_t view_id, const char* name) {
    if(!cads_view_dispatcher_switch_to(&s_dispatcher, view_id)) {
        fprintf(stderr, "gallery: switch_to %s (0x%04x) failed\n", name, view_id);
        return;
    }
    /* A few ticks so an app whose first frame depends on a tick (desktop's
     * Leo, a game's first step) has settled, advancing the clock each time. */
    /* The compositor does a full redraw on the view change (first tick after
     * switch_to), so the whole surface - status bar, content, soft keys - is
     * painted once here. Later ticks only redraw what an app re-dirties (a
     * game's moving field), which is exactly the last state we want to dump.
     * No final invalidate: that would clear to the gui background and a
     * dirty-gated view (the menu draws only when cads_menu_is_dirty) would not
     * repaint, leaving a blank surface. */
    for(int i = 0; i < 6; i++) {
        s_now += 20u;
        cads_desktop_tick(s_now);
        cads_gpio_tick(s_now);
        cads_game_tick(s_now);
        cads_active_tick(s_now);
        cads_gui_tick(&s_gui, s_now);
    }
    dump_ppm(out_dir, name);
}

int main(int argc, char** argv) {
    const char* out_dir = (argc > 1) ? argv[1] : ".";

    cads_hal_init();
    cads_canvas_init();

    /* net + storage so netinfo/filebrowser have something real to show. */
    static const uint8_t mac[6] = {0x02, 0xCA, 0xD5, 0x5E, 0x00, 0x01};
    cads_net_init(mac);
    if(cads_storage_mount() != CADS_STORAGE_OK) {
        cads_storage_format();
        cads_storage_mount();
    }

    cads_view_dispatcher_init(&s_dispatcher, s_entries, 24u, s_stack, 8u);
    cads_rect_t full = {0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT};
    cads_view_dispatcher_set_area(&s_dispatcher, full);

    cads_desktop_init(&s_dispatcher);
    cads_menu_app_init(&s_dispatcher); /* also settings/about/gpio/netinfo/filebrowser/game */

    cads_statusbar_init(&s_statusbar);
    cads_softkeys_init(&s_softkeys);
    cads_gui_init(&s_gui, &s_dispatcher, &s_statusbar, &s_softkeys);

    shoot(out_dir, CADS_VIEW_ID_DESKTOP, "01_desktop");
    shoot(out_dir, CADS_VIEW_ID_MENU, "02_menu");
    shoot(out_dir, CADS_VIEW_ID_SETTINGS, "03_settings");
    shoot(out_dir, CADS_VIEW_ID_ABOUT, "04_about");
    shoot(out_dir, CADS_VIEW_ID_GPIO, "05_gpio");
    shoot(out_dir, CADS_VIEW_ID_NETINFO, "06_netinfo");
    shoot(out_dir, CADS_VIEW_ID_FILEBROWSER, "07_filebrowser");
    shoot(out_dir, CADS_VIEW_ID_GAME, "08_arcade");
    shoot(out_dir, CADS_VIEW_ID_GAME_REFLEX, "09_reflex");
    shoot(out_dir, CADS_VIEW_ID_GAME_SNAKE, "10_snake");
    shoot(out_dir, CADS_VIEW_ID_GAME_BREAKOUT, "11_breakout");
    shoot(out_dir, CADS_VIEW_ID_GAME_DODGER, "12_dodger");
    shoot(out_dir, CADS_VIEW_ID_ACTIVE, "13_active");
    shoot(out_dir, CADS_VIEW_ID_ACTIVE_TOOL, "14_active_tool");

    /* A confirm dialog over Settings - the dialog body had no coverage here
     * and once shipped white-on-Surface text nobody could read. */
    shoot(out_dir, CADS_VIEW_ID_SETTINGS, "15_dialog_base");
    static const cads_dialog_answer_t answers[] = {{CadsKeyBack, "Cancel"}, {CadsKeyOk, "Reset"}};
    cads_dialog_t dialog;
    cads_dialog_init(
        &dialog, "Factory reset", "Erase all settings and restart? This cannot be undone.",
        answers, 2u);
    cads_dialog_layout(&dialog, full);
    cads_dialog_draw(&dialog);
    dump_ppm(out_dir, "15_dialog");

    return 0;
}

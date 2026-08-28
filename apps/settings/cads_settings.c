#include "cads_settings.h"
#include "cads_touch_calib.h"
#include "cads_splash.h"

#include "cads/config/config.h"
#include "cads/net/net.h"
#include "cads/storage/storage.h"

#include <stdbool.h>
#include <stdint.h>

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_dialog.h"
#ifdef CADS_APP_MARAUDER_ENABLED
/* Relative path, not an include-dir dependency - the same trick
 * apps/menu/cads_menu_app.c already uses to reach this optional app without
 * apps/settings needing a public link to cads_app_marauder just to compile;
 * the actual symbol resolves at final-link time (see CMakeLists.txt). */
#include "../marauder/cads_marauder.h"
#endif
#include "cads_hal.h"
#include "cads_menu.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

/*
 * core/cads_hal.h exposes cads_hal_display_backlight() and
 * cads_hal_display_set_fast_clock() as setters only - there is no getter for
 * either, so "read current values from the HAL" is not fully possible today.
 * This app keeps its own shadow, seeded from the values apps/bringup itself
 * leaves the hardware in (backlight 80%, the proven-safe /16 clock - see
 * apps/bringup/bringup.c), and treats every change it makes as the new truth.
 * A future cads_hal_display_backlight_get() / cads_hal_display_is_fast_clock()
 * would let this be read back instead of assumed.
 */
typedef struct {
    uint8_t brightness_percent;
    bool fast_clock;
} cads_settings_shadow_t;

static cads_settings_shadow_t s_shadow = {80u, false};

typedef enum {
    CADS_SETTINGS_ROW_BRIGHTNESS = 0,
    CADS_SETTINGS_ROW_SPI_CLOCK,
    CADS_SETTINGS_ROW_CALIBRATION,
    CADS_SETTINGS_ROW_TEST_PATTERN,
    CADS_SETTINGS_ROW_CONFIG_RELOAD,
#ifdef CADS_APP_MARAUDER_ENABLED
    CADS_SETTINGS_ROW_WIFI_JOIN,
#endif
    CADS_SETTINGS_ROW_RESET,
} cads_settings_row_t;

typedef enum {
    CADS_SETTINGS_CONFIRM_NONE = 0,
    CADS_SETTINGS_CONFIRM_CALIBRATION,
    CADS_SETTINGS_CONFIRM_CONFIG,
#ifdef CADS_APP_MARAUDER_ENABLED
    CADS_SETTINGS_CONFIRM_WIFI_JOIN,
#endif
    CADS_SETTINGS_CONFIRM_RESET,
} cads_settings_confirm_kind_t;

static char s_brightness_detail[8];
static char s_spi_detail[16];
#ifdef CADS_APP_MARAUDER_ENABLED
static char s_wifi_join_detail[CADS_CONFIG_SSID_MAX];
#endif

static cads_menu_item_t s_items[] = {
    {"Brightness", s_brightness_detail, CADS_SETTINGS_ROW_BRIGHTNESS},
    {"SPI clock", s_spi_detail, CADS_SETTINGS_ROW_SPI_CLOCK},
    {"Touch calibration", NULL, CADS_SETTINGS_ROW_CALIBRATION},
    {"Test pattern", NULL, CADS_SETTINGS_ROW_TEST_PATTERN},
    {"Reload config", NULL, CADS_SETTINGS_ROW_CONFIG_RELOAD},
#ifdef CADS_APP_MARAUDER_ENABLED
    {"Join WiFi", s_wifi_join_detail, CADS_SETTINGS_ROW_WIFI_JOIN},
#endif
    {"Factory reset", NULL, CADS_SETTINGS_ROW_RESET},
};

#define CADS_SETTINGS_INFO_HEIGHT 40

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_menu_t menu;
    cads_rect_t info_rect;
    char info_text[64];
    bool need_info;
} cads_settings_main_t;

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_dialog_t dialog;
    cads_softkey_t keys[CADS_DIALOG_ANSWERS];
    cads_settings_confirm_kind_t kind;
} cads_settings_confirm_t;

static cads_settings_main_t s_main;
static cads_settings_confirm_t s_confirm;

/* --- shadow state / detail text --------------------------------------------- */

static void cads_settings_refresh_details(void) {
    size_t pos = cads_fmt_uint(
        s_brightness_detail, sizeof(s_brightness_detail), s_shadow.brightness_percent);
    if(pos < sizeof(s_brightness_detail)) {
        cads_str_append(s_brightness_detail, sizeof(s_brightness_detail), "%");
    }
    cads_str_copy(
        s_spi_detail, sizeof(s_spi_detail), s_shadow.fast_clock ? "fast (/8)" : "safe (/16)");
}

static void cads_settings_refresh_info(void) {
    /* Numbers from docs/explanation/pa7-conflict.md: the longest single
     * Ethernet blackout is one display band, and raising the SPI divider
     * halves it along with the redraw time. */
    cads_str_copy(s_main.info_text, sizeof(s_main.info_text), "Ethernet blackout per band: ");
    cads_str_append(
        s_main.info_text, sizeof(s_main.info_text),
        s_shadow.fast_clock ? "11.5 ms (/8)" : "22.5 ms (/16, safe)");
}

static void cads_settings_open_confirm(
    cads_settings_confirm_kind_t kind, const char* title, const char* message,
    const cads_dialog_answer_t* answers, size_t answer_count);

/* Tracks the config last applied, so a reload only pokes a subsystem whose
 * value actually changed - re-issuing cads_net_set_config() on every reload
 * needlessly drops and re-adds the netif (finding #7). */
static cads_config_t s_applied_config;
static bool s_have_applied_config = false;

/* Set by the "Reload config" menu row (which runs on the INPUT task) and
 * serviced by cads_settings_service_config() from the app-tree loop (the
 * CONSOLE task). cads/storage is explicitly not thread-safe - doing the
 * littlefs load on the input task while the console task is mid-write (the
 * calibration kv_save, say) corrupts the volume, and its 512 B stack buffers
 * would overflow the 1 KB input-task stack. Both are avoided by making the
 * console task - the one task that already does every other storage access -
 * the single storage owner (findings #3, #4). */
static volatile bool s_config_reload_requested = false;
/* Factory reset also does storage work (rewrite /config.txt to defaults), so
 * it goes through the same console-task service rather than writing from the
 * input task - and it resets to the CONFIG defaults, not a second hardcoded
 * set that would diverge from the file and be undone on the next boot
 * (findings #18, #19). */
static volatile bool s_config_reset_requested = false;
#ifdef CADS_APP_MARAUDER_ENABLED
/* Same request/service split as reload/reset above, and for the same reason:
 * cads_marauder_join() ends up writing to the shared WiFi UART
 * (cads_hal_wifi_uart_write(), a plain busy-wait with no locking of its
 * own - see targets/itsboard/hal/hal_uart_wifi.c) which cads_marauder_tick()
 * also drives every console-task loop iteration. Calling it straight from
 * this row's INPUT-task handler would let two tasks race that one link, the
 * exact bug class the SPI-mutex incident (2026-08-26, see CLAUDE.md) taught
 * this project to check for first. Deferring to
 * cads_settings_service_config() - called from the same console-task loop
 * as cads_marauder_tick() (apps/bringup/explorer_app_demo.c) - keeps the
 * WiFi UART single-owner. */
static volatile bool s_wifi_join_requested = false;
#endif

/* Push a loaded config into the live subsystems and the settings shadow, so
 * the panel and the Brightness/SPI rows reflect the file. Only re-applies a
 * subsystem whose value changed since the last apply. WiFi/modules/wifi (the
 * PPPoS internet-connectivity path) is NOT wired in here - deliberately.
 * That module is complete, host-tested, committed, and stays that way for a
 * future second ESP32, but the one physical co-processor link this board has
 * (CN8 pins 8/9, USART6) now runs ESP32Marauder's CLI instead (2026-08-28
 * decision - see docs/reference/marauder-coprocessor.md and the ROADMAP
 * log). Calling cads_wifi_connect() here would write PPP's plaintext
 * bootstrap line onto the same wire Marauder's CLI is listening on - not
 * dangerous, just wrong-headed noise on a link with a different job now.
 * Leaving the call sites out (rather than merely config-gated) also lets
 * --gc-sections (CMakeLists.txt) drop the PPP netif's static allocations
 * from the board image entirely, RAM this firmware cannot spare twice over.
 * Re-wire cads_wifi_init()/connect() from apps/settings or a future
 * per-UART settings row if/when a second co-processor carries PPP again.
 * MUST run on the console task (the storage owner) - see
 * s_config_reload_requested. */
static void cads_settings_apply_config(const cads_config_t* cfg) {
    bool first = !s_have_applied_config;

    if(first || cfg->brightness != s_applied_config.brightness) {
        cads_hal_display_backlight(cfg->brightness);
    }
    if(first || cfg->fast_clock != s_applied_config.fast_clock) {
        cads_hal_display_set_fast_clock(cfg->fast_clock);
    }
    s_shadow.brightness_percent = cfg->brightness;
    s_shadow.fast_clock = cfg->fast_clock;

    if(first || cfg->net_dhcp != s_applied_config.net_dhcp ||
       cfg->net_ip != s_applied_config.net_ip ||
       cfg->net_netmask != s_applied_config.net_netmask ||
       cfg->net_gateway != s_applied_config.net_gateway) {
        cads_net_config_t net = {
            .use_dhcp = cfg->net_dhcp, .ip = cfg->net_ip,
            .netmask = cfg->net_netmask, .gateway = cfg->net_gateway};
        cads_net_set_config(&net);
    }

#ifdef CADS_APP_MARAUDER_ENABLED
    cads_str_copy(
        s_wifi_join_detail, sizeof(s_wifi_join_detail),
        (cfg->wifi_enabled && cfg->wifi_ssid[0] != '\0') ? cfg->wifi_ssid : "not configured");
#endif

    s_applied_config = *cfg;
    s_have_applied_config = true;
    cads_settings_refresh_details();
    cads_settings_refresh_info();
}

/* Load + apply the config on the console task. Called at settings_init (before
 * the input task is attached, so already on the console task) and by
 * cads_settings_service_config() when a reload was requested. */
static void cads_settings_load_and_apply(void) {
    cads_config_t cfg;
    if(cads_config_load(&cfg) == CADS_STORAGE_OK) {
        cads_settings_apply_config(&cfg);
        cads_menu_invalidate(&s_main.menu);
    }
}

void cads_settings_service_config(void) {
    if(s_config_reset_requested) {
        s_config_reset_requested = false;
        cads_config_t defaults;
        cads_config_defaults(&defaults);
        (void)cads_config_save(&defaults); /* persist, so the reset survives a reboot */
        cads_settings_apply_config(&defaults);
        cads_menu_invalidate(&s_main.menu);
    }
    if(s_config_reload_requested) {
        s_config_reload_requested = false;
        cads_settings_load_and_apply();
    }
#ifdef CADS_APP_MARAUDER_ENABLED
    if(s_wifi_join_requested) {
        s_wifi_join_requested = false;
        cads_marauder_join(s_applied_config.wifi_ssid, s_applied_config.wifi_password);
    }
#endif
}

/* The menu row handler: only REQUEST a reload. The actual storage work happens
 * on the console task in cads_settings_service_config(); the confirm dialog is
 * informational (the change is visible on the Settings rows next time they
 * draw). */
static void cads_settings_config_reload(void) {
    s_config_reload_requested = true;
    static const cads_dialog_answer_t answers[] = {{CadsKeyOk, "OK"}};
    cads_settings_open_confirm(
        CADS_SETTINGS_CONFIRM_CONFIG, "Reload config",
        "Re-reading /config.txt and applying it.", answers, 1u);
}

#ifdef CADS_APP_MARAUDER_ENABLED
/* Requests a join against whatever wifi.ssid/wifi.password /config.txt last
 * applied (s_applied_config, kept current by cads_settings_apply_config()) -
 * not a fresh read of the file, so "Reload config" first if it was just
 * edited. The actual join (cads_marauder_join(), which starts a background
 * scan-and-match - see apps/marauder/cads_marauder.h) runs from
 * cads_settings_service_config() on the console task; see
 * s_wifi_join_requested's own comment for why this row can't just call it
 * directly. */
static void cads_settings_wifi_join(void) {
    static const cads_dialog_answer_t answers[] = {{CadsKeyOk, "OK"}};
    if(!s_applied_config.wifi_enabled || s_applied_config.wifi_ssid[0] == '\0') {
        cads_settings_open_confirm(
            CADS_SETTINGS_CONFIRM_WIFI_JOIN, "Join WiFi",
            "No WiFi SSID configured - set wifi.enabled/wifi.ssid/wifi.password "
            "in /config.txt and Reload config first.",
            answers, 1u);
        return;
    }
    s_wifi_join_requested = true;
    cads_settings_open_confirm(
        CADS_SETTINGS_CONFIRM_WIFI_JOIN, "Join WiFi",
        "Scanning for the configured SSID and joining over the Marauder link.",
        answers, 1u);
}
#endif

static uint8_t cads_settings_next_brightness(uint8_t current) {
    if(current < 25u) return 25u;
    if(current < 50u) return 50u;
    if(current < 75u) return 75u;
    if(current < 100u) return 100u;
    return 25u;
}

/* --- the confirm/info view --------------------------------------------------
 *
 * A dialog nested inside the settings view could show its Yes/No/OK answers as
 * on-screen buttons, but the soft-key strip is only re-applied by the
 * compositor when the CURRENT VIEW changes (cads_gui_adopt_view(), driven by
 * the dispatcher's generation counter) - a view has no supported way to update
 * its own key labels while staying current. So the touch buttons would read
 * "Yes"/"No" while the physical strip still read whatever the settings list
 * left there. Pushing the dialog as its own view sidesteps that: the view
 * change itself re-applies the strip from cads_dialog_softkeys(), so both
 * rails agree. See the report to the maintainer for the general case.
 */

static void cads_settings_confirm_finish(int result) {
    if(s_confirm.kind == CADS_SETTINGS_CONFIRM_RESET && result == 0) {
        s_config_reset_requested = true; /* serviced on the console task */
    }
    s_confirm.kind = CADS_SETTINGS_CONFIRM_NONE;
    cads_view_dispatcher_pop(s_confirm.dispatcher);
}

static void cads_settings_confirm_draw(cads_rect_t area, void* context) {
    (void)area;
    (void)context;
    if(cads_dialog_is_dirty(&s_confirm.dialog)) cads_dialog_draw(&s_confirm.dialog);
}

static bool cads_settings_confirm_input(const cads_input_event_t* event, void* context) {
    (void)context;
    (void)cads_dialog_input(&s_confirm.dialog, event);
    if(cads_dialog_is_dirty(&s_confirm.dialog)) {
        cads_view_dirty_rect(&s_confirm.view, cads_dialog_damage(&s_confirm.dialog));
    }
    int result = cads_dialog_result(&s_confirm.dialog);
    if(result != CADS_DIALOG_PENDING) cads_settings_confirm_finish(result);
    return true; /* modal: nothing else in this view to offer the event to */
}

static void cads_settings_confirm_enter(void* context) {
    (void)context;
    cads_dialog_layout(&s_confirm.dialog, cads_view_area(&s_confirm.view));
}

static void cads_settings_open_confirm(
    cads_settings_confirm_kind_t kind,
    const char* title,
    const char* message,
    const cads_dialog_answer_t* answers,
    size_t answer_count) {
    s_confirm.kind = kind;
    cads_dialog_init(&s_confirm.dialog, title, message, answers, answer_count);

    size_t n = cads_dialog_softkeys(&s_confirm.dialog, s_confirm.keys, CADS_DIALOG_ANSWERS);
    cads_view_set_softkeys(&s_confirm.view, s_confirm.keys, n);
    cads_view_set_title(&s_confirm.view, title);

    (void)cads_view_dispatcher_push(s_confirm.dispatcher, CADS_VIEW_ID_SETTINGS_CONFIRM);
}

/* --- the settings list -------------------------------------------------------- */

static void cads_settings_draw_info(const cads_settings_main_t* app) {
    cads_rect_t r = app->info_rect;
    cads_canvas_fill_rect(r.x, r.y, r.width, r.height, CadsColorBackground);
    cads_canvas_draw_hline(r.x, r.y, r.width, CadsColorGrayDark);
    cads_rect_t text = {r.x, (int16_t)(r.y + 6), r.width, (int16_t)(r.height - 6)};
    cads_canvas_draw_text_aligned(
        text, CadsAlignCenter, &cads_font12, app->info_text, CadsColorGray);
}

static void cads_settings_activate(const cads_menu_item_t* item, size_t index, void* context) {
    (void)index;
    (void)context;

    switch((cads_settings_row_t)item->id) {
        case CADS_SETTINGS_ROW_BRIGHTNESS:
            s_shadow.brightness_percent =
                cads_settings_next_brightness(s_shadow.brightness_percent);
            cads_hal_display_backlight(s_shadow.brightness_percent);
            cads_settings_refresh_details();
            cads_menu_invalidate(&s_main.menu);
            break;

        case CADS_SETTINGS_ROW_SPI_CLOCK:
            s_shadow.fast_clock = !s_shadow.fast_clock;
            cads_hal_display_set_fast_clock(s_shadow.fast_clock);
            cads_settings_refresh_details();
            cads_menu_invalidate(&s_main.menu);
            cads_settings_refresh_info();
            s_main.need_info = true;
            cads_view_dirty_rect(&s_main.view, s_main.info_rect);
            break;

        case CADS_SETTINGS_ROW_CALIBRATION:
            (void)cads_view_dispatcher_push(s_main.dispatcher, CADS_VIEW_ID_TOUCH_CALIB);
            break;

        case CADS_SETTINGS_ROW_TEST_PATTERN:
            (void)cads_view_dispatcher_push(s_main.dispatcher, CADS_VIEW_ID_TEST_PATTERN);
            break;

        case CADS_SETTINGS_ROW_CONFIG_RELOAD:
            cads_settings_config_reload();
            break;

#ifdef CADS_APP_MARAUDER_ENABLED
        case CADS_SETTINGS_ROW_WIFI_JOIN:
            cads_settings_wifi_join();
            break;
#endif

        case CADS_SETTINGS_ROW_RESET: {
            static const cads_dialog_answer_t answers[] = {
                {CadsKeyOk, "Yes"}, {CadsKeyBack, "No"}};
            cads_settings_open_confirm(
                CADS_SETTINGS_CONFIRM_RESET, "Factory reset",
                "Reset brightness and SPI clock to defaults? This cannot be "
                "undone.",
                answers, 2u);
            break;
        }
    }
}

static void cads_settings_draw(cads_rect_t area, void* context) {
    (void)area;
    cads_settings_main_t* app = (cads_settings_main_t*)context;
    if(cads_menu_is_dirty(&app->menu)) cads_menu_draw(&app->menu);
    if(app->need_info) {
        cads_settings_draw_info(app);
        app->need_info = false;
    }
}

static bool cads_settings_input(const cads_input_event_t* event, void* context) {
    cads_settings_main_t* app = (cads_settings_main_t*)context;
    bool consumed = cads_menu_input(&app->menu, event);
    if(cads_menu_is_dirty(&app->menu)) {
        cads_view_dirty_rect(&app->view, cads_menu_damage(&app->menu));
    }
    return consumed;
}

static void cads_settings_enter(void* context) {
    cads_settings_main_t* app = (cads_settings_main_t*)context;
    cads_rect_t area = cads_view_area(&app->view);

    cads_rect_t menu_area = {
        area.x, area.y, area.width, (int16_t)(area.height - CADS_SETTINGS_INFO_HEIGHT)};
    cads_menu_set_area(&app->menu, menu_area);

    app->info_rect.x = area.x;
    app->info_rect.y = (int16_t)(area.y + area.height - CADS_SETTINGS_INFO_HEIGHT);
    app->info_rect.width = area.width;
    app->info_rect.height = CADS_SETTINGS_INFO_HEIGHT;
    app->need_info = true;
}

static const cads_softkey_t cads_settings_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk, "Open"},
    {CadsKeyBack, "Back"},
};

/* --- test pattern view ------------------------------------------------------ */

/* A thin view over gui/cads_splash.c's cads_test_pattern_draw() - the palette
 * swatches / corner markers / diagonals that used to run on every boot, now
 * on demand. Draws the full-screen pattern; the compositor clips it to the
 * content area, which loses only the corner markers under the status/soft-key
 * bars - the hue and byte-order checks the pattern exists for are all in the
 * middle band. No state of its own beyond the view. */
static cads_view_t s_test_pattern_view;

static void cads_settings_test_pattern_draw(cads_rect_t area, void* context) {
    (void)area;
    (void)context;
    cads_test_pattern_draw();
}

static const cads_softkey_t cads_test_pattern_keys[] = {
    {CadsKeyBack, "Back"},
};

void cads_settings_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    cads_settings_refresh_details();
    cads_settings_refresh_info();

    s_main.dispatcher = dispatcher;
    cads_menu_init(&s_main.menu, s_items, sizeof(s_items) / sizeof(s_items[0]), &cads_font16);
    cads_menu_set_activate(&s_main.menu, cads_settings_activate, &s_main);
    cads_view_init(&s_main.view, cads_settings_draw, cads_settings_input, &s_main);
    cads_view_set_lifecycle(&s_main.view, cads_settings_enter, NULL);
    cads_view_set_title(&s_main.view, "Settings");
    cads_view_set_softkeys(
        &s_main.view, cads_settings_keys, sizeof(cads_settings_keys) / sizeof(cads_settings_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_SETTINGS, &s_main.view);

    s_confirm.dispatcher = dispatcher;
    s_confirm.kind = CADS_SETTINGS_CONFIRM_NONE;
    cads_view_init(&s_confirm.view, cads_settings_confirm_draw, cads_settings_confirm_input, NULL);
    cads_view_set_lifecycle(&s_confirm.view, cads_settings_confirm_enter, NULL);
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_SETTINGS_CONFIRM, &s_confirm.view);

    cads_touch_calib_init(dispatcher);

    /* Load and apply the persistent config file at startup (writes the base
     * version if none exists). Same app-tree-init timing as touch calibration;
     * a Settings -> Reload config re-reads it without a reboot. */
    cads_settings_load_and_apply();

    /* No input handler: the dispatcher's own Back handling pops the view, and
     * the pattern is static, so there is nothing else to do here. */
    cads_view_init(&s_test_pattern_view, cads_settings_test_pattern_draw, NULL, NULL);
    cads_view_set_title(&s_test_pattern_view, "Test pattern");
    cads_view_set_softkeys(
        &s_test_pattern_view, cads_test_pattern_keys,
        sizeof(cads_test_pattern_keys) / sizeof(cads_test_pattern_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_TEST_PATTERN, &s_test_pattern_view);
}

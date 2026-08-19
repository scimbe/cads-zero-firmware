#include "cads_settings.h"

#include <stdbool.h>
#include <stdint.h>

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_dialog.h"
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
    CADS_SETTINGS_ROW_RESET,
} cads_settings_row_t;

typedef enum {
    CADS_SETTINGS_CONFIRM_NONE = 0,
    CADS_SETTINGS_CONFIRM_CALIBRATION,
    CADS_SETTINGS_CONFIRM_RESET,
} cads_settings_confirm_kind_t;

static char s_brightness_detail[8];
static char s_spi_detail[16];

static cads_menu_item_t s_items[] = {
    {"Brightness", s_brightness_detail, CADS_SETTINGS_ROW_BRIGHTNESS},
    {"SPI clock", s_spi_detail, CADS_SETTINGS_ROW_SPI_CLOCK},
    {"Touch calibration", NULL, CADS_SETTINGS_ROW_CALIBRATION},
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

static void cads_settings_apply_defaults(void) {
    s_shadow.brightness_percent = 100u;
    s_shadow.fast_clock = false;
    cads_hal_display_backlight(s_shadow.brightness_percent);
    cads_hal_display_set_fast_clock(s_shadow.fast_clock);
    cads_settings_refresh_details();
    cads_settings_refresh_info();
}

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
        cads_settings_apply_defaults();
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

        case CADS_SETTINGS_ROW_CALIBRATION: {
            static const cads_dialog_answer_t answers[] = {{CadsKeyOk, "OK"}};
            cads_settings_open_confirm(
                CADS_SETTINGS_CONFIRM_CALIBRATION, "Touch calibration",
                "Not implemented yet. This entry will start the calibration "
                "flow once the touch driver exposes one.",
                answers, 1u);
            break;
        }

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
}

#include "cads_touch_calib.h"

#include <stdbool.h>
#include <stdint.h>

#include "cads/storage/kv.h"
#include "cads/storage/storage.h"
#include "cads_hal.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

/* The raw-ADC reads and the calibration setter are hal_touch.c's board-only
 * diagnostics, declared extern here the same way apps/bringup/explorer.c
 * does rather than promoted into core/cads_hal.h - calibration is only
 * meaningful against a real XPT2046, and the host build below never calls
 * them. */
#ifdef CADS_TARGET_ITSBOARD
uint16_t cads_hal_touch_read_raw_x(void);
uint16_t cads_hal_touch_read_raw_y(void);
bool cads_hal_touch_irq_raw(void);
void cads_hal_touch_set_calibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max);
void cads_hal_touch_get_calibration(
    uint16_t* x_min, uint16_t* x_max, uint16_t* y_min, uint16_t* y_max);
#endif

/* Crosshair targets sit this many pixels in from each corner: far enough
 * from the bezel that a fingertip lands cleanly, close enough that the
 * two-point extrapolation to the true edges (see cads_touch_calib_apply)
 * spans most of the panel and so is not amplifying a small raw-count
 * difference into a large one. */
#define CADS_CALIB_INSET 40
#define CADS_CALIB_CROSS 12 /* half-length of each crosshair arm, px */

/* Persist keys. Short - cads/storage kv is a small fixed table. */
#define CADS_CALIB_KEY_XMIN "tp.xmin"
#define CADS_CALIB_KEY_XMAX "tp.xmax"
#define CADS_CALIB_KEY_YMIN "tp.ymin"
#define CADS_CALIB_KEY_YMAX "tp.ymax"

typedef enum {
    CADS_CALIB_STEP_TOP_LEFT = 0,
    CADS_CALIB_STEP_BOTTOM_RIGHT,
    CADS_CALIB_STEP_DONE,
} cads_calib_step_t;

static struct {
    cads_view_t view;
    bool active; /* set between enter and exit, gates the tick */
    bool was_pressed;
    cads_calib_step_t step;
    uint16_t raw_x_tl, raw_y_tl;
    bool saved;
} s_calib;

/* Settings live in one kv file, shared with whatever else this firmware
 * eventually persists (brightness, SPI clock...). This module is the first
 * to actually use it, so it owns opening it - lazily and once. No boot-path
 * change: the mount+open happens the first time calibration is loaded or
 * saved, which is init time (cads_touch_calib_load from cads_settings_init)
 * on the app-tree path, exactly when touch first matters. cads_kv is a
 * single global table, so a later settings-persistence task shares this same
 * open rather than opening a second file. */
#define CADS_SETTINGS_KV_PATH "/settings.kv"

#ifdef CADS_TARGET_ITSBOARD
static bool cads_settings_kv_ready(void) {
    static bool ready = false;
    if(ready) return true;
    if(cads_storage_mount() != CADS_STORAGE_OK) return false;
    if(cads_kv_open(CADS_SETTINGS_KV_PATH) != CADS_STORAGE_OK) return false;
    ready = true;
    return true;
}
#endif

void cads_touch_calib_load(void) {
#ifdef CADS_TARGET_ITSBOARD
    if(!cads_settings_kv_ready()) return;
    int32_t xmin, xmax, ymin, ymax;
    if(cads_kv_get_i32(CADS_CALIB_KEY_XMIN, &xmin) == CADS_STORAGE_OK &&
       cads_kv_get_i32(CADS_CALIB_KEY_XMAX, &xmax) == CADS_STORAGE_OK &&
       cads_kv_get_i32(CADS_CALIB_KEY_YMIN, &ymin) == CADS_STORAGE_OK &&
       cads_kv_get_i32(CADS_CALIB_KEY_YMAX, &ymax) == CADS_STORAGE_OK) {
        cads_hal_touch_set_calibration(
            (uint16_t)xmin, (uint16_t)xmax, (uint16_t)ymin, (uint16_t)ymax);
    }
    /* No saved calibration: the driver keeps its built-in defaults. */
#endif
}

#ifdef CADS_TARGET_ITSBOARD
/* Turn the two corner raw readings into a full-panel min/max calibration by
 * extrapolating the line through them out to display coordinates 0 and the
 * full span - see hal_touch.c's cads_touch_scale()/cads_hal_touch_read()
 * for the exact mapping this inverts (display X comes from the controller's
 * raw Y; display Y from raw X, mirrored). */
static void cads_touch_calib_apply(uint16_t raw_x_br, uint16_t raw_y_br) {
    const int W = CADS_DISPLAY_WIDTH;
    const int H = CADS_DISPLAY_HEIGHT;

    /* X axis (controller raw_y): TL target at display x=INSET, BR at
     * x=W-1-INSET. rpp = raw_y counts per display-x pixel. */
    int span_x = (W - 1 - 2 * CADS_CALIB_INSET);
    if(span_x == 0) span_x = 1;
    int rpp_x = ((int)raw_y_br - (int)s_calib.raw_y_tl) * 1000 / span_x; /* x1000 fixed point */
    int y_min = (int)s_calib.raw_y_tl - CADS_CALIB_INSET * rpp_x / 1000;
    int y_max = (int)s_calib.raw_y_tl + (W - CADS_CALIB_INSET) * rpp_x / 1000;

    /* Y axis (controller raw_x, mirrored): TL target at display y=INSET, BR
     * at y=H-1-INSET. raw_x decreases as display_y increases. */
    int span_y = (H - 1 - 2 * CADS_CALIB_INSET);
    if(span_y == 0) span_y = 1;
    int rpp_y = ((int)s_calib.raw_x_tl - (int)raw_x_br) * 1000 / span_y;
    int x_min = (int)raw_x_br - CADS_CALIB_INSET * rpp_y / 1000;
    int x_max = (int)raw_x_br + (H - CADS_CALIB_INSET) * rpp_y / 1000;

    /* Clamp into the ADC's 12-bit range and reject a degenerate result
     * (axes swapped or barely-separated taps) by leaving calibration
     * untouched - better a slightly-off default than an unusable panel. */
    if(x_min < 0) x_min = 0;
    if(y_min < 0) y_min = 0;
    if(x_max > 4095) x_max = 4095;
    if(y_max > 4095) y_max = 4095;
    if(x_max - x_min < 500 || y_max - y_min < 500) {
        s_calib.saved = false;
        return;
    }

    cads_hal_touch_set_calibration(
        (uint16_t)x_min, (uint16_t)x_max, (uint16_t)y_min, (uint16_t)y_max);

    /* Apply always succeeds (it is just the live driver range); persistence
     * is best-effort on top. `saved` reflects the live apply so the UI does
     * not falsely claim failure when only the flash write could not complete
     * (no card, full filesystem) - the calibration still works this session,
     * it just will not survive a reboot, which the done screen's wording
     * ("Saved") is honest enough about for a diagnostic tool. */
    s_calib.saved = true;
    if(cads_settings_kv_ready() && cads_kv_set_i32(CADS_CALIB_KEY_XMIN, x_min) == CADS_STORAGE_OK &&
       cads_kv_set_i32(CADS_CALIB_KEY_XMAX, x_max) == CADS_STORAGE_OK &&
       cads_kv_set_i32(CADS_CALIB_KEY_YMIN, y_min) == CADS_STORAGE_OK &&
       cads_kv_set_i32(CADS_CALIB_KEY_YMAX, y_max) == CADS_STORAGE_OK) {
        (void)cads_kv_save();
    }
}
#endif

static void cads_touch_calib_reset(void) {
    s_calib.step = CADS_CALIB_STEP_TOP_LEFT;
    s_calib.was_pressed = false;
    s_calib.saved = false;
}

void cads_touch_calib_tick(uint32_t now_ms) {
    (void)now_ms;
#ifdef CADS_TARGET_ITSBOARD
    if(!s_calib.active || s_calib.step == CADS_CALIB_STEP_DONE) return;

    bool pressed = cads_hal_touch_irq_raw();
    bool rising = pressed && !s_calib.was_pressed;
    s_calib.was_pressed = pressed;
    if(!rising) return;

    uint16_t raw_x = cads_hal_touch_read_raw_x();
    uint16_t raw_y = cads_hal_touch_read_raw_y();
    /* A read that came back clamped at the rails is the finger having lifted
     * mid-sample - ignore this edge and wait for a clean one. */
    if(raw_x == 0u || raw_x >= 0x0FFFu || raw_y == 0u || raw_y >= 0x0FFFu) {
        s_calib.was_pressed = false;
        return;
    }

    if(s_calib.step == CADS_CALIB_STEP_TOP_LEFT) {
        s_calib.raw_x_tl = raw_x;
        s_calib.raw_y_tl = raw_y;
        s_calib.step = CADS_CALIB_STEP_BOTTOM_RIGHT;
    } else {
        cads_touch_calib_apply(raw_x, raw_y);
        s_calib.step = CADS_CALIB_STEP_DONE;
    }
    cads_view_dirty_rect(&s_calib.view, cads_view_area(&s_calib.view));
#endif
}

static void cads_touch_calib_draw_cross(int cx, int cy, cads_color_t color) {
    cads_canvas_fill_rect(
        (int16_t)(cx - CADS_CALIB_CROSS), (int16_t)cy, (int16_t)(2 * CADS_CALIB_CROSS), 2, color);
    cads_canvas_fill_rect(
        (int16_t)cx, (int16_t)(cy - CADS_CALIB_CROSS), 2, (int16_t)(2 * CADS_CALIB_CROSS), color);
}

static void cads_touch_calib_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorBackground);

#ifndef CADS_TARGET_ITSBOARD
    cads_rect_t msg = {area.x, (int16_t)(area.y + area.height / 2 - 10), area.width, 20};
    cads_canvas_draw_text_aligned(
        msg, CadsAlignCenter, &cads_font12, "Touch calibration needs a real panel",
        CadsColorGray);
    return;
#else
    const char* prompt = "";
    int cx = 0, cy = 0;
    bool show_cross = true;

    switch(s_calib.step) {
        case CADS_CALIB_STEP_TOP_LEFT:
            prompt = "Tap the top-left crosshair";
            cx = area.x + CADS_CALIB_INSET;
            cy = area.y + CADS_CALIB_INSET;
            break;
        case CADS_CALIB_STEP_BOTTOM_RIGHT:
            prompt = "Now tap the bottom-right crosshair";
            cx = area.x + area.width - 1 - CADS_CALIB_INSET;
            cy = area.y + area.height - 1 - CADS_CALIB_INSET;
            break;
        case CADS_CALIB_STEP_DONE:
            prompt = s_calib.saved ? "Saved. Back to exit, OK to redo"
                                   : "Taps too close - OK to retry";
            show_cross = false;
            break;
    }

    if(show_cross) cads_touch_calib_draw_cross(cx, cy, CadsColorAccent);

    cads_rect_t prompt_box = {area.x, (int16_t)(area.y + area.height / 2 - 8), area.width, 20};
    cads_canvas_draw_text_aligned(
        prompt_box, CadsAlignCenter, &cads_font12, prompt, CadsColorBrandLight);
#endif
}

static bool cads_touch_calib_input(const cads_input_event_t* event, void* context) {
    (void)context;
    /* OK restarts the flow (from the done screen, or to re-tap a corner);
     * everything else, including touch taps as button events, is ignored -
     * the raw sampling is done in the tick, not here. */
    if(event->type == CadsInputPress && event->key == CadsKeyOk) {
        cads_touch_calib_reset();
        cads_view_dirty_rect(&s_calib.view, cads_view_area(&s_calib.view));
        return true;
    }
    return false;
}

static void cads_touch_calib_enter(void* context) {
    (void)context;
    s_calib.active = true;
    cads_touch_calib_reset();
}

static void cads_touch_calib_exit(void* context) {
    (void)context;
    s_calib.active = false;
}

static const cads_softkey_t cads_touch_calib_keys[] = {
    {CadsKeyOk, "Restart"},
    {CadsKeyBack, "Back"},
};

void cads_touch_calib_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    cads_view_init(&s_calib.view, cads_touch_calib_draw, cads_touch_calib_input, &s_calib);
    cads_view_set_lifecycle(&s_calib.view, cads_touch_calib_enter, cads_touch_calib_exit);
    cads_view_set_title(&s_calib.view, "Touch calibration");
    cads_view_set_softkeys(
        &s_calib.view, cads_touch_calib_keys,
        sizeof(cads_touch_calib_keys) / sizeof(cads_touch_calib_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_TOUCH_CALIB, &s_calib.view);

    /* Apply any saved calibration now, at app-tree init - the earliest point
     * touch is used through the GUI. Self-contained: opens storage/kv itself
     * (cads_settings_kv_ready), no boot-path change. */
    cads_touch_calib_load();
}

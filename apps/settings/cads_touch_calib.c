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

/* Multi-point calibration: a grid of targets across the panel, sampled one at
 * a time, then a least-squares line fit per axis. Two corners (the old flow)
 * extrapolate the whole panel from two taps, so any error at either tap - or
 * any panel non-linearity - scales up across the screen; the symptom on this
 * hardware was selection drifting by up to a row near the top while the bottom
 * (and the soft-key strip) stayed accurate. Sampling many rows, each at a left
 * and a right column, and least-squares fitting averages the per-tap error out
 * and pins the fit across the full height. */
#define CADS_CALIB_ROWS 5
#define CADS_CALIB_COLS 2
#define CADS_CALIB_POINTS (CADS_CALIB_ROWS * CADS_CALIB_COLS)

static struct {
    cads_view_t view;
    bool active; /* set between enter and exit, gates the tick */
    bool was_pressed;
    int point; /* 0..CADS_CALIB_POINTS; == CADS_CALIB_POINTS means done */
    int16_t tx[CADS_CALIB_POINTS], ty[CADS_CALIB_POINTS]; /* target display coords */
    uint16_t rx[CADS_CALIB_POINTS], ry[CADS_CALIB_POINTS]; /* raw readings at each */
    bool saved;
} s_calib;

/* Target crosshair for point i, in the view's display coordinates. Columns run
 * left->right, rows top->bottom, so the order is (L,row0),(R,row0),(L,row1)...
 * exactly "each row, left then right". Board-only: the host draw shows a
 * "needs a real panel" message and never places targets. */
#ifdef CADS_TARGET_ITSBOARD
static void cads_calib_target(int i, cads_rect_t area, int* cx, int* cy) {
    int row = i / CADS_CALIB_COLS;
    int col = i % CADS_CALIB_COLS;
    int spanx = area.width - 1 - 2 * CADS_CALIB_INSET;
    int spany = area.height - 1 - 2 * CADS_CALIB_INSET;
    *cx = area.x + CADS_CALIB_INSET + (CADS_CALIB_COLS > 1 ? col * spanx / (CADS_CALIB_COLS - 1) : 0);
    *cy = area.y + CADS_CALIB_INSET + (CADS_CALIB_ROWS > 1 ? row * spany / (CADS_CALIB_ROWS - 1) : 0);
}
#endif /* CADS_TARGET_ITSBOARD */

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
/* Least-squares line fit y = a*x + b over n points, fixed-point: returns a
 * scaled by 1000 (aq1000) and b directly, using int64 accumulators. */
static void cads_calib_fit(const int* xs, const int* ys, int n, int* aq1000, int* b) {
    long long sx = 0, sy = 0, sxx = 0, sxy = 0;
    for(int i = 0; i < n; i++) {
        sx += xs[i]; sy += ys[i];
        sxx += (long long)xs[i] * xs[i];
        sxy += (long long)xs[i] * ys[i];
    }
    long long denom = (long long)n * sxx - sx * sx;
    if(denom == 0) { *aq1000 = 0; *b = (int)(sy / (n ? n : 1)); return; }
    long long num = (long long)n * sxy - sx * sy;
    *aq1000 = (int)((num * 1000) / denom);
    *b = (int)((sy - (long long)(*aq1000) * sx / 1000) / n);
}

/* Turn the collected (target display coord, raw reading) pairs into the
 * driver's min/max calibration. The driver maps display_x from raw_y over
 * [y_min,y_max] and display_y from raw_x over [x_min,x_max], mirrored
 * (see hal_touch.c cads_hal_touch_read). So:
 *   raw_y = y_min + display_x * (y_max - y_min) / W  -> fit raw_y vs display_x
 *   raw_x = x_min + (H-1-display_y) * (x_max - x_min) / H -> fit raw_x vs u
 */
static void cads_touch_calib_apply_grid(void) {
    const int W = CADS_DISPLAY_WIDTH;
    const int H = CADS_DISPLAY_HEIGHT;
    int dx[CADS_CALIB_POINTS], rawy[CADS_CALIB_POINTS];
    int u[CADS_CALIB_POINTS], rawx[CADS_CALIB_POINTS];
    for(int i = 0; i < CADS_CALIB_POINTS; i++) {
        dx[i] = s_calib.tx[i];
        rawy[i] = s_calib.ry[i];
        u[i] = (H - 1) - s_calib.ty[i];
        rawx[i] = s_calib.rx[i];
    }

    int a_y, b_y, a_x, b_x; /* slopes x1000, intercepts */
    cads_calib_fit(dx, rawy, CADS_CALIB_POINTS, &a_y, &b_y);
    cads_calib_fit(u, rawx, CADS_CALIB_POINTS, &a_x, &b_x);

    int y_min = b_y;
    int y_max = b_y + a_y * (W - 1) / 1000;
    int x_min = b_x;
    int x_max = b_x + a_x * (H - 1) / 1000;

    if(x_min < 0) x_min = 0;
    if(y_min < 0) y_min = 0;
    if(x_max > 4095) x_max = 4095;
    if(y_max > 4095) y_max = 4095;

    /* Reject a degenerate fit (finger never moved, axes swapped) rather than
     * install an unusable map. */
    if(x_max - x_min < 500 || y_max - y_min < 500) {
        s_calib.saved = false;
        return;
    }

    cads_hal_touch_set_calibration(
        (uint16_t)x_min, (uint16_t)x_max, (uint16_t)y_min, (uint16_t)y_max);

    /* Persist. Live apply above is what matters for the session; the save is
     * best-effort on top. */
    s_calib.saved = true;
    if(cads_settings_kv_ready() && cads_kv_set_i32(CADS_CALIB_KEY_XMIN, x_min) == CADS_STORAGE_OK &&
       cads_kv_set_i32(CADS_CALIB_KEY_XMAX, x_max) == CADS_STORAGE_OK &&
       cads_kv_set_i32(CADS_CALIB_KEY_YMIN, y_min) == CADS_STORAGE_OK &&
       cads_kv_set_i32(CADS_CALIB_KEY_YMAX, y_max) == CADS_STORAGE_OK) {
        (void)cads_kv_save();
    }
}
#endif /* CADS_TARGET_ITSBOARD */

static void cads_touch_calib_reset(void) {
    s_calib.point = 0;
    s_calib.was_pressed = false;
    s_calib.saved = false;
}

void cads_touch_calib_tick(uint32_t now_ms) {
    (void)now_ms;
#ifdef CADS_TARGET_ITSBOARD
    if(!s_calib.active || s_calib.point >= CADS_CALIB_POINTS) return;

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

    /* Record the target we were showing and the raw reading for it, then
     * advance. The last tap triggers the least-squares fit + save. */
    cads_rect_t area = cads_view_area(&s_calib.view);
    int cx = 0, cy = 0;
    cads_calib_target(s_calib.point, area, &cx, &cy);
    s_calib.tx[s_calib.point] = (int16_t)cx;
    s_calib.ty[s_calib.point] = (int16_t)cy;
    s_calib.rx[s_calib.point] = raw_x;
    s_calib.ry[s_calib.point] = raw_y;
    s_calib.point++;

    if(s_calib.point >= CADS_CALIB_POINTS) {
        cads_touch_calib_apply_grid();
    }
    cads_view_dirty_rect(&s_calib.view, cads_view_area(&s_calib.view));
#endif
}

#ifdef CADS_TARGET_ITSBOARD
static void cads_touch_calib_draw_cross(int cx, int cy, cads_color_t color) {
    cads_canvas_fill_rect(
        (int16_t)(cx - CADS_CALIB_CROSS), (int16_t)cy, (int16_t)(2 * CADS_CALIB_CROSS), 2, color);
    cads_canvas_fill_rect(
        (int16_t)cx, (int16_t)(cy - CADS_CALIB_CROSS), 2, (int16_t)(2 * CADS_CALIB_CROSS), color);
}
#endif /* CADS_TARGET_ITSBOARD */

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
    if(s_calib.point >= CADS_CALIB_POINTS) {
        const char* prompt = s_calib.saved ? "Saved. Back to exit, OK to redo"
                                           : "Bad fit - OK to retry";
        cads_rect_t prompt_box = {area.x, (int16_t)(area.y + area.height / 2 - 8), area.width, 20};
        cads_canvas_draw_text_aligned(
            prompt_box, CadsAlignCenter, &cads_font12, prompt, CadsColorBrandLight);
    } else {
        int cx = 0, cy = 0;
        cads_calib_target(s_calib.point, area, &cx, &cy);
        cads_touch_calib_draw_cross(cx, cy, CadsColorAccent);

        /* Progress "N/10" plus a hint. Built without libc: point+1 and the
         * total are both <= 10, so a couple of digits by hand. */
        char label[40];
        char* w = label;
        const char* head = "Tap the crosshair  ";
        for(const char* h = head; *h; h++) *w++ = *h;
        int cur = s_calib.point + 1;
        if(cur >= 10) *w++ = (char)('0' + cur / 10);
        *w++ = (char)('0' + cur % 10);
        *w++ = '/';
        int tot = CADS_CALIB_POINTS;
        if(tot >= 10) *w++ = (char)('0' + tot / 10);
        *w++ = (char)('0' + tot % 10);
        *w = '\0';

        cads_rect_t prompt_box = {area.x, (int16_t)(area.y + area.height - 26), area.width, 20};
        cads_canvas_draw_text_aligned(
            prompt_box, CadsAlignCenter, &cads_font12, label, CadsColorBrandLight);
    }
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

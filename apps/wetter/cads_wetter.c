#include "cads_wetter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads/net/net.h"
#include "cads_hal.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "cads_wetter_net.h"
#include "l11_wetter_app_logic.h"

/*
 * Weather app - skeleton for lab L11. Registered in the apps menu, opens,
 * shows a placeholder. Everything below marked TODO(L11) is the lab task;
 * see the lesson page and tests/unit/test_wetter_app.c for what "done"
 * means (only changed fields redrawn, one per frame, nothing while idle).
 */

/*
 * The layout, given: tests/unit/test_wetter_app.c checks every blit against
 * exactly these rectangles (relative to the content area, 480 x 254). Each
 * field's draw code must paint its whole rectangle itself (background
 * first), so a field can be redrawn alone.
 */
typedef enum {
    WX_FIELD_TEMP = 0, /* "23.2 C", font24                                 */
    WX_FIELD_COND,     /* 96x96 icon at x=16 + condition text at (128,56)  */
    WX_FIELD_HUM,      /* "54 %"                                           */
    WX_FIELD_WIND,     /* "4.1 km/h"                                       */
    WX_FIELD_STATUS,   /* status line, font12, colour by level             */
    WX_FIELD_SOURCE,   /* "host:port" - changes with `lab 11 server`       */
    WX_FIELD_COUNT,
} wx_field_t;

static const cads_rect_t wx_field_rects[WX_FIELD_COUNT] = {
    [WX_FIELD_TEMP] = {128, 12, 200, 36},
    [WX_FIELD_COND] = {16, 8, 448, 96}, /* contains TEMP - redraw TEMP after it */
    [WX_FIELD_HUM] = {144, 120, 160, 24},
    [WX_FIELD_WIND] = {144, 152, 160, 24},
    [WX_FIELD_STATUS] = {16, 216, 448, 20},
    [WX_FIELD_SOURCE] = {144, 184, 320, 24},
};
/* Labels "Luftfeuchte", "Wind", "Quelle" at x=16, y=120/152/184 (static). */

typedef struct {
    cads_view_t view;
    bool active; /* view is current */
} cads_wetter_t;

static cads_wetter_t s_wx;

static const cads_softkey_t cads_wetter_keys[] = {
    {CadsKeyOk, "Abruf"},
    {CadsKeyBack, "Back"},
};

static void cads_wetter_draw(cads_rect_t area, void* context) {
    (void)context;
    /* TODO(L11): Layout zeichnen - Icon, Temperatur, Wetterlage, Feuchte,
     * Wind, Quelle, Statuszeile - aus dem View-Modell (rnlab_wx_view()).
     * Jedes Feld in einem eigenen Rechteck, das es komplett selbst uebermalt,
     * damit es allein neu gezeichnet werden kann. */
    /* Placeholder: the field frames of the layout, and a hint. */
    for(int f = 0; f < WX_FIELD_COUNT; f++) {
        const cads_rect_t* r = &wx_field_rects[f];
        cads_canvas_draw_rect((int16_t)(area.x + r->x), (int16_t)(area.y + r->y), r->width, r->height,
                              CadsColorGrayDark);
    }
    cads_rect_t box = {area.x, (int16_t)(area.y + 60), area.width, 24};
    cads_canvas_draw_text_aligned(box, CadsAlignCenter, &cads_font16, "Wetter-App: TODO(L11)",
                                  CadsColorGray);
}

void cads_wetter_tick(uint32_t now_ms) {
    if(!s_wx.active) return;
    /* TODO(L11), jeden Durchlauf der Hauptschleife:
     *  1. Netzstatus holen (cads_net_status), cads_wetter_net_service(now_ms).
     *  2. Laufenden Abruf fertig? -> rnlab_wx_fetch_done(rnlab_l11_app(), ...).
     *  3. rnlab_wx_should_fetch() -> cads_wetter_net_start() mit Host/Port aus
     *     rnlab_l11_app()->config, rnlab_wx_fetch_started().
     *  4. rnlab_wx_view() und rnlab_wx_view_diff() gegen das zuletzt
     *     Gezeichnete; pro Frame hoechstens EIN geaendertes Feld per
     *     cads_view_dirty_rect() melden (jeder Blit stoppt den Ethernet-
     *     Empfang, PA7) - und nichts, solange sich nichts aendert.
     *  5. Optional: Kosten des Redraws messen (Luecke zwischen zwei Ticks)
     *     und in rnlab_l11_app()->last_redraw_us usw. ablegen. */
    (void)now_ms;
}

static bool cads_wetter_input(const cads_input_event_t* event, void* context) {
    (void)context;
    if(event->type == CadsInputPress && event->key == CadsKeyOk) {
        rnlab_wx_request_refresh(rnlab_l11_app(), cads_hal_ticks_ms());
        return true;
    }
    return false; /* Back pops the view */
}

static void cads_wetter_enter(void* context) {
    cads_wetter_t* app = (cads_wetter_t*)context;
    app->active = true;
    rnlab_l11_app()->view_open = true;
}

static void cads_wetter_exit(void* context) {
    cads_wetter_t* app = (cads_wetter_t*)context;
    app->active = false;
    rnlab_l11_app()->view_open = false;
    /* TODO(L11): laufenden Abruf abbrechen (cads_wetter_net_abort()) - sonst
     * holt niemand das Ergebnis ab. */
}

void cads_wetter_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;
    (void)rnlab_l11_app(); /* first use initialises the shared controller */
    cads_view_init(&s_wx.view, cads_wetter_draw, cads_wetter_input, &s_wx);
    cads_view_set_lifecycle(&s_wx.view, cads_wetter_enter, cads_wetter_exit);
    cads_view_set_title(&s_wx.view, "Wetter");
    cads_view_set_softkeys(
        &s_wx.view, cads_wetter_keys, sizeof(cads_wetter_keys) / sizeof(cads_wetter_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_WETTER, &s_wx.view);
}

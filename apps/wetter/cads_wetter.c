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
    cads_rect_t box = {area.x, (int16_t)(area.y + 100), area.width, 24};
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

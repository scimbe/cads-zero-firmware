#include "l11_wetter_app_logic.h"

#include <string.h>

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"

const char* rnlab_l11_slug(void) {
    return "wetter-app";
}

/* ------------------------------------------------------------------------- */
/* WMO codes (open-meteo documentation, "WMO Weather interpretation codes")  */
/* ------------------------------------------------------------------------- */

const char* rnlab_wx_wmo_text(int32_t code) {
    /* TODO(L11): WMO-Code -> kurzer deutscher Text (ASCII, <= 23 Zeichen),
     * Tabelle in der open-meteo-Doku ("WMO Weather interpretation codes"):
     * 0 Klar, 1 Ueberwiegend klar, 2 Teilweise bewoelkt, 3 Bedeckt, 45/48
     * Nebel, 51-57 Niesel, 61-67 Regen, 71-77 Schnee, 80-82 Schauer, 85/86
     * Schneeschauer, 95-99 Gewitter; alles andere "Unbekannt". */
    (void)code;
    return "Unbekannt";
}

rnlab_wx_icon_t rnlab_wx_wmo_icon(int32_t code) {
    /* TODO(L11): dieselben Gruppen -> rnlab_wx_icon_t. */
    (void)code;
    return RNLAB_WX_ICON_UNKNOWN;
}

/* ------------------------------------------------------------------------- */
/* controller                                                                */
/* ------------------------------------------------------------------------- */

static rnlab_wx_t s_app;
static bool s_app_ready;

rnlab_wx_t* rnlab_l11_app(void) {
    if(!s_app_ready) {
        rnlab_wx_init(&s_app, 0u);
        s_app_ready = true;
    }
    return &s_app;
}

/* "a is at or after b" on a wrapping 32-bit millisecond clock (49 days). */
static bool rnlab_wx_reached(uint32_t now, uint32_t due) {
    return (int32_t)(now - due) >= 0;
}

void rnlab_wx_init(rnlab_wx_t* wx, uint32_t now_ms) {
    memset(wx, 0, sizeof(*wx));
    cads_str_copy(wx->config.host, sizeof(wx->config.host), RNLAB_L10_DEFAULT_HOST);
    wx->config.port = RNLAB_L10_DEFAULT_PORT;
    wx->config.interval_ms = RNLAB_WX_INTERVAL_DEFAULT_MS;
    wx->next_fetch_ms = now_ms;
    wx->last_error = RNLAB_FETCH_OK;
}

bool rnlab_wx_should_fetch(rnlab_wx_t* wx, uint32_t now_ms, bool link_up, bool has_ip) {
    /* TODO(L11): link_up/has_ip in wx merken. Kein Abruf ohne Link, ohne
     * Adresse oder waehrend einer laeuft. Kommt das Netz (wieder), ist ein
     * Abruf sofort faellig. Sonst: faellig, wenn now_ms next_fetch_ms
     * erreicht hat - mit rnlab_wx_reached(), die Uhr laeuft nach 49 Tagen
     * ueber. */
    (void)now_ms;
    wx->link_up = link_up;
    wx->has_ip = has_ip;
    (void)rnlab_wx_reached;
    return false;
}

void rnlab_wx_fetch_started(rnlab_wx_t* wx, uint32_t now_ms) {
    (void)now_ms;
    wx->fetching = true;
    wx->fetches++;
}

void rnlab_wx_fetch_done(rnlab_wx_t* wx, uint32_t now_ms, const rnlab_fetch_result_t* result) {
    /* TODO(L11): Erfolg -> Werte + Zeitpunkt uebernehmen, Fehlerzaehler und
     * Backoff zuruecksetzen, naechster Abruf nach config.interval_ms.
     * Fehler -> ALTE Werte behalten, failures/failed zaehlen, Backoff
     * RNLAB_WX_RETRY_FIRST_MS, dann jeweils verdoppeln bis
     * RNLAB_WX_RETRY_MAX_MS; naechster Abruf nach dem Backoff.
     * last_error / last_http_status in jedem Fall setzen. */
    (void)now_ms;
    (void)result;
    wx->fetching = false;
}

void rnlab_wx_request_refresh(rnlab_wx_t* wx, uint32_t now_ms) {
    wx->next_fetch_ms = now_ms;
    wx->failures = 0u;
    wx->backoff_ms = 0u;
}

uint32_t rnlab_wx_age_s(const rnlab_wx_t* wx, uint32_t now_ms) {
    return wx->have_data ? (now_ms - wx->data_ms) / 1000u : 0u;
}

rnlab_wx_status_t rnlab_wx_status(const rnlab_wx_t* wx, uint32_t now_ms) {
    if(!wx->link_up) return RNLAB_WX_NO_LINK;
    if(!wx->has_ip) return RNLAB_WX_NO_IP;
    if(wx->fetching) return RNLAB_WX_FETCHING;
    if(wx->failures > 0u) return RNLAB_WX_ERROR;
    if(!wx->have_data) return RNLAB_WX_WAITING;
    if(now_ms - wx->data_ms > 2u * wx->config.interval_ms) return RNLAB_WX_STALE;
    return RNLAB_WX_OK;
}

/* ------------------------------------------------------------------------- */
/* view model                                                                */
/* ------------------------------------------------------------------------- */

void rnlab_wx_view(const rnlab_wx_t* wx, uint32_t now_ms, rnlab_wx_view_t* out) {
    /* TODO(L11): Werte formatieren (rnlab_fmt_milli: Temperatur und Wind
     * 1 Nachkommastelle, Feuchte 0; ohne Daten "--"), Wetterlage und Icon
     * aus dem Code, Statuszeile + level je rnlab_wx_status() - Texte und
     * Faelle siehe tests/unit/test_rnlab_l11.c. Die Statuszeile darf sich
     * bei Daten nur einmal pro Minute aendern ("vor 3 min"). */
    (void)wx;
    (void)now_ms;
    memset(out, 0, sizeof(*out));
    cads_str_copy(out->temperature, sizeof(out->temperature), "--");
    cads_str_copy(out->humidity, sizeof(out->humidity), "--");
    cads_str_copy(out->wind, sizeof(out->wind), "--");
    cads_str_copy(out->status, sizeof(out->status), "TODO(L11)");
    out->level = RNLAB_WX_LEVEL_ERROR;
}

uint8_t rnlab_wx_view_diff(const rnlab_wx_view_t* before, const rnlab_wx_view_t* after) {
    /* TODO(L11): je geaendertem Feld ein RNLAB_WX_DIRTY_*-Bit. */
    (void)before;
    (void)after;
    return 0u;
}

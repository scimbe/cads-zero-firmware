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

typedef struct {
    uint8_t code;
    rnlab_wx_icon_t icon;
    const char* text;
} rnlab_wx_wmo_t;

static const rnlab_wx_wmo_t rnlab_wx_wmo_table[] = {
    {0, RNLAB_WX_ICON_CLEAR, "Klar"},
    {1, RNLAB_WX_ICON_PARTLY, "Ueberwiegend klar"},
    {2, RNLAB_WX_ICON_PARTLY, "Teilweise bewoelkt"},
    {3, RNLAB_WX_ICON_CLOUDY, "Bedeckt"},
    {45, RNLAB_WX_ICON_FOG, "Nebel"},
    {48, RNLAB_WX_ICON_FOG, "Reifnebel"},
    {51, RNLAB_WX_ICON_DRIZZLE, "Leichter Niesel"},
    {53, RNLAB_WX_ICON_DRIZZLE, "Niesel"},
    {55, RNLAB_WX_ICON_DRIZZLE, "Starker Niesel"},
    {56, RNLAB_WX_ICON_DRIZZLE, "Gefrierender Niesel"},
    {57, RNLAB_WX_ICON_DRIZZLE, "Gefrierender Niesel"},
    {61, RNLAB_WX_ICON_RAIN, "Leichter Regen"},
    {63, RNLAB_WX_ICON_RAIN, "Regen"},
    {65, RNLAB_WX_ICON_RAIN, "Starker Regen"},
    {66, RNLAB_WX_ICON_RAIN, "Gefrierender Regen"},
    {67, RNLAB_WX_ICON_RAIN, "Gefrierender Regen"},
    {71, RNLAB_WX_ICON_SNOW, "Leichter Schneefall"},
    {73, RNLAB_WX_ICON_SNOW, "Schneefall"},
    {75, RNLAB_WX_ICON_SNOW, "Starker Schneefall"},
    {77, RNLAB_WX_ICON_SNOW, "Schneegriesel"},
    {80, RNLAB_WX_ICON_RAIN, "Leichte Regenschauer"},
    {81, RNLAB_WX_ICON_RAIN, "Regenschauer"},
    {82, RNLAB_WX_ICON_RAIN, "Heftige Regenschauer"},
    {85, RNLAB_WX_ICON_SNOW, "Schneeschauer"},
    {86, RNLAB_WX_ICON_SNOW, "Starke Schneeschauer"},
    {95, RNLAB_WX_ICON_THUNDER, "Gewitter"},
    {96, RNLAB_WX_ICON_THUNDER, "Gewitter mit Hagel"},
    {99, RNLAB_WX_ICON_THUNDER, "Gewitter mit Hagel"},
};

static const rnlab_wx_wmo_t* rnlab_wx_wmo_find(int32_t code) {
    for(size_t i = 0; i < sizeof(rnlab_wx_wmo_table) / sizeof(rnlab_wx_wmo_table[0]); i++) {
        if((int32_t)rnlab_wx_wmo_table[i].code == code) return &rnlab_wx_wmo_table[i];
    }
    return NULL;
}

const char* rnlab_wx_wmo_text(int32_t code) {
    const rnlab_wx_wmo_t* e = rnlab_wx_wmo_find(code);
    return e != NULL ? e->text : "Unbekannt";
}

rnlab_wx_icon_t rnlab_wx_wmo_icon(int32_t code) {
    const rnlab_wx_wmo_t* e = rnlab_wx_wmo_find(code);
    return e != NULL ? e->icon : RNLAB_WX_ICON_UNKNOWN;
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

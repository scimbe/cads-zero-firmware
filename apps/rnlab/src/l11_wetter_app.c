/*
 * CaDS Zero - rnlab L11 (Wetter-App): board integration.
 *
 * `lab 11 <cmd> [args]` lands in rnlab_l11_command() (see
 * rnlab/rnlab_lesson.h). The app itself is apps/wetter (a view in the apps
 * menu); this is its console: the same controller instance
 * (rnlab_l11_app()), so a server set here is the one the display fetches
 * from, and the redraw numbers the app measured can be read out over the
 * network while the panel shows the result.
 */

#include "rnlab/rnlab_lesson.h"

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "l11_wetter_app_logic.h"

static void l11_write_status(cads_cli_session_t* s) {
    rnlab_wx_t* wx = rnlab_l11_app();
    uint32_t now = cads_hal_ticks_ms();
    rnlab_wx_view_t v;
    rnlab_wx_view(wx, now, &v);

    cads_cli_write(s, "L11 ");
    cads_cli_write(s, v.status);
    if(!wx->view_open) cads_cli_write(s, " (App nicht offen: Abruf pausiert)");
    cads_cli_write(s, "\r\nWerte: ");
    cads_cli_write(s, v.temperature);
    cads_cli_write(s, " C | ");
    cads_cli_write(s, v.humidity);
    cads_cli_write(s, " % | ");
    cads_cli_write(s, v.wind);
    cads_cli_write(s, " km/h | ");
    cads_cli_write(s, v.condition[0] != '\0' ? v.condition : "-");
    if(wx->have_data) {
        cads_cli_write(s, " (Code ");
        cads_cli_write_uint(s, (uint32_t)wx->data.code);
        cads_cli_write(s, ", Alter ");
        cads_cli_write_uint(s, rnlab_wx_age_s(wx, now));
        cads_cli_write(s, " s)");
    }
    cads_cli_write(s, "\r\nQuelle ");
    cads_cli_write(s, wx->config.host);
    cads_cli_write(s, ":");
    cads_cli_write_uint(s, wx->config.port);
    cads_cli_write(s, ", Intervall ");
    cads_cli_write_uint(s, wx->config.interval_ms / 1000u);
    cads_cli_write(s, " s, naechster Abruf in ");
    int32_t due = (int32_t)(wx->next_fetch_ms - now);
    cads_cli_write_uint(s, due > 0 ? (uint32_t)due / 1000u : 0u);
    cads_cli_write(s, " s\r\nAbrufe ");
    cads_cli_write_uint(s, wx->fetches);
    cads_cli_write(s, ", davon fehlgeschlagen ");
    cads_cli_write_uint(s, wx->failed);
    cads_cli_write(s, ", Fehler in Folge ");
    cads_cli_write_uint(s, wx->failures);
    cads_cli_write(s, ", Backoff ");
    cads_cli_write_uint(s, wx->backoff_ms / 1000u);
    cads_cli_write(s, " s\r\nRedraws ");
    cads_cli_write_uint(s, wx->redraws);
    cads_cli_write(s, ", letzter ");
    cads_cli_write_uint(s, wx->last_redraw_px);
    cads_cli_write(s, " px in ");
    cads_cli_write_uint(s, wx->last_redraw_us);
    cads_cli_write(s, " us, max ");
    cads_cli_write_uint(s, wx->max_redraw_us);
    cads_cli_write(s, " us\r\n");
}

static void l11_cmd_server(cads_cli_session_t* s, int argc, char* argv[]) {
    rnlab_wx_t* wx = rnlab_l11_app();
    rnlab_http_target_t t;
    if(argc < 2) {
        cads_str_copy(wx->config.host, sizeof(wx->config.host), RNLAB_L10_DEFAULT_HOST);
        wx->config.port = RNLAB_L10_DEFAULT_PORT;
    } else if(rnlab_http_parse_target(argv[1], &t)) {
        cads_str_copy(wx->config.host, sizeof(wx->config.host), t.host);
        wx->config.port = t.port;
    } else {
        cads_cli_write(s, "? L11: host[:port] ungueltig\r\n");
        return;
    }
    rnlab_wx_request_refresh(wx, cads_hal_ticks_ms());
    cads_cli_write(s, "L11: Quelle ");
    cads_cli_write(s, wx->config.host);
    cads_cli_write(s, ":");
    cads_cli_write_uint(s, wx->config.port);
    cads_cli_write(s, ", Abruf angefordert\r\n");
}

static void l11_cmd_interval(cads_cli_session_t* s, int argc, char* argv[]) {
    uint32_t sec = 0u;
    const char* end = NULL;
    if(argc < 2 || !cads_str_to_uint(argv[1], &sec, &end) || *end != '\0' ||
       sec < RNLAB_WX_INTERVAL_MIN_MS / 1000u || sec > 24u * 3600u) {
        cads_cli_write(s, "? L11: lab 11 interval <10..86400 s>\r\n");
        return;
    }
    rnlab_wx_t* wx = rnlab_l11_app();
    wx->config.interval_ms = sec * 1000u;
    if(wx->have_data && wx->failures == 0u) wx->next_fetch_ms = wx->data_ms + wx->config.interval_ms;
    cads_cli_write(s, "L11: Intervall ");
    cads_cli_write_uint(s, sec);
    cads_cli_write(s, " s\r\n");
}

void rnlab_l11_command(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc == 0 || cads_str_equal(argv[0], "help")) {
        cads_cli_write(session,
            "lab 11 status                 Zustand, Werte, Abrufe, Redraw-Messung der App\r\n"
            "lab 11 server [host[:port]]   Quelle setzen (ohne Argument: open-meteo)\r\n"
            "lab 11 interval <s>           Abrufintervall (10..86400 s)\r\n"
            "lab 11 refresh                sofort abrufen (wie OK in der App)\r\n"
            "Die App (Menue -> Wetter) ruft nur ab, solange sie angezeigt wird.\r\n");
    } else if(cads_str_equal(argv[0], "status")) {
        l11_write_status(session);
    } else if(cads_str_equal(argv[0], "server")) {
        l11_cmd_server(session, argc, argv);
    } else if(cads_str_equal(argv[0], "interval")) {
        l11_cmd_interval(session, argc, argv);
    } else if(cads_str_equal(argv[0], "refresh")) {
        rnlab_wx_request_refresh(rnlab_l11_app(), cads_hal_ticks_ms());
        cads_cli_write(session, "L11: Abruf angefordert\r\n");
    } else {
        cads_cli_write(session, "? L11: unbekannt - lab 11 help\r\n");
    }
}

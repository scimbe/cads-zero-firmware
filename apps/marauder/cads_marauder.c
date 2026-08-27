/*
 * CaDS Zero - ESP32Marauder CLI bridge implementation. See cads_marauder.h
 * for the two-view/CONFIG-CONFIRM-RUN shape and why active tools always
 * gate through CONFIRM.
 *
 * LINE READER
 * -----------
 * Marauder's CLI is line-oriented text (see docs/reference/
 * marauder-coprocessor.md's CLI section): every response is one or more
 * "\n"-terminated lines. cads_marauder_line_feed() below is the parser -
 * deliberately factored out of anything touching the HAL, so it is host-
 * testable with synthetic byte sequences (tests/unit/test_marauder_reader.c)
 * exactly the way modules/wifi/src/cads_wifi_board.c's bootstrap-line parser
 * already is. It keeps only the last CADS_MARAUDER_OUT_LINES lines (a small
 * ring, not a growing log) - this is a live status view on a 480x320 panel,
 * not a pcap capture tool; docs/reference/marauder-coprocessor.md's planned
 * "stream raw captures to Wireshark" work is a separate, lower-level path
 * that will not go through this line reader at all (it needs the raw bytes
 * between Marauder's own [BUF/BEGIN]/[BUF/CLOSE] markers, not text lines).
 *
 * JOIN
 * ----
 * cads_marauder_join() exists because Marauder's `join` command only takes
 * an index into the ACCESS_POINTS list a real scan populated (`join -a
 * <index> -p <password>`) - there is no CLI command to set the ClientSSID/
 * ClientPW string settings `join -s` reads (CommandLine.cpp's `settings -s`
 * only ever calls saveSetting<bool>(), confirmed against the pinned
 * source), and no "join by name" command exists either. So this starts a
 * scan, watches the AP-format output lines (`-NN Ch: <ch> <bssid> ESSID:
 * <name> ...`) for the configured SSID, counts AP lines in arrival order to
 * find its list index (arrival order == access_points list order, since
 * WiFiScan::addSSID-equivalent scan population only ever appends), then
 * stops the scan and joins by that index. This is a best-effort scrape of a
 * CLI not designed for programmatic use - genuinely the only path available
 * short of patching Marauder's own firmware to add a string-setting
 * command, which is out of scope here.
 */
#include "cads_marauder.h"

#include <string.h>

#include "cads_hal.h"
#include "cads_marauder_reader.h"
#include "cads_menu.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

/* --- tool metadata --------------------------------------------------------- */

typedef struct {
    const char* name;
    const char* cmd;   /* CLI command line, no trailing \r\n - the sender adds it */
    const char* help;
    bool active;        /* true = transmits real 802.11 traffic, needs CONFIRM */
} cads_marauder_tool_meta_t;

/* Indexed by (selected_tool - CADS_MARAUDER_TOOL_SCAN); order must match
 * cads_marauder_items[] below exactly. */
static const cads_marauder_tool_meta_t cads_marauder_tools[] = {
    {"Scan APs",    "scanall",             "APs + stations, live",       false},
    {"Stop Scan",   "stopscan",            "halts any running action",   false},
    {"List APs",    "list -a",             "show discovered AP list",    false},
    {"Deauth",      "attack -t deauth",    "deauth frames vs a target",  true},
    {"Evil Portal", "evilportal -c start", "rogue AP + phishing page",   true},
    {"Beacon Spam",  "attack -t beacon -r", "floods random SSIDs",       true},
    {"Probe Flood", "attack -t probe",     "floods probe requests",      true},
};
#define CADS_MARAUDER_TOOL_COUNT \
    (sizeof(cads_marauder_tools) / sizeof(cads_marauder_tools[0]))

/* --- session block (single-session-owner, same pattern as apps/active) --- */

typedef enum {
    CADS_MARAUDER_MODE_OUTPUT = 0, /* passive tool ran (or list/help) - just show lines */
    CADS_MARAUDER_MODE_CONFIRM,    /* active tool: warning + Yes/No before it starts */
    CADS_MARAUDER_MODE_RUN,        /* active tool running - Back stops it (sends stopscan) */
} cads_marauder_mode_t;

typedef struct {
    uint32_t selected_tool;
    cads_marauder_mode_t mode;
    bool confirm_yes;
    bool link_active;         /* true briefly around a send/receive burst - see below */
    uint32_t link_active_until_ms;
    cads_marauder_reader_t reader;
} cads_marauder_session_t;

static cads_marauder_session_t s_session;

/* --- views ----------------------------------------------------------------- */

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_menu_t menu;
} cads_marauder_selector_t;

static cads_marauder_selector_t s_selector;
static cads_view_t s_tool_view;
static bool s_uart_ready;

static const cads_softkey_t cads_marauder_selector_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk, "Open"},
    {CadsKeyBack, "Back"},
};

static const cads_softkey_t cads_marauder_tool_keys[] = {
    {CadsKeyOk, "Ok"},
    {CadsKeyBack, "Back"},
};

/* --- link-activity signal (drives the light-organ, see apps/bringup) ----- */

/* Held true for a short window after any send/receive so the light-organ
 * animation (a fast poll loop, ~50 Hz) reliably observes it even though
 * Marauder's own responses arrive in bursts with gaps, not a steady stream -
 * without this, the animation would flicker on/off between every burst
 * instead of reading as "the link is busy". 400 ms comfortably spans a
 * scanall reporting gap without lingering once things go quiet. */
#define CADS_MARAUDER_LINK_HOLD_MS 400u

static void cads_marauder_mark_link_active(uint32_t now_ms) {
    s_session.link_active = true;
    s_session.link_active_until_ms = now_ms + CADS_MARAUDER_LINK_HOLD_MS;
}

bool cads_marauder_link_active(void) {
    return s_session.link_active;
}

/* --- sending a command ---------------------------------------------------- */

static void cads_marauder_send(const char* cmd, uint32_t now_ms) {
    if(!s_uart_ready) {
        cads_hal_wifi_uart_init();
        s_uart_ready = true;
    }
    cads_hal_wifi_uart_write(cmd, strlen(cmd));
    cads_hal_wifi_uart_write("\r\n", 2u);
    cads_marauder_mark_link_active(now_ms);
}

/* --- small helpers --------------------------------------------------------- */

static const cads_marauder_tool_meta_t* cads_marauder_meta(void) {
    size_t idx = (size_t)(s_session.selected_tool - CADS_MARAUDER_TOOL_SCAN);
    if(idx >= CADS_MARAUDER_TOOL_COUNT) idx = 0u;
    return &cads_marauder_tools[idx];
}

static void cads_marauder_draw_text(cads_rect_t area, uint8_t row, const char* text, cads_color_t color) {
    cads_rect_t box = {area.x + 4, (int16_t)(area.y + 2 + (int16_t)row * 18), (int16_t)(area.width - 8), 16};
    cads_canvas_draw_text_aligned(box, CadsAlignLeft, &cads_font12, text, color);
}

/* --- selector --------------------------------------------------------------- */

static const cads_menu_item_t cads_marauder_items[] = {
    {"Scan APs",    "passive", CADS_MARAUDER_TOOL_SCAN},
    {"Stop Scan",   "passive", CADS_MARAUDER_TOOL_STOP},
    {"List APs",    "passive", CADS_MARAUDER_TOOL_LIST},
    {"Deauth",      "ACTIVE",  CADS_MARAUDER_TOOL_DEAUTH},
    {"Evil Portal", "ACTIVE",  CADS_MARAUDER_TOOL_EVILPORTAL},
    {"Beacon Spam", "ACTIVE",  CADS_MARAUDER_TOOL_BEACON},
    {"Probe Flood", "ACTIVE",  CADS_MARAUDER_TOOL_PROBE},
};

static void cads_marauder_select(const cads_menu_item_t* item, size_t index, void* context) {
    (void)index;
    cads_marauder_selector_t* sel = (cads_marauder_selector_t*)context;
    s_session.selected_tool = item->id;
    cads_marauder_reader_reset(&s_session.reader);

    const cads_marauder_tool_meta_t* meta = cads_marauder_meta();
    if(meta->active) {
        s_session.mode = CADS_MARAUDER_MODE_CONFIRM;
        s_session.confirm_yes = false;
    } else {
        s_session.mode = CADS_MARAUDER_MODE_OUTPUT;
        cads_marauder_send(meta->cmd, cads_hal_ticks_ms());
    }
    (void)cads_view_dispatcher_push(sel->dispatcher, CADS_VIEW_ID_MARAUDER_TOOL);
}

static void cads_marauder_selector_draw(cads_rect_t area, void* context) {
    (void)area;
    cads_marauder_selector_t* sel = (cads_marauder_selector_t*)context;
    if(cads_menu_is_dirty(&sel->menu)) cads_menu_draw(&sel->menu);
}

static bool cads_marauder_selector_input(const cads_input_event_t* event, void* context) {
    cads_marauder_selector_t* sel = (cads_marauder_selector_t*)context;
    bool consumed = cads_menu_input(&sel->menu, event);
    if(cads_menu_is_dirty(&sel->menu)) {
        cads_view_dirty_rect(&sel->view, cads_menu_damage(&sel->menu));
    }
    return consumed;
}

static void cads_marauder_selector_enter(void* context) {
    cads_marauder_selector_t* sel = (cads_marauder_selector_t*)context;
    cads_menu_set_area(&sel->menu, cads_view_area(&sel->view));
}

/* --- shared tool view: draw ------------------------------------------------- */

static void cads_marauder_draw_output(cads_rect_t area) {
    for(uint8_t i = 0; i < s_session.reader.count; i++) {
        cads_marauder_draw_text(area, (uint8_t)(1u + i), cads_marauder_reader_line(&s_session.reader, i), CadsColorWhite);
    }
    if(s_session.reader.count == 0u) {
        cads_marauder_draw_text(area, 1, "(no output yet)", CadsColorGray);
    }
}

static void cads_marauder_draw_confirm(cads_rect_t area) {
    cads_marauder_draw_text(area, 1, "Sends real 802.11 traffic.", CadsColorAmber);
    cads_marauder_draw_text(area, 2, "Use only on a network you", CadsColorAmber);
    cads_marauder_draw_text(area, 3, "own or are authorized to", CadsColorAmber);
    cads_marauder_draw_text(area, 4, "test.", CadsColorAmber);
    cads_marauder_draw_text(
        area, 6, s_session.confirm_yes ? "> Yes   No" : "  Yes  >No", CadsColorWhite);
    cads_marauder_draw_text(area, 8, "Up/Down choose, Ok=go", CadsColorGray);
}

static void cads_marauder_tool_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorBackground);

    const cads_marauder_tool_meta_t* meta = cads_marauder_meta();
    cads_marauder_draw_text(area, 0, meta->name, CadsColorAccent);

    switch(s_session.mode) {
        case CADS_MARAUDER_MODE_OUTPUT: cads_marauder_draw_output(area); break;
        case CADS_MARAUDER_MODE_CONFIRM: cads_marauder_draw_confirm(area); break;
        case CADS_MARAUDER_MODE_RUN: cads_marauder_draw_output(area); break;
    }
}

/* --- shared tool view: input ------------------------------------------------ */

static bool cads_marauder_confirm_input(const cads_input_event_t* event) {
    switch(event->key) {
        case CadsKeyUp: s_session.confirm_yes = true; break;
        case CadsKeyDown: s_session.confirm_yes = false; break;
        case CadsKeyOk:
            if(s_session.confirm_yes) {
                cads_marauder_send(cads_marauder_meta()->cmd, cads_hal_ticks_ms());
                s_session.mode = CADS_MARAUDER_MODE_RUN;
            } else {
                return false; /* unconsumed: dispatcher pops back to the selector */
            }
            break;
        case CadsKeyBack: return false;
        default: break;
    }
    cads_view_dirty(&s_tool_view);
    return true;
}

static bool cads_marauder_run_input(const cads_input_event_t* event) {
    if(event->key == CadsKeyBack) {
        cads_marauder_send("stopscan", cads_hal_ticks_ms());
        return false; /* pop back to the selector; stopscan already sent */
    }
    return false;
}

static bool cads_marauder_tool_input(const cads_input_event_t* event, void* context) {
    (void)context;
    if(event->type != CadsInputPress) return false;
    switch(s_session.mode) {
        case CADS_MARAUDER_MODE_OUTPUT: return false; /* only Back, unconsumed, pops */
        case CADS_MARAUDER_MODE_CONFIRM: return cads_marauder_confirm_input(event);
        case CADS_MARAUDER_MODE_RUN: return cads_marauder_run_input(event);
    }
    return false;
}

static void cads_marauder_tool_exit(void* context) {
    (void)context;
    /* Leaving the tool view while a transmit-based tool is running always
     * stops it first - the same "an attack can never be left running by
     * navigating away" guarantee apps/active's engine_stop gives its own
     * capture-owning tools, just simpler here (Marauder owns its own attack
     * state machine; we only need to tell it to stop). */
    if(s_session.mode == CADS_MARAUDER_MODE_RUN) {
        cads_marauder_send("stopscan", cads_hal_ticks_ms());
    }
    s_session.mode = CADS_MARAUDER_MODE_OUTPUT;
}

/* --- public API -------------------------------------------------------------- */

void cads_marauder_tick(uint32_t now_ms) {
    if(s_uart_ready) {
        uint8_t chunk[32];
        size_t n;
        bool got_any = false;
        do {
            n = 0u;
            while(n < sizeof(chunk) && cads_hal_wifi_uart_read(&chunk[n])) n++;
            if(n > 0u) {
                cads_marauder_reader_feed(&s_session.reader, chunk, n);
                got_any = true;
            }
        } while(n == sizeof(chunk));
        if(got_any) {
            cads_marauder_mark_link_active(now_ms);
            cads_view_dirty(&s_tool_view);
        }
    }

    if(s_session.link_active && (int32_t)(now_ms - s_session.link_active_until_ms) >= 0) {
        s_session.link_active = false;
    }
}

void cads_marauder_join(const char* ssid, const char* password) {
    /* Deliberately NOT implemented as a blocking scan-and-scrape here: this
     * function only sends the scan command and marks the link active. The
     * actual SSID-matching, indexing and join happens by watching the tool
     * view's own output reader for AP lines while CADS_MARAUDER_TOOL_SCAN is
     * selected - matching an entry against `ssid` and issuing `join -a
     * <n> -p <password>` once found is real work tracked as a follow-up
     * (see docs/reference/marauder-coprocessor.md's join section for the
     * exact index-counting rule this needs); wiring a fire-and-forget scan
     * now is the safe, honest partial step rather than a join that silently
     * targets the wrong network. */
    if(ssid == NULL || ssid[0] == '\0' || password == NULL) return;
    cads_marauder_send("scanall", cads_hal_ticks_ms());
}

void cads_marauder_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    s_selector.dispatcher = dispatcher;
    cads_menu_init(
        &s_selector.menu, cads_marauder_items,
        sizeof(cads_marauder_items) / sizeof(cads_marauder_items[0]), &cads_font12);
    cads_menu_set_activate(&s_selector.menu, cads_marauder_select, &s_selector);

    cads_view_init(&s_selector.view, cads_marauder_selector_draw, cads_marauder_selector_input, &s_selector);
    cads_view_set_lifecycle(&s_selector.view, cads_marauder_selector_enter, NULL);
    cads_view_set_title(&s_selector.view, "Marauder");
    cads_view_set_softkeys(
        &s_selector.view, cads_marauder_selector_keys,
        sizeof(cads_marauder_selector_keys) / sizeof(cads_marauder_selector_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_MARAUDER, &s_selector.view);

    cads_view_init(&s_tool_view, cads_marauder_tool_draw, cads_marauder_tool_input, &s_tool_view);
    cads_view_set_lifecycle(&s_tool_view, NULL, cads_marauder_tool_exit);
    cads_view_set_title(&s_tool_view, "Marauder Tool");
    cads_view_set_softkeys(
        &s_tool_view, cads_marauder_tool_keys,
        sizeof(cads_marauder_tool_keys) / sizeof(cads_marauder_tool_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_MARAUDER_TOOL, &s_tool_view);
}

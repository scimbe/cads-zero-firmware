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
 * not a pcap capture tool.
 *
 * PCAP RELAY
 * ----------
 * The "Sniff (PCAP)" tool's raw-byte relay to Wireshark (see
 * cads_marauder_pcap.h) sits IN FRONT of the line reader, not beside it:
 * every byte off the UART goes to cads_marauder_pcap_feed() first, which
 * demuxes Marauder's own [BUF/BEGIN]/[BUF/CLOSE]-framed binary bursts
 * (`sniffraw -serial`) from ordinary CLI text and hands only the latter to
 * the line reader via its passthrough callback. For any tool other than
 * Sniff (PCAP) - Scan, List, join's background scan, an attack's own
 * output - there are never any real bursts on the wire, so the demux is
 * provably transparent (see test_marauder_pcap.c's own passthrough tests)
 * and costs nothing behaviourally; it runs unconditionally rather than
 * being switched in/out per tool to avoid a whole class of "which mode is
 * the UART in" bugs.
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

#include "cads/config/config.h"
#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "cads_marauder_join.h"
#include "cads_marauder_pcap.h"
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
    const char* split_marker; /* NULL for most tools - see cads_marauder_reader.h's
                                * own note; only Marauder output that runs multiple
                                * records together with no real newline needs this */
} cads_marauder_tool_meta_t;

/* Indexed by (selected_tool - CADS_MARAUDER_TOOL_SCAN); order must match
 * cads_marauder_items[] below exactly. */
static const cads_marauder_tool_meta_t cads_marauder_tools[] = {
    {"Scan APs",     "scanall",             "APs + stations, live",       false, NULL},
    {"Stop Scan",    "stopscan",            "halts any running action",   false, NULL},
    {"List APs",     "list -a",             "show discovered AP list",    false, NULL},
    {"Deauth",       "attack -t deauth",    "deauth frames vs a target",  true,  NULL},
    {"Evil Portal",  "evilportal -c start", "rogue AP + phishing page",   true,  NULL},
    {"Beacon Spam",  "attack -t beacon -r", "floods random SSIDs",        true,  NULL},
    {"Probe Flood",  "attack -t probe",     "floods probe requests",      true,  NULL},
    {"Sniff (PCAP)", "sniffraw -serial",    "live relay to Wireshark",    false, NULL},
    {"Sniff BT",     "sniffbt",             "nearby BLE devices, live",   false, "Device: "},
    {"BLE Spam",     "blespam -t all",      "floods fake BLE devices",    true,  NULL},
    {"Sniff PMKID",  "sniffpmkid",          "WPA2 handshake, passive",    false, NULL},
    {"Sniff SAE",    "sniffsae",            "WPA3 handshake, passive",    false, NULL},
    {"Clear APs",    "clearlist -a",        "wipes the discovered list",  false, NULL},
    /* cmd is unused for this one - CADS_MARAUDER_MODE_SELECT builds
     * "select -a <N>" itself from s_session.select_target_index rather than
     * sending a fixed string on entry, the way every other tool does. */
    {"Select Target", NULL,                 "pick index for Deauth/etc.", false, NULL},
};
#define CADS_MARAUDER_TOOL_COUNT \
    (sizeof(cads_marauder_tools) / sizeof(cads_marauder_tools[0]))

/* --- session block (single-session-owner, same pattern as apps/active) --- */

typedef enum {
    CADS_MARAUDER_MODE_OUTPUT = 0, /* passive tool ran (or list/help) - just show lines */
    CADS_MARAUDER_MODE_CONFIRM,    /* active tool: warning + Yes/No before it starts */
    CADS_MARAUDER_MODE_RUN,        /* active tool running - Back stops it (sends stopscan) */
    CADS_MARAUDER_MODE_SELECT,     /* Select Target: numeric field, Up/Down/Ok - see cads_marauder.h */
} cads_marauder_mode_t;

typedef struct {
    uint32_t selected_tool;
    cads_marauder_mode_t mode;
    bool confirm_yes;
    bool armed; /* config's active.armed, read fresh on every CONFIRM entry -
                 * see that field's own doc comment in cads/config/config.h.
                 * Deliberately NOT cached across tool switches: a config
                 * reload from Settings must take effect on the very next
                 * CONFIRM, not require leaving and re-entering Marauder. */
    bool link_active;         /* true briefly around a send/receive burst - see below */
    uint32_t link_active_until_ms;
    cads_marauder_reader_t reader;
    uint32_t select_target_index; /* CADS_MARAUDER_MODE_SELECT's own state - deliberately NOT
                                    * reset alongside the reader on every tool switch, so dialing
                                    * in an index once survives navigating away and back */
} cads_marauder_session_t;

static cads_marauder_session_t s_session;

/* --- background SSID join (independent of whichever tool view is open,
 * see cads_marauder_join() below and cads_marauder_join.h for the protocol
 * reasoning) --------------------------------------------------------------- */

#define CADS_MARAUDER_JOIN_SCAN_TIMEOUT_MS 15000u /* generous: real scans take several seconds */

static cads_marauder_join_state_t s_join;
static char s_join_password[64]; /* CADS_CONFIG_PASS_MAX */
static bool s_join_active;
static uint32_t s_join_deadline_ms;

/* --- views ----------------------------------------------------------------- */

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_menu_t menu;
} cads_marauder_selector_t;

static cads_marauder_selector_t s_selector;
static cads_view_t s_tool_view;
static bool s_uart_ready;

/* --- PCAP relay (Sniff (PCAP) tool) - see cads_marauder_pcap.h and this
 * file's own header comment on why the demux runs unconditionally. ------- */

static cads_marauder_pcap_t s_pcap;
static uint32_t s_pcap_target_ip;   /* host order, 0 = relay stays silent */
static uint32_t s_pcap_frame_count; /* frames decoded this session, shown in the tool view */

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

/* --- background SSID join -------------------------------------------------- */

/* cads_marauder_reader_t's line_cb signature - forwards every completed
 * output line to the join state machine while a join search is running.
 * Registered on the SAME reader the tool view's own display uses (there is
 * only one UART, one reader); see cads_marauder_select()'s own note on why
 * it must be re-armed after a tool switch resets that reader. */
static void cads_marauder_join_line_cb(void* ctx, const char* line) {
    (void)ctx;
    cads_marauder_join_feed_line(&s_join, line);
}

/* Arms (or re-arms, after a reader reset) the join line observer - a no-op
 * when no join is in progress. */
static void cads_marauder_join_arm_reader(void) {
    if(s_join_active) {
        cads_marauder_reader_set_line_cb(&s_session.reader, cads_marauder_join_line_cb, NULL);
    }
}

/* Checked from cads_marauder_tick() every tick while a join is in progress:
 * once the state machine reports FOUND or TIMED_OUT, send the follow-up
 * command(s) and end the background join. */
static void cads_marauder_join_service(uint32_t now_ms) {
    if(!s_join_active) return;

    cads_marauder_join_check_timeout(&s_join, now_ms, s_join_deadline_ms);

    if(s_join.status == CADS_MARAUDER_JOIN_FOUND) {
        cads_marauder_send("stopscan", now_ms);

        /* Sized for the longest possible command: a 10-digit index and a
         * full 63-char WPA passphrase. A 64-byte buffer silently cut
         * passphrases longer than ~49 chars, and the join then failed with
         * the wrong key and no error. */
        char num[12];
        char cmd[sizeof("join -a ") + sizeof(num) + sizeof(" -p ") + sizeof(s_join_password)];
        cads_str_copy(cmd, sizeof(cmd), "join -a ");
        cads_fmt_uint(num, sizeof(num), s_join.found_index);
        cads_str_append(cmd, sizeof(cmd), num);
        cads_str_append(cmd, sizeof(cmd), " -p ");
        cads_str_append(cmd, sizeof(cmd), s_join_password);
        cads_marauder_send(cmd, now_ms);

        s_join_active = false;
        cads_marauder_reader_set_line_cb(&s_session.reader, NULL, NULL);
    } else if(s_join.status == CADS_MARAUDER_JOIN_TIMED_OUT) {
        /* The configured SSID never showed up in range within the scan
         * window - stop the scan and give up quietly. Whoever triggered
         * the join (Settings) has no separate error channel today; the
         * "no line ever matched" outcome is visible in the tool view's own
         * output if Scan APs happens to be open, same as any other scan. */
        cads_marauder_send("stopscan", now_ms);
        s_join_active = false;
        cads_marauder_reader_set_line_cb(&s_session.reader, NULL, NULL);
    }
}

/* --- PCAP relay callbacks --------------------------------------------------
 *
 * Both are driven synchronously from inside cads_marauder_pcap_feed(),
 * called from cads_marauder_tick()'s own read loop - so both run on the
 * console task, same as everything else that touches s_session.reader or
 * the WiFi UART (see s_wifi_join_requested's comment in
 * apps/settings/cads_settings.c for why that single-task discipline
 * matters on this link). */

static void cads_marauder_pcap_passthrough(void* ctx, const uint8_t* data, uint8_t len) {
    (void)ctx;
    cads_marauder_reader_feed(&s_session.reader, data, len);
}

static void cads_marauder_pcap_frame(void* ctx, const uint8_t* frame, uint16_t frame_len, uint32_t orig_len) {
    (void)ctx;
    (void)orig_len;
    s_pcap_frame_count++;
    if(s_pcap_target_ip == 0u) return;

    uint8_t datagram[CADS_MARAUDER_TZSP_HDR_LEN + CADS_MARAUDER_PCAP_FRAME_MAX];
    size_t n = cads_marauder_tzsp_build(datagram, sizeof(datagram), frame, frame_len);
    if(n > 0u) {
        cads_net_udp_send(s_pcap_target_ip, CADS_MARAUDER_PCAP_UDP_PORT, datagram, (uint16_t)n);
    }
}

void cads_marauder_set_pcap_target(uint32_t ip_host) {
    s_pcap_target_ip = ip_host;
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

/* WiFi and Bluetooth tools together, always all shown - see
 * cads_marauder.h's own "BLUETOOTH TOOLS ARE ALWAYS IN THE MENU" note for
 * why this used to be conditional and isn't any more. */
static const cads_menu_item_t cads_marauder_items[] = {
    {"Scan APs",    "passive", CADS_MARAUDER_TOOL_SCAN},
    {"Stop Scan",   "passive", CADS_MARAUDER_TOOL_STOP},
    {"List APs",    "passive", CADS_MARAUDER_TOOL_LIST},
    {"Deauth",      "ACTIVE",  CADS_MARAUDER_TOOL_DEAUTH},
    {"Evil Portal", "ACTIVE",  CADS_MARAUDER_TOOL_EVILPORTAL},
    {"Beacon Spam", "ACTIVE",  CADS_MARAUDER_TOOL_BEACON},
    {"Probe Flood", "ACTIVE",  CADS_MARAUDER_TOOL_PROBE},
    {"Sniff (PCAP)", "passive", CADS_MARAUDER_TOOL_PCAP},
    {"Sniff BT",    "passive", CADS_MARAUDER_TOOL_SNIFFBT},
    {"BLE Spam",    "ACTIVE",  CADS_MARAUDER_TOOL_BLESPAM},
    {"Sniff PMKID", "passive", CADS_MARAUDER_TOOL_SNIFFPMKID},
    {"Sniff SAE",   "passive", CADS_MARAUDER_TOOL_SNIFFSAE},
    {"Clear APs",   "passive", CADS_MARAUDER_TOOL_CLEARAPS},
    {"Select Target", "config", CADS_MARAUDER_TOOL_SELECT},
};

static void cads_marauder_select(const cads_menu_item_t* item, size_t index, void* context) {
    (void)index;
    cads_marauder_selector_t* sel = (cads_marauder_selector_t*)context;
    s_session.selected_tool = item->id;
    cads_marauder_reader_reset(&s_session.reader);
    /* Defensive, not required for correctness (see cads_marauder_pcap.h's
     * "self-describing" note - it always resyncs on its own at the next
     * "[BUF/BEGIN]" regardless): avoids any lingering mid-burst state from
     * a Sniff (PCAP) session bleeding past a tool switch. */
    cads_marauder_pcap_resync(&s_pcap);
    /* reader_reset() just cleared the line_cb too - re-arm it so a
     * background join (started from Settings, independent of which tool
     * view is open) keeps seeing every line even though the user just
     * navigated to a different tool. */
    cads_marauder_join_arm_reader();

    const cads_marauder_tool_meta_t* meta = cads_marauder_meta();
    /* reader_reset() also cleared any split_marker from the previous tool -
     * re-set it (or leave it NULL) for whichever tool this is now, per
     * cads_marauder_reader.h's own note on why some Marauder output needs
     * this and most doesn't. */
    cads_marauder_reader_set_split_marker(&s_session.reader, meta->split_marker);
    if(s_session.selected_tool == CADS_MARAUDER_TOOL_SELECT) {
        /* Deliberately does not touch s_session.select_target_index - see
         * that field's own comment on why it survives a tool switch. */
        s_session.mode = CADS_MARAUDER_MODE_SELECT;
    } else if(meta->active) {
        s_session.mode = CADS_MARAUDER_MODE_CONFIRM;
        s_session.confirm_yes = false;
        cads_config_t cfg;
        (void)cads_config_load(&cfg); /* always leaves cfg valid, error or not */
        s_session.armed = cfg.active_armed;
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

    /* Fixes a real Marauder-firmware gotcha (2026-08-28, see
     * docs/reference/marauder-coprocessor.md): a scan left running from
     * anywhere silently swallows every later scan/attack command
     * (`wifi_scan_obj.scanning()` gate in Marauder's own CommandLine.cpp).
     * stopscan's own handler sits outside that gate and always replies, so
     * entering this menu now always leaves the co-processor idle instead of
     * occasionally inheriting a stuck scan from an earlier session. */
    cads_marauder_send("stopscan", cads_hal_ticks_ms());
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

/* Only meaningful for the Sniff (PCAP) tool - shown so the count means
 * something even before Wireshark is pointed at this board (see
 * cads_marauder_set_pcap_target()'s own comment on why 0 still counts
 * frames, it just does not send them). */
static void cads_marauder_draw_pcap_status(cads_rect_t area) {
    char line[48];
    char num[12];
    cads_str_copy(line, sizeof(line), "Relayed: ");
    cads_fmt_uint(num, sizeof(num), s_pcap_frame_count);
    cads_str_append(line, sizeof(line), num);
    if(s_pcap_target_ip != 0u) {
        char ip[16];
        cads_fmt_ipv4(ip, sizeof(ip), s_pcap_target_ip);
        cads_str_append(line, sizeof(line), " -> ");
        cads_str_append(line, sizeof(line), ip);
    } else {
        cads_str_append(line, sizeof(line), " (no target - set wifi.pcap_target)");
    }
    cads_marauder_draw_text(area, 7, line, CadsColorGray);
}

static void cads_marauder_draw_confirm(cads_rect_t area) {
    if(!s_session.armed) {
        cads_marauder_draw_text(area, 2, "BLOCKED: device not armed.", CadsColorRed);
        cads_marauder_draw_text(area, 4, "Set active.armed = 1 in", CadsColorAmber);
        cads_marauder_draw_text(area, 5, "config.txt to enable any", CadsColorAmber);
        cads_marauder_draw_text(area, 6, "active tool on this device.", CadsColorAmber);
        cads_marauder_draw_text(area, 8, "Back to cancel", CadsColorGray);
        return;
    }
    cads_marauder_draw_text(area, 1, "Sends real 802.11 traffic.", CadsColorAmber);
    cads_marauder_draw_text(area, 2, "Use only on a network you", CadsColorAmber);
    cads_marauder_draw_text(area, 3, "own or are authorized to", CadsColorAmber);
    cads_marauder_draw_text(area, 4, "test.", CadsColorAmber);
    cads_marauder_draw_text(
        area, 6, s_session.confirm_yes ? "> Yes   No" : "  Yes  >No", CadsColorWhite);
    cads_marauder_draw_text(area, 8, "Up/Down choose, Ok=go", CadsColorGray);
}

static void cads_marauder_draw_select(cads_rect_t area) {
    char line[32];
    char num[12];
    cads_str_copy(line, sizeof(line), "Index: ");
    cads_fmt_uint(num, sizeof(num), s_session.select_target_index);
    cads_str_append(line, sizeof(line), num);
    cads_marauder_draw_text(area, 1, line, CadsColorWhite);
    cads_marauder_draw_text(area, 3, "Read the index off \"List", CadsColorGray);
    cads_marauder_draw_text(area, 4, "APs\" first, then dial it", CadsColorGray);
    cads_marauder_draw_text(area, 5, "in here.", CadsColorGray);
    cads_marauder_draw_text(area, 7, "Up/Down adjust, Ok=select", CadsColorGray);
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
        case CADS_MARAUDER_MODE_SELECT: cads_marauder_draw_select(area); break;
    }
    if(s_session.selected_tool == CADS_MARAUDER_TOOL_PCAP) cads_marauder_draw_pcap_status(area);
}

/* --- shared tool view: input ------------------------------------------------ */

static bool cads_marauder_confirm_input(const cads_input_event_t* event) {
    if(!s_session.armed) {
        /* Blocked view (cads_marauder_draw_confirm) - the only live key is
         * Back, handled by the default case below returning false. Up/Down/
         * Ok are deliberately inert here, not just visually hidden: this is
         * the actual enforcement point, not the drawing. */
        return event->key == CadsKeyBack ? false : true;
    }
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

static bool cads_marauder_select_input(const cads_input_event_t* event) {
    switch(event->key) {
        case CadsKeyUp: s_session.select_target_index++; break;
        case CadsKeyDown:
            if(s_session.select_target_index > 0u) s_session.select_target_index--;
            break;
        case CadsKeyOk: {
            /* "select -a <N>" - marks index N (from a prior scanall, read off
             * "List APs") as selected in Marauder's own access_points list.
             * Without this, Deauth/AP-list Beacon Spam/Probe Flood all
             * silently refuse to start - see cads_marauder.h's own "CONFIG"
             * note. An out-of-range N is Marauder's own problem to report
             * ("Index not in range") - shown by switching to OUTPUT so the
             * reply is visible, same as every other command's response. */
            char cmd[24];
            char num[12];
            cads_str_copy(cmd, sizeof(cmd), "select -a ");
            cads_fmt_uint(num, sizeof(num), s_session.select_target_index);
            cads_str_append(cmd, sizeof(cmd), num);
            cads_marauder_reader_reset(&s_session.reader);
            cads_marauder_send(cmd, cads_hal_ticks_ms());
            s_session.mode = CADS_MARAUDER_MODE_OUTPUT;
            break;
        }
        case CadsKeyBack: return false;
        default: break;
    }
    cads_view_dirty(&s_tool_view);
    return true;
}

static bool cads_marauder_tool_input(const cads_input_event_t* event, void* context) {
    (void)context;
    if(event->type != CadsInputPress) return false;
    switch(s_session.mode) {
        case CADS_MARAUDER_MODE_OUTPUT: return false; /* only Back, unconsumed, pops */
        case CADS_MARAUDER_MODE_CONFIRM: return cads_marauder_confirm_input(event);
        case CADS_MARAUDER_MODE_RUN: return cads_marauder_run_input(event);
        case CADS_MARAUDER_MODE_SELECT: return cads_marauder_select_input(event);
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
                /* Demuxes bursts from CLI text and feeds the reader itself
                 * via the passthrough callback - see this file's own header
                 * comment ("PCAP RELAY") on why this always runs, not just
                 * while Sniff (PCAP) is the selected tool. */
                cads_marauder_pcap_feed(&s_pcap, chunk, n);
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

    cads_marauder_join_service(now_ms);
}

void cads_marauder_join(const char* ssid, const char* password) {
    if(ssid == NULL || ssid[0] == '\0' || password == NULL) return;

    uint32_t now = cads_hal_ticks_ms();
    cads_str_copy(s_join_password, sizeof(s_join_password), password);
    cads_marauder_join_start(&s_join, ssid);
    if(s_join.status != CADS_MARAUDER_JOIN_SEARCHING) return; /* empty ssid, defensively */

    s_join_active = true;
    s_join_deadline_ms = now + CADS_MARAUDER_JOIN_SCAN_TIMEOUT_MS;
    cads_marauder_join_arm_reader();
    cads_marauder_send("scanall", now);
}

void cads_marauder_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    cads_marauder_pcap_init(&s_pcap);
    cads_marauder_pcap_set_frame_cb(&s_pcap, cads_marauder_pcap_frame, NULL);
    cads_marauder_pcap_set_passthrough_cb(&s_pcap, cads_marauder_pcap_passthrough, NULL);

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

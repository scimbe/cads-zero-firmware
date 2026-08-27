/*
 * CaDS Zero - iperf server/client, simulator stub.
 *
 * Same reasoning as apps/bringup/explorer_iperf_demo_sim.c: lwiperf itself
 * (lib/lwip/src/apps/lwiperf) is only compiled in for CADS_TARGET itsboard
 * (modules/net/CMakeLists.txt), so there is nothing real to call here. Both
 * views still exist and are reachable in the app tree on host - same title,
 * same soft-keys - so the menu and view-dispatcher wiring is identical on
 * both targets; only the action behind OK differs.
 */

#include "cads_netiperf.h"

#include <stdbool.h>

#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

typedef struct {
    cads_view_t view;
} cads_netiperf_view_t;

static cads_netiperf_view_t s_server;
static cads_netiperf_view_t s_client;

static void cads_netiperf_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorBackground);
    cads_rect_t box = {area.x, (int16_t)(area.y + area.height / 2 - 10), area.width, 20};
    cads_canvas_draw_text_aligned(
        box, CadsAlignCenter, &cads_font12, "iperf: not available in the simulator",
        CadsColorGray);
}

static bool cads_netiperf_input(const cads_input_event_t* event, void* context) {
    (void)event;
    (void)context;
    return false;
}

static const cads_softkey_t cads_netiperf_server_keys[] = {
    {CadsKeyOk, "Run/Stop"},
    {CadsKeyBack, "Back"},
};

static const cads_softkey_t cads_netiperf_client_keys[] = {
    {CadsKeyUp, "IP+"},
    {CadsKeyDown, "IP-"},
    {CadsKeyOk, "Run/Stop"},
    {CadsKeyBack, "Back"},
};

void cads_netiperf_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    cads_view_init(&s_server.view, cads_netiperf_draw, cads_netiperf_input, &s_server);
    cads_view_set_title(&s_server.view, "iperf Server");
    cads_view_set_softkeys(
        &s_server.view, cads_netiperf_server_keys,
        sizeof(cads_netiperf_server_keys) / sizeof(cads_netiperf_server_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_IPERF_SERVER, &s_server.view);

    cads_view_init(&s_client.view, cads_netiperf_draw, cads_netiperf_input, &s_client);
    cads_view_set_title(&s_client.view, "iperf Client");
    cads_view_set_softkeys(
        &s_client.view, cads_netiperf_client_keys,
        sizeof(cads_netiperf_client_keys) / sizeof(cads_netiperf_client_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_IPERF_CLIENT, &s_client.view);
}

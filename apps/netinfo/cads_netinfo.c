#include "cads_netinfo.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "cads_hal.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"

#define CADS_NETINFO_ROW_HEIGHT  20
#define CADS_NETINFO_LABEL_WIDTH 112

/*
 * The Ethernet data path does not exist yet - only MDIO based PHY management
 * (targets/itsboard/hal/hal_eth_mdio.h), and this portable app must not
 * include a targets/ header (docs/reference/module-layout.md; this is the
 * exact mistake already found and fixed in apps/bringup/explorer.c). Every
 * field below is therefore a placeholder except has_network, which comes from
 * cads_hal_board_info(). Once the maintainer exposes PHY status through a
 * portable service, wiring it in is filling in these fields in
 * cads_netinfo_refresh() - the draw path already reads all of them.
 */
typedef struct {
    bool has_network;    /**< board capability                              */
    bool link_up;
    uint16_t speed_mbit; /**< 0 = unknown or down                           */
    bool full_duplex;
    const char* ip_address; /**< NULL = not assigned                        */
} cads_netinfo_status_t;

typedef struct {
    cads_view_t view;
    cads_netinfo_status_t status;
} cads_netinfo_t;

static cads_netinfo_t s_netinfo;

static const cads_softkey_t cads_netinfo_keys[] = {
    {CadsKeyBack, "Back"},
};

static void cads_netinfo_refresh(cads_netinfo_t* app) {
    const cads_board_info_t* info = cads_hal_board_info();
    app->status.has_network = info->has_network;
    app->status.link_up = false;
    app->status.speed_mbit = 0u;
    app->status.full_duplex = false;
    app->status.ip_address = NULL;
}

static void cads_netinfo_draw_field(
    cads_rect_t area, int16_t row, const char* label, const char* value, cads_color_t color) {
    int16_t y = (int16_t)(area.y + row * CADS_NETINFO_ROW_HEIGHT);
    cads_rect_t label_box = {area.x, y, CADS_NETINFO_LABEL_WIDTH, CADS_NETINFO_ROW_HEIGHT};
    cads_rect_t value_box = {
        (int16_t)(area.x + CADS_NETINFO_LABEL_WIDTH), y,
        (int16_t)(area.width - CADS_NETINFO_LABEL_WIDTH), CADS_NETINFO_ROW_HEIGHT};
    if(label != NULL) {
        cads_canvas_draw_text_aligned(label_box, CadsAlignLeft, &cads_font12, label, CadsColorGray);
    }
    cads_canvas_draw_text_aligned(value_box, CadsAlignLeft, &cads_font12, value, color);
}

static void cads_netinfo_draw_line(cads_rect_t area, int16_t row, const char* text) {
    int16_t y = (int16_t)(area.y + row * CADS_NETINFO_ROW_HEIGHT);
    cads_rect_t box = {area.x, y, area.width, CADS_NETINFO_ROW_HEIGHT};
    cads_canvas_draw_text_aligned(box, CadsAlignLeft, &cads_font12, text, CadsColorGrayDark);
}

static void cads_netinfo_draw(cads_rect_t area, void* context) {
    cads_netinfo_t* app = (cads_netinfo_t*)context;
    const cads_netinfo_status_t* s = &app->status;

    /* "65535 Mbit, half duplex" is the longest possible expansion (speed_mbit
     * is a uint16_t); size for that rather than for the realistic 10/100/1000
     * cases, so GCC's -Wformat-truncation has nothing to warn about. */
    char speed_text[32];
    if(s->link_up && s->speed_mbit > 0u) {
        snprintf(
            speed_text, sizeof(speed_text), "%u Mbit, %s", (unsigned)s->speed_mbit,
            s->full_duplex ? "full duplex" : "half duplex");
    } else {
        snprintf(speed_text, sizeof(speed_text), "-");
    }

    int16_t row = 0;
    cads_netinfo_draw_field(
        area, row++, "Interface:", s->has_network ? "Ethernet (RMII), LAN8742A" : "not present on this board",
        CadsColorBrandLight);
    cads_netinfo_draw_field(
        area, row++, "Status:", s->link_up ? "Connected" : "Not connected",
        s->link_up ? CadsColorAccent : CadsColorRed);
    cads_netinfo_draw_field(area, row++, "Speed:", speed_text, CadsColorGray);
    cads_netinfo_draw_field(
        area, row++, "IP address:", s->ip_address != NULL ? s->ip_address : "-", CadsColorGray);

    row++;
    cads_netinfo_draw_line(area, row++, "Display and Ethernet time-share pin PA7 during a redraw.");
    cads_netinfo_draw_line(area, row++, "Longest blackout: ~22.5 ms per band (safe /16 clock).");
    cads_netinfo_draw_line(area, row++, "See docs/explanation/pa7-conflict.md.");
}

static void cads_netinfo_enter(void* context) {
    cads_netinfo_t* app = (cads_netinfo_t*)context;
    cads_netinfo_refresh(app);
}

void cads_netinfo_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    cads_view_init(&s_netinfo.view, cads_netinfo_draw, NULL, &s_netinfo);
    cads_view_set_lifecycle(&s_netinfo.view, cads_netinfo_enter, NULL);
    cads_view_set_title(&s_netinfo.view, "Network Info");
    cads_view_set_softkeys(
        &s_netinfo.view, cads_netinfo_keys, sizeof(cads_netinfo_keys) / sizeof(cads_netinfo_keys[0]));

    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_NETINFO, &s_netinfo.view);
}

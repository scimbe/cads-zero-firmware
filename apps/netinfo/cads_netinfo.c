#include "cads_netinfo.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"

#define CADS_NETINFO_ROW_HEIGHT  20
#define CADS_NETINFO_LABEL_WIDTH 112

/*
 * cads/net/net.h (modules/net) is the portable service this file's own
 * comment used to say did not exist yet - two implementations behind one
 * header, same as cads/storage/flash.h, so this app needs no targets/
 * header to read real link state (docs/reference/module-layout.md).
 * has_network still comes from cads_hal_board_info() - a hardware
 * capability question, independent of whether a cable is plugged in.
 */
typedef struct {
    bool has_network;    /**< board capability                              */
    bool link_up;
    uint16_t speed_mbit; /**< 0 = unknown or down                           */
    bool full_duplex;
    char ip_address[16]; /**< "255.255.255.255" + NUL; "" = not assigned    */
    char gateway[16];
    char dns_server[16];
    bool dhcp_bound;
} cads_netinfo_status_t;

typedef struct {
    cads_view_t view;
    cads_netinfo_status_t status;
} cads_netinfo_t;

static cads_netinfo_t s_netinfo;

static const cads_softkey_t cads_netinfo_keys[] = {
    {CadsKeyOk, "DHCP/Static"},
    {CadsKeyBack, "Back"},
};

static void cads_netinfo_format_ip(uint32_t ip_addr, char* out, size_t size) {
    if(ip_addr == 0u) {
        cads_str_copy(out, size, "");
        return;
    }
    cads_fmt_ipv4(out, size, ip_addr);
}

static void cads_netinfo_refresh(cads_netinfo_t* app) {
    const cads_board_info_t* info = cads_hal_board_info();
    app->status.has_network = info->has_network;

    cads_net_status_t net;
    cads_net_status(&net);
    app->status.link_up = net.link_up;
    app->status.speed_mbit = net.speed_mbit;
    app->status.full_duplex = net.full_duplex;
    app->status.dhcp_bound = net.dhcp_bound;
    cads_netinfo_format_ip(net.ip_addr, app->status.ip_address, sizeof(app->status.ip_address));
    cads_netinfo_format_ip(net.gw_addr, app->status.gateway, sizeof(app->status.gateway));
    cads_netinfo_format_ip(net.dns_addr, app->status.dns_server, sizeof(app->status.dns_server));
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
    /* "65535 Mbit, half duplex" is the longest possible expansion (speed_mbit
     * is a uint16_t), which is comfortably inside the 32 byte buffer, so the
     * intermediate offset below never needs clamping the way cads_about.c's
     * longer, multi-field buffer does. */
    char speed_text[32];
    if(s->link_up && s->speed_mbit > 0u) {
        size_t pos = cads_fmt_uint(speed_text, sizeof(speed_text), s->speed_mbit);
        cads_str_copy(speed_text + pos, sizeof(speed_text) - pos, " Mbit, ");
        cads_str_append(speed_text, sizeof(speed_text), s->full_duplex ? "full duplex" : "half duplex");
    } else {
        cads_str_copy(speed_text, sizeof(speed_text), "-");
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
        area, row++, "IP address:", s->ip_address[0] != '\0' ? s->ip_address : "-", CadsColorGray);
    cads_netinfo_draw_field(
        area, row++, "Lease:",
        !s->link_up ? "-" : (s->dhcp_bound ? "DHCP" : (s->ip_address[0] != '\0' ? "static" : "none")),
        CadsColorGray);
    cads_netinfo_draw_field(
        area, row++, "Gateway:", s->gateway[0] != '\0' ? s->gateway : "-", CadsColorGray);
    cads_netinfo_draw_field(
        area, row++, "DNS server:", s->dns_server[0] != '\0' ? s->dns_server : "-", CadsColorGray);

    cads_net_config_t cfg;
    cads_net_get_config(&cfg);
    char cfg_text[24];
    if(cfg.use_dhcp) {
        cads_str_copy(cfg_text, sizeof(cfg_text), "DHCP");
    } else {
        char cfg_ip[16];
        cads_netinfo_format_ip(cfg.ip, cfg_ip, sizeof(cfg_ip));
        cads_str_copy(cfg_text, sizeof(cfg_text), "Static ");
        cads_str_append(cfg_text, sizeof(cfg_text), cfg_ip);
    }
    cads_netinfo_draw_field(area, row++, "Config:", cfg_text, CadsColorBrandLight);

    row++;
    cads_netinfo_draw_line(
        area, row++, cfg.use_dhcp ? "OK: switch to static IP" : "OK: switch to DHCP");
    cads_netinfo_draw_line(area, row++, "Display and Ethernet time-share pin PA7 during a redraw.");
    cads_netinfo_draw_line(area, row++, "Longest blackout: ~22.5 ms per band (safe /16 clock).");
    cads_netinfo_draw_line(area, row++, "See docs/explanation/pa7-conflict.md.");
}

static bool cads_netinfo_input(const cads_input_event_t* event, void* context) {
    cads_netinfo_t* app = (cads_netinfo_t*)context;
    /* OK toggles between DHCP and the static address. A touch on the "OK"
     * soft-key label arrives here as the same CadsKeyOk press, so tap and
     * button both work. */
    if(event->type == CadsInputPress && event->key == CadsKeyOk) {
        cads_net_config_t cfg;
        cads_net_get_config(&cfg);
        cfg.use_dhcp = !cfg.use_dhcp;
        cads_net_set_config(&cfg);
        cads_netinfo_refresh(app);
        cads_view_dirty_rect(&app->view, cads_view_area(&app->view));
        return true;
    }
    return false;
}

static void cads_netinfo_enter(void* context) {
    cads_netinfo_t* app = (cads_netinfo_t*)context;
    cads_netinfo_refresh(app);
}

void cads_netinfo_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    cads_view_init(&s_netinfo.view, cads_netinfo_draw, cads_netinfo_input, &s_netinfo);
    cads_view_set_lifecycle(&s_netinfo.view, cads_netinfo_enter, NULL);
    cads_view_set_title(&s_netinfo.view, "Network Info");
    cads_view_set_softkeys(
        &s_netinfo.view, cads_netinfo_keys, sizeof(cads_netinfo_keys) / sizeof(cads_netinfo_keys[0]));

    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_NETINFO, &s_netinfo.view);
}

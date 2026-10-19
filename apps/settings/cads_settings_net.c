#include "cads_settings_net.h"

#include <stdbool.h>
#include <stdint.h>

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_menu.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

/* --- state ------------------------------------------------------------------- */

typedef enum {
    NET_ROW_DHCP = 0,
    NET_ROW_IP,
    NET_ROW_NETMASK,
    NET_ROW_GATEWAY,
    NET_ROW_SAVE,
} net_row_t;

#define NET_INFO_HEIGHT 40

/* The working copy: what the panel edits. Nothing here is live until the console
 * task has saved and applied it. */
static struct {
    bool dhcp;
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
} s_work;

static volatile bool s_save_requested = false;

static char s_dhcp_detail[4];
static char s_ip_detail[16];
static char s_mask_detail[16];
static char s_gw_detail[16];

static cads_menu_item_t s_items[] = {
    {"DHCP", s_dhcp_detail, NET_ROW_DHCP},
    {"IP address", s_ip_detail, NET_ROW_IP},
    {"Netmask", s_mask_detail, NET_ROW_NETMASK},
    {"Gateway", s_gw_detail, NET_ROW_GATEWAY},
    {"Save & apply", NULL, NET_ROW_SAVE},
};

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_menu_t menu;
    cads_rect_t info_rect;
    char info_text[48];
    bool need_info;
} net_list_t;

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    net_row_t field;      /* which address is being edited */
    uint8_t octet[4];
    uint8_t sel;          /* 0..3 */
    cads_rect_t box[4];   /* where the octets are drawn, set by draw */
} net_edit_t;

static net_list_t s_list;
static net_edit_t s_edit;

static void set_info(const char* text) {
    cads_str_copy(s_list.info_text, sizeof(s_list.info_text), text);
    s_list.need_info = true;
    cads_view_dirty_rect(&s_list.view, s_list.info_rect);
}

static void refresh_details(void) {
    cads_str_copy(s_dhcp_detail, sizeof(s_dhcp_detail), s_work.dhcp ? "on" : "off");
    cads_fmt_ipv4(s_ip_detail, sizeof(s_ip_detail), s_work.ip);
    cads_fmt_ipv4(s_mask_detail, sizeof(s_mask_detail), s_work.netmask);
    cads_fmt_ipv4(s_gw_detail, sizeof(s_gw_detail), s_work.gateway);
}

static uint32_t* field_value(net_row_t field) {
    switch(field) {
        case NET_ROW_IP: return &s_work.ip;
        case NET_ROW_NETMASK: return &s_work.netmask;
        default: return &s_work.gateway;
    }
}

/* --- the octet editor --------------------------------------------------------- */

static const cads_softkey_t s_edit_keys[] = {
    {CadsKeyUp, "+"},
    {CadsKeyDown, "-"},
    {CadsKeyLeft, "<"},
    {CadsKeyRight, ">"},
    {CadsKeyOk, "Set"},
    {CadsKeyBack, "Cancel"},
};

#define EDIT_GAP 14 /* between two octets: room for the dot */

static void edit_layout(cads_rect_t area) {
    int16_t cell = (int16_t)(cads_canvas_text_width(&cads_font24, "255") + 12);
    int16_t total = (int16_t)(4 * cell + 3 * EDIT_GAP);
    int16_t x = (int16_t)(area.x + (area.width - total) / 2);
    int16_t y = (int16_t)(area.y + area.height / 2 - 24);
    for(int i = 0; i < 4; i++) {
        s_edit.box[i].x = x;
        s_edit.box[i].y = y;
        s_edit.box[i].width = cell;
        s_edit.box[i].height = 40;
        x = (int16_t)(x + cell + EDIT_GAP);
    }
}

static void edit_draw(cads_rect_t area, void* context) {
    (void)context;
    cads_canvas_fill_rect(area.x, area.y, area.width, area.height, CadsColorBackground);
    edit_layout(area);

    for(int i = 0; i < 4; i++) {
        char num[4];
        cads_fmt_uint(num, sizeof(num), s_edit.octet[i]);
        bool sel = (i == s_edit.sel);
        cads_rect_t b = s_edit.box[i];
        if(sel) {
            cads_canvas_fill_rect(b.x, b.y, b.width, b.height, CadsColorBrand);
        } else {
            cads_canvas_draw_rect(b.x, b.y, b.width, b.height, CadsColorGrayDark);
        }
        cads_canvas_draw_text_aligned(b, CadsAlignCenter, &cads_font24, num, CadsColorWhite);
        if(i < 3) {
            cads_rect_t dot = {(int16_t)(b.x + b.width), b.y, EDIT_GAP, b.height};
            cads_canvas_draw_text_aligned(dot, CadsAlignCenter, &cads_font24, ".", CadsColorGray);
        }
    }

    cads_rect_t hint = {
        area.x, (int16_t)(s_edit.box[0].y + 56), area.width, 28};
    cads_canvas_draw_text_aligned(
        hint, CadsAlignCenter, &cads_font12, "+/- change   < > pick   hold = fast",
        CadsColorGray);
}

static void edit_step(int delta) {
    int v = (int)s_edit.octet[s_edit.sel] + delta;
    while(v < 0) v += 256;
    s_edit.octet[s_edit.sel] = (uint8_t)(v % 256);
    cads_view_dirty(&s_edit.view);
}

static bool edit_input(const cads_input_event_t* event, void* context) {
    (void)context;

    if(event->type == CadsInputPress || event->type == CadsInputRepeat) {
        /* A held key accelerates: +-1 at first, +-10 once held for 1.6 s, so 0..255
         * is reachable in a couple of seconds. */
        int step = (event->type == CadsInputRepeat && event->hold_ms >= 1600u) ? 10 : 1;
        switch(event->key) {
            case CadsKeyUp: edit_step(step); return true;
            case CadsKeyDown: edit_step(-step); return true;
            case CadsKeyLeft:
                if(event->type == CadsInputPress && s_edit.sel > 0u) {
                    s_edit.sel--;
                    cads_view_dirty(&s_edit.view);
                }
                return true;
            case CadsKeyRight:
                if(event->type == CadsInputPress && s_edit.sel < 3u) {
                    s_edit.sel++;
                    cads_view_dirty(&s_edit.view);
                }
                return true;
            default: break;
        }
        return false;
    }

    if(event->type == CadsInputRelease && event->key == CadsKeyOk) {
        uint32_t ip = ((uint32_t)s_edit.octet[0] << 24) | ((uint32_t)s_edit.octet[1] << 16) |
                      ((uint32_t)s_edit.octet[2] << 8) | (uint32_t)s_edit.octet[3];
        *field_value(s_edit.field) = ip;
        refresh_details();
        cads_menu_invalidate(&s_list.menu);
        set_info("Not saved yet - choose Save & apply");
        cads_view_dispatcher_pop(s_edit.dispatcher);
        return true;
    }
    return false; /* Back (unconsumed) pops the view: cancel */
}

static void edit_open(net_row_t field, const char* title) {
    uint32_t v = *field_value(field);
    s_edit.field = field;
    s_edit.octet[0] = (uint8_t)(v >> 24);
    s_edit.octet[1] = (uint8_t)(v >> 16);
    s_edit.octet[2] = (uint8_t)(v >> 8);
    s_edit.octet[3] = (uint8_t)v;
    s_edit.sel = 3u; /* the host part is what one usually changes */
    cads_view_set_title(&s_edit.view, title);
    (void)cads_view_dispatcher_push(s_edit.dispatcher, CADS_VIEW_ID_SETTINGS_NET_EDIT);
}

/* --- the list ------------------------------------------------------------------ */

static const cads_softkey_t s_list_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk, "Open"},
    {CadsKeyBack, "Back"},
};

static void list_activate(const cads_menu_item_t* item, size_t index, void* context) {
    (void)index;
    (void)context;
    switch((net_row_t)item->id) {
        case NET_ROW_DHCP:
            s_work.dhcp = !s_work.dhcp;
            refresh_details();
            cads_menu_invalidate(&s_list.menu);
            set_info("Not saved yet - choose Save & apply");
            break;
        case NET_ROW_IP: edit_open(NET_ROW_IP, "IP address"); break;
        case NET_ROW_NETMASK: edit_open(NET_ROW_NETMASK, "Netmask"); break;
        case NET_ROW_GATEWAY: edit_open(NET_ROW_GATEWAY, "Gateway"); break;
        case NET_ROW_SAVE:
            if(!s_work.dhcp && !cads_config_net_valid(s_work.ip, s_work.netmask, s_work.gateway)) {
                set_info("Not usable: check mask, host and gateway");
                break;
            }
            s_save_requested = true;
            set_info("Saving...");
            break;
    }
}

static void list_draw(cads_rect_t area, void* context) {
    (void)area;
    (void)context;
    if(cads_menu_is_dirty(&s_list.menu)) cads_menu_draw(&s_list.menu);
    if(s_list.need_info) {
        cads_rect_t r = s_list.info_rect;
        cads_canvas_fill_rect(r.x, r.y, r.width, r.height, CadsColorBackground);
        cads_canvas_draw_hline(r.x, r.y, r.width, CadsColorGrayDark);
        cads_rect_t text = {r.x, (int16_t)(r.y + 6), r.width, (int16_t)(r.height - 6)};
        cads_canvas_draw_text_aligned(
            text, CadsAlignCenter, &cads_font12, s_list.info_text, CadsColorGray);
        s_list.need_info = false;
    }
}

static bool list_input(const cads_input_event_t* event, void* context) {
    (void)context;
    bool consumed = cads_menu_input(&s_list.menu, event);
    if(cads_menu_is_dirty(&s_list.menu)) {
        cads_view_dirty_rect(&s_list.view, cads_menu_damage(&s_list.menu));
    }
    return consumed;
}

static void list_enter(void* context) {
    (void)context;
    cads_rect_t area = cads_view_area(&s_list.view);
    cads_rect_t menu_area = {
        area.x, area.y, area.width, (int16_t)(area.height - NET_INFO_HEIGHT)};
    cads_menu_set_area(&s_list.menu, menu_area);
    s_list.info_rect.x = area.x;
    s_list.info_rect.y = (int16_t)(area.y + area.height - NET_INFO_HEIGHT);
    s_list.info_rect.width = area.width;
    s_list.info_rect.height = NET_INFO_HEIGHT;
    s_list.need_info = true;
}

/* --- public -------------------------------------------------------------------- */

void cads_settings_net_open(const cads_config_t* current) {
    if(current == NULL || s_list.dispatcher == NULL) return;
    s_work.dhcp = current->net_dhcp;
    s_work.ip = current->net_ip;
    s_work.netmask = current->net_netmask;
    s_work.gateway = current->net_gateway;
    refresh_details();
    cads_menu_invalidate(&s_list.menu);
    cads_str_copy(s_list.info_text, sizeof(s_list.info_text), "Edit, then choose Save & apply");
    s_list.need_info = true;
    (void)cads_view_dispatcher_push(s_list.dispatcher, CADS_VIEW_ID_SETTINGS_NET);
}

bool cads_settings_net_take_save(cads_config_t* out) {
    if(!s_save_requested) return false;
    s_save_requested = false;
    out->net_dhcp = s_work.dhcp;
    out->net_ip = s_work.ip;
    out->net_netmask = s_work.netmask;
    out->net_gateway = s_work.gateway;
    return true;
}

void cads_settings_net_saved(bool ok) {
    set_info(ok ? "Saved and applied" : "Saving failed (storage error)");
}

void cads_settings_net_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    s_list.dispatcher = dispatcher;
    refresh_details();
    cads_menu_init(&s_list.menu, s_items, sizeof(s_items) / sizeof(s_items[0]), &cads_font16);
    cads_menu_set_activate(&s_list.menu, list_activate, &s_list);
    cads_view_init(&s_list.view, list_draw, list_input, &s_list);
    cads_view_set_lifecycle(&s_list.view, list_enter, NULL);
    cads_view_set_title(&s_list.view, "Network");
    cads_view_set_softkeys(&s_list.view, s_list_keys, sizeof(s_list_keys) / sizeof(s_list_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_SETTINGS_NET, &s_list.view);

    s_edit.dispatcher = dispatcher;
    cads_view_init(&s_edit.view, edit_draw, edit_input, NULL);
    cads_view_set_title(&s_edit.view, "IP address");
    cads_view_set_softkeys(&s_edit.view, s_edit_keys, sizeof(s_edit_keys) / sizeof(s_edit_keys[0]));
    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_SETTINGS_NET_EDIT, &s_edit.view);
}

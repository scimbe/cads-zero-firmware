#include "cads_gpio.h"

#include <stdbool.h>
#include <stdio.h>

#include "cads_hal.h"
#include "cads_list.h"
#include "cads_softkeys.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

#define CADS_GPIO_IN_COUNT  8u
#define CADS_GPIO_INT_COUNT 6u
#define CADS_GPIO_OUT_COUNT 16u

#define CADS_GPIO_STRIP_HEIGHT 36
#define CADS_GPIO_LABEL_WIDTH  56
#define CADS_GPIO_POLL_PERIOD_MS 50u

typedef struct {
    cads_view_t view;
    cads_view_dispatcher_t* dispatcher;
    cads_list_t out_list;

    cads_rect_t in_strip;
    cads_rect_t int_strip;

    /*
     * cads_hal_adapter_outputs() takes the whole 16-bit word and there is no
     * getter, so - like the settings app's brightness/clock - the app is the
     * only record of which OUTn are on. IN/INT have the opposite shape: they
     * are read-only and can be polled directly, no shadow needed for them.
     */
    uint16_t out_state;
    uint8_t in_state;
    uint8_t int_state;
    uint8_t in_dirty;
    uint8_t int_dirty;
    bool need_strips;
    uint32_t next_poll_ms;
} cads_gpio_t;

static cads_gpio_t s_gpio;

static const cads_softkey_t cads_gpio_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyOk, "Toggle"},
    {CadsKeyBack, "Back"},
    {CadsKeyF1, "All off"},
};

/* --- layout ------------------------------------------------------------------ */

static cads_rect_t cads_gpio_cell_rect(cads_rect_t strip, uint32_t index, uint32_t count) {
    int16_t cells_x = (int16_t)(strip.x + CADS_GPIO_LABEL_WIDTH);
    int16_t cells_w = (int16_t)(strip.width - CADS_GPIO_LABEL_WIDTH);
    int16_t cell_w = (int16_t)(cells_w / (int16_t)count);

    cads_rect_t r;
    r.x = (int16_t)(cells_x + (int16_t)index * cell_w + 3);
    r.y = (int16_t)(strip.y + 4);
    r.width = (int16_t)(cell_w - 6);
    r.height = (int16_t)(strip.height - 8);
    return r;
}

static cads_rect_t cads_gpio_in_cell_rect(const cads_gpio_t* app, uint32_t index) {
    return cads_gpio_cell_rect(app->in_strip, index, CADS_GPIO_IN_COUNT);
}

static cads_rect_t cads_gpio_int_cell_rect(const cads_gpio_t* app, uint32_t index) {
    return cads_gpio_cell_rect(app->int_strip, index, CADS_GPIO_INT_COUNT);
}

static cads_rect_t cads_gpio_union(cads_rect_t a, cads_rect_t b) {
    int16_t x0 = a.x < b.x ? a.x : b.x;
    int16_t y0 = a.y < b.y ? a.y : b.y;
    int16_t x1 = (int16_t)(a.x + a.width) > (int16_t)(b.x + b.width) ? (int16_t)(a.x + a.width) :
                                                                        (int16_t)(b.x + b.width);
    int16_t y1 = (int16_t)(a.y + a.height) > (int16_t)(b.y + b.height) ?
                     (int16_t)(a.y + a.height) :
                     (int16_t)(b.y + b.height);
    cads_rect_t out = {x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
    return out;
}

static void cads_gpio_layout(cads_gpio_t* app, cads_rect_t area) {
    app->in_strip.x = area.x;
    app->in_strip.y = area.y;
    app->in_strip.width = area.width;
    app->in_strip.height = CADS_GPIO_STRIP_HEIGHT;

    app->int_strip.x = area.x;
    app->int_strip.y = (int16_t)(area.y + CADS_GPIO_STRIP_HEIGHT);
    app->int_strip.width = area.width;
    app->int_strip.height = CADS_GPIO_STRIP_HEIGHT;
}

/* --- drawing: IN / INT indicator strips --------------------------------------- */

static void cads_gpio_paint_cell(cads_rect_t r, bool active, uint32_t index) {
    cads_canvas_fill_rect(r.x, r.y, r.width, r.height, active ? CadsColorAccent : CadsColorGrayDark);
    cads_canvas_draw_rect(r.x, r.y, r.width, r.height, CadsColorBlack);
    char digit[4];
    snprintf(digit, sizeof(digit), "%u", (unsigned)index);
    cads_canvas_draw_text_aligned(
        r, CadsAlignCenter, &cads_font12, digit, active ? CadsColorBlack : CadsColorGrayLight);
}

static void cads_gpio_paint_in_cell(const cads_gpio_t* app, uint32_t index) {
    bool active = (app->in_state >> index) & 1u;
    cads_gpio_paint_cell(cads_gpio_in_cell_rect(app, index), active, index);
}

static void cads_gpio_paint_int_cell(const cads_gpio_t* app, uint32_t index) {
    bool active = (app->int_state >> index) & 1u;
    cads_gpio_paint_cell(cads_gpio_int_cell_rect(app, index), active, index);
}

static void cads_gpio_paint_strips(const cads_gpio_t* app) {
    cads_rect_t in_label = {
        app->in_strip.x, app->in_strip.y, CADS_GPIO_LABEL_WIDTH, app->in_strip.height};
    cads_rect_t int_label = {
        app->int_strip.x, app->int_strip.y, CADS_GPIO_LABEL_WIDTH, app->int_strip.height};
    cads_canvas_fill_rect(in_label.x, in_label.y, in_label.width, in_label.height, CadsColorBackground);
    cads_canvas_fill_rect(
        int_label.x, int_label.y, int_label.width, int_label.height, CadsColorBackground);
    cads_canvas_draw_text_aligned(in_label, CadsAlignLeft, &cads_font12, "IN", CadsColorGray);
    cads_canvas_draw_text_aligned(int_label, CadsAlignLeft, &cads_font12, "INT", CadsColorGray);

    for(uint32_t i = 0u; i < CADS_GPIO_IN_COUNT; i++) cads_gpio_paint_in_cell(app, i);
    for(uint32_t i = 0u; i < CADS_GPIO_INT_COUNT; i++) cads_gpio_paint_int_cell(app, i);

    int16_t divider_y = (int16_t)(app->int_strip.y + app->int_strip.height);
    cads_canvas_draw_hline(app->in_strip.x, divider_y, app->in_strip.width, CadsColorGrayDark);
}

/* --- drawing: OUT list --------------------------------------------------------- */

static void cads_gpio_out_row_draw(size_t index, cads_rect_t row, bool selected, void* context) {
    const cads_gpio_t* app = (const cads_gpio_t*)context;
    bool on = (app->out_state >> index) & 1u;

    cads_canvas_fill_rect(row.x, row.y, row.width, row.height, selected ? CadsColorBrand : CadsColorBackground);

    char label[8];
    snprintf(label, sizeof(label), "OUT%u", (unsigned)index);
    cads_rect_t label_box = {(int16_t)(row.x + 10), row.y, (int16_t)(row.width / 2), row.height};
    cads_canvas_draw_text_aligned(
        label_box, CadsAlignLeft, &cads_font16, label,
        selected ? CadsColorWhite : CadsColorBrandLight);

    cads_rect_t state_box = {
        (int16_t)(row.x + row.width / 2), row.y, (int16_t)(row.width / 2 - 10), row.height};
    cads_canvas_draw_text_aligned(
        state_box, CadsAlignRight, &cads_font16, on ? "ON" : "off",
        on ? CadsColorAccent : CadsColorGray);
}

/*
 * PF0-7 (IN0..7) and PG0-5 (INT0..5) are inputs and must never be driven -
 * docs/SAFETY.md SS3. Nothing here attempts to: cads_hal_adapter_outputs()
 * only ever reaches PD0..7/PE0..7 (OUT0..15), and this app never calls
 * anything that could reconfigure PF/PG.
 */
static void cads_gpio_out_activate(size_t index, void* context) {
    cads_gpio_t* app = (cads_gpio_t*)context;
    app->out_state ^= (uint16_t)(1u << index);
    cads_hal_adapter_outputs(app->out_state);
    /* cads_list only tracks dirty rows from structural changes (selection,
     * scroll); a row's own content changing needs the blunt invalidate. */
    cads_list_invalidate(&app->out_list);
}

/* --- view --------------------------------------------------------------------- */

static void cads_gpio_draw(cads_rect_t area, void* context) {
    (void)area;
    cads_gpio_t* app = (cads_gpio_t*)context;

    if(app->need_strips) {
        cads_gpio_paint_strips(app);
        app->need_strips = false;
    } else {
        for(uint32_t i = 0u; i < CADS_GPIO_IN_COUNT; i++) {
            if(app->in_dirty & (1u << i)) cads_gpio_paint_in_cell(app, i);
        }
        for(uint32_t i = 0u; i < CADS_GPIO_INT_COUNT; i++) {
            if(app->int_dirty & (1u << i)) cads_gpio_paint_int_cell(app, i);
        }
    }
    app->in_dirty = 0u;
    app->int_dirty = 0u;

    if(cads_list_is_dirty(&app->out_list)) cads_list_draw(&app->out_list);
}

static void cads_gpio_all_off(cads_gpio_t* app) {
    app->out_state = 0u;
    cads_hal_adapter_outputs(0u);
    cads_list_invalidate(&app->out_list);
}

static bool cads_gpio_input(const cads_input_event_t* event, void* context) {
    cads_gpio_t* app = (cads_gpio_t*)context;
    bool consumed = false;

    if(event->type == CadsInputRelease && event->key == CadsKeyF1) {
        cads_gpio_all_off(app);
        consumed = true;
    } else {
        consumed = cads_list_input(&app->out_list, event);
    }

    if(cads_list_is_dirty(&app->out_list)) {
        cads_view_dirty_rect(&app->view, cads_list_damage(&app->out_list));
    }
    return consumed;
}

static void cads_gpio_enter(void* context) {
    cads_gpio_t* app = (cads_gpio_t*)context;
    cads_rect_t area = cads_view_area(&app->view);
    cads_gpio_layout(app, area);

    cads_rect_t list_area = {
        area.x, (int16_t)(area.y + 2 * CADS_GPIO_STRIP_HEIGHT + 2), area.width,
        (int16_t)(area.height - 2 * CADS_GPIO_STRIP_HEIGHT - 2)};
    cads_list_set_area(&app->out_list, list_area);

    app->in_state = cads_hal_adapter_inputs();
    app->int_state = (uint8_t)(cads_hal_adapter_interrupts() & 0x3Fu);
    app->in_dirty = 0u;
    app->int_dirty = 0u;
    app->need_strips = true;
    app->next_poll_ms = 0u;
}

void cads_gpio_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    s_gpio.dispatcher = dispatcher;
    s_gpio.out_state = 0u;

    cads_list_init(&s_gpio.out_list, CADS_GPIO_OUT_COUNT, &cads_font16, cads_gpio_out_row_draw, &s_gpio);
    cads_list_set_activate(&s_gpio.out_list, cads_gpio_out_activate);

    cads_view_init(&s_gpio.view, cads_gpio_draw, cads_gpio_input, &s_gpio);
    cads_view_set_lifecycle(&s_gpio.view, cads_gpio_enter, NULL);
    cads_view_set_title(&s_gpio.view, "GPIO");
    cads_view_set_softkeys(
        &s_gpio.view, cads_gpio_keys, sizeof(cads_gpio_keys) / sizeof(cads_gpio_keys[0]));

    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_GPIO, &s_gpio.view);
}

void cads_gpio_tick(uint32_t now_ms) {
    if(s_gpio.dispatcher == NULL) return;
    if(cads_view_dispatcher_current_id(s_gpio.dispatcher) != CADS_VIEW_ID_GPIO) return;
    if((int32_t)(now_ms - s_gpio.next_poll_ms) < 0) return;
    s_gpio.next_poll_ms = now_ms + CADS_GPIO_POLL_PERIOD_MS;

    uint8_t in = cads_hal_adapter_inputs();
    uint8_t interrupts = (uint8_t)(cads_hal_adapter_interrupts() & 0x3Fu);
    uint8_t in_changed = (uint8_t)(in ^ s_gpio.in_state);
    uint8_t int_changed = (uint8_t)(interrupts ^ s_gpio.int_state);
    if(in_changed == 0u && int_changed == 0u) return;

    s_gpio.in_state = in;
    s_gpio.int_state = interrupts;
    s_gpio.in_dirty = (uint8_t)(s_gpio.in_dirty | in_changed);
    s_gpio.int_dirty = (uint8_t)(s_gpio.int_dirty | int_changed);

    cads_rect_t damage = {0, 0, 0, 0};
    bool have_damage = false;
    for(uint32_t i = 0u; i < CADS_GPIO_IN_COUNT; i++) {
        if(!(in_changed & (1u << i))) continue;
        cads_rect_t r = cads_gpio_in_cell_rect(&s_gpio, i);
        damage = have_damage ? cads_gpio_union(damage, r) : r;
        have_damage = true;
    }
    for(uint32_t i = 0u; i < CADS_GPIO_INT_COUNT; i++) {
        if(!(int_changed & (1u << i))) continue;
        cads_rect_t r = cads_gpio_int_cell_rect(&s_gpio, i);
        damage = have_damage ? cads_gpio_union(damage, r) : r;
        have_damage = true;
    }
    if(have_damage) cads_view_dirty_rect(&s_gpio.view, damage);
}

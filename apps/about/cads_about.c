#include "cads_about.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "cads_hal.h"
#include "cads_softkeys.h"
#include "cads_textbox.h"
#include "cads_view.h"
#include "canvas.h"
#include "input/cads_input.h"

#define CADS_ABOUT_TEXT_CAPACITY 768u
#define CADS_ABOUT_LINE_CAPACITY 40u

typedef struct {
    cads_view_t view;
    cads_textbox_t box;
    cads_text_line_t lines[CADS_ABOUT_LINE_CAPACITY];
} cads_about_t;

static cads_about_t s_about;
static char s_about_text[CADS_ABOUT_TEXT_CAPACITY];

static const cads_softkey_t cads_about_keys[] = {
    {CadsKeyUp, "Up"},
    {CadsKeyDown, "Down"},
    {CadsKeyLeft, "PgUp"},
    {CadsKeyRight, "PgDn"},
    {CadsKeyBack, "Back"},
};

/*
 * Every field below comes from cads_hal_board_info() or a compiler-provided
 * build date macro, never a hard-coded 480/320 or similar - this screen is the
 * proof the descriptor is actually load-bearing.
 */
static void cads_about_build_text(void) {
    const cads_board_info_t* info = cads_hal_board_info();
    size_t pos = 0u;
    int n;

#define CADS_ABOUT_APPEND(...)                                                     \
    do {                                                                           \
        n = snprintf(s_about_text + pos, sizeof(s_about_text) - pos, __VA_ARGS__); \
        if(n > 0) pos += (size_t)n;                                                \
        if(pos >= sizeof(s_about_text)) pos = sizeof(s_about_text) - 1u;           \
    } while(0)

    CADS_ABOUT_APPEND("Board: %s\n", info->board_name);
    CADS_ABOUT_APPEND("MCU: %s\n", info->mcu_name);
    CADS_ABOUT_APPEND("CPU clock: %lu MHz\n", (unsigned long)(info->cpu_hz / 1000000u));
    CADS_ABOUT_APPEND(
        "Display: %u x %u, %s\n", (unsigned)info->display_width, (unsigned)info->display_height,
        info->display_readable ? "readable" : "write-only");

    uint32_t full_screen_ms = 0u;
    if(info->display_pixels_per_second > 0u) {
        full_screen_ms = (uint32_t)(
            ((uint64_t)info->display_width * info->display_height * 1000u) /
            info->display_pixels_per_second);
    }
    CADS_ABOUT_APPEND(
        "Throughput: %lu px/s (~%lu ms full redraw)\n",
        (unsigned long)info->display_pixels_per_second, (unsigned long)full_screen_ms);

    CADS_ABOUT_APPEND("Buttons: %u\n", (unsigned)info->button_count);
    CADS_ABOUT_APPEND("Touch: %s\n", info->has_touch ? "yes" : "no");
    CADS_ABOUT_APPEND("Network: %s\n", info->has_network ? "yes" : "no");
    CADS_ABOUT_APPEND("Storage: %s\n", info->has_storage ? "yes" : "no");
    CADS_ABOUT_APPEND("Flash: %lu KB usable\n", (unsigned long)(info->flash_bytes / 1024u));
    CADS_ABOUT_APPEND("RAM (DMA-capable): %lu KB\n", (unsigned long)(info->ram_bytes / 1024u));
    CADS_ABOUT_APPEND("Firmware built: %s %s\n", __DATE__, __TIME__);

#undef CADS_ABOUT_APPEND
}

static void cads_about_draw(cads_rect_t area, void* context) {
    (void)area;
    cads_about_t* app = (cads_about_t*)context;
    if(cads_textbox_is_dirty(&app->box)) cads_textbox_draw(&app->box);
}

static bool cads_about_input(const cads_input_event_t* event, void* context) {
    cads_about_t* app = (cads_about_t*)context;
    bool consumed = cads_textbox_input(&app->box, event);
    if(cads_textbox_is_dirty(&app->box)) {
        cads_view_dirty_rect(&app->view, cads_textbox_damage(&app->box));
    }
    return consumed;
}

static void cads_about_enter(void* context) {
    cads_about_t* app = (cads_about_t*)context;
    cads_textbox_set_area(&app->box, cads_view_area(&app->view));
}

void cads_about_init(cads_view_dispatcher_t* dispatcher) {
    if(dispatcher == NULL) return;

    cads_about_build_text();
    cads_textbox_init(
        &s_about.box, s_about_text, &cads_font12, s_about.lines, CADS_ABOUT_LINE_CAPACITY);

    cads_view_init(&s_about.view, cads_about_draw, cads_about_input, &s_about);
    cads_view_set_lifecycle(&s_about.view, cads_about_enter, NULL);
    cads_view_set_title(&s_about.view, "About");
    cads_view_set_softkeys(
        &s_about.view, cads_about_keys, sizeof(cads_about_keys) / sizeof(cads_about_keys[0]));

    (void)cads_view_dispatcher_add(dispatcher, CADS_VIEW_ID_ABOUT, &s_about.view);
}

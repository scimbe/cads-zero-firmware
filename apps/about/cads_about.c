#include "cads_about.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
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
 *
 * No snprintf: linking it pulls in newlib's heap-init syscall stub (_sbrk),
 * which this project's linker script deliberately does not provide (no heap
 * anywhere - the same bug caught and fixed in apps/gpio, see docs/ROADMAP.md).
 * cads_str_append() and cads_fmt_uint() self-locate the end of the buffer, so
 * a plain sequence of calls does the same job with no format string at all.
 */
static size_t s_about_pos;

static void cads_about_str(const char* text) {
    s_about_pos = cads_str_append(s_about_text, sizeof(s_about_text), text);
    if(s_about_pos >= sizeof(s_about_text)) s_about_pos = sizeof(s_about_text) - 1u;
}

static void cads_about_uint(uint32_t value) {
    s_about_pos += cads_fmt_uint(s_about_text + s_about_pos, sizeof(s_about_text) - s_about_pos, value);
    if(s_about_pos >= sizeof(s_about_text)) s_about_pos = sizeof(s_about_text) - 1u;
}

static void cads_about_build_text(void) {
    const cads_board_info_t* info = cads_hal_board_info();
    s_about_pos = 0u;
    s_about_text[0] = '\0';

    cads_about_str("Board: ");
    cads_about_str(info->board_name);
    cads_about_str("\nMCU: ");
    cads_about_str(info->mcu_name);
    cads_about_str("\nCPU clock: ");
    cads_about_uint(info->cpu_hz / 1000000u);
    cads_about_str(" MHz\nDisplay: ");
    cads_about_uint(info->display_width);
    cads_about_str(" x ");
    cads_about_uint(info->display_height);
    cads_about_str(", ");
    cads_about_str(info->display_readable ? "readable" : "write-only");

    uint32_t full_screen_ms = 0u;
    if(info->display_pixels_per_second > 0u) {
        full_screen_ms = (uint32_t)(
            ((uint64_t)info->display_width * info->display_height * 1000u) /
            info->display_pixels_per_second);
    }
    cads_about_str("\nThroughput: ");
    cads_about_uint(info->display_pixels_per_second);
    cads_about_str(" px/s (~");
    cads_about_uint(full_screen_ms);
    cads_about_str(" ms full redraw)\nButtons: ");
    cads_about_uint(info->button_count);
    cads_about_str("\nTouch: ");
    cads_about_str(info->has_touch ? "yes" : "no");
    cads_about_str("\nNetwork: ");
    cads_about_str(info->has_network ? "yes" : "no");
    cads_about_str("\nStorage: ");
    cads_about_str(info->has_storage ? "yes" : "no");
    cads_about_str("\nFlash: ");
    cads_about_uint(info->flash_bytes / 1024u);
    cads_about_str(" KB usable\nRAM (DMA-capable): ");
    cads_about_uint(info->ram_bytes / 1024u);
    cads_about_str(" KB\nFirmware built: ");
    cads_about_str(__DATE__);
    cads_about_str(" ");
    cads_about_str(__TIME__);
    cads_about_str("\n");
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

/*
 * CaDS Zero - one-off demo driver for apps/filebrowser, reached from the
 * hardware explorer's 'v' command. Same non-reentrancy rule as
 * explorer_gui_demo.c and explorer_app_demo.c: never calls
 * cads_input_tick() itself, only cads_gui_attach_input().
 */

#include "explorer_filebrowser_demo.h"

#include "cads_filebrowser.h"
#include "cads_gui.h"
#include "cads_hal.h"
#include "cads_softkeys.h"
#include "cads_statusbar.h"
#include "cads_view_dispatcher.h"
#include "input_probe.h" /* cads_probe_puts / cads_probe_put_uint */

#define CADS_FILEBROWSER_DEMO_VIEW_CAPACITY 2u
#define CADS_FILEBROWSER_DEMO_STACK_DEPTH   4u

static cads_view_entry_t s_entries[CADS_FILEBROWSER_DEMO_VIEW_CAPACITY];
static uint32_t s_stack[CADS_FILEBROWSER_DEMO_STACK_DEPTH];
static cads_view_dispatcher_t s_dispatcher;
static cads_gui_t s_gui;
static cads_statusbar_t s_statusbar;
static cads_softkeys_t s_softkeys;

void cads_explorer_filebrowser_demo(uint32_t seconds) {
    cads_view_dispatcher_init(
        &s_dispatcher, s_entries, CADS_FILEBROWSER_DEMO_VIEW_CAPACITY, s_stack,
        CADS_FILEBROWSER_DEMO_STACK_DEPTH);

    cads_rect_t full = {0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT};
    cads_view_dispatcher_set_area(&s_dispatcher, full);

    cads_filebrowser_init(&s_dispatcher);
    if(!cads_view_dispatcher_switch_to(&s_dispatcher, CADS_VIEW_ID_FILEBROWSER)) {
        cads_probe_puts("# filebrowser demo: failed to switch to the file browser view\r\n");
        return;
    }

    cads_statusbar_init(&s_statusbar);
    cads_softkeys_init(&s_softkeys);
    cads_gui_init(&s_gui, &s_dispatcher, &s_statusbar, &s_softkeys);
    cads_gui_attach_input(&s_gui);

    cads_probe_puts("# filebrowser demo: live on the panel for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts(
        "s - OK opens a directory or shows a file's size, Back goes up "
        "a level then exits\r\n");

    uint32_t start = cads_hal_ticks_ms();
    uint32_t total_pixels = 0u;
    uint32_t frames = 0u;

    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        uint32_t pixels = cads_gui_tick(&s_gui, cads_hal_ticks_ms());
        if(pixels) {
            total_pixels += pixels;
            frames++;
        }
        cads_hal_delay_ms(10u);
    }

    cads_gui_detach_input();

    cads_probe_puts("# filebrowser demo done: ");
    cads_probe_put_uint(frames);
    cads_probe_puts(" frames flushed, ");
    cads_probe_put_uint(total_pixels);
    cads_probe_puts(" pixels total\r\n");
}

/*
 * CaDS Zero - one-off demo driver for the GUI stack, reached from the hardware
 * explorer's 'g' command.
 *
 * WHY THIS DOES NOT CALL cads_input_tick() ITSELF
 * ------------------------------------------------
 * Under the scheduler (apps/bringup/tasks.c) the input task already polls
 * cads_input_tick() at 100 Hz in its own thread. cads_input.c was not written
 * for concurrent callers, so a second call site here would race it. This demo
 * only calls cads_gui_attach_input(), which redirects the ALREADY-RUNNING
 * input task's events to the GUI for as long as the demo owns the screen, and
 * hands the input task's own callback back on the way out.
 */

#include "explorer_gui_demo.h"

#include "cads_gpio.h"
#include "cads_gui.h"
#include "cads_hal.h"
#include "cads_softkeys.h"
#include "cads_statusbar.h"
#include "cads_view_dispatcher.h"
#include "input_probe.h" /* cads_probe_puts / cads_probe_put_uint */

#define CADS_DEMO_VIEW_CAPACITY 4u
#define CADS_DEMO_STACK_DEPTH 4u

static cads_view_entry_t s_entries[CADS_DEMO_VIEW_CAPACITY];
static uint32_t s_stack[CADS_DEMO_STACK_DEPTH];
static cads_view_dispatcher_t s_dispatcher;
static cads_gui_t s_gui;

void cads_explorer_gui_demo(uint32_t seconds) {
    cads_view_dispatcher_init(
        &s_dispatcher, s_entries, CADS_DEMO_VIEW_CAPACITY, s_stack, CADS_DEMO_STACK_DEPTH);

    /* Full canvas, no status bar or soft-key strip for this first smoke test -
     * the point here is proving the view/widget/app stack renders and takes
     * input on real silicon, not exercising the chrome. */
    cads_rect_t full = {0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT};
    cads_view_dispatcher_set_area(&s_dispatcher, full);

    cads_gpio_init(&s_dispatcher);
    if(!cads_view_dispatcher_switch_to(&s_dispatcher, CADS_VIEW_ID_GPIO)) {
        cads_probe_puts("# gui demo: failed to switch to the GPIO view\r\n");
        return;
    }

    cads_gui_init(&s_gui, &s_dispatcher, NULL, NULL);
    cads_gui_attach_input(&s_gui);

    cads_probe_puts("# gui demo: apps/gpio live on the panel for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s - touch adapter output cells, watch IN/INT update\r\n");

    uint32_t start = cads_hal_ticks_ms();
    uint32_t total_pixels = 0u;
    uint32_t frames = 0u;

    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        cads_gpio_tick(cads_hal_ticks_ms());
        uint32_t pixels = cads_gui_tick(&s_gui, cads_hal_ticks_ms());
        if(pixels) {
            total_pixels += pixels;
            frames++;
        }
        cads_hal_delay_ms(10u);
    }

    cads_gui_detach_input();

    cads_probe_puts("# gui demo done: ");
    cads_probe_put_uint(frames);
    cads_probe_puts(" frames flushed, ");
    cads_probe_put_uint(total_pixels);
    cads_probe_puts(" pixels total\r\n");
}

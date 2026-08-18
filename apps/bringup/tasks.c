/*
 * CaDS Zero - the firmware's task set.
 *
 * Three tasks, chosen so that each of the three things that can starve the
 * others is represented:
 *
 *   ui        owns the display. A flush blocks for up to 448 ms, which is by
 *             far the longest single operation in the system, so it runs at
 *             normal priority and everything else must tolerate it.
 *   input     polls the buttons and the touch panel at 100 Hz. Higher priority
 *             than the UI, because an input sampled late feels like a bug even
 *             when nothing is lost.
 *   console   the hardware explorer and, later, the CLI. Lowest priority: it
 *             is a diagnostic channel and must never delay anything real.
 *
 * Stacks are static and sized from measurement - each task reports its unused
 * high-water mark, and the numbers are printed periodically so the sizes stay
 * honest as the code grows.
 */

#include "tasks.h"

#include "cads/kernel/kernel.h"
#include "cads_hal.h"
#include "canvas.h"
#include "explorer.h"
#include "input/cads_input.h"
#include "input_probe.h"

/* Stack sizes in words. The UI task carries the canvas call chain, which is
 * the deepest; the others are shallow. */
#define CADS_UI_STACK      512
#define CADS_INPUT_STACK   256
#define CADS_CONSOLE_STACK 512

__attribute__((section(".ccm"), aligned(8))) static uint32_t cads_ui_stack[CADS_UI_STACK];
__attribute__((section(".ccm"), aligned(8))) static uint32_t cads_input_stack[CADS_INPUT_STACK];
__attribute__((section(".ccm"), aligned(8))) static uint32_t cads_console_stack[CADS_CONSOLE_STACK];

static cads_thread_t cads_ui_thread;
static cads_thread_t cads_input_thread;
static cads_thread_t cads_console_thread;

/*
 * THE DISPLAY HAS EXACTLY ONE FLUSHER.
 *
 * Any task may draw into the canvas - drawing is cheap and the buffer is
 * private to the CPU - but only the ui task calls cads_canvas_flush(). A flush
 * holds the SPI bus for up to 448 ms, reconfigures the Ethernet MAC around it,
 * and drives a DMA transfer; two of them overlapping would interleave pixel
 * data into the panel and corrupt the frame.
 *
 * The mutex below exists for the second flusher that does not exist yet. The
 * rule is the real protection: everything else marks the canvas dirty and
 * waits, via cads_tasks_redraw_sync().
 */
static cads_mutex_t cads_display_mutex;

static volatile uint32_t cads_input_events;
static volatile uint32_t cads_last_key;

static void cads_on_input(const cads_input_event_t* event, void* context) {
    (void)context;
    cads_input_events++;
    if(event->type == CadsInputPress) {
        cads_last_key = (uint32_t)event->key + 1u;
    }
}

static void cads_input_task(void* context) {
    (void)context;
    cads_input_init();
    cads_input_set_callback(cads_on_input, NULL);

    uint32_t wake = cads_kernel_ticks();
    for(;;) {
        cads_input_tick();
        cads_kernel_sleep_until(&wake, 10u); /* 100 Hz */
    }
}

static void cads_ui_task(void* context) {
    (void)context;
    uint32_t wake = cads_kernel_ticks();

    for(;;) {
        if(cads_canvas_is_dirty()) {
            if(cads_mutex_lock(&cads_display_mutex, 1000u)) {
                cads_canvas_flush();
                cads_mutex_unlock(&cads_display_mutex);
            }
        }
        cads_kernel_sleep_until(&wake, 50u); /* 20 Hz ceiling */
    }
}

static void cads_console_task(void* context) {
    (void)context;
    cads_explorer_run(); /* never returns */
}

void cads_tasks_start(void) {
    cads_kernel_init();
    cads_mutex_init(&cads_display_mutex);

    cads_thread_start(
        &cads_input_thread, "input", cads_input_task, NULL, cads_input_stack,
        CADS_INPUT_STACK, CadsPriorityHigh);

    cads_thread_start(
        &cads_ui_thread, "ui", cads_ui_task, NULL, cads_ui_stack, CADS_UI_STACK,
        CadsPriorityNormal);

    cads_thread_start(
        &cads_console_thread, "console", cads_console_task, NULL, cads_console_stack,
        CADS_CONSOLE_STACK, CadsPriorityLow);

    cads_kernel_start(); /* does not return */
}

bool cads_tasks_redraw_sync(uint32_t timeout_ms) {
    /* The caller has already drawn; the ui task owns the transfer. Wait for it
     * to pick the work up rather than flushing here, which is what keeps the
     * single-flusher rule true instead of merely intended. */
    uint32_t deadline = cads_kernel_ticks() + timeout_ms;
    while(cads_canvas_is_dirty()) {
        if((int32_t)(cads_kernel_ticks() - deadline) >= 0) return false;
        cads_kernel_sleep_ms(5u);
    }
    return true;
}

void cads_tasks_report(void) {
    cads_probe_puts("# tasks  ui_free=");
    cads_probe_put_uint(cads_thread_stack_free(&cads_ui_thread));
    cads_probe_puts(" input_free=");
    cads_probe_put_uint(cads_thread_stack_free(&cads_input_thread));
    cads_probe_puts(" console_free=");
    cads_probe_put_uint(cads_thread_stack_free(&cads_console_thread));
    cads_probe_puts(" tasks=");
    cads_probe_put_uint(cads_kernel_task_count());
    cads_probe_puts(" events=");
    cads_probe_put_uint(cads_input_events);
    cads_probe_puts(" last_key=");
    cads_probe_put_uint(cads_last_key);
    cads_probe_puts("\r\n");
}

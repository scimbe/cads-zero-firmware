/*
 * CaDS Zero - the task layer on the host.
 *
 * The simulator has no scheduler, so the three things the rest of the firmware
 * asks of the task layer collapse to their single-threaded equivalents. Kept
 * as a separate translation unit rather than as #ifdefs inside tasks.c,
 * because the board's version is about concurrency and this one is about the
 * absence of it - interleaving them would make both harder to read.
 */

#include "tasks.h"

#include "cads_hal.h"
#include "canvas.h"
#include "explorer.h"

void cads_tasks_sleep_ms(uint32_t ms) {
    cads_hal_delay_ms(ms);
}

bool cads_tasks_redraw_sync(uint32_t timeout_ms) {
    (void)timeout_ms;
    /* No ui task to hand the work to, so the caller flushes it themselves.
     * The single-flusher rule that governs the board is about two tasks
     * racing; here there is only one. */
    if(cads_canvas_is_dirty()) cads_canvas_flush();
    return true;
}

void cads_tasks_report(void) {
    /* Stack high-water marks and task counts are properties of a scheduler
     * that does not exist here. Saying so beats printing zeroes that look
     * like measurements. */
}

void cads_tasks_start(void) {
    cads_explorer_run(); /* never returns */
}

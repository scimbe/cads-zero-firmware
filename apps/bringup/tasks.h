#ifndef CADS_TASKS_H
#define CADS_TASKS_H

#include <stdbool.h>
#include <stdint.h>

/** Create the firmware's tasks and start the scheduler. Does not return. */
void cads_tasks_start(void);

/**
 * Sleep, whether or not a scheduler exists.
 *
 * The board yields to other tasks; the simulator, which has none, busy waits.
 * Callers above this line have no business knowing which - that is the whole
 * reason this exists rather than everyone including the kernel header.
 */
void cads_tasks_sleep_ms(uint32_t ms);

/**
 * Wait until the ui task has pushed everything currently drawn.
 *
 * Only the ui task flushes the display - a flush holds the bus for up to
 * 448 ms and two overlapping ones would corrupt the frame - so a task that has
 * drawn something and needs to know it reached the panel waits here instead of
 * flushing itself. Returns false on timeout.
 */
bool cads_tasks_redraw_sync(uint32_t timeout_ms);

/** Print stack high-water marks, task count and input counters. Exposed so the
 *  console can ask for it, which is how the static stack sizes stay honest. */
void cads_tasks_report(void);

#endif /* CADS_TASKS_H */

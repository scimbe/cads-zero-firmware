#ifndef CADS_BRINGUP_H
#define CADS_BRINGUP_H

/**
 * Milestone 0 bring-up and self test.
 *
 * Exercises every hardware path the rest of the firmware depends on and
 * reports the result as TAP on the console, so scripts/board_test.py can
 * decide pass or fail without a human looking at the panel. Afterwards it
 * stays in an interactive loop echoing touch and adapter input.
 *
 * Portable: the simulator runs this unchanged.
 */
void cads_bringup_run(void);

#endif /* CADS_BRINGUP_H */

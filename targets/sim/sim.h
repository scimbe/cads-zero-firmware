/*
 * CaDS Zero - simulator internals shared between main.c and hal_sim.c.
 *
 * Deliberately not part of the HAL: nothing above core/cads_hal.h may include
 * this, or the portability claim the simulator exists to defend would be void.
 */

#ifndef CADS_SIM_H
#define CADS_SIM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /** Non-NULL: render the first quiescent frame to this BMP and exit. */
    const char* screenshot_path;
    /** How long the panel must go untouched before it counts as quiescent. */
    uint32_t screenshot_idle_ms;
    /** Give up if the application never draws anything within this long. */
    uint32_t screenshot_timeout_ms;
    /** Integer window magnification, 1..4. Retina panels want 2. */
    uint32_t scale;
} cads_sim_options_t;

/** Must be called before cads_hal_init(); the window is built from this. */
void cads_sim_configure(const cads_sim_options_t* options);

/**
 * Drain the SDL event queue and repaint if anything changed.
 *
 * The HAL calls this from every blocking or polling entry point, because the
 * portable application never returns to a host event loop. main() only needs
 * it for the day cads_bringup_run() does return.
 */
void cads_sim_pump(void);

#endif /* CADS_SIM_H */

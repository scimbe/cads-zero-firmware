#ifndef CADS_EXPLORER_FREQ_DEMO_H
#define CADS_EXPLORER_FREQ_DEMO_H

#include <stdint.h>

/**
 * Measure the period AND duty cycle of whatever signal drives CN8 pin 5
 * (PB10, TIM2 channel 3 for the period, channel 4 riding the same pin
 * for the high time) for `seconds` (default 5), and report how many of
 * each were captured, how many were missed, and the resulting
 * min/max/average frequency plus average duty cycle. See
 * hal_freqcounter.c's own file header for the pin choice (not one of
 * the adapter's INT0..5 lines - none of them carry any timer alternate
 * function at all) and cads/toolbox/freqcounter.h for exactly what
 * "missed" means and why it is counted rather than silently folded into
 * a wrong measurement.
 *
 * Board only - TIM2 input capture and the physical CN8 header are both
 * real hardware. explorer_freq_demo_sim.c explains why the simulator
 * does not need its own version.
 */
void cads_explorer_freq_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_FREQ_DEMO_H */

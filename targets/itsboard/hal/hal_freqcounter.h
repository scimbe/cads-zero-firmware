#ifndef CADS_HAL_FREQCOUNTER_H
#define CADS_HAL_FREQCOUNTER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * TIM2 channel 3 input capture on CN8 pin 5 (PB10) - see board.h's own
 * `CADS_PIN_FREQCOUNTER_*` comment for why this pin, not one of the
 * adapter's INT0..5 lines. Channel 4 rides the same pin via the timer's
 * own channel-swap mapping for duty cycle - no second GPIO pin
 * involved. This file is only the register-level driver; the
 * capture-to-period/duty-cycle math (wraparound, missed-edge handling)
 * lives in the portable, host-tested `cads/toolbox/freqcounter.h`,
 * which this driver feeds raw captures into. See hal_freqcounter.c's
 * file header for the clock/prescaler reasoning and why channel 4 does
 * not use the textbook reset-mode PWM Input technique.
 */

/** Configure PB10 for TIM2_CH3 (rising edge) and TIM2_CH4 (falling
 *  edge, same pin) input capture, and start the timer. Resets any
 *  period/duty-cycle statistics from a previous run. */
void cads_hal_freqcounter_start(void);

/** Stop TIM2 and release PB10 back to a plain pulled-down input. */
void cads_hal_freqcounter_stop(void);

/**
 * Poll for a newly completed period since the last call.
 *
 * Returns true and fills `period_ticks` at most once per rising edge
 * TIM2 actually captured - polling faster than the signal returns false
 * on the calls that find nothing new. Divide
 * cads_hal_freqcounter_tick_hz() by `period_ticks` for the frequency in
 * Hz. See cads/toolbox/freqcounter.h for why a missed edge resynchronises
 * silently (no period reported for that call) rather than reporting a
 * period that skipped one or more real transitions - use
 * cads_hal_freqcounter_missed_count() to see how often that happened.
 */
bool cads_hal_freqcounter_poll(uint32_t* period_ticks);

/**
 * Poll for a newly completed high time since the last call - the
 * falling-edge counterpart of cads_hal_freqcounter_poll(), for duty
 * cycle. Same units (ticks), same "at most once per edge, false when
 * there is nothing new yet" contract, and the same "a missed edge
 * resynchronises rather than reporting a wrong measurement" policy -
 * see cads/toolbox/freqcounter.h. Meaningless (returns false) before
 * cads_hal_freqcounter_poll() has reported at least one period.
 */
bool cads_hal_freqcounter_poll_high(uint32_t* high_ticks);

/** TIM2's own post-prescaler tick rate, fixed regardless of the signal
 *  being measured. */
uint32_t cads_hal_freqcounter_tick_hz(void);

uint32_t cads_hal_freqcounter_period_count(void);
uint32_t cads_hal_freqcounter_missed_count(void);
uint32_t cads_hal_freqcounter_min_period_ticks(void);
uint32_t cads_hal_freqcounter_max_period_ticks(void);
uint32_t cads_hal_freqcounter_avg_period_ticks(void);

uint32_t cads_hal_freqcounter_high_count(void);
uint32_t cads_hal_freqcounter_missed_high_count(void);
uint32_t cads_hal_freqcounter_min_high_ticks(void);
uint32_t cads_hal_freqcounter_max_high_ticks(void);
uint32_t cads_hal_freqcounter_avg_high_ticks(void);

#endif /* CADS_HAL_FREQCOUNTER_H */

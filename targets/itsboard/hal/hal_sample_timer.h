#ifndef CADS_HAL_SAMPLE_TIMER_H
#define CADS_HAL_SAMPLE_TIMER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * A free-running periodic timer (TIM7, a basic timer - no GPIO pin, no
 * alternate function, nothing docs/SAFETY.md restricts) for pacing the
 * logic analyzer's sample rate precisely instead of with a calibrated
 * software delay loop.
 *
 * A separate driver from hal_pktgen_timer.c (TIM6) rather than reusing
 * it: the two are paced by genuinely different things (a wire-rate
 * transmit interval vs a GPIO sample interval) that could plausibly
 * need to run at once one day, and "pacing timer" is generic enough
 * that borrowing pktgen's own name for it here would read backwards.
 * The register-level reasoning is otherwise identical - see
 * hal_pktgen_timer.c's own file header for the clock/resolution
 * derivation this driver reuses without re-deriving it a third time.
 */

/**
 * Configure and start the timer for `period_us`. Clamped to what a
 * 16-bit auto-reload register can express at this driver's fixed 1 us
 * resolution: 1..65536.
 */
void cads_hal_sample_timer_start(uint32_t period_us);

/** Stop the timer. Safe to call when already stopped. */
void cads_hal_sample_timer_stop(void);

/**
 * True once per elapsed period, consuming (clearing) the event - a
 * second call before the next period elapses returns false. See
 * hal_pktgen_timer.c's own cads_hal_pktgen_timer_elapsed() for the
 * exact same contract, restated here rather than shared because the
 * two timers are genuinely independent peripherals.
 */
bool cads_hal_sample_timer_elapsed(void);

#endif /* CADS_HAL_SAMPLE_TIMER_H */

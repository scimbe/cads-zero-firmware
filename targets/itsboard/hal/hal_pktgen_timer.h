#ifndef CADS_HAL_PKTGEN_TIMER_H
#define CADS_HAL_PKTGEN_TIMER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * A free-running periodic timer (TIM6, a basic timer - no GPIO pin, no
 * alternate function, nothing docs/SAFETY.md restricts) for pacing the
 * packet generator's transmit rate precisely instead of with a calibrated
 * software delay loop. See hal_pktgen_timer.c's file header for the
 * register-level reasoning - clock source, resolution, achievable range.
 */

/**
 * Configure and start the timer for `period_us`. Clamped to what a 16-bit
 * auto-reload register can express at this driver's fixed 1 us
 * resolution: 1..65536.
 */
void cads_hal_pktgen_timer_start(uint32_t period_us);

/** Stop the timer. Safe to call when already stopped. */
void cads_hal_pktgen_timer_stop(void);

/**
 * True once per elapsed period, consuming (clearing) the event - a second
 * call before the next period elapses returns false. Call as often as
 * convenient; nothing is missed between calls, only coalesced (a period
 * that already elapsed by the time this is next called still reports as
 * exactly one event, not one per call).
 */
bool cads_hal_pktgen_timer_elapsed(void);

#endif /* CADS_HAL_PKTGEN_TIMER_H */

#ifndef CADS_HAL_PWM_H
#define CADS_HAL_PWM_H

#include <stdint.h>

/**
 * TIM9 channel 1 PWM output on PE5 (adapter OUT13) - see board.h's own
 * `CADS_PIN_PWM_*` comment for why this pin, out of all sixteen OUT
 * pins, is the one that actually carries a PWM-capable timer channel.
 */

/**
 * Configure PE5 for TIM9_CH1 PWM output and start it at `frequency_hz`
 * (clamped to 1..100000) and `duty_percent` (clamped to 0..100).
 *
 * Claims PE5 away from plain GPIO output for as long as the generator
 * runs - apps/gpio's OUT13 LED does nothing meaningful until
 * cads_hal_pwm_stop() returns the pin. Safe to call again to change
 * frequency or duty while already running.
 */
void cads_hal_pwm_start(uint32_t frequency_hz, uint32_t duty_percent);

/** Stop TIM9 and release PE5 back to a plain low output, matching
 *  apps/gpio's own OUT13-off idle state. */
void cads_hal_pwm_stop(void);

#endif /* CADS_HAL_PWM_H */

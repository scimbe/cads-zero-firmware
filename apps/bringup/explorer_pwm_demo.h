#ifndef CADS_EXPLORER_PWM_DEMO_H
#define CADS_EXPLORER_PWM_DEMO_H

#include <stdint.h>

/**
 * Drive a PWM signal on PE5 (adapter OUT13) at `frequency_hz` (default
 * 1000) and `duty_percent` (default 50) for `seconds` (default 5). See
 * board.h's own `CADS_PIN_PWM_*` comment for why PE5/TIM9_CH1 is the
 * only genuinely PWM-capable pin among all sixteen OUT0..15.
 *
 * Board only - TIM9 output compare and the physical OUT13 pin are both
 * real hardware. explorer_pwm_demo_sim.c explains why the simulator
 * does not need its own version.
 */
void cads_explorer_pwm_demo(uint32_t frequency_hz, uint32_t duty_percent, uint32_t seconds);

#endif /* CADS_EXPLORER_PWM_DEMO_H */

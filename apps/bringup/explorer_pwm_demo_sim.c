/*
 * CaDS Zero - PWM generator (host side).
 *
 * TIM9 output compare on a physical pin is real hardware - nothing here
 * to drive.
 */

#include "explorer_pwm_demo.h"

#include "input_probe.h"

void cads_explorer_pwm_demo(uint32_t frequency_hz, uint32_t duty_percent, uint32_t seconds) {
    (void)frequency_hz;
    (void)duty_percent;
    (void)seconds;
    cads_probe_puts("# pwm: not available in the simulator\r\n");
}

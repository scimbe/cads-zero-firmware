/*
 * CaDS Zero - PWM generator (board side).
 *
 * Uses PE5 (adapter OUT13, TIM9 channel 1) - see board.h's own
 * CADS_PIN_PWM_* comment for why this is the only one of all sixteen
 * OUT pins that actually carries a PWM-capable timer channel, checked
 * against the sibling datasheet's alternate function table rather than
 * assumed from "it's an output pin, timers drive outputs".
 *
 * OUT13 already has a job (apps/gpio's own LED bank) - hal_pwm.c's
 * start()/stop() claim and release it exactly like hal_spi.c already
 * does for PA7, just at far lower stakes. That claimed LED is also
 * this task's own verification path: unlike the frequency/duty-cycle
 * counter's CN8 pin (nothing to look at without a physical jumper this
 * bench does not have), OUT13's LED is directly visible - a low enough
 * frequency makes the generator's own correctness something a camera
 * can confirm, not just a register dump.
 */

#include "explorer_pwm_demo.h"

#include "cads_hal.h"
#include "hal_pwm.h"
#include "input_probe.h"

void cads_explorer_pwm_demo(uint32_t frequency_hz, uint32_t duty_percent, uint32_t seconds) {
    /* Same "0 means the argument was omitted, use the default" convention
     * as every other multi-argument explorer command this session
     * (explorer_pktgen_demo.c and friends) - a genuinely-requested 0%
     * duty is not independently reachable through this command for the
     * same reason a genuinely-requested 0 pps was not there either, and
     * is not a meaningful way to exercise this driver anyway (the
     * observable result, LED off, is indistinguishable from not running
     * it at all). */
    if(frequency_hz == 0u) frequency_hz = 1000u;
    if(duty_percent == 0u) duty_percent = 50u;
    if(seconds == 0u) seconds = 5u;

    cads_probe_puts("# pwm: OUT13 (PE5, TIM9_CH1) at ");
    cads_probe_put_uint(frequency_hz);
    cads_probe_puts("Hz, ");
    cads_probe_put_uint(duty_percent);
    cads_probe_puts("% duty, for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    cads_hal_pwm_start(frequency_hz, duty_percent);
    cads_hal_delay_ms(seconds * 1000u);
    cads_hal_pwm_stop();

    cads_probe_puts("# pwm: done\r\n");
}

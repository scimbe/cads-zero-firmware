/*
 * CaDS Zero - TIM9 channel 1 PWM output (adapter OUT13, PE5).
 *
 * CLOCK, VERIFIED AGAINST THE PRIMARY SOURCE - AGAIN, NOT A REPEAT
 * -----------------------------------------------------------------
 * TIM9 is on APB2, not APB1 like every timer this milestone has used
 * so far (TIM2, TIM6): hal_clock.c's PPRE2 = DIV2, so APB2 = HCLK/2 =
 * 90 MHz. RM0090's own doubling rule ("if the APB prescaler is not 1,
 * TIMxCLK = 2 x the APB domain") already checked against the archived
 * PDF for TIM6/TIM2 applies here too, on the OTHER APB bus - TIM9CLK =
 * 2 x 90 MHz = 180 MHz, twice what TIM2 got from APB1's own 45 MHz base
 * (see board.h's own CADS_PIN_PWM_* comment).
 *
 * DYNAMIC PRESCALER, NOT A FIXED TICK RATE
 * -----------------------------------------------
 * hal_pktgen_timer.c and hal_freqcounter.c both fix PSC once, at design
 * time, because their valid frequency range is narrow enough that one
 * tick rate covers it. A PWM generator's whole point is a wide,
 * user-chosen frequency range (1 Hz..100 kHz here), and TIM9 is a
 * 16-bit timer - no fixed prescaler leaves enough ARR resolution across
 * that whole span. So PSC is computed per call, chosen as the smallest
 * value that still lets ARR (also 16-bit) express the requested period,
 * which maximises duty-cycle resolution for whatever frequency was
 * asked for rather than trading it away for a round tick rate nothing
 * else needs to match.
 *
 * PWM MODE 1, VERIFIED AGAINST THE PRIMARY SOURCE
 * -------------------------------------------------------
 * RM0090 on CCMR1's OC1M field: "110: PWM mode 1 - In upcounting,
 * channel 1 is active as long as TIMx_CNT<TIMx_CCR1" - CCR1 is
 * therefore the duty-cycle threshold directly, not something needing a
 * translation step. CCR1 = 0 is always-inactive (0% duty); CCR1 at or
 * past the counter's own top (ARR+1) is always-active (100% duty) -
 * both fall out of that same inequality for free, no special-casing
 * needed at either boundary.
 */

#include "hal_pwm.h"

#include "board.h"
#include "hal_gpio.h"

#define CADS_PWM_TIMER_CLK_HZ 180000000u
#define CADS_PWM_MIN_FREQ_HZ  1u
#define CADS_PWM_MAX_FREQ_HZ  100000u

void cads_hal_pwm_start(uint32_t frequency_hz, uint32_t duty_percent) {
    if(frequency_hz < CADS_PWM_MIN_FREQ_HZ) frequency_hz = CADS_PWM_MIN_FREQ_HZ;
    if(frequency_hz > CADS_PWM_MAX_FREQ_HZ) frequency_hz = CADS_PWM_MAX_FREQ_HZ;
    if(duty_percent > 100u) duty_percent = 100u;

    cads_gpio_init_alternate(CADS_PIN_PWM_PORT, CADS_PIN_PWM_PIN, CADS_PIN_PWM_AF, CadsGpioPullNone);

    RCC->APB2ENR |= RCC_APB2ENR_TIM9EN;
    (void)RCC->APB2ENR;

    TIM9->CR1 &= ~TIM_CR1_CEN;

    uint32_t total_ticks = CADS_PWM_TIMER_CLK_HZ / frequency_hz;
    if(total_ticks < 2u) total_ticks = 2u; /* a 0- or 1-tick period cannot carry a duty cycle */

    uint32_t psc_factor = (total_ticks + 65535u) / 65536u; /* smallest PSC that keeps ARR in 16 bits */
    if(psc_factor < 1u) psc_factor = 1u;

    uint32_t arr_ticks = total_ticks / psc_factor;
    if(arr_ticks < 2u) arr_ticks = 2u;
    if(arr_ticks > 65536u) arr_ticks = 65536u;

    TIM9->PSC = (uint16_t)(psc_factor - 1u);
    TIM9->ARR = (uint16_t)(arr_ticks - 1u);
    TIM9->CCR1 = (uint16_t)((arr_ticks * duty_percent) / 100u);

    /* OC1M = 110 (PWM mode 1). OC1PE + ARPE: CCR1/ARR changes latch at
     * the next update event instead of mid-cycle, so a live frequency
     * or duty change (this function is safe to call again while
     * running) never produces one glitched pulse in between. */
    TIM9->CCMR1 = (TIM9->CCMR1 & ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M)) |
                  TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE;
    TIM9->CCER = (TIM9->CCER & ~TIM_CCER_CC1P) | TIM_CCER_CC1E;
    TIM9->CR1 |= TIM_CR1_ARPE;

    TIM9->EGR = TIM_EGR_UG; /* force PSC/ARR/CCR1 from their shadow registers into effect now */
    TIM9->CR1 |= TIM_CR1_CEN;
}

void cads_hal_pwm_stop(void) {
    TIM9->CR1 &= ~TIM_CR1_CEN;
    cads_gpio_init_output(CADS_PIN_PWM_PORT, CADS_PIN_PWM_PIN, false);
}

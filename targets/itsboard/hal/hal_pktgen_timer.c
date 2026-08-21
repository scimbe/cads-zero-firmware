/*
 * CaDS Zero - TIM6-based packet pacing timer.
 *
 * WHY TIM6, NOT A SOFTWARE DELAY LOOP
 * ------------------------------------
 * cads_hal_delay_ms()/cads_hal_delay_us() are calibrated busy-wait loops -
 * fine for "wait roughly this long", wrong for "fire at exactly this
 * rate": every call accumulates a little calibration and call-overhead
 * error. TIM6 free-runs in hardware and this driver only polls its own
 * update flag, so the period is exact and nothing drifts between calls.
 * TIM6 is a basic timer: no GPIO pin, no alternate function, nothing
 * docs/SAFETY.md restricts - chosen for exactly that reason, not because
 * of any feature this driver actually uses beyond "counts and reloads".
 *
 * CLOCK, VERIFIED AGAINST THE PRIMARY SOURCE
 * -------------------------------------------
 * TIM6 is on APB1. hal_clock.c configures AHB=SYSCLK/1 (180 MHz),
 * APB1=HCLK/4 (45 MHz) - docs/HARDWARE.md's own clock table. RM0090's RCC
 * chapter (RCC_DCKCFGR.TIMPRE, default 0 - this project never touches
 * that bit) states plainly: "If the APB prescaler is configured to a
 * division factor of 1, TIMxCLK = PCLKx. Otherwise, the timer clock
 * frequencies are set to twice the frequency of the APB domain": since
 * APB1's prescaler is 4, not 1, TIM6CLK = 2 x 45 MHz = 90 MHz - checked
 * in the archived PDF rather than assumed, the same discipline this
 * project already applied to the Ethernet DMA descriptor layout.
 *
 * RESOLUTION AND RANGE
 * ---------------------
 * PSC = 89 divides that 90 MHz down to an exact 1 MHz (1 us/tick) count.
 * ARR is 16-bit, so the achievable period at this fixed prescaler is
 * 1..65536 us - hal_pktgen_timer_start() clamps to that range rather than
 * silently wrapping a period that does not fit.
 */

#include "hal_pktgen_timer.h"

#include "board.h"

#define CADS_PKTGEN_TIMER_CLK_HZ 90000000u
#define CADS_PKTGEN_TICK_HZ      1000000u
#define CADS_PKTGEN_PSC          ((CADS_PKTGEN_TIMER_CLK_HZ / CADS_PKTGEN_TICK_HZ) - 1u)
#define CADS_PKTGEN_MIN_PERIOD_US 1u
#define CADS_PKTGEN_MAX_PERIOD_US 65536u

void cads_hal_pktgen_timer_start(uint32_t period_us) {
    if(period_us < CADS_PKTGEN_MIN_PERIOD_US) period_us = CADS_PKTGEN_MIN_PERIOD_US;
    if(period_us > CADS_PKTGEN_MAX_PERIOD_US) period_us = CADS_PKTGEN_MAX_PERIOD_US;

    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
    (void)RCC->APB1ENR;

    TIM6->CR1 &= ~TIM_CR1_CEN;
    TIM6->PSC = (uint16_t)CADS_PKTGEN_PSC;
    TIM6->ARR = (uint16_t)(period_us - 1u);
    /* Force PSC/ARR from their shadow registers into immediate effect
     * before the first period, and clear the UIF this itself sets. */
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR &= ~TIM_SR_UIF;
    TIM6->CR1 |= TIM_CR1_CEN;
}

void cads_hal_pktgen_timer_stop(void) {
    TIM6->CR1 &= ~TIM_CR1_CEN;
}

bool cads_hal_pktgen_timer_elapsed(void) {
    if(!(TIM6->SR & TIM_SR_UIF)) return false;
    TIM6->SR &= ~TIM_SR_UIF;
    return true;
}

/*
 * CaDS Zero - TIM7-based logic analyzer sample pacing timer.
 *
 * Same clock chain and resolution choice as hal_pktgen_timer.c's TIM6,
 * not re-derived here: TIM7 is a basic timer on APB1, same as TIM6, so
 * the same RM0090-verified doubling rule gives it the identical
 * 90 MHz input clock, and PSC=89 gives the same exact 1 us/tick.
 */

#include "hal_sample_timer.h"

#include "board.h"

#define CADS_SAMPLE_TIMER_CLK_HZ  90000000u
#define CADS_SAMPLE_TICK_HZ       1000000u
#define CADS_SAMPLE_PSC           ((CADS_SAMPLE_TIMER_CLK_HZ / CADS_SAMPLE_TICK_HZ) - 1u)
#define CADS_SAMPLE_MIN_PERIOD_US 1u
#define CADS_SAMPLE_MAX_PERIOD_US 65536u

void cads_hal_sample_timer_start(uint32_t period_us) {
    if(period_us < CADS_SAMPLE_MIN_PERIOD_US) period_us = CADS_SAMPLE_MIN_PERIOD_US;
    if(period_us > CADS_SAMPLE_MAX_PERIOD_US) period_us = CADS_SAMPLE_MAX_PERIOD_US;

    RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;
    (void)RCC->APB1ENR;

    TIM7->CR1 &= ~TIM_CR1_CEN;
    TIM7->PSC = (uint16_t)CADS_SAMPLE_PSC;
    TIM7->ARR = (uint16_t)(period_us - 1u);
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR &= ~TIM_SR_UIF;
    TIM7->CR1 |= TIM_CR1_CEN;
}

void cads_hal_sample_timer_stop(void) {
    TIM7->CR1 &= ~TIM_CR1_CEN;
}

bool cads_hal_sample_timer_elapsed(void) {
    if(!(TIM7->SR & TIM_SR_UIF)) return false;
    TIM7->SR &= ~TIM_SR_UIF;
    return true;
}

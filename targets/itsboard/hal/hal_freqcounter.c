/*
 * CaDS Zero - TIM2 channel 3 input capture (CN8 pin 5, PB10), plus
 * channel 4 for duty cycle.
 *
 * CLOCK, THE SAME FACT hal_pktgen_timer.c ALREADY VERIFIED
 * ------------------------------------------------------------
 * TIM2 is on APB1, same as TIM6: hal_clock.c's APB1 = HCLK/4, and
 * RM0090's own rule ("if the APB prescaler is not 1, the timer clock is
 * twice the APB domain") already checked against the archived PDF for
 * TIM6 applies identically here - TIM2CLK = 2 x 45 MHz = 90 MHz. Not
 * re-verified from the PDF a second time; the rule is general, not
 * timer-specific.
 *
 * PSC = 89 divides that down to an exact 1 MHz (1 us/tick) count, the
 * same resolution choice hal_pktgen_timer.c made, for the same reason:
 * a round tick rate makes the tick-to-Hz arithmetic simple at the call
 * site. Unlike TIM6, TIM2 is a 32-bit general-purpose timer (the reason
 * docs/ROADMAP.md's own bullet asks for TIM2 or TIM5 specifically, not
 * any timer) - ARR is left at the full 0xFFFFFFFF, so at 1 MHz the
 * counter free-runs for ~4295 seconds before it wraps even once,
 * comfortably longer than any one measurement session.
 *
 * A LIGHT INPUT FILTER, NOT NONE
 * -----------------------------------
 * CN8 is an unbuffered breakout (docs/HARDWARE.md's own note on the
 * adapter's INT lines applies to this header too: straight to the MCU,
 * no shaping in between), so a single noise glitch on an external wire
 * would otherwise register as a real edge. IC3F = 0001 per RM0090's own
 * filter table (Bits 7:4 IC1F, the same layout for IC3F): N=2 samples
 * at the internal timer clock - just enough to reject a single-sample
 * glitch without meaningfully limiting the top end of what this driver
 * can measure. IC4F below uses the same value, for the same reason.
 *
 * CHANNEL 4 CAPTURES THE SAME PIN, NOT A SECOND ONE
 * -------------------------------------------------------
 * docs/ROADMAP.md's duty-cycle bullet: "same input-capture channel,
 * second capture compare register". TIM2_CH4 has no pin of its own in
 * this driver at all - CCMR2's CC4S = 10 configures "IC4 is mapped on
 * TI3" (confirmed against RM0090's own CC4S bit description, the same
 * primary-source check already done for CC3S = 01 = "IC3 is mapped on
 * TI3"), the timer's own channel-swap feature for exactly this PWM/duty
 * measurement case. CC4P = 1 (falling edge, mirroring CC3's CC3P = 0
 * rising) means CH3 and CH4 now capture the rising and falling edges of
 * the identical PB10 signal, independently, into CCR3 and CCR4.
 *
 * FREE-RUNNING, NOT RESET-MODE
 * -----------------------------------
 * RM0090's own textbook PWM Input Mode additionally drives the slave
 * mode controller into Reset mode (TI3FP3 resets CNT on every rising
 * edge), so CCR4 reads the high time directly. Deliberately not done
 * here: this driver's free-running configuration is exactly what the
 * frequency counter above depends on, and reset-on-trigger would change
 * what CCR3 itself means out from under it. The mathematically
 * equivalent alternative - leave the counter free-running and compute
 * high time as the wraparound-safe delta between a falling capture and
 * the most recent rising one (cads_freqcounter_capture_high(),
 * cads/toolbox/freqcounter.h) - gives the same answer without touching
 * the period counter's own already-committed behaviour at all.
 *
 * WHAT THIS FILE DOES NOT DO
 * -------------------------------
 * The actual period/frequency/duty-cycle math - the wraparound-safe
 * deltas, the "a missed edge resynchronises rather than reporting a
 * wrong measurement" policy - lives in cads/toolbox/freqcounter.h, not
 * here. This file only reads TIM2's raw CCR3/CCR4/SR and hands them to
 * that state machine; see its own file header for why, and
 * tests/unit/test_freqcounter.c for the proof, since there is no way to
 * drive a known test frequency into CN8 pin 5 without a physical jumper
 * wire.
 */

#include "hal_freqcounter.h"

#include "board.h"
#include "cads/toolbox/freqcounter.h"
#include "hal_gpio.h"

#define CADS_FREQCOUNTER_TIMER_CLK_HZ 90000000u
#define CADS_FREQCOUNTER_TICK_HZ      1000000u
#define CADS_FREQCOUNTER_PSC          ((CADS_FREQCOUNTER_TIMER_CLK_HZ / CADS_FREQCOUNTER_TICK_HZ) - 1u)

static cads_freqcounter_t s_freqcounter;

void cads_hal_freqcounter_start(void) {
    cads_gpio_init_alternate(
        CADS_PIN_FREQCOUNTER_PORT, CADS_PIN_FREQCOUNTER_PIN, CADS_PIN_FREQCOUNTER_AF,
        CadsGpioPullDown);

    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;

    TIM2->CR1 &= ~TIM_CR1_CEN;

    TIM2->PSC = CADS_FREQCOUNTER_PSC;
    TIM2->ARR = 0xFFFFFFFFu;

    /* CC3S = 01: IC3 mapped directly to TI3 (this pin's own input).
     * CC4S = 10: IC4 mapped indirectly to TI3 (the same pin, via the
     * channel swap - see this file's own header). IC3PSC/IC4PSC left
     * at 00 on both: capture every valid edge, not every Nth one - a
     * period/duty counter needs every edge. */
    TIM2->CCMR2 = (TIM2->CCMR2 &
                   ~(TIM_CCMR2_CC3S | TIM_CCMR2_IC3PSC | TIM_CCMR2_IC3F | TIM_CCMR2_CC4S |
                     TIM_CCMR2_IC4PSC | TIM_CCMR2_IC4F)) |
                  TIM_CCMR2_CC3S_0 | (0x1u << TIM_CCMR2_IC3F_Pos) | TIM_CCMR2_CC4S_1 |
                  (0x1u << TIM_CCMR2_IC4F_Pos);

    /* CC3P = 0, CC3NP = 0: capture on the rising edge (the period).
     * CC4P = 1, CC4NP = 0: capture on the falling edge (the high
     * time), of the same signal via the indirect mapping above. */
    TIM2->CCER = (TIM2->CCER & ~(TIM_CCER_CC3P | TIM_CCER_CC3NP | TIM_CCER_CC4NP)) |
                 TIM_CCER_CC3E | TIM_CCER_CC4P | TIM_CCER_CC4E;

    /* Force PSC/ARR from their shadow registers into immediate effect,
     * then clear every flag the UG itself just set - the same two-step
     * hal_pktgen_timer.c already established for TIM6. */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0u;

    TIM2->CR1 |= TIM_CR1_CEN;

    cads_freqcounter_init(&s_freqcounter);
}

void cads_hal_freqcounter_stop(void) {
    TIM2->CR1 &= ~TIM_CR1_CEN;
    cads_gpio_set_mode(
        CADS_PIN_FREQCOUNTER_PORT, CADS_PIN_FREQCOUNTER_PIN, CadsGpioModeInput, CadsGpioPullDown,
        CadsGpioSpeedFast);
}

bool cads_hal_freqcounter_poll(uint32_t* period_ticks) {
    if(!(TIM2->SR & TIM_SR_CC3IF)) return false;

    bool overcaptured = (TIM2->SR & TIM_SR_CC3OF) != 0u;
    uint32_t capture = TIM2->CCR3; /* reading CCR3 clears CC3IF */
    if(overcaptured) TIM2->SR = ~TIM_SR_CC3OF; /* rc_w0: write 0 to clear, 1s leave other flags alone */

    return cads_freqcounter_capture(&s_freqcounter, capture, overcaptured, period_ticks);
}

bool cads_hal_freqcounter_poll_high(uint32_t* high_ticks) {
    if(!(TIM2->SR & TIM_SR_CC4IF)) return false;

    bool overcaptured = (TIM2->SR & TIM_SR_CC4OF) != 0u;
    uint32_t capture = TIM2->CCR4; /* reading CCR4 clears CC4IF */
    if(overcaptured) TIM2->SR = ~TIM_SR_CC4OF; /* rc_w0: write 0 to clear, 1s leave other flags alone */

    return cads_freqcounter_capture_high(&s_freqcounter, capture, overcaptured, high_ticks);
}

uint32_t cads_hal_freqcounter_tick_hz(void) {
    return CADS_FREQCOUNTER_TICK_HZ;
}

uint32_t cads_hal_freqcounter_period_count(void) {
    return cads_freqcounter_period_count(&s_freqcounter);
}

uint32_t cads_hal_freqcounter_missed_count(void) {
    return cads_freqcounter_missed_count(&s_freqcounter);
}

uint32_t cads_hal_freqcounter_min_period_ticks(void) {
    return cads_freqcounter_min_period_ticks(&s_freqcounter);
}

uint32_t cads_hal_freqcounter_max_period_ticks(void) {
    return cads_freqcounter_max_period_ticks(&s_freqcounter);
}

uint32_t cads_hal_freqcounter_avg_period_ticks(void) {
    return cads_freqcounter_avg_period_ticks(&s_freqcounter);
}

uint32_t cads_hal_freqcounter_high_count(void) {
    return cads_freqcounter_high_count(&s_freqcounter);
}

uint32_t cads_hal_freqcounter_missed_high_count(void) {
    return cads_freqcounter_missed_high_count(&s_freqcounter);
}

uint32_t cads_hal_freqcounter_min_high_ticks(void) {
    return cads_freqcounter_min_high_ticks(&s_freqcounter);
}

uint32_t cads_hal_freqcounter_max_high_ticks(void) {
    return cads_freqcounter_max_high_ticks(&s_freqcounter);
}

uint32_t cads_hal_freqcounter_avg_high_ticks(void) {
    return cads_freqcounter_avg_high_ticks(&s_freqcounter);
}

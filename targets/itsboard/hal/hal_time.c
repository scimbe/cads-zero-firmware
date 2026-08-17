/*
 * CaDS Zero - time base.
 *
 * Two independent sources, on purpose:
 *   SysTick  -> 1 ms tick, and later the FreeRTOS scheduler tick
 *   DWT      -> free running CPU cycle counter for microsecond work
 *
 * The display and touch drivers need sub-microsecond settling delays that a
 * 1 ms tick simply cannot express, and busy-waiting on DWT costs nothing extra
 * because the counter is already running.
 */

#include "board.h"
#include "cads_hal.h"

static volatile uint32_t cads_tick_ms = 0u;

void cads_hal_time_init(void) {
    /* DWT cycle counter. Requires the debug block to be powered. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* 1 ms SysTick. Lowest priority: it must never delay the display DMA or
     * the Ethernet interrupt. */
    SysTick->LOAD = (CADS_HCLK_HZ / 1000u) - 1u;
    SysTick->VAL = 0u;
    NVIC_SetPriority(SysTick_IRQn, 15u);
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
}

void SysTick_Handler(void) {
    cads_tick_ms++;
}

uint32_t cads_hal_ticks_ms(void) {
    return cads_tick_ms;
}

uint64_t cads_hal_ticks_us(void) {
    /* CYCCNT wraps every ~23.8 s at 180 MHz. Extend it to 64 bit by latching
     * the wrap count; safe as long as this is polled more often than that,
     * which every caller does. */
    static uint32_t last_cycles = 0u;
    static uint64_t accumulated = 0u;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint32_t now = DWT->CYCCNT;
    accumulated += (uint32_t)(now - last_cycles);
    last_cycles = now;
    uint64_t total = accumulated;

    if(!primask) __enable_irq();

    return total / (CADS_HCLK_HZ / 1000000u);
}

void cads_hal_delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t target = us * (CADS_HCLK_HZ / 1000000u);
    while((uint32_t)(DWT->CYCCNT - start) < target) {
        __NOP();
    }
}

void cads_hal_delay_ms(uint32_t ms) {
    while(ms--) {
        cads_hal_delay_us(1000u);
    }
}

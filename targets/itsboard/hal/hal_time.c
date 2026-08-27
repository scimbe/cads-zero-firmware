/*
 * CaDS Zero - time base, built entirely on the DWT cycle counter.
 *
 * SysTick is deliberately NOT used. FreeRTOS wants it for the scheduler tick,
 * and two owners of one timer is the kind of arrangement that works until it
 * suddenly does not. Deriving milliseconds from the same free-running cycle
 * counter that already serves microsecond delays costs one division and leaves
 * SysTick entirely to the kernel.
 *
 * It is also strictly better as a time base: DWT counts CPU cycles with no
 * interrupt, so it keeps time correctly inside a critical section, inside an
 * ISR, and while the scheduler is suspended - all places where a tick counter
 * incremented by an interrupt quietly stops.
 *
 * CYCCNT wraps every 23.8 s at 180 MHz, so the 64-bit extension below has to
 * be polled more often than that. Every caller does, and the scheduler tick
 * guarantees it once the kernel is running.
 */

#include "board.h"
#include "cads_hal.h"

void cads_hal_time_init(void) {
    /* DWT cycle counter. Requires the debug block to be powered, which TRCENA
     * does; it works with no debugger attached. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t cads_hal_irq_save(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

void cads_hal_irq_restore(uint32_t state) {
    if(!state) __enable_irq();
}

uint32_t cads_hal_ticks_ms(void) {
    return (uint32_t)(cads_hal_ticks_us() / 1000u);
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

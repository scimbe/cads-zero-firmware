/*
 * CaDS Zero - reset entry point for STM32F429ZI.
 *
 * Deliberately written in C rather than assembly: the only part that genuinely
 * needs assembly is setting up the stack pointer, and on Cortex-M the core
 * loads MSP from the first vector table entry before the reset handler runs.
 */

#include <stdint.h>

#include "stm32f4xx.h"

/* Provided by the linker script. */
extern uint32_t __cads_data_load;
extern uint32_t __cads_data_start;
extern uint32_t __cads_data_end;
extern uint32_t __cads_ramfunc_load;
extern uint32_t __cads_ramfunc_start;
extern uint32_t __cads_ramfunc_end;
extern uint32_t __cads_bss_start;
extern uint32_t __cads_bss_end;

extern void cads_hal_early_init(void);
extern int main(void);

typedef void (*cads_init_fn_t)(void);
extern cads_init_fn_t __init_array_start[];
extern cads_init_fn_t __init_array_end[];

/* Defined by the generated vectors_stm32f429.c. */
extern const cads_init_fn_t cads_vector_table[];

static void cads_copy(const uint32_t* src, uint32_t* dst, const uint32_t* dst_end) {
    while(dst < dst_end) {
        *dst++ = *src++;
    }
}

static void cads_zero(uint32_t* dst, const uint32_t* dst_end) {
    while(dst < dst_end) {
        *dst++ = 0u;
    }
}

__attribute__((noreturn)) void Reset_Handler(void) {
    /* The FPU must be enabled before any code that the compiler may have
     * vectorised with floating point instructions runs - which, with
     * -mfloat-abi=hard, includes the C runtime startup below. CP10/CP11 full
     * access. */
    SCB->CPACR |= (3u << 20) | (3u << 22);
    __DSB();
    __ISB();

    cads_copy(&__cads_data_load, &__cads_data_start, &__cads_data_end);
    cads_copy(&__cads_ramfunc_load, &__cads_ramfunc_start, &__cads_ramfunc_end);
    cads_zero(&__cads_bss_start, &__cads_bss_end);

    /* A previously flashed image or a bootloader may have moved VTOR; point it
     * back at our own table before enabling any interrupt. */
    SCB->VTOR = (uint32_t)cads_vector_table;
    __DSB();

    cads_hal_early_init();

    for(cads_init_fn_t* fn = __init_array_start; fn != __init_array_end; fn++) {
        (*fn)();
    }

    (void)main();

    /* main() must not return. If it does, park in a debuggable loop rather
     * than letting execution run off into undefined memory. */
    for(;;) {
        __WFI();
    }
}

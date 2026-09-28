/*
 * CaDS Zero - clock tree and core timing for STM32F429ZI.
 *
 * Target: 180 MHz from the 8 MHz ST-Link MCO fed in as an HSE bypass clock.
 * The sequence below mirrors the one the ITS lab firmware has been running for
 * years on this exact board, so it is known-good silicon-wise; it is simply
 * expressed against the registers instead of the ST HAL.
 *
 * Order matters and is not negotiable (RM0090 6.3.1 / 5.1.4):
 *   1. PWR clock on, voltage scale 1
 *   2. HSE bypass on, wait ready
 *   3. PLL configured and started, wait locked
 *   4. Over-drive on, wait ODRDY, then over-drive switch, wait ODSWRDY
 *   5. Flash latency raised BEFORE the core speeds up
 *   6. Bus prescalers, then switch SYSCLK to the PLL
 */

#include "board.h"
#include "cads_hal.h"
#include "hal_gpio.h"

/* Bounded spin so a missing HSE cannot hang the boot forever. Roughly a few
 * hundred milliseconds at any plausible startup clock. */
#define CADS_CLOCK_TIMEOUT 0x08000000u

/* CMSIS convention. We do not link ST's system_stm32f4xx.c, so it lives here.
 * Starts at the HSI value the core actually boots with. */
uint32_t SystemCoreClock = 16000000u;

static void cads_clock_fail(const char* reason) {
    /* Too early for the console. Fall back to the on-board red LED and a
     * breakpoint: an attached ST-Link stops here with the reason in r0. */
    cads_gpio_init_output(CADS_PIN_LED_RED_PORT, CADS_PIN_LED_RED, true);
    __asm volatile("mov r0, %0 \n bkpt #0" ::"r"(reason) : "r0", "memory");
    for(;;) {
    }
}

void cads_hal_early_init(void) {
    /* --- 1. power controller ------------------------------------------- */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_VOS; /* scale 1: required for 180 MHz */

    /* --- 2. HSE bypass (8 MHz square wave from the ST-Link MCO) --------- */
    RCC->CR |= RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;
    for(uint32_t guard = 0;; guard++) {
        if(RCC->CR & RCC_CR_HSERDY) break;
        if(guard > CADS_CLOCK_TIMEOUT) cads_clock_fail("HSE did not start");
    }

    /* --- 3. main PLL: 8 / 8 * 360 / 2 = 180 MHz ------------------------- */
    RCC->CR &= ~RCC_CR_PLLON;
    while(RCC->CR & RCC_CR_PLLRDY) {
    }
    /* PLLQ /8: VCO 360 MHz / 8 = 45 MHz on the 48 MHz domain. Only the
     * hardware RNG uses it here (no USB OTG, no SDIO), and RM0090 caps the
     * RNG clock at 48 MHz - the previous /7 ran it at 51.4 MHz, out of spec,
     * now that lwIP draws every random number from it (modules/net,
     * cads_net_rand). */
    RCC->PLLCFGR = (8u << RCC_PLLCFGR_PLLM_Pos) | (360u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u << RCC_PLLCFGR_PLLP_Pos) /* 00 = /2 */ |
                   (8u << RCC_PLLCFGR_PLLQ_Pos) | RCC_PLLCFGR_PLLSRC_HSE;
    RCC->CR |= RCC_CR_PLLON;
    for(uint32_t guard = 0;; guard++) {
        if(RCC->CR & RCC_CR_PLLRDY) break;
        if(guard > CADS_CLOCK_TIMEOUT) cads_clock_fail("PLL did not lock");
    }

    /* --- 4. over-drive, needed above 168 MHz ---------------------------- */
    PWR->CR |= PWR_CR_ODEN;
    for(uint32_t guard = 0;; guard++) {
        if(PWR->CSR & PWR_CSR_ODRDY) break;
        if(guard > CADS_CLOCK_TIMEOUT) cads_clock_fail("over-drive not ready");
    }
    PWR->CR |= PWR_CR_ODSWEN;
    for(uint32_t guard = 0;; guard++) {
        if(PWR->CSR & PWR_CSR_ODSWRDY) break;
        if(guard > CADS_CLOCK_TIMEOUT) cads_clock_fail("over-drive switch failed");
    }

    /* --- 5. flash: 5 wait states at 180 MHz / 3.3 V, prefetch on -------- */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_5WS;
    while((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_5WS) {
    }

    /* --- 6. bus prescalers, then switch over ---------------------------- */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
                RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    for(uint32_t guard = 0;; guard++) {
        if((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) break;
        if(guard > CADS_CLOCK_TIMEOUT) cads_clock_fail("SYSCLK switch failed");
    }

    SystemCoreClock = CADS_SYSCLK_HZ;
}

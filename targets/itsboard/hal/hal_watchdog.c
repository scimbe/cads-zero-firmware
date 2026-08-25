/*
 * CaDS Zero - independent watchdog (IWDG) and reset-cause decode.
 *
 * See core/cads_hal.h's own comment for the full design reasoning (why the
 * FreeRTOS tick feeds this rather than an application task, why the reset
 * cause is latched once). This file is the two hardware primitives that
 * design needs: touching IWDG, DBGMCU and RCC->CSR directly, nothing else.
 *
 * NEITHER OF THESE TOUCHES A GPIO PIN.
 * IWDG runs off LSI, its own internal RC oscillator, entirely independent
 * of every pin this board's SAFETY.md protects. DBGMCU and RCC->CSR are
 * likewise pure internal peripherals. Nothing here can violate the pin
 * rules - there is no pin to violate.
 */

#include "board.h"
#include "cads_hal.h"

/* RM0090 20.4.3: writing this key to IWDG_KR starts the watchdog. Once
 * written, IWDG cannot be stopped by software - not by a different key, not
 * by a peripheral reset, only by a full power-on reset of the chip. */
#define CADS_IWDG_START_KEY 0xCCCCu
/* RM0090 20.4.3: writing this key reloads the down-counter - the "feed". */
#define CADS_IWDG_RELOAD_KEY 0xAAAAu
/* RM0090 20.4.3: writing this key unlocks PR/RLR for the ~100us it takes
 * IWDG's own async clock domain to apply them; any other key re-locks
 * them (and starts the watchdog if it is 0xCCCC). */
#define CADS_IWDG_UNLOCK_KEY 0x5555u

/* LSI is nominally 32 kHz on this part (RM0090 6.3.9) but is an internal RC
 * oscillator with no tight tolerance spec - unlike HSE, nothing here reads
 * back an achieved frequency the way hal_clock.c does for the PLL. The
 * prescaler/reload below are chosen for a timeout generous enough that no
 * legitimate operation in this firmware comes close: the slowest measured
 * single operation is a 448 ms full-screen display flush
 * (docs/ROADMAP.md's own M1 measurement), and explorer demos run for
 * minutes at a time but are fed every SysTick regardless of what they are
 * doing - see vApplicationTickHook in modules/kernel/src/kernel.c. A 2 s
 * timeout is roughly 4x the worst normal blocking span with room to spare,
 * while still recovering a genuine lockup in a couple of seconds rather
 * than making a human wait around wondering if the board is dead.
 *
 * IWDG_PR prescaler /64 (PR=100b) gives a ~2.048 ms tick at nominal 32 kHz;
 * IWDG_RLR=1000 (max 0xFFF=4095) gives ~2.048 s.
 */
#define CADS_IWDG_PRESCALER_BITS 0x4u /* /64 */
#define CADS_IWDG_RELOAD_VALUE 1000u

void cads_hal_watchdog_init(uint32_t timeout_ms) {
    (void)timeout_ms; /* Fixed prescaler/reload for now - see the comment above. */

    /* Freeze IWDG (and every other DBGMCU_APB1_FZ peripheral this bit
     * covers) whenever the core is halted by a debugger. Without this, an
     * ST-Link/GDB session that stops at the fault_handlers.c bkpt - exactly
     * the documented "halts usefully with a debugger attached" behaviour
     * in docs/SAFETY.md - would keep counting down in the background and
     * reset the board out from under the person investigating it. Must be
     * set before the first feed below; harmless if no debugger ever
     * attaches. */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    IWDG->KR = CADS_IWDG_UNLOCK_KEY;
    IWDG->PR = CADS_IWDG_PRESCALER_BITS;
    IWDG->RLR = CADS_IWDG_RELOAD_VALUE;
    while(IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) {
        /* RM0090 20.4.3: PR/RLR are on IWDG's own async clock domain: the
         * write is not visible to a read-back until the update completes.
         * Bounded by construction - LSI is running and this loop is only
         * waiting out that domain-crossing delay, not an external event
         * that could hang. */
    }

    IWDG->KR = CADS_IWDG_RELOAD_KEY; /* First feed. */
    IWDG->KR = CADS_IWDG_START_KEY;
}

void cads_hal_watchdog_feed(void) {
    IWDG->KR = CADS_IWDG_RELOAD_KEY;
}

/*
 * RCC->CSR's five reset-cause flags (RM0090 6.3.19) are sticky: they
 * accumulate across resets until explicitly cleared by RMVF, which is
 * exactly why this has to be read and cleared exactly once, at the start
 * of the boot that wants to know the cause - a second boot without an
 * intervening RMVF would otherwise see both its own cause and the
 * previous boot's OR'd together.
 */
static cads_reset_cause_t cads_reset_cause_decode(uint32_t csr) {
    /* Checked most-specific-first: a watchdog reset also sets PINRSTF on
     * this part (RM0090's own reset-flow diagram - IWDG's reset asserts
     * through the same internal reset line NRST does), so IWDGRSTF/
     * WWDGRSTF must be tested before the more generic pin/power flags or
     * a real watchdog reset would misreport as a plain pin reset. */
    if(csr & RCC_CSR_IWDGRSTF) return CadsResetWatchdogIndependent;
    if(csr & RCC_CSR_WWDGRSTF) return CadsResetWatchdogWindow;
    if(csr & RCC_CSR_SFTRSTF) return CadsResetSoftware;
    if(csr & RCC_CSR_LPWRRSTF) return CadsResetLowPower;
    if(csr & RCC_CSR_PORRSTF) return CadsResetPowerOn;
    if(csr & RCC_CSR_PINRSTF) return CadsResetPin;
    return CadsResetUnknown;
}

cads_reset_cause_t cads_hal_reset_cause(void) {
    static bool latched = false;
    static cads_reset_cause_t cached = CadsResetUnknown;

    if(!latched) {
        cached = cads_reset_cause_decode(RCC->CSR);
        RCC->CSR |= RCC_CSR_RMVF; /* Clear for the next boot's own reading. */
        latched = true;
    }
    return cached;
}

/*
 * CaDS Zero - fault handlers that dump the stacked frame before halting.
 *
 * The generated vector table (vectors_stm32f429.c) wires every one of these
 * as a weak alias of Default_Handler, which traps to a debugger and spins.
 * That is a safe default and a useless one: it says a fault happened and
 * nothing about which instruction, which registers, or why. The four
 * functions below are strong definitions of the same symbol names, so the
 * linker prefers them over the weak aliases without anything in the
 * generated file needing to change.
 *
 * WHY MemManage/BusFault/UsageFault NEED cads_fault_init()
 * -----------------------------------------------------------
 * At reset, only HardFault is enabled. A memory, bus or usage fault with its
 * own handler disabled does not go nowhere - it escalates straight to
 * HardFault (PM0214 4.4.1), with SCB->HFSR.FORCED set and the real cause
 * still visible in SCB->CFSR. But the *handler* that runs is the generic one,
 * and the point of having four distinct handlers is knowing which one fired
 * without decoding HFSR by hand. cads_fault_init() enables the three
 * sub-handlers in SCB->SHCSR; called from targets/itsboard/hal/hal_init.c,
 * after the console so a fault during the small init window before it exists
 * still falls through to Default_Handler's bkpt-and-spin rather than hanging
 * inside a console write that has nowhere to go yet.
 *
 * WHY A NAKED FUNCTION AND INLINE ASM AT ALL
 * ---------------------------------------------
 * The stacked frame (R0-R3, R12, LR, PC, xPSR) lives on whichever stack was
 * active when the fault happened - MSP for anything that faulted in an
 * exception or before the scheduler starts, PSP for a task once FreeRTOS is
 * running. A normal C function prologue would push its own registers first,
 * and by the time any C code ran, the two would be indistinguishable. The
 * standard, textbook technique (ARM's own application notes and PM0214 both
 * describe it) is a naked entry that reads EXC_RETURN's bit 2 out of LR to
 * pick the right stack pointer *before* any C prologue runs, then branches
 * straight to a plain C function with that pointer as its first argument.
 * This is generic Cortex-M4 exception handling, not anything from
 * flipperzero-firmware - the exact same pattern appears in every vendor HAL
 * and every from-scratch Cortex-M startup file that bothers to decode a
 * fault at all.
 *
 * WHY THE DUMP DOES NOT GO THROUGH cads_log OR cads/toolbox/tap.h
 * --------------------------------------------------------------------
 * Both of those are for code that is not actively broken. A fault handler
 * runs with unknown scheduler state, unknown stack integrity above the
 * frame, and no guarantee that anything relying on more than the UART's own
 * registers still works. This writes straight to cads_hal_console_write()
 * and cads/toolbox/fmt.h's pure, stateless hex formatter - nothing else.
 *
 * WHY IT HALTS RATHER THAN RESETS
 * -----------------------------------
 * Same reasoning as Default_Handler in vectors_stm32f429.c: a reset would
 * throw away the only copy of the evidence. bkpt traps to an attached
 * debugger with the fault still live in memory; the loop after it is what
 * happens with nothing attached - gated on DHCSR.C_DEBUGEN (see
 * cads_fault_dump's own comment just above the bkpt) because an
 * unconditional bkpt with no debugger attached does not fall through to
 * that loop on this part, it re-faults - found live 2026-08-29 as a real
 * fault-within-a-fault loop, only ever surfaced once this ran genuinely
 * unattended for hours instead of at a desk with st-util open.
 */

#include <stdint.h>

#include "board.h"
#include "cads/diag/forensic.h"
#include "cads/toolbox/fmt.h"
#include "cads_hal.h"

/* R0, R1, R2, R3, R12, LR, PC (return address), xPSR - in the order the
 * Cortex-M4 automatically pushes them on exception entry (PM0214 2.3.7). */
typedef struct {
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
} cads_fault_frame_t;

static void cads_fault_puts(const char* text) {
    size_t length = 0u;
    while(text[length] != '\0') {
        length++;
    }
    cads_hal_console_write(text, length);
}

static void cads_fault_put_hex(uint32_t value) {
    char digits[CADS_FMT_BUFFER];
    size_t length = cads_fmt_hex(digits, sizeof(digits), value, 8u, true);
    cads_hal_console_write(digits, length);
}

static void cads_fault_field(const char* name, uint32_t value) {
    cads_fault_puts(name);
    cads_fault_puts(" = 0x");
    cads_fault_put_hex(value);
    cads_fault_puts("\r\n");
}

__attribute__((noreturn)) static void cads_fault_dump(const char* name, uint32_t* stacked) {
    const cads_fault_frame_t* frame = (const cads_fault_frame_t*)stacked;

    cads_fault_puts("\r\n*** CaDS FAULT: ");
    cads_fault_puts(name);
    cads_fault_puts(" ***\r\n");

    cads_fault_field("R0 ", frame->r0);
    cads_fault_field("R1 ", frame->r1);
    cads_fault_field("R2 ", frame->r2);
    cads_fault_field("R3 ", frame->r3);
    cads_fault_field("R12", frame->r12);
    cads_fault_field("LR ", frame->lr);
    cads_fault_field("PC ", frame->pc);
    cads_fault_field("PSR", frame->xpsr);

    /* Stack pointers of the faulting context. psp is exact - Handler mode
     * never alters it, so it still holds the interrupted task's SP; msp is
     * this handler's live MSP, a small fixed frame below the fault-time
     * value, enough to see an MSP run to its 4K CCM limit. `stacked` itself
     * points into whichever of the two the exception frame was pushed onto. */
    uint32_t msp_at_fault, psp_at_fault;
    __asm volatile("mrs %0, msp" : "=r"(msp_at_fault));
    __asm volatile("mrs %0, psp" : "=r"(psp_at_fault));
    cads_fault_field("MSP", msp_at_fault);
    cads_fault_field("PSP", psp_at_fault);

    /* CFSR packs three byte/halfword sub-registers: MMFSR (bits 7:0), BFSR
     * (bits 15:8), UFSR (bits 31:16) - PM0214 4.4.7-4.4.9. Read as one word
     * here rather than split, since a reader with the manual open can shift
     * it apart faster than four extra lines would let them. */
    uint32_t cfsr = SCB->CFSR;
    uint32_t hfsr = SCB->HFSR;
    cads_fault_field("CFSR", cfsr);
    cads_fault_field("HFSR", hfsr);

    bool mmfar_valid = (cfsr & SCB_CFSR_MMARVALID_Msk) != 0u;
    bool bfar_valid = (cfsr & SCB_CFSR_BFARVALID_Msk) != 0u;
    if(mmfar_valid) cads_fault_field("MMFAR", SCB->MMFAR);
    if(bfar_valid) cads_fault_field("BFAR", SCB->BFAR);

    /* Recorded into the persistent ring (modules/diag) before the halt
     * below, so it is still readable by a debugger at the next reboot even
     * if nothing is attached right now - see docs/ROADMAP.md's dated Log
     * entry for the watchdog/forensics feature this is part of. */
    cads_forensic_frame_t forensic_frame = {
        .r0 = frame->r0, .r1 = frame->r1, .r2 = frame->r2, .r3 = frame->r3,
        .r12 = frame->r12, .lr = frame->lr, .pc = frame->pc, .xpsr = frame->xpsr};
    cads_forensic_record(
        name, &forensic_frame, cfsr, hfsr, mmfar_valid, SCB->MMFAR, bfar_valid, SCB->BFAR,
        msp_at_fault, psp_at_fault);

    /* bkpt with no debugger attached does not fall through quietly - on
     * this part it escalates straight back into another HardFault (PM0214
     * confirms this is expected Cortex-M behaviour, not a hypothetical: a
     * BKPT instruction with DHCSR.C_DEBUGEN clear traps to the fault
     * handler, not to a debug monitor that does not exist). Found live,
     * 2026-08-29: a real fault with nobody attached re-entered this exact
     * function via its own bkpt, over and over, each cycle only stopped by
     * the IWDG timing out and resetting the board - which then booted,
     * re-hit the ORIGINAL bug, and repeated. The forensic ring's own
     * comment ("still readable... even if nothing is attached right now")
     * was the intent; this bkpt defeated it for exactly the unattended
     * case it exists for. Gate it on DHCSR.C_DEBUGEN so a live debugger
     * still gets the trap it's built for, and an unattended board gets the
     * clean, quiet halt the loop below was always meant to be. */
    if((DCB->DHCSR & DCB_DHCSR_C_DEBUGEN_Msk) != 0u) {
        __asm volatile("bkpt #0" ::: "memory");
    }
    for(;;) {
    }
}

/* Referenced only from the inline asm "b" below, which the compiler cannot
 * see as a call site - __attribute__((used)) is what stops it being dropped
 * as dead code. */
__attribute__((used)) static void cads_hardfault_c(uint32_t* stacked) {
    cads_fault_dump("HardFault", stacked);
}

__attribute__((used)) static void cads_memmanage_c(uint32_t* stacked) {
    cads_fault_dump("MemManage", stacked);
}

__attribute__((used)) static void cads_busfault_c(uint32_t* stacked) {
    cads_fault_dump("BusFault", stacked);
}

__attribute__((used)) static void cads_usagefault_c(uint32_t* stacked) {
    cads_fault_dump("UsageFault", stacked);
}

/*
 * EXC_RETURN is in LR on exception entry; bit 2 says which stack the pushed
 * frame is on (0 = MSP, 1 = PSP - PM0214 2.3.7). "naked" means no prologue,
 * so LR still holds EXC_RETURN when this runs rather than whatever a normal
 * function's frame setup would have overwritten it with.
 */
#define CADS_FAULT_TRAMPOLINE(name, target)                 \
    void name(void) __attribute__((naked));                 \
    void name(void) {                                       \
        __asm volatile(                                     \
            "tst lr, #4      \n"                             \
            "ite eq          \n"                             \
            "mrseq r0, msp   \n"                              \
            "mrsne r0, psp   \n"                              \
            "b " #target "   \n" ::: "memory");              \
    }

CADS_FAULT_TRAMPOLINE(HardFault_Handler, cads_hardfault_c)
CADS_FAULT_TRAMPOLINE(MemManage_Handler, cads_memmanage_c)
CADS_FAULT_TRAMPOLINE(BusFault_Handler, cads_busfault_c)
CADS_FAULT_TRAMPOLINE(UsageFault_Handler, cads_usagefault_c)

#undef CADS_FAULT_TRAMPOLINE

void cads_fault_init(void) {
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
}

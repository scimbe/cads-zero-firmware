/*
 * CaDS Zero - continuity/cable tester.
 *
 * Drives OUT0 (PD0) through both levels and reads the result back on
 * INT0/AUX0 (PG0) - the exact "jumper OUT0 to INTx" pattern the
 * manufacturer's own GPIOTest already established for exercising the
 * INT0..5 lines (docs/HARDWARE.md's "The buttons" section: "The same
 * test exercises them by asking the operator to jumper OUT0 to INTx
 * with a wire").
 *
 * INT0, NOT INT2: INT2/PG2 is ETH_RXER, already on docs/SAFETY.md's
 * RMII no-touch list - established while choosing the frequency
 * counter's own pin two tasks ago. INT0..5 differ only in which
 * physical AUX header pin they land on; INT0 is simply the first one.
 *
 * cads_hal_adapter_interrupts() INVERTS THE WIRE, LIKE _inputs() DOES
 * -------------------------------------------------------------------
 * Caught on the very first hardware run of this file, not from reading
 * the source first: an un-jumpered board reported "low ok, high not
 * seen" - backwards from what "INT0 is pulled up, so it should read
 * high regardless of OUT0" predicts. hal_io.c's own
 * cads_hal_adapter_interrupts() is `(~IDR) & mask`, the identical
 * inversion cads_hal_adapter_inputs() carries its own comment for
 * ("active low on the wire; report active high so callers read
 * naturally") but that this function does not repeat: bit=1 means the
 * pin is being pulled LOW (a jumper actively driving it, or a button
 * press on IN0..7's own pins), bit=0 means the pin is at its own idle
 * HIGH (pulled up, nothing driving it). So the correct expectation is
 * the mirror of the naive one: OUT0 driven LOW should read back as
 * bit=1 (active), OUT0 driven HIGH should read back as bit=0 (idle) -
 * cads_continuity_wait_for()'s `want_active` parameter names it by
 * this function's own bit meaning directly, specifically so a second
 * reader (or a second look from the first one) does not have to
 * re-derive the inversion from hal_io.c to get this right.
 *
 * WHY TWO LEVELS, NOT ONE
 * -----------------------------
 * An un-jumpered pin always reads idle (bit=0) regardless of what OUT0
 * does, so a test that only ever drove OUT0 high and checked for the
 * matching idle readback would report a false PASS on every
 * un-jumpered board. Driving LOW first, then HIGH, and requiring the
 * readback to follow BOTH transitions is what actually proves a
 * low-impedance path exists between the two pins rather than
 * coincidental agreement with INT0's own idle level.
 *
 * PORTABLE, NOT BOARD-ONLY
 * -------------------------------
 * Unlike every earlier GPIO Swiss-army-knife bullet this milestone,
 * this needs no new HAL driver at all: cads_hal_adapter_outputs() and
 * cads_hal_adapter_interrupts() are already declared in the portable
 * core/cads_hal.h and implemented on both targets (targets/itsboard/
 * hal/hal_io.c for the board, targets/sim/hal_sim.c for the
 * simulator - the same functions apps/gpio already calls from
 * portable code). One file, no _sim.c split, wired directly into
 * cads_apps' unconditional source list the same way
 * explorer_arp_demo.c/explorer_ping_demo.c already are.
 */

#include "explorer_continuity_demo.h"

#include "cads_hal.h"
#include "input_probe.h"

#define CADS_CONTINUITY_OUT_BIT    0u /* OUT0 */
#define CADS_CONTINUITY_INT_BIT    0u /* INT0 / AUX0 */
#define CADS_CONTINUITY_TIMEOUT_MS 3000u
#define CADS_CONTINUITY_POLL_MS    20u

/** `want_active` in cads_hal_adapter_interrupts()'s own bit sense: true
 *  waits for bit=1 (the pin being pulled low), false waits for bit=0
 *  (the pin at its own idle high) - see this file's own header for why
 *  that is the opposite of what "INT0 is pulled up" naively suggests. */
static bool cads_continuity_wait_for(bool want_active) {
    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < CADS_CONTINUITY_TIMEOUT_MS) {
        bool active = (cads_hal_adapter_interrupts() & (1u << CADS_CONTINUITY_INT_BIT)) != 0u;
        if(active == want_active) return true;
        cads_hal_delay_ms(CADS_CONTINUITY_POLL_MS);
    }
    return false;
}

void cads_explorer_continuity_demo(void) {
    cads_probe_puts(
        "# continuity: jumper OUT0 to INT0 (AUX0) now - driving OUT0 low then high, "
        "reading INT0 back\r\n");

    cads_hal_adapter_outputs(0u); /* OUT0 low; every other OUT off for the duration */
    bool low_ok = cads_continuity_wait_for(true); /* low on the wire -> reads active (bit=1) */

    cads_hal_adapter_outputs(1u << CADS_CONTINUITY_OUT_BIT); /* OUT0 high */
    bool high_ok = cads_continuity_wait_for(false); /* high on the wire -> reads idle (bit=0) */

    cads_hal_adapter_outputs(0u); /* leave outputs off */

    if(low_ok && high_ok) {
        cads_probe_puts("# continuity: PASS - OUT0 <-> INT0 follows both levels\r\n");
    } else {
        cads_probe_puts("# continuity: FAIL - no continuity (low ");
        cads_probe_puts(low_ok ? "ok" : "not seen");
        cads_probe_puts(", high ");
        cads_probe_puts(high_ok ? "ok" : "not seen");
        cads_probe_puts(")\r\n");
    }
}

/*
 * CaDS Zero - deliberately trip a fault, reached from the hardware explorer's
 * 'z' command.
 *
 * UDF (PM0214 3.14) exists for exactly this: an instruction the architecture
 * guarantees is always undefined, so triggering UsageFault is deterministic
 * and does not depend on the state of any register, any peripheral or any
 * data in memory the way a null-pointer write or a divide by zero would.
 * Nothing here writes to memory, reconfigures a peripheral or touches a pin -
 * docs/SAFETY.md has nothing to say about a CPU exception.
 */

#include "explorer_fault_test.h"

#include "cads/toolbox/str.h"
#include "input_probe.h"

void cads_explorer_fault_test(const char* argument) {
    if(!cads_str_equal(argument, "FAULT")) {
        cads_probe_puts(
            "# fault test: refused. This halts the firmware for good - a reset or a "
            "reflash is the only way back. Type 'z FAULT' to actually do it.\r\n");
        return;
    }

    cads_probe_puts("# fault test: tripping UsageFault now\r\n");
    __asm volatile("udf #0" ::: "memory");

    /* Unreachable: fault_handlers.c's dump halts in its own infinite loop.
     * A build where this line somehow ran would mean UDF did not fault,
     * which would itself be worth knowing about. */
    cads_probe_puts("# fault test: UDF did not fault - this should never print\r\n");
}

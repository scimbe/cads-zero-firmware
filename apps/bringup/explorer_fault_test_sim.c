/*
 * CaDS Zero - fault test stub for the host.
 *
 * targets/itsboard/startup/fault_handlers.c does not exist in the simulator:
 * there is no Cortex-M exception model to test on the host, and deliberately
 * crashing the simulator process would not prove anything about the board's
 * fault handlers. Saying so plainly beats silently doing nothing.
 */

#include "explorer_fault_test.h"

#include "input_probe.h"

void cads_explorer_fault_test(const char* argument) {
    (void)argument;
    cads_probe_puts(
        "# fault test: not available in the simulator (no Cortex-M exception model "
        "on the host)\r\n");
}

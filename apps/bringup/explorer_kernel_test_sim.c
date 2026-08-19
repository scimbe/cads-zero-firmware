/*
 * CaDS Zero - kernel test stub for the host.
 *
 * modules/kernel wraps FreeRTOS, which does not exist in the simulator (see
 * apps/bringup/tasks_sim.c). Saying so plainly beats silently doing nothing
 * and printing PASS.
 */

#include "explorer_kernel_test.h"

#include "input_probe.h"

void cads_explorer_kernel_test(void) {
    cads_probe_puts("# kernel test: not available in the simulator (no FreeRTOS on the host)\r\n");
}

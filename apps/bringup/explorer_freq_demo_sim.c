/*
 * CaDS Zero - frequency/period/duty-cycle counter (host side).
 *
 * TIM2 input capture on a physical CN8 pin is real hardware - nothing
 * here to measure. The actual capture-to-period/duty-cycle math this
 * command drives (cads/toolbox/freqcounter.h) has its own host unit
 * tests (tests/unit/test_freqcounter.c) that do not depend on this
 * command, or on real hardware, at all.
 */

#include "explorer_freq_demo.h"

#include "input_probe.h"

void cads_explorer_freq_demo(uint32_t seconds) {
    (void)seconds;
    cads_probe_puts("# freq: not available in the simulator\r\n");
}

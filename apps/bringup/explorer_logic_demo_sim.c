/*
 * CaDS Zero - logic analyzer (host side).
 *
 * TIM7-paced sampling of the adapter's real IN0..7/INT0..5 pins is real
 * hardware - nothing here to capture.
 */

#include "explorer_logic_demo.h"

#include "input_probe.h"

void cads_explorer_logic_demo(uint32_t sample_rate_hz, uint32_t seconds) {
    (void)sample_rate_hz;
    (void)seconds;
    cads_probe_puts("# logic: not available in the simulator\r\n");
}

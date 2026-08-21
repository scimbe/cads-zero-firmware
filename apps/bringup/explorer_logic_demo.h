#ifndef CADS_EXPLORER_LOGIC_DEMO_H
#define CADS_EXPLORER_LOGIC_DEMO_H

#include <stdint.h>

/**
 * Simple logic analyzer: sample IN0..7/INT0..5 at `sample_rate_hz`
 * (default 25) for `seconds` (default 5), then render the whole
 * capture as a 14-channel waveform on the panel.
 *
 * Capture-then-render, not live-scrolling - see explorer_logic_demo.c's
 * own file header for why: this codebase's display has exactly one
 * flusher (apps/bringup/tasks.c's ui task), and a live view would mean
 * either losing samples every time the sampling loop yields to wait for
 * a flush, or teaching this driver to sample from an interrupt - a
 * bigger change than this bullet's own "M" sizing calls for. A capture
 * buffer sized well below the sample-rate x duration this command asks
 * for by default just captures fewer samples than requested, reported
 * honestly, not silently.
 *
 * Board only - the adapter's IN/INT pins are real hardware.
 * explorer_logic_demo_sim.c explains why the simulator does not need
 * its own version.
 */
void cads_explorer_logic_demo(uint32_t sample_rate_hz, uint32_t seconds);

#endif /* CADS_EXPLORER_LOGIC_DEMO_H */

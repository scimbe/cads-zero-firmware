#ifndef CADS_EXPLORER_CONTINUITY_DEMO_H
#define CADS_EXPLORER_CONTINUITY_DEMO_H

/**
 * Continuity/cable test: drive OUT0 (PD0) low then high, read the
 * result back on INT0/AUX0 (PG0), report PASS only if both
 * transitions were observed - see explorer_continuity_demo.c's own
 * file header for why one level alone would not prove anything given
 * INT0's own pull-up.
 *
 * Jumper OUT0 to INT0, or connect the cable under test between the two
 * corresponding header pins, before running this - without one, the
 * correct, honest result is FAIL, not an error.
 *
 * Portable: builds and runs the same on board and simulator, unlike
 * every earlier GPIO Swiss-army-knife command this milestone -
 * cads_hal_adapter_outputs()/_interrupts() are already cross-target,
 * nothing here needs a real timer, DMA or GPIO alternate function. The
 * simulator does not model a virtual jumper between its own fake
 * outputs and fake inputs, so a run there always reports FAIL - the
 * same honest result an un-jumpered real board gives.
 */
void cads_explorer_continuity_demo(void);

#endif /* CADS_EXPLORER_CONTINUITY_DEMO_H */

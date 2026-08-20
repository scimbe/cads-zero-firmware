#ifndef CADS_EXPLORER_FAULT_TEST_H
#define CADS_EXPLORER_FAULT_TEST_H

/**
 * Deliberately execute an undefined instruction, which trips UsageFault (or
 * HardFault if UsageFault turns out not to be enabled) and, if
 * targets/itsboard/startup/fault_handlers.c is doing its job, dumps the
 * stacked frame and CFSR/HFSR over the console before trapping to bkpt and
 * halting for good.
 *
 * `argument` must be exactly "FAULT" or nothing happens - this permanently
 * ends the session (a reset or a reflash is the only way back), and a
 * confirmation argument is cheap insurance against a stray keypress doing
 * that by accident on hardware someone else might be using.
 */
void cads_explorer_fault_test(const char* argument);

#endif /* CADS_EXPLORER_FAULT_TEST_H */

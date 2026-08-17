#ifndef CADS_EXPLORER_H
#define CADS_EXPLORER_H

/**
 * Interactive hardware explorer over the serial console.
 *
 * Exists because the ITS adapter's wiring is not documented in this repository
 * and reflashing to answer one question about one pin is too slow a loop. Never
 * returns.
 */
void cads_explorer_run(void);

#endif /* CADS_EXPLORER_H */

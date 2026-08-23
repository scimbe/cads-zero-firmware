#ifndef CADS_EXPLORER_THROUGHPUT_DEMO_H
#define CADS_EXPLORER_THROUGHPUT_DEMO_H

#include <stdint.h>

/**
 * Re-measure full-screen flush throughput while the scheduler and the
 * network stack are both live, for `seconds` (0 = 10s default). See
 * this command's own .c file header for why this exists and what it
 * answers that the M0/M1 pre-scheduler measurement could not.
 */
void cads_explorer_throughput_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_THROUGHPUT_DEMO_H */

#ifndef CADS_EXPLORER_SCREENCAST_DEMO_H
#define CADS_EXPLORER_SCREENCAST_DEMO_H

#include <stdint.h>

/**
 * Stream the live framebuffer to a TCP client for `seconds` (default 30),
 * cycling a simple changing test pattern on the panel so there is actually
 * something to watch move. See explorer_screencast_demo.c's file header for
 * the wire protocol - board only, real streaming. The simulator IS the host
 * viewer already (its own window), so explorer_screencast_demo_sim.c says
 * so instead of streaming its own display back to itself.
 */
void cads_explorer_screencast_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_SCREENCAST_DEMO_H */

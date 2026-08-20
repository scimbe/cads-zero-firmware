#ifndef CADS_EXPLORER_HTTP_DEMO_H
#define CADS_EXPLORER_HTTP_DEMO_H

#include <stdint.h>

/**
 * Serve a small HTTP status page on port 80 for `seconds` (default 30).
 * Board only - explorer_http_demo_sim.c explains why the simulator does
 * not need one. See explorer_http_demo.c's file header for exactly what
 * the page shows.
 */
void cads_explorer_http_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_HTTP_DEMO_H */

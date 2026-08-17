#ifndef CADS_INPUT_PROBE_H
#define CADS_INPUT_PROBE_H

#include <stdint.h>

/**
 * Report every edge on the adapter's input lines for `duration_ms`.
 *
 * Used once, to find out what is physically wired to PF0..PF7 and PG0..PG5 on
 * this board, and to measure how long the switches bounce for. The resulting
 * map is what the input service is built on; see docs/reference/input-map.md.
 */
void cads_input_probe_run(uint32_t duration_ms);

/* Provided by the bring-up app so the probe shares its console helpers. */
void cads_probe_puts(const char* text);
void cads_probe_put_uint(uint32_t value);

#endif /* CADS_INPUT_PROBE_H */

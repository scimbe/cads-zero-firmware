#ifndef CADS_EXPLORER_CLI_DEMO_H
#define CADS_EXPLORER_CLI_DEMO_H

#include <stdint.h>

/**
 * Run cads_cli for `seconds` (default 30), reachable two ways at once:
 * over the serial console (this call reads cads_hal_console_read() itself,
 * the same "hand it full control for the duration" contract every other
 * bounded explorer demo already follows) and over TCP on port 4242 if
 * cads_cli_tcp_start() succeeds (board only - honestly reported when not,
 * e.g. on the simulator or before the netif has a usable link).
 */
void cads_explorer_cli_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_CLI_DEMO_H */

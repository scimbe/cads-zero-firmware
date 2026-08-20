/*
 * CaDS Zero - screen streaming (host side).
 *
 * The simulator's own window already shows the framebuffer live - streaming
 * it back over TCP to "a host viewer" would be showing the same host its
 * own screen a second time through a socket. Saying so plainly rather than
 * building a redundant loopback path.
 */

#include "explorer_screencast_demo.h"

#include "input_probe.h"

void cads_explorer_screencast_demo(uint32_t seconds) {
    (void)seconds;
    cads_probe_puts("# screencast: not needed in the simulator (its own window is already live)\r\n");
}

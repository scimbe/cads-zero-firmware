/*
 * CaDS Zero - cads_cli over TCP, simulator stub.
 *
 * The simulator has no network stack (modules/net/src/cads_net_sim.c's own
 * file header) - there is nothing here to fake a listening socket with, so
 * this just says no rather than pretending to have started one.
 */

#include "cads/cli/cli_tcp.h"

bool cads_cli_tcp_start(uint16_t port) {
    (void)port;
    return false;
}

void cads_cli_tcp_stop(void) {
}

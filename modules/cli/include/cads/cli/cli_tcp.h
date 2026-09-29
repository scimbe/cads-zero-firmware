/*
 * CaDS Zero - cads_cli reachable over TCP.
 *
 * Board only - the simulator has no network stack at all (see
 * modules/net/src/cads_net_sim.c's own file header), so
 * cads_cli_tcp_sim.c's cads_cli_tcp_start() honestly reports that instead
 * of pretending a socket exists.
 */

#ifndef CADS_CLI_TCP_H
#define CADS_CLI_TCP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start listening for cads_cli connections on `port`.
 *
 * Requires cads_net_init() to already have brought the netif up - this
 * only adds a listening PCB to it. Idempotent: a second call while a
 * listener already exists is a no-op that returns true.
 *
 * One connection at a time. A second concurrent connection attempt is
 * accepted at the TCP level and then immediately closed - this is a
 * diagnostic tool for one operator, not a multi-user shell, and letting two
 * sessions dispatch into the same command table's side effects
 * unsynchronised would be a real bug, not a feature.
 *
 * Returns false if the listening socket could not be created or bound
 * (out of PCBs, or the port is already taken by something else).
 */
bool cads_cli_tcp_start(uint16_t port);

/**
 * Close the listener and drop the current session, if any. Every caller of
 * cads_cli_tcp_start() that only wants the CLI reachable for a while (the
 * explorer's `j` demo) must call this when done - otherwise the port stays
 * open for the rest of the boot, served from any later cads_net_poll().
 * Harmless when nothing is running.
 */
void cads_cli_tcp_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* CADS_CLI_TCP_H */
